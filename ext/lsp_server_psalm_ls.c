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

#define LSP_PSALM_LS_WRITE_TIMEOUT 2.0
#define LSP_PSALM_LS_SHUTDOWN_TIMEOUT 0.5
#define LSP_PSALM_LS_TERMINATE_TIMEOUT 1.0
#define LSP_PSALM_LS_KILL_TIMEOUT 1.0

typedef struct _lsp_psalm_ls_buffer {
	char *data;
	size_t length;
	size_t capacity;
} lsp_psalm_ls_buffer;

typedef struct _lsp_psalm_ls_session {
	zend_string *root;
	zend_string *config;
	lsp_process_id pid;
	lsp_pipe_handle input_pipe;
	lsp_pipe_handle output_pipe;
	lsp_pipe_handle error_pipe;
	zend_long next_id;
	zend_long initialize_id;
	bool running;
	bool initialized;
	bool failed;
	HashTable diagnostics;
	HashTable pending;
	lsp_psalm_ls_buffer output_buffer;
	lsp_psalm_ls_buffer error_buffer;
} lsp_psalm_ls_session;

static inline double lsp_psalm_ls_now(void)
{
	return lsp_now_seconds();
}

static inline void lsp_psalm_ls_buffer_destroy(lsp_psalm_ls_buffer *buffer)
{
	if (buffer->data) {
		efree(buffer->data);
	}

	memset(buffer, 0, sizeof(*buffer));
}

static inline bool lsp_psalm_ls_buffer_append(lsp_psalm_ls_buffer *buffer, const char *data, size_t length)
{
	size_t capacity;

	if (length == 0) {
		return true;
	}

	if (buffer->length + length > buffer->capacity) {
		capacity = buffer->capacity == 0 ? 8192 : buffer->capacity;
		while (capacity < buffer->length + length) {
			capacity *= 2;
		}

		buffer->data = erealloc(buffer->data, capacity);
		buffer->capacity = capacity;
	}

	memcpy(buffer->data + buffer->length, data, length);
	buffer->length += length;

	return true;
}

static inline void lsp_psalm_ls_buffer_consume(lsp_psalm_ls_buffer *buffer, size_t length)
{
	if (length >= buffer->length) {
		buffer->length = 0;

		return;
	}

	memmove(buffer->data, buffer->data + length, buffer->length - length);
	buffer->length -= length;
}

static inline bool lsp_psalm_ls_find_header(lsp_psalm_ls_buffer *buffer, size_t *header_end, size_t *body_start)
{
	size_t i;

	for (i = 0; i + 3 < buffer->length; i++) {
		if (buffer->data[i] == '\r' && buffer->data[i + 1] == '\n' && buffer->data[i + 2] == '\r' && buffer->data[i + 3] == '\n') {
			*header_end = i;
			*body_start = i + 4;

			return true;
		}
	}

	for (i = 0; i + 1 < buffer->length; i++) {
		if (buffer->data[i] == '\n' && buffer->data[i + 1] == '\n') {
			*header_end = i;
			*body_start = i + 2;

			return true;
		}
	}

	return false;
}

static inline bool lsp_psalm_ls_header_length(lsp_psalm_ls_buffer *buffer, size_t header_end, size_t *length)
{
	const char *prefix = "Content-Length:";
	size_t prefix_length = strlen(prefix), line_start = 0, line_end, p;

	*length = 0;
	while (line_start < header_end) {
		line_end = line_start;
		while (line_end < header_end && buffer->data[line_end] != '\n' && buffer->data[line_end] != '\r') {
			line_end++;
		}

		if (line_end - line_start >= prefix_length &&
			strncasecmp(buffer->data + line_start, prefix, prefix_length) == 0
		) {
			p = line_start + prefix_length;
			while (p < line_end && isspace((unsigned char) buffer->data[p])) {
				p++;
			}
			*length = (size_t) strtoull(buffer->data + p, NULL, 10);

			return *length > 0;
		}

		line_start = line_end;
		while (line_start < header_end && (buffer->data[line_start] == '\n' || buffer->data[line_start] == '\r')) {
			line_start++;
		}
	}

	return false;
}

static inline bool lsp_psalm_ls_next_body(lsp_psalm_ls_buffer *buffer, zend_string **body)
{
	size_t header_end, body_start, length, frame_end;

	*body = NULL;
	if (!lsp_psalm_ls_find_header(buffer, &header_end, &body_start)) {
		return false;
	}

	if (!lsp_psalm_ls_header_length(buffer, header_end, &length)) {
		lsp_psalm_ls_buffer_consume(buffer, body_start);

		return false;
	}

	frame_end = body_start + length;
	if (buffer->length < frame_end) {
		return false;
	}

	*body = zend_string_init(buffer->data + body_start, length, 0);
	lsp_psalm_ls_buffer_consume(buffer, frame_end);

	return true;
}

static inline bool lsp_psalm_ls_send_zval(lsp_psalm_ls_session *session, zval *message)
{
	smart_str json = {0};
	zend_string *header;
	bool ok;

	if (!session->running || !lsp_pipe_handle_valid(session->input_pipe)) {
		return false;
	}

	php_json_encode(&json, message, 0);
	if (!json.s) {
		return false;
	}

	smart_str_0(&json);
	header = strpprintf(0, "Content-Length: %zu\r\n\r\n", ZSTR_LEN(json.s));
	ok = lsp_pipe_write_all_timeout(session->input_pipe, ZSTR_VAL(header), ZSTR_LEN(header), LSP_PSALM_LS_WRITE_TIMEOUT) &&
		lsp_pipe_write_all_timeout(session->input_pipe, ZSTR_VAL(json.s), ZSTR_LEN(json.s), LSP_PSALM_LS_WRITE_TIMEOUT)
	;
	zend_string_release(header);
	zend_string_release(json.s);
	if (!ok) {
		session->failed = true;
		session->running = false;
	}

	return ok;
}

static inline zend_string *lsp_psalm_ls_uri_for_path(zend_string *path)
{
	return lsp_uri_from_path(path);
}

static inline const char *lsp_psalm_ls_basename(zend_string *path)
{
	const char *slash;

	slash = lsp_last_path_separator_const(ZSTR_VAL(path));

	return slash ? slash + 1 : ZSTR_VAL(path);
}

static inline void lsp_psalm_ls_add_bool_arg(lsp_command *command, const char *name, bool value)
{
	zend_string *arg;

	arg = strpprintf(0, "%s=%s", name, value ? "true" : "false");
	lsp_command_add(command, ZSTR_VAL(arg));
	zend_string_release(arg);
}

