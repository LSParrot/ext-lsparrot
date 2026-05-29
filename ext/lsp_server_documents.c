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

static inline void lsp_protocol_write(zval *payload)
{
	const char *fallback = "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32603,\"message\":\"Failed to encode response\"}}";
	smart_str json = {0};

	if (php_json_encode(&json, payload, PHP_JSON_UNESCAPED_SLASHES | PHP_JSON_UNESCAPED_UNICODE) == SUCCESS && json.s) {
		smart_str_0(&json);
		fprintf(stdout, "Content-Length: %zu\r\n\r\n", ZSTR_LEN(json.s));
		fwrite(ZSTR_VAL(json.s), 1, ZSTR_LEN(json.s), stdout);
		fflush(stdout);
		smart_str_free(&json);

		return;
	}

	fprintf(stdout, "Content-Length: %zu\r\n\r\n", strlen(fallback));
	fwrite(fallback, 1, strlen(fallback), stdout);
	fflush(stdout);
	smart_str_free(&json);
}

extern void lsp_protocol_respond(zval *id, zval *result)
{
	zval payload, id_copy, result_copy;

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
}

extern bool lsp_protocol_read(zval *message)
{
	size_t length = 0, read_length = 0, n;
	bool has_length = false;
	char line[8192], *body;

	while (fgets(line, sizeof(line), stdin) != NULL) {
		if (line[0] == '\r' || line[0] == '\n' || line[0] == '\0') {
			break;
		}

		if (strncasecmp(line, "Content-Length:", sizeof("Content-Length:") - 1) == 0) {
			length = (size_t) strtoull(line + sizeof("Content-Length:") - 1, NULL, 10);
			has_length = true;
		}
	}

	if (!has_length || length == 0 || feof(stdin)) {
		return false;
	}

	body = emalloc(length + 1);
	while (read_length < length) {
		n = fread(body + read_length, 1, length - read_length, stdin);
		if (n == 0) {
			break;
		}

		read_length += n;
	}

	body[read_length] = '\0';

	if (read_length != length) {
		efree(body);
		return false;
	}

	ZVAL_UNDEF(message);
	php_json_decode_ex(message, body, length, PHP_JSON_OBJECT_AS_ARRAY, 512);
	efree(body);

	if (Z_TYPE_P(message) != IS_ARRAY) {
		if (!Z_ISUNDEF_P(message)) {
			zval_ptr_dtor(message);
		}

		return false;
	}

	return true;
}

extern void lsp_document_analyze(lsp_document *document)
{
	if (!Z_ISUNDEF(document->lsparrot)) {
		zval_ptr_dtor(&document->lsparrot);
		ZVAL_UNDEF(&document->lsparrot);
	}

	lsp_lsparrot_parse_to_zval(&document->lsparrot, document->text, document->uri);
}
