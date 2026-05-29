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

extern void lsp_document_analyze(lsp_document *document);

extern lsp_document *lsp_document_open_or_change(lsp_server *server, zend_string *uri, zend_long version, zend_string *text)
{
	lsp_document *document;
	zval *existing = zend_hash_find(&server->documents, uri), ptr;

	if (existing) {
		document = (lsp_document *) Z_PTR_P(existing);
		zend_string_release(document->text);
		document->text = zend_string_copy(text);
		document->version = version;
		lsp_document_analyze(document);

		return document;
	}

	document = ecalloc(1, sizeof(lsp_document));
	document->uri = zend_string_copy(uri);
	document->path = lsp_uri_to_path(uri);
	document->text = zend_string_copy(text);
	document->version = version;
	ZVAL_UNDEF(&document->lsparrot);
	lsp_document_analyze(document);

	ZVAL_PTR(&ptr, document);
	zend_hash_update(&server->documents, uri, &ptr);

	return document;
}

extern lsp_document *lsp_document_from_uri(lsp_server *server, zend_string *uri)
{
	lsp_document *document;
	zend_string *path, *text;
	zval *existing = zend_hash_find(&server->documents, uri);

	if (existing) {
		return (lsp_document *) Z_PTR_P(existing);
	}

	path = lsp_uri_to_path(uri);
	text = lsp_read_file(path);
	document = lsp_document_open_or_change(server, uri, 0, text);

	if (text != zend_empty_string) {
		zend_string_release(text);
	}

	zend_string_release(path);

	return document;
}

static inline void lsp_protocol_write_frame(const char *body, size_t length)
{
	fprintf(stdout, "Content-Length: %zu\r\n\r\n", length);
	fwrite(body, 1, length, stdout);
	fflush(stdout);
}

static inline void lsp_protocol_write(zval *payload)
{
	const char *fallback = "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32603,\"message\":\"Failed to encode response\"}}";
	smart_str json = {0};

	if (php_json_encode(&json, payload, PHP_JSON_UNESCAPED_SLASHES | PHP_JSON_UNESCAPED_UNICODE) == SUCCESS && json.s) {
		smart_str_0(&json);
		lsp_protocol_write_frame(ZSTR_VAL(json.s), ZSTR_LEN(json.s));
		smart_str_free(&json);

		return;
	}

	lsp_protocol_write_frame(fallback, strlen(fallback));
	smart_str_free(&json);
}

/* Responses whose result was pre-encoded by the handler (completion lists)
 * are spliced into the envelope without re-encoding. */
static inline bool lsp_protocol_respond_raw(zval *id, zval *result)
{
	smart_str json = {0};
	zend_string *raw;

	if (!result || Z_TYPE_P(result) != IS_ARRAY || zend_hash_num_elements(Z_ARRVAL_P(result)) != 1) {
		return false;
	}

	raw = lsp_array_string(result, "__raw_json");
	if (!raw) {
		return false;
	}

	smart_str_appendl(&json, "{\"jsonrpc\":\"2.0\",\"id\":", sizeof("{\"jsonrpc\":\"2.0\",\"id\":") - 1);

	if (id) {
		if (php_json_encode(&json, id, PHP_JSON_UNESCAPED_SLASHES | PHP_JSON_UNESCAPED_UNICODE) != SUCCESS) {
			smart_str_free(&json);

			return false;
		}
	} else {
		smart_str_appendl(&json, "null", 4);
	}

	smart_str_appendl(&json, ",\"result\":", sizeof(",\"result\":") - 1);
	smart_str_append(&json, raw);
	smart_str_appendc(&json, '}');
	smart_str_0(&json);
	lsp_protocol_write_frame(ZSTR_VAL(json.s), ZSTR_LEN(json.s));
	smart_str_free(&json);

	return true;
}

extern void lsp_protocol_respond(zval *id, zval *result)
{
	zval payload, id_copy, result_copy;

	if (lsp_protocol_respond_raw(id, result)) {
		return;
	}

	array_init(&payload);
	add_assoc_string(&payload, "jsonrpc", "2.0");

	if (id) {
		ZVAL_COPY(&id_copy, id);
		add_assoc_zval(&payload, "id", &id_copy);
	} else {
		add_assoc_null(&payload, "id");
	}

	ZVAL_COPY(&result_copy, result);
	add_assoc_zval(&payload, "result", &result_copy);
	lsp_protocol_write(&payload);
	zval_ptr_dtor(&payload);
}

