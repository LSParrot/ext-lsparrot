/*
  +----------------------------------------------------------------------+
  | LSParrot PHP LSP Extension                                              |
  +----------------------------------------------------------------------+
  | Copyright (c) LSParrot GitHub Organization                              |
  +----------------------------------------------------------------------+
  | This source file is subject to the 0BSD license that is              |
  | bundled with this package in the file LICENSE.                       |
  +----------------------------------------------------------------------+
  | Author: Go Kudo <zeriyoshi@gmail.com>                                |
  +----------------------------------------------------------------------+
*/

#include "lsp_internal.h"

extern void lsp_document_destroy(zval *value)
{
	lsp_document *document = (lsp_document *) Z_PTR_P(value);

	if (!document) {
		return;
	}

	zend_string_release(document->uri);
	zend_string_release(document->path);
	zend_string_release(document->text);

	if (!Z_ISUNDEF(document->lsparrot)) {
		zval_ptr_dtor(&document->lsparrot);
	}

	efree(document);
}

extern void lsp_analyzer_job_clear(lsp_analyzer_job *job)
{
	if (lsp_process_id_valid(job->pid)) {
		lsp_process_close(job->pid);
	}

	if (job->uri) {
		zend_string_release(job->uri);
		job->uri = NULL;
	}

	if (job->cache_key) {
		zend_string_release(job->cache_key);
		job->cache_key = NULL;
	}

	if (job->cache_file) {
		zend_string_release(job->cache_file);
		job->cache_file = NULL;
	}

	if (job->project_root) {
		zend_string_release(job->project_root);
		job->project_root = NULL;
	}

	job->pid = LSP_INVALID_PROCESS_ID;
	job->version = 0;
	job->running = false;
}

extern void lsp_analyzer_job_destroy(lsp_analyzer_job *job)
{
	int status;

	status = 0;
	if (job->running && lsp_process_id_valid(job->pid)) {
		lsp_process_terminate(job->pid);
		lsp_process_wait(job->pid, &status);
	}

	if (job->cache_file) {
		VCWD_UNLINK(ZSTR_VAL(job->cache_file));
	}

	lsp_analyzer_job_clear(job);
}

static inline bool lsp_analyzer_job_matches_document(lsp_analyzer_job *job, lsp_document *document)
{
	return job->running &&
		job->uri &&
		zend_string_equals(job->uri, document->uri) &&
		job->version == document->version
	;
}

extern bool lsp_analyzer_jobs_running_for_document(lsp_server *server, lsp_document *document)
{
	return lsp_analyzer_job_matches_document(&server->phpstan_job, document) ||
		lsp_analyzer_job_matches_document(&server->psalm_job, document) ||
		lsp_analyzer_job_matches_document(&server->phpstan_completion_job, document) ||
		lsp_analyzer_job_matches_document(&server->psalm_completion_job, document)
	;
}

extern uint32_t lsp_active_process_count(lsp_server *server)
{
	uint32_t count = 0;

	if (server->phpstan_job.running && lsp_process_id_valid(server->phpstan_job.pid)) {
		count++;
	}

	if (server->psalm_job.running && lsp_process_id_valid(server->psalm_job.pid)) {
		count++;
	}

	if (server->phpstan_completion_job.running && lsp_process_id_valid(server->phpstan_completion_job.pid)) {
		count++;
	}

	if (server->psalm_completion_job.running && lsp_process_id_valid(server->psalm_completion_job.pid)) {
		count++;
	}

	return count;
}

static inline int lsp_hex_value(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}

	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}

	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}

	return -1;
}

static inline zend_string *lsp_raw_url_decode(const char *value, size_t length)
{
	zend_string *decoded = zend_string_alloc(length, 0);
	size_t i, out_length = 0;
	char *out = ZSTR_VAL(decoded);
	int hi, lo;

	for (i = 0; i < length; i++) {
		if (value[i] == '%' && i + 2 < length) {
			hi = lsp_hex_value(value[i + 1]);
			lo = lsp_hex_value(value[i + 2]);
			if (hi >= 0 && lo >= 0) {
				out[out_length++] = (char) ((hi << 4) | lo);
				i += 2;
				continue;
			}
		}
		out[out_length++] = value[i];
	}

	out[out_length] = '\0';
	ZSTR_LEN(decoded) = out_length;

	return decoded;
}

