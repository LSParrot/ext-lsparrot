/*
  +----------------------------------------------------------------------+
  | LSParrot PHP LSP Extension                                           |
  +----------------------------------------------------------------------+
  | Copyright (c) LSParrot GitHub Organization                           |
  +----------------------------------------------------------------------+
  | This source file is subject to the 0BSD license that is              |
  | bundled with this package in the file LICENSE.                       |
  +----------------------------------------------------------------------+
  | Author: Go Kudo <zeriyoshi@gmail.com>                                |
  +----------------------------------------------------------------------+
*/

#include "lsp_internal.h"

/* Resident analyzer runner: one long-lived PHP process per (analyzer,
 * project root) that executes per-file analysis and type-query commands on
 * behalf of the LSP server. The runner pre-requires the project's Composer
 * autoloader and, when ext-pcntl is available, executes PHP CLI analyzers
 * via fork+include so the booted engine (and opcache CLI file cache, which
 * the runner enables) is reused instead of paying a full interpreter and
 * bootstrap start per request. Non-PHP analyzer entry points (and platforms
 * without fork) transparently fall back to proc_open inside the runner, and
 * any runner failure falls back to a direct one-shot process spawn. */

static const char lsp_runner_script_source[] =
	"<?php\n"
	"error_reporting(E_ERROR | E_PARSE);\n"
	"$in = fopen('php://stdin', 'rb');\n"
	"$autoload = $argv[1] ?? '';\n"
	"if ($autoload !== '' && is_file($autoload)) {\n"
	"    require_once $autoload;\n"
	"}\n"
	"$canFork = function_exists('pcntl_fork') && function_exists('pcntl_waitpid')\n"
	"    && function_exists('pcntl_wexitstatus') && DIRECTORY_SEPARATOR === '/';\n"
	"function lsp_runner_read_frame($in): ?array {\n"
	"    $length = 0;\n"
	"    for (;;) {\n"
	"        $line = fgets($in);\n"
	"        if ($line === false) { return null; }\n"
	"        $line = rtrim($line, \"\\r\\n\");\n"
	"        if ($line === '') { break; }\n"
	"        if (stripos($line, 'Content-Length:') === 0) { $length = (int) trim(substr($line, 15)); }\n"
	"    }\n"
	"    if ($length <= 0) { return null; }\n"
	"    $body = '';\n"
	"    while (strlen($body) < $length) {\n"
	"        $chunk = fread($in, $length - strlen($body));\n"
	"        if ($chunk === false || $chunk === '') { return null; }\n"
	"        $body .= $chunk;\n"
	"    }\n"
	"    $decoded = json_decode($body, true);\n"
	"    return is_array($decoded) ? $decoded : null;\n"
	"}\n"
	"function lsp_runner_write_frame(array $payload): void {\n"
	"    $json = json_encode($payload, JSON_UNESCAPED_SLASHES | JSON_INVALID_UTF8_SUBSTITUTE);\n"
	"    fwrite(STDOUT, 'Content-Length: ' . strlen($json) . \"\\r\\n\\r\\n\" . $json);\n"
	"    fflush(STDOUT);\n"
	"}\n"
	"function lsp_runner_target_is_php_script(string $target): bool {\n"
	"    $head = (string) @file_get_contents($target, false, null, 0, 128);\n"
	"    if (strncmp($head, '<?php', 5) === 0) { return true; }\n"
	"    if (strncmp($head, '#!', 2) === 0) {\n"
	"        $firstLine = strtok($head, \"\\n\");\n"
	"        return $firstLine !== false && preg_match('/\\\\bphp[0-9.]*$/', trim($firstLine)) === 1;\n"
	"    }\n"
	"    return false;\n"
	"}\n"
	"function lsp_runner_run_fork(array $argvList, string $cwd): ?array {\n"
	"    $target = $argvList[0] ?? '';\n"
	"    if (!is_string($target) || $target === '' || !is_file($target) || !lsp_runner_target_is_php_script($target)) {\n"
	"        return null;\n"
	"    }\n"
	"    $stdoutFile = tempnam(sys_get_temp_dir(), 'lspr');\n"
	"    $stderrFile = tempnam(sys_get_temp_dir(), 'lspr');\n"
	"    if ($stdoutFile === false || $stderrFile === false) { return null; }\n"
	"    $pid = pcntl_fork();\n"
	"    if ($pid < 0) { @unlink($stdoutFile); @unlink($stderrFile); return null; }\n"
	"    if ($pid === 0) {\n"
	"        fclose(STDIN);\n"
	"        fclose(STDOUT);\n"
	"        fclose(STDERR);\n"
	"        $childIn = fopen('/dev/null', 'rb');\n"
	"        $childOut = fopen($stdoutFile, 'wb');\n"
	"        $childErr = fopen($stderrFile, 'wb');\n"
	"        if ($cwd !== '') { @chdir($cwd); }\n"
	"        $_SERVER['argv'] = $argvList;\n"
	"        $_SERVER['argc'] = count($argvList);\n"
	"        $GLOBALS['argv'] = $argvList;\n"
	"        $GLOBALS['argc'] = count($argvList);\n"
	"        try {\n"
	"            include $target;\n"
	"        } catch (Throwable $error) {\n"
	"            fwrite($childErr, (string) $error);\n"
	"            exit(255);\n"
	"        }\n"
	"        exit(0);\n"
	"    }\n"
	"    $status = 0;\n"
	"    pcntl_waitpid($pid, $status);\n"
	"    $output = (string) @file_get_contents($stdoutFile) . (string) @file_get_contents($stderrFile);\n"
	"    @unlink($stdoutFile);\n"
	"    @unlink($stderrFile);\n"
	"    return [pcntl_wifexited($status) ? pcntl_wexitstatus($status) : 255, $output];\n"
	"}\n"
	"function lsp_runner_run_proc(array $argvList, string $cwd): array {\n"
	"    $null = DIRECTORY_SEPARATOR === '\\\\' ? 'NUL' : '/dev/null';\n"
	"    $proc = @proc_open($argvList, [0 => ['file', $null, 'r'], 1 => ['pipe', 'w'], 2 => ['pipe', 'w']], $pipes, $cwd !== '' ? $cwd : null);\n"
	"    if (!is_resource($proc)) { return [127, '']; }\n"
	"    stream_set_blocking($pipes[1], false);\n"
	"    stream_set_blocking($pipes[2], false);\n"
	"    $open = [$pipes[1], $pipes[2]];\n"
	"    $output = '';\n"
	"    while ($open !== []) {\n"
	"        $read = $open;\n"
	"        $write = null;\n"
	"        $except = null;\n"
	"        if (@stream_select($read, $write, $except, 1) === false) { break; }\n"
	"        foreach ($read as $stream) {\n"
	"            $chunk = fread($stream, 65536);\n"
	"            if ($chunk !== false && $chunk !== '') {\n"
	"                $output .= $chunk;\n"
	"            } elseif (feof($stream)) {\n"
	"                fclose($stream);\n"
	"                $remaining = [];\n"
	"                foreach ($open as $candidate) {\n"
	"                    if ($candidate !== $stream) { $remaining[] = $candidate; }\n"
	"                }\n"
	"                $open = $remaining;\n"
	"            }\n"
	"        }\n"
	"    }\n"
	"    return [proc_close($proc), $output];\n"
	"}\n"
	"for (;;) {\n"
	"    $job = lsp_runner_read_frame($in);\n"
	"    if ($job === null) { exit(0); }\n"
	"    $argvList = $job['argv'] ?? null;\n"
	"    $cwd = (string) ($job['cwd'] ?? '');\n"
	"    $id = $job['id'] ?? 0;\n"
	"    if (!is_array($argvList) || $argvList === []) {\n"
	"        lsp_runner_write_frame(['id' => $id, 'exitCode' => 127, 'output' => '']);\n"
	"        continue;\n"
	"    }\n"
	"    $result = $canFork ? lsp_runner_run_fork($argvList, $cwd) : null;\n"
	"    if ($result === null) { $result = lsp_runner_run_proc($argvList, $cwd); }\n"
	"    lsp_runner_write_frame(['id' => $id, 'exitCode' => $result[0], 'output' => $result[1]]);\n"
	"}\n"