extern void lsp_protocol_error(zval *id, int code, const char *message)
{
	zval payload, error, id_copy;

	array_init(&payload);
	add_assoc_string(&payload, "jsonrpc", "2.0");

	if (id) {
		ZVAL_COPY(&id_copy, id);
		add_assoc_zval(&payload, "id", &id_copy);
	} else {
		add_assoc_null(&payload, "id");
	}

	array_init(&error);
	add_assoc_long(&error, "code", code);
	add_assoc_string(&error, "message", message);
	add_assoc_zval(&payload, "error", &error);
	lsp_protocol_write(&payload);
	zval_ptr_dtor(&payload);
}

extern void lsp_protocol_notify(const char *method, zval *params)
{
	zval payload, params_copy, empty_params;

	array_init(&payload);
	add_assoc_string(&payload, "jsonrpc", "2.0");
	add_assoc_string(&payload, "method", method);

	if (params) {
		ZVAL_COPY(&params_copy, params);
		add_assoc_zval(&payload, "params", &params_copy);
	} else {
		array_init(&empty_params);
		add_assoc_zval(&payload, "params", &empty_params);
	}

	lsp_protocol_write(&payload);
	zval_ptr_dtor(&payload);
}

extern void lsp_analyzer_project_status(const char *analyzer, const char *state, const char *message, zend_string *project_root)
{
	zval params;

	array_init(&params);
	add_assoc_string(&params, "analyzer", analyzer);
	add_assoc_string(&params, "state", state);
	add_assoc_string(&params, "message", message);

	if (project_root) {
		add_assoc_str(&params, "projectRoot", zend_string_copy(project_root));
	}

	lsp_protocol_notify("lsparrot.php/analyzerStatus", &params);
	zval_ptr_dtor(&params);
}

extern void lsp_analyzer_status(const char *analyzer, const char *state, const char *message)
{
	lsp_analyzer_project_status(analyzer, state, message, NULL);
}

static inline const char *lsp_analyzer_finished_message(const char *analyzer)
{
	if (strcmp(analyzer, "phpstan") == 0) {
		return "PHPStan diagnostics finished.";
	}

	return "Psalm diagnostics finished.";
}

static inline void lsp_reap_analyzer_job(lsp_server *server, lsp_analyzer_job *job, const char *analyzer)
{
	zend_string *project_root = NULL, *output_file = NULL;
	int status = 0;

	if (!job->running || !lsp_process_id_valid(job->pid)) {
		return;
	}

	if (!lsp_process_wait_nonblocking(job->pid, &status)) {
		return;
	}

	if (job->project_root) {
		project_root = zend_string_copy(job->project_root);
	}

	if (job->cache_file) {
		output_file = zend_string_copy(job->cache_file);
	}

	lsp_analyzer_job_clear(job);

	if (project_root) {
		lsp_analyzer_project_status(analyzer, "idle", lsp_analyzer_finished_message(analyzer), project_root);
	} else {
		lsp_analyzer_status(analyzer, "idle", lsp_analyzer_finished_message(analyzer));
	}

	if (project_root) {
		lsp_analyzer_project_finished(server, analyzer, project_root, output_file);
		zend_string_release(project_root);
	}

	if (output_file) {
		zend_string_release(output_file);
	}
}

extern void lsp_reap_analyzer_jobs(lsp_server *server)
{
	lsp_index_poll_worker(server);
	lsp_reap_analyzer_job(server, &server->phpstan_job, "phpstan");
	lsp_reap_analyzer_job(server, &server->psalm_job, "psalm");
	lsp_psalm_ls_pump(server, 0.0);
	lsp_reap_analyzer_completion_jobs();
}

static inline void lsp_window_show_message(zend_long type, const char *message)
{
	zval params;

	array_init(&params);
	add_assoc_long(&params, "type", type);
	add_assoc_string(&params, "message", message);
	lsp_protocol_notify("window/showMessage", &params);
	zval_ptr_dtor(&params);
}