extern zend_string *lsp_uri_to_path(zend_string *uri)
{
	const char *value;
	zend_string *decoded;
	size_t length;
#if defined(_WIN32)
	const char *path_start;
	zend_string *path;
#endif

	value = ZSTR_VAL(uri);
	length = ZSTR_LEN(uri);
	if (length >= sizeof("file://") - 1 && memcmp(value, "file://", sizeof("file://") - 1) == 0) {
		value += sizeof("file://") - 1;
		length -= sizeof("file://") - 1;

		decoded = lsp_raw_url_decode(value, length);
#if defined(_WIN32)
		if (ZSTR_LEN(decoded) >= sizeof("localhost") - 1 &&
			strncasecmp(ZSTR_VAL(decoded), "localhost", sizeof("localhost") - 1) == 0 &&
			(ZSTR_LEN(decoded) == sizeof("localhost") - 1 || lsp_is_path_separator(ZSTR_VAL(decoded)[sizeof("localhost") - 1]))
		) {
			path_start = ZSTR_VAL(decoded) + sizeof("localhost") - 1;
			path = zend_string_init(path_start, ZSTR_LEN(decoded) - (sizeof("localhost") - 1), 0);
			zend_string_release(decoded);
			decoded = path;
		}

		if (ZSTR_LEN(decoded) >= 3 &&
			lsp_is_path_separator(ZSTR_VAL(decoded)[0]) &&
			isalpha((unsigned char) ZSTR_VAL(decoded)[1]) &&
			ZSTR_VAL(decoded)[2] == ':'
		) {
			path = zend_string_init(ZSTR_VAL(decoded) + 1, ZSTR_LEN(decoded) - 1, 0);
			zend_string_release(decoded);

			return path;
		}

		if (!lsp_path_is_absolute(decoded) && strchr(ZSTR_VAL(decoded), '/') != NULL) {
			path = strpprintf(0, "//%s", ZSTR_VAL(decoded));
			zend_string_release(decoded);

			return path;
		}
#endif

		return decoded;
	}

	return zend_string_copy(uri);
}

extern zend_string *lsp_uri_from_path(zend_string *path)
{
#if defined(_WIN32)
	zend_string *uri_path, *uri;
	size_t i;

	uri_path = zend_string_init(ZSTR_VAL(path), ZSTR_LEN(path), 0);
	for (i = 0; i < ZSTR_LEN(uri_path); i++) {
		if (ZSTR_VAL(uri_path)[i] == '\\') {
			ZSTR_VAL(uri_path)[i] = '/';
		}
	}

	if (ZSTR_LEN(uri_path) >= 2 && lsp_is_path_separator(ZSTR_VAL(uri_path)[0]) && lsp_is_path_separator(ZSTR_VAL(uri_path)[1])) {
		uri = strpprintf(0, "file:%s", ZSTR_VAL(uri_path));
	} else if (ZSTR_LEN(uri_path) >= 2 && isalpha((unsigned char) ZSTR_VAL(uri_path)[0]) && ZSTR_VAL(uri_path)[1] == ':') {
		uri = strpprintf(0, "file:///%s", ZSTR_VAL(uri_path));
	} else {
		uri = strpprintf(0, "file://%s", ZSTR_VAL(uri_path));
	}

	zend_string_release(uri_path);

	return uri;
#else
	return strpprintf(0, "file://%s", ZSTR_VAL(path));
#endif
}

extern zend_string *lsp_read_file(zend_string *path)
{
	zend_string *contents;
	php_stream *stream;

	stream = php_stream_open_wrapper(ZSTR_VAL(path), "rb", IGNORE_PATH, NULL);
	if (!stream) {
		return zend_empty_string;
	}

	contents = php_stream_copy_to_mem(stream, PHP_STREAM_COPY_ALL, 0);
	php_stream_close(stream);
	if (!contents) {
		return zend_empty_string;
	}

	return contents;
}