static inline bool lsp_psalm_ls_id_from_zval(zval *value, zend_long *id)
{
	char *end;
	long parsed;

	if (!value) {
		return false;
	}

	if (Z_TYPE_P(value) == IS_LONG) {
		*id = Z_LVAL_P(value);

		return true;
	}

	if (Z_TYPE_P(value) != IS_STRING || Z_STRLEN_P(value) == 0) {
		return false;
	}

	errno = 0;
	parsed = strtol(Z_STRVAL_P(value), &end, 10);
	if (errno != 0 || end == Z_STRVAL_P(value) || *end != '\0') {
		return false;
	}

	*id = (zend_long) parsed;

	return true;
}

static inline bool lsp_psalm_ls_tool(zend_string *project_root, zend_string **tool, bool *language_server_bin)
{
	*tool = lsp_tool_project_candidate(project_root, "psalm-language-server");
	*language_server_bin = true;
	if (*tool) {
		return true;
	}

	*tool = lsp_tool_project_candidate(project_root, "psalm");
	*language_server_bin = false;

	return *tool != NULL;
}

static inline void lsp_psalm_ls_build_command(lsp_server *server, zend_string *project_root, zend_string *config, lsp_command *command)
{
	zend_string *tool = NULL, *root_arg, *config_arg, *debounce_arg;
	bool language_server_bin = true;
	uint32_t i;

	lsp_command_init(command);
	if (!lsp_psalm_ls_tool(project_root, &tool, &language_server_bin)) {
		return;
	}

	lsp_command_add(command, lsp_php_binary());

	for (i = 0; i < server->options.worker_php_arg_count; i++) {
		lsp_command_add_zstr(command, server->options.worker_php_args[i]);
	}

	lsp_command_add_zstr(command, tool);

	if (!language_server_bin) {
		lsp_command_add(command, "--language-server");
	}

	root_arg = strpprintf(0, "--root=%s", ZSTR_VAL(project_root));
	config_arg = strpprintf(0, "--config=%s", ZSTR_VAL(config));
	debounce_arg = strpprintf(0, "--on-change-debounce-ms=" ZEND_LONG_FMT, server->options.psalm_on_change_debounce_ms);
	lsp_command_add_zstr(command, root_arg);
	lsp_command_add_zstr(command, config_arg);
	lsp_command_add_zstr(command, debounce_arg);

	if (!server->options.psalm_on_change) {
		lsp_command_add(command, "--disable-on-change");
	}

	lsp_psalm_ls_add_bool_arg(command, "--enable-autocomplete", server->options.psalm_enable_autocomplete);
	lsp_psalm_ls_add_bool_arg(command, "--enable-provide-diagnostics", server->options.psalm_enable_diagnostics);
	lsp_psalm_ls_add_bool_arg(command, "--enable-provide-hover", server->options.psalm_enable_hover);
	lsp_psalm_ls_add_bool_arg(command, "--enable-provide-definition", server->options.psalm_enable_definition);
	lsp_psalm_ls_add_bool_arg(command, "--enable-provide-signature-help", server->options.psalm_enable_signature_help);
	lsp_psalm_ls_add_bool_arg(command, "--show-diagnostic-warnings", server->options.psalm_show_info);
	lsp_psalm_ls_add_bool_arg(command, "--in-memory", server->options.psalm_in_memory);
	lsp_psalm_ls_add_bool_arg(command, "--disable-xdebug", true);
	zend_string_release(root_arg);
	zend_string_release(config_arg);
	zend_string_release(debounce_arg);
	zend_string_release(tool);
}

static inline void lsp_psalm_ls_send_shutdown_exit(lsp_psalm_ls_session *session)
{
	zval message, params;
	zend_long id;

	if (!session->running || !lsp_pipe_handle_valid(session->input_pipe)) {
		return;
	}

	id = session->next_id++;
	array_init(&message);
	add_assoc_string(&message, "jsonrpc", "2.0");
	add_assoc_long(&message, "id", id);
	add_assoc_string(&message, "method", "shutdown");
	array_init(&params);
	add_assoc_zval(&message, "params", &params);
	lsp_psalm_ls_send_zval(session, &message);
	zval_ptr_dtor(&message);

	if (!lsp_pipe_handle_valid(session->input_pipe)) {
		return;
	}

	array_init(&message);
	add_assoc_string(&message, "jsonrpc", "2.0");
	add_assoc_string(&message, "method", "exit");
	lsp_psalm_ls_send_zval(session, &message);
	zval_ptr_dtor(&message);
}

static inline lsp_psalm_ls_session *lsp_psalm_ls_session_alloc(zend_string *project_root, zend_string *config)
{
	lsp_psalm_ls_session *session;

	session = ecalloc(1, sizeof(*session));
	session->root = zend_string_copy(project_root);
	session->config = zend_string_copy(config);
	session->pid = LSP_INVALID_PROCESS_ID;
	session->input_pipe = LSP_INVALID_PIPE_HANDLE;
	session->output_pipe = LSP_INVALID_PIPE_HANDLE;
	session->error_pipe = LSP_INVALID_PIPE_HANDLE;
	session->next_id = 1;
	session->initialize_id = 0;
	zend_hash_init(&session->diagnostics, 8, NULL, ZVAL_PTR_DTOR, 0);
	zend_hash_init(&session->pending, 8, NULL, ZVAL_PTR_DTOR, 0);

	return session;
}

static inline void lsp_psalm_ls_session_stop(lsp_psalm_ls_session *session)
{
	int status;
	bool exited;

	status = 0;
	exited = false;
	if (session->running && lsp_process_id_valid(session->pid)) {
		lsp_psalm_ls_send_shutdown_exit(session);
	}

	lsp_pipe_close(&session->input_pipe);
	lsp_pipe_close(&session->output_pipe);
	lsp_pipe_close(&session->error_pipe);
	if (lsp_process_id_valid(session->pid)) {
		exited = lsp_process_wait_timeout(session->pid, &status, LSP_PSALM_LS_SHUTDOWN_TIMEOUT);
		if (!exited) {
			lsp_process_terminate(session->pid);
			exited = lsp_process_wait_timeout(session->pid, &status, LSP_PSALM_LS_TERMINATE_TIMEOUT);
		}
		if (!exited) {
			lsp_process_terminate_force(session->pid);
			lsp_process_wait_timeout(session->pid, &status, LSP_PSALM_LS_KILL_TIMEOUT);
		}
		lsp_process_close(session->pid);
		session->pid = LSP_INVALID_PROCESS_ID;
	}

	session->running = false;
}