;

typedef struct _lsp_runner_session {
	lsp_process_pipes pipes;
	smart_str rx;
	zend_string *analyzer;
	zend_string *project_root;
	uint64_t jobs_completed;
	zend_long next_job_id;
} lsp_runner_session;

static inline void lsp_runner_session_free(lsp_runner_session *session)
{
	int status = 0;

	if (lsp_process_id_valid(session->pipes.process)) {
		lsp_pipe_close(&session->pipes.input);
		lsp_pipe_close(&session->pipes.output);
		lsp_pipe_close(&session->pipes.error);
		lsp_process_terminate(session->pipes.process);
		lsp_process_wait(session->pipes.process, &status);
		lsp_process_close(session->pipes.process);
	}

	smart_str_free(&session->rx);

	if (session->analyzer) {
		zend_string_release(session->analyzer);
	}

	if (session->project_root) {
		zend_string_release(session->project_root);
	}

	efree(session);
}

extern void lsp_runner_session_destroy(zval *value)
{
	lsp_runner_session *session = (lsp_runner_session *) Z_PTR_P(value);

	if (session) {
		lsp_runner_session_free(session);
	}
}

static inline zend_string *lsp_runner_session_key(const char *analyzer, zend_string *project_root)
{
	return strpprintf(0, "%s:%s", analyzer, ZSTR_VAL(project_root));
}