extern zval *lsp_array_find(zval *array, const char *key)
{
	if (!array || Z_TYPE_P(array) != IS_ARRAY) {
		return NULL;
	}

	return zend_hash_str_find(Z_ARRVAL_P(array), key, strlen(key));
}

extern zend_string *lsp_array_string(zval *array, const char *key)
{
	zval *value = lsp_array_find(array, key);

	return value && Z_TYPE_P(value) == IS_STRING ? Z_STR_P(value) : NULL;
}

extern zend_long lsp_array_long(zval *array, const char *key, zend_long fallback)
{
	zval *value = lsp_array_find(array, key);

	return value && Z_TYPE_P(value) == IS_LONG ? Z_LVAL_P(value) : fallback;
}

static inline double lsp_array_double(zval *array, const char *key, double fallback)
{
	zval *value = lsp_array_find(array, key);

	if (!value) {
		return fallback;
	}

	if (Z_TYPE_P(value) == IS_DOUBLE) {
		return Z_DVAL_P(value);
	}

	if (Z_TYPE_P(value) == IS_LONG) {
		return (double) Z_LVAL_P(value);
	}

	return fallback;
}

static inline bool lsp_array_bool(zval *array, const char *key, bool fallback)
{
	zval *value = lsp_array_find(array, key);

	return value && Z_TYPE_P(value) == IS_TRUE ? true : (value && Z_TYPE_P(value) == IS_FALSE ? false : fallback);
}

static inline size_t lsp_parse_size_value(zval *value, size_t fallback)
{
	const char *raw, *end;
	zend_string *string;
	size_t multiplier = 1;
	double number;

	if (!value) {
		return fallback;
	}

	if (Z_TYPE_P(value) == IS_LONG && Z_LVAL_P(value) > 0) {
		return (size_t) Z_LVAL_P(value);
	}

	if (Z_TYPE_P(value) != IS_STRING) {
		return fallback;
	}

	string = Z_STR_P(value);
	raw = ZSTR_VAL(string);
	errno = 0;
	number = strtod(raw, (char **) &end);
	if (errno != 0 || number <= 0) {
		return fallback;
	}

	while (*end && isspace((unsigned char) *end)) {
		end++;
	}

	if (*end == 'k' || *end == 'K') {
		multiplier = 1024u;
	} else if (*end == 'm' || *end == 'M') {
		multiplier = 1024u * 1024u;
	} else if (*end == 'g' || *end == 'G') {
		multiplier = 1024u * 1024u * 1024u;
	}

	return (size_t) (number * (double) multiplier);
}

static inline void lsp_options_parse_analyzer(lsp_options *options, zval *value)
{
	zval *entry;

	options->analyzer_auto = true;
	options->analyzer_phpstan = false;
	options->analyzer_psalm = false;
	options->analyzer_psalm_ls = false;

	if (!value || lsp_zval_string_equals_literal(value, "auto")) {
		return;
	}

	if (lsp_zval_string_equals_literal(value, "lsparrot")) {
		options->analyzer_auto = false;

		return;
	}

	if (lsp_zval_string_equals_literal(value, "phpstan")) {
		options->analyzer_auto = false;
		options->analyzer_phpstan = true;

		return;
	}

	if (lsp_zval_string_equals_literal(value, "psalm")) {
		options->analyzer_auto = false;
		options->analyzer_psalm = true;

		return;
	}

	if (lsp_zval_string_equals_literal(value, "psalm-ls")) {
		options->analyzer_auto = false;
		options->analyzer_psalm_ls = true;

		return;
	}

	if (Z_TYPE_P(value) != IS_ARRAY) {
		return;
	}

	options->analyzer_auto = false;
	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(value), entry) {
		if (lsp_zval_string_equals_literal(entry, "phpstan")) {
			options->analyzer_phpstan = true;
		} else if (lsp_zval_string_equals_literal(entry, "psalm")) {
			options->analyzer_psalm = true;
		} else if (lsp_zval_string_equals_literal(entry, "psalm-ls")) {
			options->analyzer_psalm_ls = true;
		}
	} ZEND_HASH_FOREACH_END();
}