static inline void lsp_psalm_ls_session_free(lsp_psalm_ls_session *session)
{
	if (!session) {
		return;
	}

	lsp_psalm_ls_session_stop(session);
	zend_hash_destroy(&session->pending);
	zend_hash_destroy(&session->diagnostics);
	lsp_psalm_ls_buffer_destroy(&session->output_buffer);
	lsp_psalm_ls_buffer_destroy(&session->error_buffer);

	if (session->config) {
		zend_string_release(session->config);
	}

	if (session->root) {
		zend_string_release(session->root);
	}

	efree(session);
}

static inline bool lsp_psalm_ls_start_process(lsp_server *server, lsp_psalm_ls_session *session)
{
	lsp_command command;
	lsp_process_pipes pipes;

	lsp_command_init(&command);
	lsp_psalm_ls_build_command(server, session->root, session->config, &command);
	if (!command.argv || command.count == 0) {
		lsp_command_destroy(&command);

		return false;
	}
	if (!lsp_process_spawn_piped(&command, session->root, &pipes)) {
		lsp_command_destroy(&command);

		return false;
	}

	session->pid = pipes.process;
	session->input_pipe = pipes.input;
	session->output_pipe = pipes.output;
	session->error_pipe = pipes.error;
	session->running = true;
	lsp_command_destroy(&command);

	return true;
}

static inline void lsp_psalm_ls_send_initialized(lsp_psalm_ls_session *session)
{
	zval message, params;

	array_init(&message);
	add_assoc_string(&message, "jsonrpc", "2.0");
	add_assoc_string(&message, "method", "initialized");
	array_init(&params);
	add_assoc_zval(&message, "params", &params);
	lsp_psalm_ls_send_zval(session, &message);
	zval_ptr_dtor(&message);
}

static inline void lsp_psalm_ls_send_initialize(lsp_psalm_ls_session *session)
{
	zval message, params, capabilities, workspace, folders, folder;
	zend_string *root_uri;

	root_uri = lsp_psalm_ls_uri_for_path(session->root);
	session->initialize_id = session->next_id++;
	array_init(&message);
	add_assoc_string(&message, "jsonrpc", "2.0");
	add_assoc_long(&message, "id", session->initialize_id);
	add_assoc_string(&message, "method", "initialize");
	array_init(&params);
	add_assoc_long(&params, "processId", (zend_long) lsp_current_process_id());
	add_assoc_str(&params, "rootUri", zend_string_copy(root_uri));
	add_assoc_str(&params, "rootPath", zend_string_copy(session->root));
	array_init(&capabilities);
	array_init(&workspace);
	add_assoc_bool(&workspace, "workspaceFolders", true);
	add_assoc_zval(&capabilities, "workspace", &workspace);
	add_assoc_zval(&params, "capabilities", &capabilities);
	array_init(&folders);
	array_init(&folder);
	add_assoc_str(&folder, "uri", zend_string_copy(root_uri));
	add_assoc_string(&folder, "name", lsp_psalm_ls_basename(session->root));
	add_next_index_zval(&folders, &folder);
	add_assoc_zval(&params, "workspaceFolders", &folders);
	add_assoc_zval(&message, "params", &params);
	lsp_psalm_ls_send_zval(session, &message);
	zval_ptr_dtor(&message);
	zend_string_release(root_uri);
}

static inline bool lsp_psalm_ls_document_belongs(lsp_server *server, lsp_document *document, zend_string *project_root)
{
	zend_string *root;
	bool result;

	root = lsp_document_project_root(server, document);
	result = zend_string_equals(root, project_root);
	zend_string_release(root);

	return result;
}

static inline void lsp_psalm_ls_send_document_open(lsp_psalm_ls_session *session, lsp_document *document)
{
	zval message, params, td;

	if (!session->initialized) {
		return;
	}

	array_init(&message);
	add_assoc_string(&message, "jsonrpc", "2.0");
	add_assoc_string(&message, "method", "textDocument/didOpen");
	array_init(&params);
	array_init(&td);
	add_assoc_str(&td, "uri", zend_string_copy(document->uri));
	add_assoc_string(&td, "languageId", "php");
	add_assoc_long(&td, "version", document->version);
	add_assoc_str(&td, "text", zend_string_copy(document->text));
	add_assoc_zval(&params, "textDocument", &td);
	add_assoc_zval(&message, "params", &params);
	lsp_psalm_ls_send_zval(session, &message);
	zval_ptr_dtor(&message);
}

static inline void lsp_psalm_ls_send_document_change(lsp_psalm_ls_session *session, lsp_document *document)
{
	zval message, params, td, changes, change;

	if (!session->initialized) {
		return;
	}

	array_init(&message);
	add_assoc_string(&message, "jsonrpc", "2.0");
	add_assoc_string(&message, "method", "textDocument/didChange");
	array_init(&params);
	array_init(&td);
	add_assoc_str(&td, "uri", zend_string_copy(document->uri));
	add_assoc_long(&td, "version", document->version);
	add_assoc_zval(&params, "textDocument", &td);
	array_init(&changes);
	array_init(&change);
	add_assoc_str(&change, "text", zend_string_copy(document->text));
	add_next_index_zval(&changes, &change);
	add_assoc_zval(&params, "contentChanges", &changes);
	add_assoc_zval(&message, "params", &params);
	lsp_psalm_ls_send_zval(session, &message);
	zval_ptr_dtor(&message);
}

static inline void lsp_psalm_ls_send_document_save(lsp_psalm_ls_session *session, lsp_document *document)
{
	zval message, params, td;

	if (!session->initialized) {
		return;
	}

	array_init(&message);
	add_assoc_string(&message, "jsonrpc", "2.0");
	add_assoc_string(&message, "method", "textDocument/didSave");
	array_init(&params);
	array_init(&td);
	add_assoc_str(&td, "uri", zend_string_copy(document->uri));
	add_assoc_zval(&params, "textDocument", &td);
	add_assoc_str(&params, "text", zend_string_copy(document->text));
	add_assoc_zval(&message, "params", &params);
	lsp_psalm_ls_send_zval(session, &message);
	zval_ptr_dtor(&message);
}

static inline void lsp_psalm_ls_send_document_close(lsp_psalm_ls_session *session, zend_string *uri)
{
	zval message, params, td;

	if (!session->initialized) {
		return;
	}

	array_init(&message);
	add_assoc_string(&message, "jsonrpc", "2.0");
	add_assoc_string(&message, "method", "textDocument/didClose");
	array_init(&params);
	array_init(&td);
	add_assoc_str(&td, "uri", zend_string_copy(uri));
	add_assoc_zval(&params, "textDocument", &td);
	add_assoc_zval(&message, "params", &params);
	lsp_psalm_ls_send_zval(session, &message);
	zval_ptr_dtor(&message);
}