extern void lsp_analyzer_unavailable(const char *analyzer, const char *label)
{
	zend_string *message;
	zval params;

	message = strpprintf(0, "%s is not installed; falling back to LSParrot Engine.", label);

	array_init(&params);
	add_assoc_string(&params, "analyzer", analyzer);
	add_assoc_string(&params, "state", "error");
	add_assoc_str(&params, "message", zend_string_copy(message));
	add_assoc_string(&params, "missingAnalyzer", analyzer);
	lsp_protocol_notify("lsparrot.php/analyzerStatus", &params);
	zval_ptr_dtor(&params);
	lsp_window_show_message(1, ZSTR_VAL(message));
	zend_string_release(message);
}

static inline void lsp_active_driver(lsp_server *server, const char **driver, const char **label)
{
	if (server->phpstan_enabled && server->psalm_enabled) {
		*driver = "lsparrot+phpstan+psalm";
		*label = "LSParrot Engine + PHPStan + Psalm";

		return;
	}

	if (server->phpstan_enabled) {
		*driver = "lsparrot+phpstan";
		*label = "LSParrot Engine + PHPStan";

		return;
	}

	if (server->psalm_enabled) {
		*driver = "lsparrot+psalm";
		*label = "LSParrot Engine + Psalm";

		return;
	}

	*driver = "lsparrot";
	*label = "LSParrot Engine";
}

extern void lsp_driver_status(lsp_server *server)
{
	const char *driver, *label;
	zend_string *message;
	zval params;

	lsp_active_driver(server, &driver, &label);
	message = strpprintf(0, "Using %s analyzer.", label);

	array_init(&params);
	add_assoc_string(&params, "analyzer", "driver");
	add_assoc_string(&params, "state", "idle");
	add_assoc_string(&params, "driver", driver);
	add_assoc_string(&params, "label", label);
	add_assoc_str(&params, "message", message);
	lsp_protocol_notify("lsparrot.php/analyzerStatus", &params);
	zval_ptr_dtor(&params);
}

static inline const char *lsp_analyzer_project_state_name(zend_long state)
{
	switch (state) {
		case LSP_ANALYZER_PROJECT_READY:
			return "ready";
		case LSP_ANALYZER_PROJECT_RUNNING:
			return "running";
		case LSP_ANALYZER_PROJECT_PENDING:
			return "pending";
		case LSP_ANALYZER_PROJECT_ERROR:
			return "error";
	}

	return "unknown";
}

static inline void lsp_add_analyzer_project_states(zval *target, HashTable *projects)
{
	zend_long state;
	zend_string *project_root;
	zval states, *state_zv;

	array_init(&states);

	ZEND_HASH_FOREACH_STR_KEY_VAL(projects, project_root, state_zv) {
		if (!project_root || Z_TYPE_P(state_zv) != IS_LONG) {
			continue;
		}
		state = Z_LVAL_P(state_zv);
		add_assoc_string(&states, ZSTR_VAL(project_root), lsp_analyzer_project_state_name(state));
	} ZEND_HASH_FOREACH_END();

	add_assoc_zval(target, "projects", &states);
}

static inline void lsp_add_analyzer_status_entry(zval *target, const char *name, bool enabled, bool running, HashTable *projects)
{
	zval entry;

	array_init(&entry);
	add_assoc_bool(&entry, "enabled", enabled);
	add_assoc_bool(&entry, "running", running);
	lsp_add_analyzer_project_states(&entry, projects);
	add_assoc_zval(target, name, &entry);
}

static inline void lsp_add_perf_stats(lsp_server *server, zval *target)
{
	lsp_perf_counter *counter;
	zend_string *method;
	zval stats, entry;

	array_init(&stats);

	ZEND_HASH_FOREACH_STR_KEY_PTR(&server->perf_stats, method, counter) {
		if (!method || !counter) {
			continue;
		}

		array_init(&entry);
		add_assoc_long(&entry, "count", (zend_long) counter->count);
		add_assoc_double(&entry, "totalMs", counter->total_seconds * 1000.0);
		add_assoc_double(&entry, "maxMs", counter->max_seconds * 1000.0);
		add_assoc_double(&entry, "avgMs", counter->count > 0 ? (counter->total_seconds * 1000.0) / (double) counter->count : 0.0);
		add_assoc_zval(&stats, ZSTR_VAL(method), &entry);
	} ZEND_HASH_FOREACH_END();

	add_assoc_zval(target, "performance", &stats);
}