static inline void lsp_options_parse_worker_php_args(lsp_options *options, zval *value)
{
	zval *entry;
	uint32_t count = 0, index = 0;

	options->worker_php_args = NULL;
	options->worker_php_arg_count = 0;
	if (!value || Z_TYPE_P(value) != IS_ARRAY) {
		return;
	}

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(value), entry) {
		if (Z_TYPE_P(entry) == IS_STRING && Z_STRLEN_P(entry) > 0) {
			count++;
		}
	} ZEND_HASH_FOREACH_END();

	if (count == 0) {
		return;
	}

	options->worker_php_args = ecalloc(count, sizeof(zend_string *));
	options->worker_php_arg_count = count;
	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(value), entry) {
		if (Z_TYPE_P(entry) == IS_STRING && Z_STRLEN_P(entry) > 0) {
			options->worker_php_args[index++] = zend_string_copy(Z_STR_P(entry));
		}
	} ZEND_HASH_FOREACH_END();
}

static inline void lsp_options_parse_phpstan(lsp_options *options, zval *value)
{
	zend_long level;

	if (!value || Z_TYPE_P(value) != IS_ARRAY) {
		return;
	}

	level = lsp_array_long(value, "level", options->phpstan_level);
	options->phpstan_level = level >= 0 ? level : options->phpstan_level;
}

static inline void lsp_options_parse_psalm_cli(lsp_options *options, zval *value)
{
	zend_long level;

	if (!value || Z_TYPE_P(value) != IS_ARRAY) {
		return;
	}

	level = lsp_array_long(value, "level", options->psalm_level);
	options->psalm_level = level >= 0 ? level : options->psalm_level;
}

static inline lsp_psalm_transport lsp_parse_psalm_transport(zval *value, lsp_psalm_transport fallback)
{
	if (!value || Z_TYPE_P(value) != IS_STRING) {
		return fallback;
	}

	if (zend_string_equals_literal(Z_STR_P(value), "cli")) {
		return LSP_PSALM_TRANSPORT_CLI;
	}

	if (zend_string_equals_literal(Z_STR_P(value), "languageServer") ||
		zend_string_equals_literal(Z_STR_P(value), "language-server") ||
		zend_string_equals_literal(Z_STR_P(value), "lsp")
	) {
		return LSP_PSALM_TRANSPORT_LANGUAGE_SERVER;
	}

	return LSP_PSALM_TRANSPORT_AUTO;
}

static inline void lsp_options_parse_psalm(lsp_options *options, zval *value)
{
	zend_long debounce, wait_ms;

	if (!value || Z_TYPE_P(value) != IS_ARRAY) {
		return;
	}

	options->psalm_transport = lsp_parse_psalm_transport(lsp_array_find(value, "transport"), options->psalm_transport);
	options->psalm_on_change = lsp_array_bool(value, "onChange", options->psalm_on_change);
	options->psalm_enable_autocomplete = lsp_array_bool(value, "enableAutocomplete", options->psalm_enable_autocomplete);
	options->psalm_enable_diagnostics = lsp_array_bool(value, "enableDiagnostics", options->psalm_enable_diagnostics);
	options->psalm_enable_hover = lsp_array_bool(value, "enableHover", options->psalm_enable_hover);
	options->psalm_enable_definition = lsp_array_bool(value, "enableDefinition", options->psalm_enable_definition);
	options->psalm_enable_signature_help = lsp_array_bool(value, "enableSignatureHelp", options->psalm_enable_signature_help);
	options->psalm_show_info = lsp_array_bool(value, "showInfo", options->psalm_show_info);
	options->psalm_live_dead_code_diagnostics = lsp_array_bool(value, "liveDeadCodeDiagnostics", options->psalm_live_dead_code_diagnostics);
	options->psalm_in_memory = lsp_array_bool(value, "inMemory", options->psalm_in_memory);
	debounce = lsp_array_long(value, "onChangeDebounceMs", options->psalm_on_change_debounce_ms);
	wait_ms = lsp_array_long(value, "maxResponseWaitMs", options->psalm_max_response_wait_ms);
	options->psalm_on_change_debounce_ms = debounce >= 0 ? debounce : options->psalm_on_change_debounce_ms;
	options->psalm_max_response_wait_ms = wait_ms >= 0 ? wait_ms : options->psalm_max_response_wait_ms;
}