static inline void lsp_psalm_ls_send_open_documents(lsp_server *server, lsp_psalm_ls_session *session)
{
	lsp_document *document;
	zval *value;

	ZEND_HASH_FOREACH_VAL(&server->documents, value) {
		if (Z_TYPE_P(value) != IS_PTR) {
			continue;
		}
		document = (lsp_document *) Z_PTR_P(value);
		if (document && lsp_psalm_ls_document_belongs(server, document, session->root)) {
			lsp_psalm_ls_send_document_open(session, document);
		}
	} ZEND_HASH_FOREACH_END();
}

static inline void lsp_psalm_ls_completion_item_set_source(zval *item)
{
	zval *data, new_data;

	if (Z_TYPE_P(item) != IS_ARRAY) {
		return;
	}

	SEPARATE_ARRAY(item);
	data = zend_hash_str_find(Z_ARRVAL_P(item), "data", sizeof("data") - 1);
	if (!data || Z_TYPE_P(data) != IS_ARRAY) {
		array_init(&new_data);
		add_assoc_string(&new_data, "source", "psalm-ls");
		add_assoc_zval(item, "data", &new_data);

		return;
	}

	SEPARATE_ARRAY(data);
	zend_hash_str_del(Z_ARRVAL_P(data), "source", sizeof("source") - 1);
	add_assoc_string(data, "source", "psalm-ls");
}

static inline void lsp_psalm_ls_store_completion_result(lsp_server *server, zval *pending, zval *result)
{
	zend_string *cache_key;
	zval items, *source_items, *item, copy;

	cache_key = lsp_array_string(pending, "cacheKey");
	if (!cache_key || !result) {
		return;
	}

	source_items = result;
	if (Z_TYPE_P(result) == IS_ARRAY) {
		item = zend_hash_str_find(Z_ARRVAL_P(result), "items", sizeof("items") - 1);
		if (item && Z_TYPE_P(item) == IS_ARRAY) {
			source_items = item;
		}
	}
	if (Z_TYPE_P(source_items) != IS_ARRAY) {
		return;
	}

	array_init(&items);
	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(source_items), item) {
		if (Z_TYPE_P(item) != IS_ARRAY) {
			continue;
		}
		ZVAL_COPY(&copy, item);
		lsp_psalm_ls_completion_item_set_source(&copy);
		add_next_index_zval(&items, &copy);
	} ZEND_HASH_FOREACH_END();

	zend_hash_update(&server->completion_cache, cache_key, &items);
}

static inline zend_string *lsp_psalm_ls_trim_type_text(const char *value, size_t length)
{
	const char *start, *end, *line_end, *colon, *fence;
	zend_string *type;

	start = value;
	end = value + length;
	while (start < end && isspace((unsigned char) *start)) {
		start++;
	}
	while (end > start && isspace((unsigned char) end[-1])) {
		end--;
	}

	if (end - start >= 3 && memcmp(start, "```", 3) == 0) {
		line_end = start;
		while (line_end < end && *line_end != '\n' && *line_end != '\r') {
			line_end++;
		}
		if (line_end < end) {
			start = line_end + 1;
			while (start < end && (*start == '\n' || *start == '\r')) {
				start++;
			}
		}
		fence = start;
		while (fence + 3 <= end && memcmp(fence, "```", 3) != 0) {
			fence++;
		}
		if (fence + 3 <= end) {
			end = fence;
		}
	}

	while (start < end && isspace((unsigned char) *start)) {
		start++;
	}
	while (end > start && isspace((unsigned char) end[-1])) {
		end--;
	}

	if (end - start >= 3 && memcmp(start, "php", 3) == 0 && (start + 3 == end || isspace((unsigned char) start[3]))) {
		start += 3;
		while (start < end && isspace((unsigned char) *start)) {
			start++;
		}
	}

	if (end - start >= 5 && memcmp(start, "<?php", 5) == 0) {
		line_end = start;
		while (line_end < end && *line_end != '\n' && *line_end != '\r') {
			line_end++;
		}
		if (line_end < end) {
			start = line_end + 1;
			while (start < end && isspace((unsigned char) *start)) {
				start++;
			}
		}
	}

	colon = memchr(start, ':', end - start);
	if (colon && colon + 1 < end && (memchr(start, '$', colon - start) || isdigit((unsigned char) *start))) {
		start = colon + 1;
		while (start < end && isspace((unsigned char) *start)) {
			start++;
		}
	}

	while (start < end && (*start == '`' || *start == '\'' || *start == '"')) {
		start++;
	}
	while (end > start && (end[-1] == '`' || end[-1] == '\'' || end[-1] == '"')) {
		end--;
	}

	if (end <= start) {
		return NULL;
	}

	type = zend_string_init(start, end - start, 0);
	if (lsp_type_is_unhelpful(type)) {
		zend_string_release(type);

		return NULL;
	}

	return type;
}

static inline zend_string *lsp_psalm_ls_hover_type_from_contents(zval *contents)
{
	zend_string *type;
	zval *value_zv, *entry;

	if (!contents) {
		return NULL;
	}

	if (Z_TYPE_P(contents) == IS_STRING) {
		return lsp_psalm_ls_trim_type_text(Z_STRVAL_P(contents), Z_STRLEN_P(contents));
	}

	if (Z_TYPE_P(contents) != IS_ARRAY) {
		return NULL;
	}

	value_zv = zend_hash_str_find(Z_ARRVAL_P(contents), "value", sizeof("value") - 1);
	if (value_zv && Z_TYPE_P(value_zv) == IS_STRING) {
		return lsp_psalm_ls_trim_type_text(Z_STRVAL_P(value_zv), Z_STRLEN_P(value_zv));
	}

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(contents), entry) {
		type = lsp_psalm_ls_hover_type_from_contents(entry);
		if (type) {
			return type;
		}
	} ZEND_HASH_FOREACH_END();

	return NULL;
}