extern void lsp_server_status(lsp_server *server, zval *return_value)
{
	lsp_symbol_index_header *header = NULL;
	zend_long memory_limit = PG(memory_limit);
	zval memory, symbol_index, processes, analyzers;
	uint32_t symbol_count = 0;
	size_t memory_current, memory_peak, symbol_index_used = 0, symbol_index_capacity = server->symbol_index.size;

	memory_current = zend_memory_usage(true);
	memory_peak = zend_memory_peak_usage(true);

	lsp_reap_analyzer_jobs(server);

	if (server->symbol_index.available && server->symbol_index.addr) {
		header = (lsp_symbol_index_header *) server->symbol_index.addr;
		if (header->magic == LSP_SYMBOL_INDEX_MAGIC && header->used <= header->capacity) {
			symbol_index_used = header->used;
			symbol_index_capacity = header->capacity;
			symbol_count = header->symbol_count;
		}
	}

	array_init(return_value);

	array_init(&memory);

	if (memory_peak < memory_current) {
		memory_peak = memory_current;
	}

	add_assoc_long(&memory, "current", (zend_long) memory_current);
	add_assoc_long(&memory, "peak", (zend_long) memory_peak);
	add_assoc_long(&memory, "max", memory_limit > 0 ? memory_limit : 0);
	add_assoc_zval(return_value, "memory", &memory);

	array_init(&symbol_index);
	add_assoc_bool(&symbol_index, "available", server->symbol_index.available);
	add_assoc_bool(&symbol_index, "indexing", server->index_worker_running);
	add_assoc_long(&symbol_index, "used", (zend_long) symbol_index_used);
	add_assoc_long(&symbol_index, "max", (zend_long) symbol_index_capacity);
	add_assoc_long(&symbol_index, "symbols", symbol_count);
	add_assoc_zval(return_value, "symbolIndex", &symbol_index);

	array_init(&processes);
	add_assoc_long(&processes, "active", lsp_active_process_count(server));
	add_assoc_long(&processes, "configured", server->options.worker_count);
	add_assoc_bool(&processes, "phpstanRunning", server->phpstan_job.running);
	add_assoc_bool(&processes, "psalmRunning", server->psalm_job.running);
	add_assoc_zval(return_value, "processes", &processes);

	array_init(&analyzers);
	lsp_add_analyzer_status_entry(&analyzers, "phpstan", server->phpstan_enabled, server->phpstan_job.running, &server->phpstan_projects);
	lsp_add_analyzer_status_entry(&analyzers, "psalm", server->psalm_enabled, server->psalm_job.running, &server->psalm_projects);
	lsp_add_analyzer_status_entry(&analyzers, "psalm-ls", server->psalm_ls_enabled, false, &server->psalm_ls_project_states);
	add_assoc_zval(return_value, "analyzers", &analyzers);

	lsp_runner_status(server, return_value);
	lsp_add_perf_stats(server, return_value);
}

/* ------------------------------------------------------------------------
 * Non-blocking transport: a poll(2)-driven receive buffer feeding a pending
 * message queue. While waiting for client input the server keeps pumping
 * analyzer jobs, so diagnostics are published as soon as they are ready
 * instead of on the next client message. The queue also enables
 * $/cancelRequest support and coalescing of bursty didChange notifications.
 * Windows falls back to the historical blocking reader (no queue lookahead).
 * ------------------------------------------------------------------------ */

static smart_str lsp_rx_buffer = {0};
static size_t lsp_rx_offset = 0;
static bool lsp_rx_eof = false;
static zval lsp_pending_messages;
static uint32_t lsp_pending_head = 0;
static bool lsp_pending_initialized = false;
static HashTable lsp_cancelled_ids;
static bool lsp_cancelled_initialized = false;