extern void lsp_options_from_zval(lsp_options *options, zval *value)
{
	zend_long phpstan_level, psalm_level;
	zend_string *memory_limit;
	zval *symbol_index, *workers, *phpstan, *psalm;

	memset(options, 0, sizeof(*options));
	options->symbol_index_size = 64u * 1024u * 1024u;
	options->worker_count = 0;
	options->phpstan_level = 7;
	options->psalm_level = 3;
	options->memory_limit = zend_string_init("-1", sizeof("-1") - 1, 0);
	options->analyzer_diagnostics_timeout = 60.0;
	options->analyzer_auto = true;
	options->psalm_transport = LSP_PSALM_TRANSPORT_AUTO;
	options->psalm_on_change = true;
	options->psalm_enable_autocomplete = true;
	options->psalm_enable_diagnostics = true;
	options->psalm_enable_hover = true;
	options->psalm_enable_definition = true;
	options->psalm_enable_signature_help = true;
	options->psalm_show_info = false;
	options->psalm_live_dead_code_diagnostics = false;
	options->psalm_in_memory = false;
	options->psalm_on_change_debounce_ms = 500;
	options->psalm_max_response_wait_ms = 200;

	if (!value || Z_TYPE_P(value) != IS_ARRAY) {
		return;
	}

	lsp_options_parse_analyzer(options, lsp_array_find(value, "analyzer"));
	lsp_options_parse_worker_php_args(options, lsp_array_find(value, "workerPhpArgs"));

	memory_limit = lsp_array_string(value, "memoryLimit");
	if (memory_limit && ZSTR_LEN(memory_limit) > 0) {
		zend_string_release(options->memory_limit);
		options->memory_limit = zend_string_copy(memory_limit);
	}

	phpstan_level = lsp_array_long(value, "phpstanLevel", options->phpstan_level);
	options->phpstan_level = phpstan_level >= 0 ? phpstan_level : options->phpstan_level;
	psalm_level = lsp_array_long(value, "psalmLevel", options->psalm_level);
	options->psalm_level = psalm_level >= 0 ? psalm_level : options->psalm_level;

	symbol_index = lsp_array_find(value, "symbolIndex");
	if (symbol_index && Z_TYPE_P(symbol_index) == IS_ARRAY) {
		options->symbol_index_size = lsp_parse_size_value(lsp_array_find(symbol_index, "size"), options->symbol_index_size);
	}

	workers = lsp_array_find(value, "workers");
	if (workers && Z_TYPE_P(workers) == IS_ARRAY) {
		options->worker_count = lsp_array_long(workers, "count", 0);
		if (options->worker_count < 0) {
			options->worker_count = 0;
		}

		options->analyzer_diagnostics_timeout = lsp_array_double(workers, "analyzerDiagnosticsTimeout", 60.0);
		if (options->analyzer_diagnostics_timeout < 0.0) {
			options->analyzer_diagnostics_timeout = 60.0;
		}
	}

	phpstan = lsp_array_find(value, "phpstan");
	lsp_options_parse_phpstan(options, phpstan);

	psalm = lsp_array_find(value, "psalm");
	lsp_options_parse_psalm_cli(options, psalm);
	lsp_options_parse_psalm(options, psalm);
}

extern void lsp_options_destroy(lsp_options *options)
{
	uint32_t i;

	for (i = 0; i < options->worker_php_arg_count; i++) {
		if (options->worker_php_args[i]) {
			zend_string_release(options->worker_php_args[i]);
		}
	}

	if (options->worker_php_args) {
		efree(options->worker_php_args);
	}

	if (options->memory_limit) {
		zend_string_release(options->memory_limit);
	}

	memset(options, 0, sizeof(*options));
}