static inline zend_string *lsp_runner_script_path(zend_string *project_root)
{
	return strpprintf(0, "%s/.lsparrot/runner/lsp-runner.php", ZSTR_VAL(project_root));
}

static inline bool lsp_runner_write_script(zend_string *project_root, zend_string *script_path)
{
	zend_string *dir, *contents;
	bool result;

	dir = strpprintf(0, "%s/.lsparrot/runner", ZSTR_VAL(project_root));
	lsp_mkdir_p(dir);
	contents = zend_string_init(lsp_runner_script_source, sizeof(lsp_runner_script_source) - 1, 0);
	result = lsp_write_string_file(script_path, contents);
	zend_string_release(contents);
	zend_string_release(dir);

	return result;
}

static inline const char *lsp_runner_php_binary(void)
{
	if (PG(php_binary) && PG(php_binary)[0] != '\0') {
		return PG(php_binary);
	}

	return "php";
}

static inline lsp_runner_session *lsp_runner_session_start(const char *analyzer, zend_string *project_root)
{
	lsp_runner_session *session;
	lsp_command command;
	zend_string *script_path, *autoload, *opcache_dir, *opcache_flag;
	bool spawned;

	script_path = lsp_runner_script_path(project_root);
	if (!lsp_runner_write_script(project_root, script_path)) {
		zend_string_release(script_path);

		return NULL;
	}

	opcache_dir = strpprintf(0, "%s/.lsparrot/runner/opcache", ZSTR_VAL(project_root));
	lsp_mkdir_p(opcache_dir);
	opcache_flag = strpprintf(0, "-dopcache.file_cache=%s", ZSTR_VAL(opcache_dir));
	autoload = strpprintf(0, "%s/vendor/autoload.php", ZSTR_VAL(project_root));

	lsp_command_init(&command);
	lsp_command_add(&command, lsp_runner_php_binary());
	lsp_command_add(&command, "-dopcache.enable_cli=1");
	lsp_command_add_zstr(&command, opcache_flag);
	lsp_command_add_zstr(&command, script_path);
	if (lsp_is_regular_file(autoload)) {
		lsp_command_add_zstr(&command, autoload);
	}

	session = ecalloc(1, sizeof(*session));
	spawned = lsp_process_spawn_piped(&command, project_root, &session->pipes);
	lsp_command_destroy(&command);
	zend_string_release(autoload);
	zend_string_release(opcache_flag);
	zend_string_release(opcache_dir);
	zend_string_release(script_path);

	if (!spawned) {
		efree(session);

		return NULL;
	}

	session->analyzer = zend_string_init(analyzer, strlen(analyzer), 0);
	session->project_root = zend_string_copy(project_root);
	session->next_job_id = 1;

	return session;
}