static inline void lsp_transport_ensure_initialized(void)
{
	if (!lsp_pending_initialized) {
		array_init(&lsp_pending_messages);
		lsp_pending_initialized = true;
	}

	if (!lsp_cancelled_initialized) {
		zend_hash_init(&lsp_cancelled_ids, 8, NULL, NULL, 0);
		lsp_cancelled_initialized = true;
	}
}

extern void lsp_transport_shutdown(void)
{
	smart_str_free(&lsp_rx_buffer);
	lsp_rx_offset = 0;
	lsp_rx_eof = false;

	if (lsp_pending_initialized) {
		zval_ptr_dtor(&lsp_pending_messages);
		lsp_pending_initialized = false;
	}
	lsp_pending_head = 0;

	if (lsp_cancelled_initialized) {
		zend_hash_destroy(&lsp_cancelled_ids);
		lsp_cancelled_initialized = false;
	}
}

static inline void lsp_rx_compact(void)
{
	size_t remaining;

	if (!lsp_rx_buffer.s || lsp_rx_offset == 0) {
		return;
	}

	remaining = ZSTR_LEN(lsp_rx_buffer.s) - lsp_rx_offset;
	if (remaining > 0) {
		memmove(ZSTR_VAL(lsp_rx_buffer.s), ZSTR_VAL(lsp_rx_buffer.s) + lsp_rx_offset, remaining);
	}

	ZSTR_LEN(lsp_rx_buffer.s) = remaining;
	lsp_rx_offset = 0;
}

/* Parse one complete Content-Length framed message from the receive buffer.
 * Accepts both \r\n and bare \n header line endings. Returns false when the
 * buffered bytes do not yet contain a complete frame. */
static inline bool lsp_transport_parse_frame(zval *message)
{
	const char *data, *line_start, *line_end, *headers_end = NULL;
	char *body;
	size_t available, content_length = 0, header_size;
	bool has_length = false;

	if (!lsp_rx_buffer.s) {
		return false;
	}

	data = ZSTR_VAL(lsp_rx_buffer.s) + lsp_rx_offset;
	available = ZSTR_LEN(lsp_rx_buffer.s) - lsp_rx_offset;
	line_start = data;

	while ((size_t) (line_start - data) < available) {
		line_end = memchr(line_start, '\n', available - (line_start - data));
		if (!line_end) {
			return false;
		}

		if (line_start == line_end || (line_end - line_start == 1 && line_start[0] == '\r')) {
			headers_end = line_end + 1;
			break;
		}

		if ((size_t) (line_end - line_start) > sizeof("Content-Length:") - 1 &&
			strncasecmp(line_start, "Content-Length:", sizeof("Content-Length:") - 1) == 0
		) {
			content_length = (size_t) strtoull(line_start + sizeof("Content-Length:") - 1, NULL, 10);
			has_length = true;
		}

		line_start = line_end + 1;
	}

	if (!headers_end || !has_length || content_length == 0) {
		if (headers_end) {
			/* Skip a malformed frame header so the stream can resynchronize. */
			lsp_rx_offset += headers_end - data;
		}

		return false;
	}

	header_size = headers_end - data;
	if (available < header_size + content_length) {
		return false;
	}

	/* The JSON scanner requires a NUL sentinel at body[length]; the framed
	 * body sits mid-buffer followed by the next frame, so decode from a
	 * terminated copy. */
	body = emalloc(content_length + 1);
	memcpy(body, headers_end, content_length);
	body[content_length] = '\0';
	ZVAL_UNDEF(message);
	php_json_decode_ex(message, body, content_length, PHP_JSON_OBJECT_AS_ARRAY, 512);
	efree(body);
	lsp_rx_offset += header_size + content_length;
	lsp_rx_compact();

	if (Z_TYPE_P(message) != IS_ARRAY) {
		if (!Z_ISUNDEF_P(message)) {
			zval_ptr_dtor(message);
		}
		ZVAL_UNDEF(message);

		return false;
	}

	return true;
}

static inline void lsp_transport_drain_frames(void)
{
	zval message;

	lsp_transport_ensure_initialized();
	while (lsp_transport_parse_frame(&message)) {
		add_next_index_zval(&lsp_pending_messages, &message);
	}
}