extern void lsp_position_from_zval(zval *position, zend_long *line, zend_long *character)
{
	*line = 0;
	*character = 0;

	if (!position || Z_TYPE_P(position) != IS_ARRAY) {
		return;
	}

	*line = lsp_array_long(position, "line", 0);
	*character = lsp_array_long(position, "character", 0);

	if (*line < 0) {
		*line = 0;
	}

	if (*character < 0) {
		*character = 0;
	}
}

extern size_t lsp_offset_at(zend_string *text, zend_long line, zend_long character)
{
	const char *value = ZSTR_VAL(text), *next;
	zend_long current_line;
	size_t length = ZSTR_LEN(text), offset = 0;

	for (current_line = 0; current_line < line; current_line++) {
		next = memchr(value + offset, '\n', length - offset);
		if (!next) {
			return length;
		}

		offset = (size_t) (next - value) + 1;
	}

	if ((size_t) character > length - offset) {
		return length;
	}

	return offset + (size_t) character;
}

static inline bool lsp_is_word_char(char c)
{
	unsigned char ch = (unsigned char) c;

	return isalnum(ch) || ch == '_' || ch == '$' || ch == '\\';
}

extern zend_string *lsp_prefix_at(zend_string *text, size_t offset)
{
	const char *value = ZSTR_VAL(text);
	size_t start;

	if (offset > ZSTR_LEN(text)) {
		offset = ZSTR_LEN(text);
	}

	start = offset;
	while (start > 0 && lsp_is_word_char(value[start - 1])) {
		start--;
	}

	return zend_string_init(value + start, offset - start, 0);
}

extern zend_string *lsp_word_at(zend_string *text, size_t offset)
{
	const char *value = ZSTR_VAL(text);
	size_t start, end, length = ZSTR_LEN(text);

	if (offset > length) {
		offset = length;
	}

	start = offset;

	while (start > 0 && lsp_is_word_char(value[start - 1])) {
		start--;
	}

	end = offset;

	while (end < length && lsp_is_word_char(value[end])) {
		end++;
	}

	return zend_string_init(value + start, end - start, 0);
}

extern bool lsp_matches_prefix_string(zend_string *label, zend_string *prefix)
{
	const char *label_value = ZSTR_VAL(label), *prefix_value = ZSTR_VAL(prefix);
	size_t label_length = ZSTR_LEN(label), prefix_length = ZSTR_LEN(prefix), i;

	prefix_value = lsp_skip_global_namespace_prefix(prefix_value, &prefix_length);
	label_value = lsp_skip_global_namespace_prefix(label_value, &label_length);

	if (prefix_length > 0 && prefix_value[0] == '$') {
		prefix_value++;
		prefix_length--;
	}

	if (label_length > 0 && label_value[0] == '$') {
		label_value++;
		label_length--;
	}

	if (prefix_length == 0) {
		return true;
	}

	if (prefix_length > label_length) {
		return false;
	}

	for (i = 0; i < prefix_length; i++) {
		if (tolower((unsigned char) label_value[i]) != tolower((unsigned char) prefix_value[i])) {
			return false;
		}
	}

	return true;
}

extern bool lsp_matches_prefix_literal(const char *label, zend_string *prefix)
{
	zend_string *label_string = zend_string_init(label, strlen(label), 0);
	bool result = lsp_matches_prefix_string(label_string, prefix);

	zend_string_release(label_string);

	return result;
}

extern bool lsp_is_member_access_completion(zend_string *text, size_t offset, zend_string *prefix)
{
	const char *value = ZSTR_VAL(text);
	size_t i;

	if (offset > ZSTR_LEN(text)) {
		offset = ZSTR_LEN(text);
	}

	i = offset >= ZSTR_LEN(prefix) ? offset - ZSTR_LEN(prefix) : 0;

	while (i > 0 && isspace((unsigned char) value[i - 1])) {
		i--;
	}

	return (i >= 2 && value[i - 2] == '-' && value[i - 1] == '>') ||
		(i >= 3 && value[i - 3] == '?' && value[i - 2] == '-' && value[i - 1] == '>')
	;
}