static inline void lsp_psalm_ls_store_hover_result(lsp_server *server, zval *pending, zval *result)
{
	zend_string *cache_key, *symbol_key, *type, *uri, *word;
	zval *contents, cache_value;

	cache_key = lsp_array_string(pending, "cacheKey");
	if (!cache_key) {
		return;
	}

	contents = result && Z_TYPE_P(result) == IS_ARRAY ? zend_hash_str_find(Z_ARRVAL_P(result), "contents", sizeof("contents") - 1) : NULL;
	type = lsp_psalm_ls_hover_type_from_contents(contents);
	if (type) {
		ZVAL_STR_COPY(&cache_value, type);
		zend_hash_update(&server->type_cache, cache_key, &cache_value);
		uri = lsp_array_string(pending, "uri");
		word = lsp_array_string(pending, "word");
		if (uri && word && ZSTR_LEN(word) > 1 && ZSTR_VAL(word)[0] == '$') {
			symbol_key = strpprintf(0, "psalm-ls:symbol-type:%s:%s", ZSTR_VAL(uri), ZSTR_VAL(word));
			ZVAL_STR_COPY(&cache_value, type);
			zend_hash_update(&server->type_cache, symbol_key, &cache_value);
			zend_string_release(symbol_key);
		}
		zend_string_release(type);

		return;
	}

	ZVAL_FALSE(&cache_value);
	zend_hash_update(&server->type_cache, cache_key, &cache_value);
}

static inline void lsp_psalm_ls_notify_completion_ready(zend_string *uri)
{
	zval params;

	array_init(&params);
	add_assoc_string(&params, "analyzer", "psalm-ls");

	if (uri) {
		add_assoc_str(&params, "uri", zend_string_copy(uri));
	}

	lsp_protocol_notify("lsparrot.php/completionReady", &params);

	zval_ptr_dtor(&params);
}

static inline void lsp_psalm_ls_handle_diagnostics(lsp_server *server, lsp_psalm_ls_session *session, zval *params)
{
	lsp_document *document;
	zend_string *uri;
	zval *diagnostics, diagnostics_copy;

	uri = lsp_array_string(params, "uri");
	diagnostics = lsp_array_find(params, "diagnostics");
	if (!uri || !diagnostics || Z_TYPE_P(diagnostics) != IS_ARRAY) {
		return;
	}

	ZVAL_COPY(&diagnostics_copy, diagnostics);
	zend_hash_update(&session->diagnostics, uri, &diagnostics_copy);
	lsp_analyzer_project_state(server, "psalm-ls", session->root, LSP_ANALYZER_PROJECT_READY);
	lsp_analyzer_project_status("psalm-ls", "idle", "Psalm language server diagnostics updated.", session->root);

	document = lsp_document_from_uri(server, uri);
	if (document) {
		lsp_publish_document_diagnostics(server, document);
	}
}

static inline void lsp_psalm_ls_handle_response(lsp_server *server, lsp_psalm_ls_session *session, zval *message)
{
	zend_long id;
	zend_string *uri;
	zval *id_zv, *pending, *result, *uri_zv, *completion_ready_zv;

	id_zv = zend_hash_str_find(Z_ARRVAL_P(message), "id", sizeof("id") - 1);
	if (!lsp_psalm_ls_id_from_zval(id_zv, &id)) {
		return;
	}

	if (id == session->initialize_id) {
		session->initialized = true;
		lsp_analyzer_project_state(server, "psalm-ls", session->root, LSP_ANALYZER_PROJECT_READY);
		lsp_analyzer_project_status("psalm-ls", "idle", "Psalm language server ready.", session->root);
		lsp_psalm_ls_send_initialized(session);
		lsp_psalm_ls_send_open_documents(server, session);

		return;
	}

	pending = zend_hash_index_find(&session->pending, (zend_ulong) id);
	if (!pending || Z_TYPE_P(pending) != IS_ARRAY) {
		return;
	}

	result = zend_hash_str_find(Z_ARRVAL_P(message), "result", sizeof("result") - 1);
	if (lsp_zval_string_equals_literal(lsp_array_find(pending, "type"), "completion")) {
		lsp_psalm_ls_store_completion_result(server, pending, result);
		uri_zv = lsp_array_find(pending, "uri");
		uri = uri_zv && Z_TYPE_P(uri_zv) == IS_STRING ? Z_STR_P(uri_zv) : NULL;
		lsp_psalm_ls_notify_completion_ready(uri);
	} else if (lsp_zval_string_equals_literal(lsp_array_find(pending, "type"), "hover")) {
		lsp_psalm_ls_store_hover_result(server, pending, result);
		completion_ready_zv = lsp_array_find(pending, "completionReady");
		if (completion_ready_zv && zend_is_true(completion_ready_zv)) {
			uri_zv = lsp_array_find(pending, "uri");
			uri = uri_zv && Z_TYPE_P(uri_zv) == IS_STRING ? Z_STR_P(uri_zv) : NULL;
			lsp_psalm_ls_notify_completion_ready(uri);
		}
	}

	zend_hash_index_del(&session->pending, (zend_ulong) id);
}

static inline void lsp_psalm_ls_handle_message(lsp_server *server, lsp_psalm_ls_session *session, zend_string *body)
{
	zval decoded, *method_zv, *params;

	ZVAL_UNDEF(&decoded);
	php_json_decode_ex(&decoded, ZSTR_VAL(body), ZSTR_LEN(body), PHP_JSON_OBJECT_AS_ARRAY, 512);
	if (Z_TYPE(decoded) != IS_ARRAY) {
		if (!Z_ISUNDEF(decoded)) {
			zval_ptr_dtor(&decoded);
		}

		return;
	}

	method_zv = zend_hash_str_find(Z_ARRVAL(decoded), "method", sizeof("method") - 1);
	if (method_zv && Z_TYPE_P(method_zv) == IS_STRING) {
		params = zend_hash_str_find(Z_ARRVAL(decoded), "params", sizeof("params") - 1);
		if (params && Z_TYPE_P(params) == IS_ARRAY && zend_string_equals_literal(Z_STR_P(method_zv), "textDocument/publishDiagnostics")) {
			lsp_psalm_ls_handle_diagnostics(server, session, params);
		}
	} else {
		lsp_psalm_ls_handle_response(server, session, &decoded);
	}

	zval_ptr_dtor(&decoded);
}

static inline void lsp_psalm_ls_read_pipe(lsp_server *server, lsp_psalm_ls_session *session, lsp_pipe_handle pipe, lsp_psalm_ls_buffer *buffer, bool parse_messages)
{
	smart_str chunk = {0};
	zend_string *body;
	bool closed;

	if (!lsp_pipe_handle_valid(pipe)) {
		return;
	}

	closed = false;
	if (!lsp_pipe_read_available(pipe, &chunk, &closed)) {
		session->failed = true;
		session->running = false;

		return;
	}
	if (chunk.s) {
		lsp_psalm_ls_buffer_append(buffer, ZSTR_VAL(chunk.s), ZSTR_LEN(chunk.s));
		zend_string_release(chunk.s);
	}
	if (closed) {
		session->failed = true;
		session->running = false;
		return;
	}

	if (!parse_messages) {
		if (buffer->length > 65536) {
			buffer->length = 0;
		}

		return;
	}

	while (lsp_psalm_ls_next_body(buffer, &body)) {
		lsp_psalm_ls_handle_message(server, session, body);
		zend_string_release(body);
	}
}