#if defined(_WIN32)
static inline int lsp_transport_fill_from_pipe(HANDLE handle, double timeout_seconds)
{
	char chunk[8192];
	DWORD available, n, wait_ms, wanted;
	double deadline, now, remaining;

	deadline = lsp_now_seconds() + timeout_seconds;
	for (;;) {
		available = 0;
		if (!PeekNamedPipe(handle, NULL, 0, NULL, &available, NULL)) {
			lsp_rx_eof = true;

			return -1;
		}

		if (available > 0) {
			wanted = available < sizeof(chunk) ? available : (DWORD) sizeof(chunk);
			n = 0;
			if (!ReadFile(handle, chunk, wanted, &n, NULL)) {
				lsp_rx_eof = true;

				return -1;
			}

			if (n == 0) {
				lsp_rx_eof = true;

				return -1;
			}

			smart_str_appendl(&lsp_rx_buffer, chunk, (size_t) n);

			return 1;
		}

		if (timeout_seconds == 0.0) {
			return 0;
		}

		now = lsp_now_seconds();
		if (now >= deadline) {
			return 0;
		}

		remaining = deadline - now;
		wait_ms = (DWORD) (remaining * 1000.0);
		if (wait_ms == 0) {
			wait_ms = 1;
		}
		if (wait_ms > 10) {
			wait_ms = 10;
		}

		Sleep(wait_ms);
	}
}