static inline lsp_runner_session *lsp_runner_session_ensure(lsp_server *server, const char *analyzer, zend_string *project_root)
{
	lsp_runner_session *session;
	zend_string *key;
	zval *existing, ptr;

	key = lsp_runner_session_key(analyzer, project_root);
	existing = zend_hash_find(&server->runner_sessions, key);
	if (existing) {
		zend_string_release(key);

		return (lsp_runner_session *) Z_PTR_P(existing);
	}

	session = lsp_runner_session_start(analyzer, project_root);
	if (!session) {
		zend_string_release(key);

		return NULL;
	}

	ZVAL_PTR(&ptr, session);
	zend_hash_update(&server->runner_sessions, key, &ptr);
	zend_string_release(key);

	return session;
}

static inline void lsp_runner_session_drop(lsp_server *server, lsp_runner_session *session)
{
	zend_string *key;

	key = lsp_runner_session_key(ZSTR_VAL(session->analyzer), session->project_root);
	zend_hash_del(&server->runner_sessions, key);
	zend_string_release(key);
}

static inline zend_string *lsp_runner_encode_job(zend_long job_id, lsp_command *command, zend_string *cwd, double timeout)
{
	smart_str json = {0};
	zend_string *encoded = NULL;
	zval payload, argv_zv;
	uint32_t i;

	array_init(&payload);
	add_assoc_long(&payload, "id", job_id);
	array_init(&argv_zv);
	for (i = 0; i < command->count; i++) {
		add_next_index_string(&argv_zv, command->argv[i]);
	}
	add_assoc_zval(&payload, "argv", &argv_zv);
	add_assoc_str(&payload, "cwd", cwd ? zend_string_copy(cwd) : zend_empty_string);
	add_assoc_long(&payload, "timeoutMs", (zend_long) (timeout > 0.0 ? timeout * 1000.0 : 30000.0));

	if (php_json_encode(&json, &payload, PHP_JSON_UNESCAPED_SLASHES) == SUCCESS && json.s) {
		smart_str_0(&json);
		encoded = zend_string_copy(json.s);
	}

	smart_str_free(&json);
	zval_ptr_dtor(&payload);

	return encoded;
}

/* Parse one complete Content-Length framed JSON message out of the session
 * receive buffer. */
static inline bool lsp_runner_parse_frame(lsp_runner_session *session, zval *message)
{
	const char *data, *headers_end, *length_marker;
	char *body;
	size_t available, header_size, content_length;

	if (!session->rx.s) {
		return false;
	}

	data = ZSTR_VAL(session->rx.s);
	available = ZSTR_LEN(session->rx.s);
	headers_end = NULL;
	if (available > 3) {
		headers_end = (const char *) php_memnstr((char *) data, "\r\n\r\n", 4, (char *) data + available);
	}
	if (!headers_end) {
		return false;
	}

	length_marker = data;
	if (strncasecmp(length_marker, "Content-Length:", sizeof("Content-Length:") - 1) != 0) {
		return false;
	}

	content_length = (size_t) strtoull(length_marker + sizeof("Content-Length:") - 1, NULL, 10);
	header_size = (headers_end - data) + 4;
	if (content_length == 0 || available < header_size + content_length) {
		return false;
	}

	body = emalloc(content_length + 1);
	memcpy(body, data + header_size, content_length);
	body[content_length] = '\0';
	ZVAL_UNDEF(message);
	php_json_decode_ex(message, body, content_length, PHP_JSON_OBJECT_AS_ARRAY, 64);
	efree(body);

	/* Consume the frame. */
	if (available > header_size + content_length) {
		memmove(ZSTR_VAL(session->rx.s), data + header_size + content_length, available - header_size - content_length);
		ZSTR_LEN(session->rx.s) = available - header_size - content_length;
	} else {
		ZSTR_LEN(session->rx.s) = 0;
	}

	if (Z_TYPE_P(message) != IS_ARRAY) {
		if (!Z_ISUNDEF_P(message)) {
			zval_ptr_dtor(message);
		}

		return false;
	}

	return true;
}