static inline zend_string *lsp_psalm_ls_error_message(lsp_psalm_ls_buffer *buffer)
{
	const char *value, *end, *line_start, *line_end, *marker;

	if (!buffer || buffer->length == 0) {
		return zend_string_init("Psalm language server exited.", sizeof("Psalm language server exited.") - 1, 0);
	}

	value = buffer->data;
	end = value + buffer->length;
	marker = strstr(value, "Error:");
	if (marker && marker < end) {
		line_start = marker;
	} else {
		line_start = value;
		while (line_start < end && isspace((unsigned char) *line_start)) {
			line_start++;
		}
	}

	line_end = line_start;
	while (line_end < end && *line_end != '\r' && *line_end != '\n') {
		line_end++;
	}
	while (line_end > line_start && isspace((unsigned char) line_end[-1])) {
		line_end--;
	}

	if (line_end <= line_start) {
		return zend_string_init("Psalm language server exited.", sizeof("Psalm language server exited.") - 1, 0);
	}

	return strpprintf(0, "Psalm language server failed: %.*s", (int) (line_end - line_start), line_start);
}

static inline void lsp_psalm_ls_reap_session(lsp_server *server, lsp_psalm_ls_session *session)
{
	zend_string *message;
	int status;

	if (!session->running || !lsp_process_id_valid(session->pid)) {
		return;
	}

	status = 0;
	if (lsp_process_wait_nonblocking(session->pid, &status)) {
		session->running = false;
		session->failed = true;
		lsp_process_close(session->pid);
		session->pid = LSP_INVALID_PROCESS_ID;
		lsp_analyzer_project_state(server, "psalm-ls", session->root, LSP_ANALYZER_PROJECT_ERROR);
		message = lsp_psalm_ls_error_message(&session->error_buffer);
		lsp_analyzer_project_status("psalm-ls", "error", ZSTR_VAL(message), session->root);
		zend_string_release(message);
	}
}

static inline lsp_psalm_ls_session *lsp_psalm_ls_find_session(lsp_server *server, zend_string *project_root)
{
	zval *value;

	value = zend_hash_find(&server->psalm_ls_projects, project_root);

	return value && Z_TYPE_P(value) == IS_PTR ? (lsp_psalm_ls_session *) Z_PTR_P(value) : NULL;
}

static inline bool lsp_psalm_ls_project_tool_available(zend_string *project_root)
{
	zend_string *tool;

	tool = lsp_tool_project_candidate(project_root, "psalm-language-server");
	if (tool) {
		zend_string_release(tool);

		return true;
	}

	return false;
}

static inline void lsp_psalm_ls_append_cached_items(lsp_server *server, zval *items, zend_string *key)
{
	zval *cached, *item, copy;

	cached = zend_hash_find(&server->completion_cache, key);
	if (!cached || Z_TYPE_P(cached) != IS_ARRAY) {
		return;
	}

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(cached), item) {
		ZVAL_COPY(&copy, item);
		add_next_index_zval(items, &copy);
	} ZEND_HASH_FOREACH_END();
}

static inline zend_string *lsp_psalm_ls_completion_key(lsp_document *document, size_t offset, zend_string *prefix)
{
	return strpprintf(0, "psalm-ls:%s:" ZEND_LONG_FMT ":%zu:%s", ZSTR_VAL(document->uri), document->version, offset, ZSTR_VAL(prefix));
}

static inline zend_string *lsp_psalm_ls_hover_key(lsp_document *document, size_t offset, zend_string *word)
{
	return lsp_type_cache_key("psalm-ls", document, word, offset);
}

static inline zend_string *lsp_psalm_ls_symbol_type_key(lsp_document *document, zend_string *word)
{
	return strpprintf(0, "psalm-ls:symbol-type:%s:%s", ZSTR_VAL(document->uri), ZSTR_VAL(word));
}

static inline void lsp_psalm_ls_add_pending_completion(lsp_psalm_ls_session *session, zend_long id, zend_string *uri, zend_string *cache_key)
{
	zval pending;

	array_init(&pending);
	add_assoc_string(&pending, "type", "completion");
	add_assoc_str(&pending, "uri", zend_string_copy(uri));
	add_assoc_str(&pending, "cacheKey", zend_string_copy(cache_key));
	zend_hash_index_update(&session->pending, (zend_ulong) id, &pending);
}

static inline void lsp_psalm_ls_add_pending_hover(lsp_psalm_ls_session *session, zend_long id, zend_string *uri, zend_string *cache_key, zend_string *word, bool completion_ready)
{
	zval pending;

	array_init(&pending);
	add_assoc_string(&pending, "type", "hover");
	add_assoc_str(&pending, "uri", zend_string_copy(uri));
	add_assoc_str(&pending, "cacheKey", zend_string_copy(cache_key));
	add_assoc_str(&pending, "word", zend_string_copy(word));
	add_assoc_bool(&pending, "completionReady", completion_ready);
	zend_hash_index_update(&session->pending, (zend_ulong) id, &pending);
}

extern void lsp_psalm_ls_project_destroy(zval *value)
{
	if (Z_TYPE_P(value) == IS_PTR) {
		lsp_psalm_ls_session_free((lsp_psalm_ls_session *) Z_PTR_P(value));
	}
}

extern void lsp_psalm_ls_shutdown_all(lsp_server *server)
{
	zval *value;

	ZEND_HASH_FOREACH_VAL(&server->psalm_ls_projects, value) {
		if (Z_TYPE_P(value) == IS_PTR) {
			lsp_psalm_ls_session_stop((lsp_psalm_ls_session *) Z_PTR_P(value));
		}
	} ZEND_HASH_FOREACH_END();
}

extern bool lsp_psalm_ls_enabled(lsp_server *server)
{
	return server->psalm_ls_enabled;
}

extern void lsp_psalm_ls_pump(lsp_server *server, double timeout)
{
	lsp_psalm_ls_session *session;
	zval *value;
	double deadline;
	bool again;

	deadline = lsp_psalm_ls_now() + timeout;
	do {
		again = false;
		ZEND_HASH_FOREACH_VAL(&server->psalm_ls_projects, value) {
			if (Z_TYPE_P(value) != IS_PTR) {
				continue;
			}
			session = (lsp_psalm_ls_session *) Z_PTR_P(value);
			lsp_psalm_ls_reap_session(server, session);
			lsp_psalm_ls_read_pipe(server, session, session->output_pipe, &session->output_buffer, true);
			lsp_psalm_ls_read_pipe(server, session, session->error_pipe, &session->error_buffer, false);
			if (session->running) {
				again = true;
			}
		} ZEND_HASH_FOREACH_END();

		if (timeout <= 0.0 || !again || lsp_psalm_ls_now() >= deadline) {
			break;
		}
		lsp_sleep_milliseconds(10);
	} while (lsp_psalm_ls_now() < deadline);
}