static inline int lsp_transport_fill(double timeout_seconds)
{
	char chunk[8192];
	HANDLE handle;
	size_t n;

	if (lsp_rx_eof) {
		return -1;
	}

	handle = GetStdHandle(STD_INPUT_HANDLE);
	if (timeout_seconds >= 0.0 &&
		handle != NULL &&
		handle != INVALID_HANDLE_VALUE &&
		GetFileType(handle) == FILE_TYPE_PIPE
	) {
		return lsp_transport_fill_from_pipe(handle, timeout_seconds);
	}

	n = fread(chunk, 1, sizeof(chunk), stdin);
	if (n == 0) {
		lsp_rx_eof = true;

		return -1;
	}

	smart_str_appendl(&lsp_rx_buffer, chunk, n);

	return 1;
}
#else
static inline int lsp_transport_fill(double timeout_seconds)
{
	struct pollfd pfd;
	char chunk[65536];
	ssize_t n;
	int timeout_ms, result;

	if (lsp_rx_eof) {
		return -1;
	}

	pfd.fd = STDIN_FILENO;
	pfd.events = POLLIN;
	pfd.revents = 0;
	timeout_ms = timeout_seconds < 0 ? -1 : (int) (timeout_seconds * 1000.0);

	result = poll(&pfd, 1, timeout_ms);
	if (result == 0) {
		return 0;
	}

	if (result < 0) {
		return errno == EINTR ? 0 : -1;
	}

	if ((pfd.revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
		return 0;
	}

	n = read(STDIN_FILENO, chunk, sizeof(chunk));
	if (n > 0) {
		smart_str_appendl(&lsp_rx_buffer, chunk, (size_t) n);

		return 1;
	}

	if (n == 0 || (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
		lsp_rx_eof = true;

		return -1;
	}

	return 0;
}
#endif

static inline bool lsp_server_has_background_work(lsp_server *server)
{
	return server->index_worker_running ||
		server->phpstan_job.running ||
		server->psalm_job.running ||
		server->phpstan_completion_job.running ||
		server->psalm_completion_job.running ||
		zend_hash_num_elements(&server->psalm_ls_projects) > 0
	;
}

static inline zend_string *lsp_request_id_key(zval *id)
{
	if (!id) {
		return NULL;
	}

	if (Z_TYPE_P(id) == IS_LONG) {
		return strpprintf(0, "i:" ZEND_LONG_FMT, Z_LVAL_P(id));
	}

	if (Z_TYPE_P(id) == IS_STRING) {
		return strpprintf(0, "s:%s", Z_STRVAL_P(id));
	}

	return NULL;
}

static inline bool lsp_message_method_equals(zval *message, const char *method)
{
	zend_string *value = lsp_array_string(message, "method");

	return value && php_ver_abstract.string_equals_cstr(value, method, strlen(method));
}

static inline void lsp_register_cancellation(zval *message)
{
	zend_string *key;
	zval *params = lsp_array_find(message, "params"), *id;

	if (!params || Z_TYPE_P(params) != IS_ARRAY) {
		return;
	}

	id = zend_hash_str_find(Z_ARRVAL_P(params), "id", sizeof("id") - 1);
	key = lsp_request_id_key(id);
	if (!key) {
		return;
	}

	lsp_transport_ensure_initialized();
	zend_hash_add_empty_element(&lsp_cancelled_ids, key);
	zend_string_release(key);
}

static inline bool lsp_pending_take_cancellation(zend_string *id_key)
{
	zend_string *queued_key;
	zval *message, *params, *queued_id, null_zv;
	uint32_t i, count;
	bool matched;

	if (!lsp_pending_initialized) {
		return false;
	}

	count = zend_hash_num_elements(Z_ARRVAL(lsp_pending_messages));
	for (i = lsp_pending_head; i < count; i++) {
		message = zend_hash_index_find(Z_ARRVAL(lsp_pending_messages), i);
		if (!message || Z_TYPE_P(message) != IS_ARRAY || !lsp_message_method_equals(message, "$/cancelRequest")) {
			continue;
		}

		params = lsp_array_find(message, "params");
		queued_id = params && Z_TYPE_P(params) == IS_ARRAY
			? zend_hash_str_find(Z_ARRVAL_P(params), "id", sizeof("id") - 1)
			: NULL
		;
		queued_key = lsp_request_id_key(queued_id);
		if (!queued_key) {
			continue;
		}

		matched = zend_string_equals(queued_key, id_key);
		zend_string_release(queued_key);
		if (matched) {
			/* Consume the queued cancellation so it cannot linger. */
			ZVAL_NULL(&null_zv);
			zend_hash_index_update(Z_ARRVAL(lsp_pending_messages), i, &null_zv);

			return true;
		}
	}

	return false;
}

extern bool lsp_protocol_request_is_cancelled(zval *id)
{
	zend_string *key;
	bool cancelled;

	key = lsp_request_id_key(id);
	if (!key) {
		return false;
	}

	cancelled = false;
	if (lsp_cancelled_initialized && zend_hash_exists(&lsp_cancelled_ids, key)) {
		zend_hash_del(&lsp_cancelled_ids, key);
		cancelled = true;
	}

	if (!cancelled) {
		cancelled = lsp_pending_take_cancellation(key);
	}

	zend_string_release(key);

	return cancelled;
}

/* True when a newer didChange (or didClose) for the same document is already
 * queued with no request in between: the current change's diagnostics would
 * be immediately superseded, so the caller may skip publishing them. */
extern bool lsp_protocol_queue_supersedes_change(zend_string *uri)
{
	zend_string *queued_uri;
	zval *message, *params, *td;
	uint32_t i, count;

	if (!lsp_pending_initialized) {
		return false;
	}

	count = zend_hash_num_elements(Z_ARRVAL(lsp_pending_messages));
	for (i = lsp_pending_head; i < count; i++) {
		message = zend_hash_index_find(Z_ARRVAL(lsp_pending_messages), i);
		if (!message || Z_TYPE_P(message) != IS_ARRAY) {
			continue;
		}

		if (zend_hash_str_exists(Z_ARRVAL_P(message), "id", sizeof("id") - 1)) {
			return false;
		}

		if (!lsp_message_method_equals(message, "textDocument/didChange") &&
			!lsp_message_method_equals(message, "textDocument/didClose")
		) {
			continue;
		}

		params = lsp_array_find(message, "params");
		td = params ? lsp_array_find(params, "textDocument") : NULL;
		queued_uri = td ? lsp_array_string(td, "uri") : NULL;
		if (queued_uri && zend_string_equals(queued_uri, uri)) {
			return true;
		}
	}

	return false;
}

static inline bool lsp_pending_pop(zval *message)
{
	zval *head;
	uint32_t count;

	if (!lsp_pending_initialized) {
		return false;
	}

	count = zend_hash_num_elements(Z_ARRVAL(lsp_pending_messages));
	while (lsp_pending_head < count) {
		head = zend_hash_index_find(Z_ARRVAL(lsp_pending_messages), lsp_pending_head);
		lsp_pending_head++;

		if (!head || Z_TYPE_P(head) != IS_ARRAY) {
			continue;
		}

		ZVAL_COPY(message, head);

		if (lsp_pending_head >= count) {
			zend_hash_clean(Z_ARRVAL(lsp_pending_messages));
			lsp_pending_head = 0;
		}

		return true;
	}

	if (count > 0) {
		zend_hash_clean(Z_ARRVAL(lsp_pending_messages));
		lsp_pending_head = 0;
	}

	return false;
}

extern bool lsp_protocol_next_message(lsp_server *server, zval *message)
{
	double timeout;
	int filled;

	lsp_transport_ensure_initialized();

	for (;;) {
		lsp_transport_drain_frames();

		if (lsp_pending_pop(message)) {
			/* Track cancellations as soon as they pass through the queue. */
			if (lsp_message_method_equals(message, "$/cancelRequest")) {
				lsp_register_cancellation(message);
			}

			return true;
		}

		if (lsp_rx_eof) {
			return false;
		}

		timeout = lsp_server_has_background_work(server) ? 0.05 : -1.0;
		filled = lsp_transport_fill(timeout);
		if (filled == 0) {
			/* Idle tick: reap finished analyzers and pump the Psalm LS
			 * proxy so diagnostics reach the client without waiting for
			 * the next request. */
			lsp_reap_analyzer_jobs(server);
		}
	}
}

extern void lsp_document_analyze(lsp_document *document)
{
	if (!Z_ISUNDEF(document->lsparrot)) {
		zval_ptr_dtor(&document->lsparrot);
		ZVAL_UNDEF(&document->lsparrot);
	}

	lsp_document_derived_invalidate(document);
	lsp_lsparrot_parse_to_zval(&document->lsparrot, document->text, document->uri);
}

extern void lsp_document_derived_invalidate(lsp_document *document)
{
	if (document->derived_namespace) {
		if (document->derived_namespace != zend_empty_string) {
			zend_string_release(document->derived_namespace);
		}
		document->derived_namespace = NULL;
	}

	if (document->derived_imports) {
		zend_hash_destroy(document->derived_imports);
		efree(document->derived_imports);
		document->derived_imports = NULL;
	}

	document->derived_import_insert_offset = 0;
	document->derived_import_after_use = false;
	document->derived_valid = false;
}

extern void lsp_document_derived_ensure(lsp_document *document)
{
	if (document->derived_valid) {
		return;
	}

	document->derived_namespace = lsp_document_namespace(document->text);
	document->derived_imports = emalloc(sizeof(HashTable));
	zend_hash_init(document->derived_imports, 8, NULL, NULL, 0);
	lsp_document_collect_imports(document->text, document->derived_imports);
	document->derived_import_insert_offset = lsp_import_insert_offset(document->text, &document->derived_import_after_use);
	document->derived_valid = true;
}

extern zend_string *lsp_document_namespace_cached(lsp_document *document)
{
	lsp_document_derived_ensure(document);

	return document->derived_namespace;
}

extern size_t lsp_document_import_insert_offset_cached(lsp_document *document, bool *after_existing_use)
{
	lsp_document_derived_ensure(document);
	*after_existing_use = document->derived_import_after_use;

	return document->derived_import_insert_offset;
}

extern void lsp_perf_stats_record(lsp_server *server, zend_string *method, double elapsed_seconds)
{
	lsp_perf_counter *counter, fresh;

	counter = zend_hash_find_ptr(&server->perf_stats, method);
	if (!counter) {
		memset(&fresh, 0, sizeof(fresh));
		counter = zend_hash_add_mem(&server->perf_stats, method, &fresh, sizeof(fresh));
		if (!counter) {
			return;
		}
	}

	counter->count++;
	counter->total_seconds += elapsed_seconds;
	if (elapsed_seconds > counter->max_seconds) {
		counter->max_seconds = elapsed_seconds;
	}
}