static inline zend_string *lsp_runner_wait_response(lsp_runner_session *session, zend_long job_id, double timeout)
{
	smart_str discard = {0};
	zend_string *output = NULL;
	zval message, *id_zv, *output_zv;
	double deadline;
	bool output_closed = false, error_closed = false, matched;

	deadline = lsp_now_seconds() + (timeout > 0.0 ? timeout : 30.0) + 5.0;

	for (;;) {
		lsp_pipe_read_available(session->pipes.output, &session->rx, &output_closed);
		lsp_pipe_read_available(session->pipes.error, &discard, &error_closed);
		smart_str_free(&discard);

		while (lsp_runner_parse_frame(session, &message)) {
			id_zv = zend_hash_str_find(Z_ARRVAL(message), "id", sizeof("id") - 1);
			matched = id_zv && Z_TYPE_P(id_zv) == IS_LONG && Z_LVAL_P(id_zv) == job_id;

			if (matched) {
				output_zv = zend_hash_str_find(Z_ARRVAL(message), "output", sizeof("output") - 1);
				if (output_zv && Z_TYPE_P(output_zv) == IS_STRING) {
					output = zend_string_copy(Z_STR_P(output_zv));
				} else {
					output = zend_empty_string;
				}
			}

			zval_ptr_dtor(&message);

			if (matched) {
				return output;
			}
		}

		if (output_closed || lsp_now_seconds() >= deadline) {
			return NULL;
		}

		lsp_sleep_milliseconds(5);
	}
}

extern zend_string *lsp_runner_run_capture(lsp_server *server, const char *analyzer, zend_string *project_root, lsp_command *command, zend_string *cwd, double timeout)
{
	lsp_runner_session *session;
	zend_string *job, *frame, *output;
	zend_long job_id;
	bool sent;

	session = lsp_runner_session_ensure(server, analyzer, project_root);
	if (!session) {
		return lsp_run_command_capture(command, cwd, timeout);
	}

	job_id = session->next_job_id++;
	job = lsp_runner_encode_job(job_id, command, cwd, timeout);
	if (!job) {
		return lsp_run_command_capture(command, cwd, timeout);
	}

	frame = strpprintf(0, "Content-Length: %zu\r\n\r\n%s", ZSTR_LEN(job), ZSTR_VAL(job));
	zend_string_release(job);
	sent = lsp_pipe_write_all_timeout(session->pipes.input, ZSTR_VAL(frame), ZSTR_LEN(frame), 5.0);
	zend_string_release(frame);

	output = sent ? lsp_runner_wait_response(session, job_id, timeout) : NULL;
	if (!output) {
		/* The runner is wedged or dead: drop the session and fall back to a
		 * direct one-shot spawn so the request still completes. */
		lsp_runner_session_drop(server, session);

		return lsp_run_command_capture(command, cwd, timeout);
	}

	session->jobs_completed++;

	return output;
}

extern void lsp_runner_shutdown_all(lsp_server *server)
{
	zend_hash_clean(&server->runner_sessions);
}

extern void lsp_runner_status(lsp_server *server, zval *target)
{
	lsp_runner_session *session;
	zval runners, entry;
	int status = 0;
	bool alive;

	array_init(&runners);

	ZEND_HASH_FOREACH_PTR(&server->runner_sessions, session) {
		if (!session) {
			continue;
		}

		alive = lsp_process_id_valid(session->pipes.process) &&
			!lsp_process_wait_nonblocking(session->pipes.process, &status)
		;

		array_init(&entry);
		add_assoc_str(&entry, "analyzer", zend_string_copy(session->analyzer));
		add_assoc_str(&entry, "projectRoot", zend_string_copy(session->project_root));
		add_assoc_long(&entry, "jobs", (zend_long) session->jobs_completed);
		add_assoc_bool(&entry, "alive", alive);
		add_next_index_zval(&runners, &entry);
	} ZEND_HASH_FOREACH_END();

	add_assoc_zval(target, "runners", &runners);
}