extern bool lsp_psalm_ls_schedule_project(lsp_server *server, zend_string *project_root)
{
	lsp_psalm_ls_session *session;
	zend_string *config, *generated_config;
	zval value;

	if (!lsp_psalm_ls_enabled(server)) {
		return false;
	}

	if (!lsp_composer_project_has_analysis_paths(project_root)) {
		return false;
	}

	session = lsp_psalm_ls_find_session(server, project_root);
	if (session) {
		return true;
	}

	if (server->options.analyzer_auto && server->options.psalm_transport == LSP_PSALM_TRANSPORT_AUTO && !lsp_psalm_ls_project_tool_available(project_root)) {
		return false;
	}

	config = lsp_psalm_config_file(project_root);
	generated_config = lsp_psalm_ls_config_file(project_root, config, lsp_project_psalm_level(server, project_root), server->options.psalm_live_dead_code_diagnostics);
	if (!generated_config && !config) {
		lsp_analyzer_project_state(server, "psalm-ls", project_root, LSP_ANALYZER_PROJECT_ERROR);
		lsp_analyzer_project_status("psalm-ls", "error", "Psalm config file could not be generated; skipping Psalm language server.", project_root);

		return true;
	}

	session = lsp_psalm_ls_session_alloc(project_root, generated_config ? generated_config : config);
	if (generated_config) {
		zend_string_release(generated_config);
	}

	if (config) {
		zend_string_release(config);
	}

	if (!lsp_psalm_ls_start_process(server, session)) {
		lsp_psalm_ls_session_free(session);

		if (server->options.psalm_transport == LSP_PSALM_TRANSPORT_AUTO) {
			return false;
		}

		lsp_analyzer_project_state(server, "psalm-ls", project_root, LSP_ANALYZER_PROJECT_ERROR);
		lsp_analyzer_project_status("psalm-ls", "error", "Psalm language server could not be started.", project_root);

		return true;
	}

	ZVAL_PTR(&value, session);
	zend_hash_update(&server->psalm_ls_projects, project_root, &value);
	lsp_analyzer_project_state(server, "psalm-ls", project_root, LSP_ANALYZER_PROJECT_RUNNING);
	lsp_analyzer_project_status("psalm-ls", "running", "Starting Psalm language server.", project_root);
	lsp_psalm_ls_send_initialize(session);
	lsp_psalm_ls_pump(server, 0.05);

	return true;
}

extern void lsp_psalm_ls_document_open(lsp_server *server, lsp_document *document)
{
	lsp_psalm_ls_session *session;
	zend_string *project_root;

	if (!lsp_psalm_ls_enabled(server)) {
		return;
	}

	project_root = lsp_document_project_root(server, document);
	if (!lsp_path_is_in_composer_analysis_paths(document->path, project_root)) {
		zend_string_release(project_root);

		return;
	}

	lsp_psalm_ls_schedule_project(server, project_root);

	session = lsp_psalm_ls_find_session(server, project_root);
	if (session && session->running && !session->initialized) {
		lsp_psalm_ls_pump(server, 0.05);
	}
	if (session) {
		lsp_psalm_ls_send_document_open(session, document);
	}

	zend_string_release(project_root);
}

extern void lsp_psalm_ls_document_change(lsp_server *server, lsp_document *document)
{
	lsp_psalm_ls_session *session;
	zend_string *project_root;

	if (!lsp_psalm_ls_enabled(server) || !server->options.psalm_on_change) {
		return;
	}

	project_root = lsp_document_project_root(server, document);
	if (!lsp_path_is_in_composer_analysis_paths(document->path, project_root)) {
		zend_string_release(project_root);

		return;
	}

	session = lsp_psalm_ls_find_session(server, project_root);
	if (session && session->running && !session->initialized) {
		lsp_psalm_ls_pump(server, 0.05);
	}
	if (session) {
		lsp_psalm_ls_send_document_change(session, document);

		if (session->initialized) {
			lsp_analyzer_project_state(server, "psalm-ls", project_root, LSP_ANALYZER_PROJECT_RUNNING);
			lsp_analyzer_project_status("psalm-ls", "running", "Psalm language server analyzing changes.", project_root);
		}
	}

	zend_string_release(project_root);
}

extern void lsp_psalm_ls_document_save(lsp_server *server, lsp_document *document)
{
	lsp_psalm_ls_session *session;
	zend_string *project_root;

	if (!lsp_psalm_ls_enabled(server)) {
		return;
	}

	project_root = lsp_document_project_root(server, document);
	if (!lsp_path_is_in_composer_analysis_paths(document->path, project_root)) {
		zend_string_release(project_root);

		return;
	}

	lsp_psalm_ls_schedule_project(server, project_root);

	session = lsp_psalm_ls_find_session(server, project_root);
	if (session && session->running && !session->initialized) {
		lsp_psalm_ls_pump(server, 0.05);
	}
	if (session) {
		lsp_psalm_ls_send_document_save(session, document);
		if (session->initialized) {
			lsp_analyzer_project_state(server, "psalm-ls", project_root, LSP_ANALYZER_PROJECT_RUNNING);
			lsp_analyzer_project_status("psalm-ls", "running", "Psalm language server analyzing saved document.", project_root);
		}
	}

	zend_string_release(project_root);
}

extern void lsp_psalm_ls_document_close(lsp_server *server, zend_string *uri)
{
	lsp_psalm_ls_session *session;
	zval *value;

	if (!lsp_psalm_ls_enabled(server)) {
		return;
	}

	ZEND_HASH_FOREACH_VAL(&server->psalm_ls_projects, value) {
		if (Z_TYPE_P(value) != IS_PTR) {
			continue;
		}

		session = (lsp_psalm_ls_session *) Z_PTR_P(value);
		lsp_psalm_ls_send_document_close(session, uri);
		zend_hash_del(&session->diagnostics, uri);
	} ZEND_HASH_FOREACH_END();
}

extern void lsp_psalm_ls_append_diagnostics(lsp_server *server, lsp_document *document, zval *diagnostics)
{
	lsp_psalm_ls_session *session;
	zend_string *project_root;
	zval *cached, *item, copy;

	if (!lsp_psalm_ls_enabled(server)) {
		return;
	}

	project_root = lsp_document_project_root(server, document);
	if (!lsp_path_is_in_composer_analysis_paths(document->path, project_root)) {
		zend_string_release(project_root);

		return;
	}

	session = lsp_psalm_ls_find_session(server, project_root);
	zend_string_release(project_root);
	if (!session) {
		return;
	}

	cached = zend_hash_find(&session->diagnostics, document->uri);
	if (!cached || Z_TYPE_P(cached) != IS_ARRAY) {
		return;
	}

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(cached), item) {
		ZVAL_COPY(&copy, item);
		add_next_index_zval(diagnostics, &copy);
	} ZEND_HASH_FOREACH_END();
}

static inline zend_string *lsp_psalm_ls_type_for_position_ex(lsp_server *server, lsp_document *document, zval *position, size_t offset, zend_string *word, bool wait_for_response, bool notify_completion_ready)
{
	lsp_psalm_ls_session *session;
	zend_string *project_root, *key, *symbol_key, *type;
	zend_long id;
	zval message, params, td, pos, *cached;
	double wait_timeout;

	if (!lsp_psalm_ls_enabled(server) || !server->options.psalm_enable_hover) {
		return NULL;
	}

	wait_timeout = wait_for_response ? (double) server->options.psalm_max_response_wait_ms / 1000.0 : 0.0;

	key = lsp_psalm_ls_hover_key(document, offset, word);
	cached = zend_hash_find(&server->type_cache, key);
	if (cached) {
		if (Z_TYPE_P(cached) == IS_STRING) {
			type = zend_string_copy(Z_STR_P(cached));
			zend_string_release(key);

			return type;
		}
	}

	if (ZSTR_LEN(word) > 1 && ZSTR_VAL(word)[0] == '$') {
		symbol_key = lsp_psalm_ls_symbol_type_key(document, word);
		cached = zend_hash_find(&server->type_cache, symbol_key);
		if (cached && Z_TYPE_P(cached) == IS_STRING) {
			type = zend_string_copy(Z_STR_P(cached));
			zend_string_release(symbol_key);
			zend_string_release(key);

			return type;
		}
		zend_string_release(symbol_key);
	}

	project_root = lsp_document_project_root(server, document);
	if (!lsp_path_is_in_composer_analysis_paths(document->path, project_root)) {
		zend_string_release(project_root);
		zend_string_release(key);

		return NULL;
	}

	lsp_psalm_ls_schedule_project(server, project_root);
	session = lsp_psalm_ls_find_session(server, project_root);
	zend_string_release(project_root);
	if (session && session->running && !session->initialized) {
		lsp_psalm_ls_pump(server, wait_timeout);
	}
	if (!session || !session->running || !session->initialized) {
		zend_string_release(key);

		return NULL;
	}

	id = session->next_id++;
	array_init(&message);
	add_assoc_string(&message, "jsonrpc", "2.0");
	add_assoc_long(&message, "id", id);
	add_assoc_string(&message, "method", "textDocument/hover");
	array_init(&params);
	array_init(&td);
	add_assoc_str(&td, "uri", zend_string_copy(document->uri));
	add_assoc_zval(&params, "textDocument", &td);
	ZVAL_COPY(&pos, position);
	add_assoc_zval(&params, "position", &pos);
	add_assoc_zval(&message, "params", &params);
	lsp_psalm_ls_add_pending_hover(session, id, document->uri, key, word, notify_completion_ready);
	if (lsp_psalm_ls_send_zval(session, &message)) {
		lsp_psalm_ls_pump(server, wait_timeout);
	}
	zval_ptr_dtor(&message);

	cached = zend_hash_find(&server->type_cache, key);
	if (cached && Z_TYPE_P(cached) == IS_STRING) {
		type = zend_string_copy(Z_STR_P(cached));
		zend_string_release(key);

		return type;
	}

	zend_string_release(key);

	return NULL;
}

extern zend_string *lsp_psalm_ls_type_for_position(lsp_server *server, lsp_document *document, zval *position, size_t offset, zend_string *word)
{
	return lsp_psalm_ls_type_for_position_ex(server, document, position, offset, word, true, false);
}

extern zend_string *lsp_psalm_ls_type_for_position_async(lsp_server *server, lsp_document *document, zval *position, size_t offset, zend_string *word)
{
	return lsp_psalm_ls_type_for_position_ex(server, document, position, offset, word, false, true);
}

extern bool lsp_psalm_ls_completion_cache_or_schedule(lsp_server *server, zval *items, lsp_document *document, zval *position, size_t offset, zend_string *prefix)
{
	lsp_psalm_ls_session *session;
	zend_string *project_root, *key;
	zend_long id;
	zval message, params, td, pos;
	bool pending = false;

	if (!lsp_psalm_ls_enabled(server) || !server->options.psalm_enable_autocomplete) {
		return false;
	}

	key = lsp_psalm_ls_completion_key(document, offset, prefix);
	if (zend_hash_exists(&server->completion_cache, key)) {
		lsp_psalm_ls_append_cached_items(server, items, key);
		zend_string_release(key);

		return false;
	}

	project_root = lsp_document_project_root(server, document);
	if (!lsp_path_is_in_composer_analysis_paths(document->path, project_root)) {
		zend_string_release(project_root);
		zend_string_release(key);

		return false;
	}

	lsp_psalm_ls_schedule_project(server, project_root);
	session = lsp_psalm_ls_find_session(server, project_root);
	zend_string_release(project_root);
	if (session && session->running && !session->initialized) {
		lsp_psalm_ls_pump(server, 0.05);
	}
	if (!session || !session->running || !session->initialized) {
		zend_string_release(key);

		return true;
	}

	id = session->next_id++;
	array_init(&message);
	add_assoc_string(&message, "jsonrpc", "2.0");
	add_assoc_long(&message, "id", id);
	add_assoc_string(&message, "method", "textDocument/completion");
	array_init(&params);
	array_init(&td);
	add_assoc_str(&td, "uri", zend_string_copy(document->uri));
	add_assoc_zval(&params, "textDocument", &td);
	ZVAL_COPY(&pos, position);
	add_assoc_zval(&params, "position", &pos);
	add_assoc_zval(&message, "params", &params);
	lsp_psalm_ls_add_pending_completion(session, id, document->uri, key);
	if (lsp_psalm_ls_send_zval(session, &message)) {
		pending = true;
		lsp_psalm_ls_pump(server, 0.0);
		if (zend_hash_exists(&server->completion_cache, key)) {
			lsp_psalm_ls_append_cached_items(server, items, key);
			pending = false;
		}
	}
	zval_ptr_dtor(&message);
	zend_string_release(key);

	return pending;
}
