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

static inline void lsp_initialize(lsp_server *server, zval *params, zval *return_value)
{
	zend_string *root_uri = lsp_array_string(params, "rootUri"), *root_path = lsp_array_string(params, "rootPath");
	zval capabilities, sync, save, completion, triggers, code_lens, signature, signature_triggers, server_info;

	if (root_uri) {
		zend_string_release(server->root);
		server->root = lsp_uri_to_path(root_uri);
	} else if (root_path) {
		zend_string_release(server->root);
		server->root = zend_string_copy(root_path);
	}

	lsp_resolve_analyzers(server);
	lsp_build_project_index(server);
	lsp_schedule_workspace_analyzers(server);

	array_init(return_value);
	array_init(&capabilities);

	array_init(&sync);
	add_assoc_bool(&sync, "openClose", true);
	add_assoc_long(&sync, "change", 1);
	array_init(&save);
	add_assoc_bool(&save, "includeText", true);
	add_assoc_zval(&sync, "save", &save);
	add_assoc_zval(&capabilities, "textDocumentSync", &sync);

	array_init(&completion);
	add_assoc_bool(&completion, "resolveProvider", false);
	array_init(&triggers);
	add_next_index_string(&triggers, "$");
	add_next_index_string(&triggers, ">");
	add_next_index_string(&triggers, ":");
	add_next_index_string(&triggers, "[");
	add_next_index_string(&triggers, "(");
	add_next_index_string(&triggers, ",");
	add_next_index_string(&triggers, " ");
	add_next_index_string(&triggers, "@");
	add_next_index_string(&triggers, "-");
	add_next_index_string(&triggers, "<");
	add_next_index_string(&triggers, "{");
	add_next_index_string(&triggers, "\\");
	add_assoc_zval(&completion, "triggerCharacters", &triggers);
	add_assoc_zval(&capabilities, "completionProvider", &completion);

	add_assoc_bool(&capabilities, "hoverProvider", true);
	add_assoc_bool(&capabilities, "definitionProvider", true);
	array_init(&code_lens);
	add_assoc_bool(&code_lens, "resolveProvider", false);
	add_assoc_zval(&capabilities, "codeLensProvider", &code_lens);
	array_init(&signature);
	array_init(&signature_triggers);
	add_next_index_string(&signature_triggers, "(");
	add_next_index_string(&signature_triggers, ",");
	add_assoc_zval(&signature, "triggerCharacters", &signature_triggers);
	add_assoc_zval(&capabilities, "signatureHelpProvider", &signature);
	add_assoc_bool(&capabilities, "documentSymbolProvider", true);
	add_assoc_bool(&capabilities, "workspaceSymbolProvider", true);
	add_assoc_zval(return_value, "capabilities", &capabilities);

	array_init(&server_info);
	add_assoc_string(&server_info, "name", "PHP CLI LSP Extension");
	add_assoc_string(&server_info, "version", PHP_LSPARROT_VERSION);
	add_assoc_zval(return_value, "serverInfo", &server_info);
}

static inline void lsp_did_open(lsp_server *server, zval *params)
{
	lsp_document *document;
	zend_long version;
	zend_string *uri, *text;
	zval *td;

	td = lsp_array_find(params, "textDocument");
	version = lsp_array_long(td, "version", 0);
	uri =  lsp_array_string(td, "uri");
	text = lsp_array_string(td, "text");

	if (!uri || !text) {
		return;
	}

	document = lsp_document_open_or_change(server, uri, version, text);
	lsp_psalm_ls_document_open(server, document);
	lsp_publish_document_diagnostics(server, document);
}

static inline void lsp_did_change(lsp_server *server, zval *params)
{
	lsp_document *document;
	zend_long version;
	zend_string *uri, *text;
	zval *td , *changes = lsp_array_find(params, "contentChanges"), *change = NULL;

	td = lsp_array_find(params, "textDocument");
	version = lsp_array_long(td, "version", 0);
	uri = lsp_array_string(td, "uri");

	if (changes && Z_TYPE_P(changes) == IS_ARRAY) {
		change = zend_hash_index_find(Z_ARRVAL_P(changes), 0);
	}

	text = lsp_array_string(change, "text");
	if (!uri || !text) {
		return;
	}

	document = lsp_document_open_or_change(server, uri, version, text);
	lsp_psalm_ls_document_change(server, document);
	lsp_publish_document_diagnostics(server, document);
}

static inline void lsp_did_save(lsp_server *server, zval *params)
{
	lsp_document *document;
	zend_string *uri, *text = lsp_array_string(params, "text");
	zval *td, *existing;

	td = lsp_array_find(params, "textDocument");
	uri = lsp_array_string(td, "uri");

	if (!uri) {
		return;
	}

	existing = zend_hash_find(&server->documents, uri);
	if (!existing) {
		return;
	}

	document = (lsp_document *) Z_PTR_P(existing);
	if (text) {
		zend_string_release(document->text);
		document->text = zend_string_copy(text);
	}

	lsp_document_analyze(document);
	zend_hash_clean(&server->member_cache);
	lsp_psalm_ls_document_save(server, document);
	lsp_reschedule_project_analyzers(server, document);
	lsp_publish_document_diagnostics(server, document);
}

static inline void lsp_did_close(lsp_server *server, zval *params)
{
	zval *td;
	zend_string *uri;

	td = lsp_array_find(params, "textDocument");
	uri = lsp_array_string(td, "uri");

	if (!uri) {
		return;
	}

	lsp_psalm_ls_document_close(server, uri);
	zend_hash_del(&server->documents, uri);
	lsp_publish_empty_diagnostics(uri);
}

static inline void lsp_document_request(lsp_server *server, zval *params, void (*handler)(lsp_server *, zval *, lsp_document *, zval *), zval *return_value)
{
	lsp_document *document;
	zend_string *uri;
	zval *td, *position = lsp_array_find(params, "position");

	td = lsp_array_find(params, "textDocument");
	uri = lsp_array_string(td, "uri");

	if (!uri) {
		ZVAL_NULL(return_value);

		return;
	}

	document = lsp_document_from_uri(server, uri);
	handler(server, return_value, document, position);
}

static inline void lsp_document_request_no_server(lsp_server *server, zval *params, void (*handler)(zval *, lsp_document *, zval *), zval *return_value)
{
	lsp_document *document;
	zend_string *uri;
	zval *td, *position = lsp_array_find(params, "position");

	td = lsp_array_find(params, "textDocument");
	uri = lsp_array_string(td, "uri");

	if (!uri) {
		ZVAL_NULL(return_value);

		return;
	}

	document = lsp_document_from_uri(server, uri);
	handler(return_value, document, position);
}

static inline void lsp_document_request_no_position(lsp_server *server, zval *params, void (*handler)(lsp_server *, zval *, lsp_document *), zval *return_value)
{
	lsp_document *document;
	zend_string *uri;
	zval *td;

	td = lsp_array_find(params, "textDocument");
	uri = lsp_array_string(td, "uri");

	if (!uri) {
		ZVAL_NULL(return_value);

		return;
	}

	document = lsp_document_from_uri(server, uri);
	handler(server, return_value, document);
}

static inline void lsp_document_request_no_server_no_position(lsp_server *server, zval *params, void (*handler)(zval *, lsp_document *), zval *return_value)
{
	lsp_document *document;
	zend_string *uri;
	zval *td;

	td = lsp_array_find(params, "textDocument");
	uri = lsp_array_string(td, "uri");

	if (!uri) {
		ZVAL_NULL(return_value);

		return;
	}

	document = lsp_document_from_uri(server, uri);
	handler(return_value, document);
}

static inline zend_string *lsp_path_to_uri(zend_string *path)
{
	return lsp_uri_from_path(path);
}

static inline void lsp_default_range(zval *range)
{
	zval start, end;

	array_init(range);
	array_init(&start);
	add_assoc_long(&start, "line", 0);
	add_assoc_long(&start, "character", 0);
	array_init(&end);
	add_assoc_long(&end, "line", 0);
	add_assoc_long(&end, "character", 1);
	add_assoc_zval(range, "start", &start);
	add_assoc_zval(range, "end", &end);
}

static inline void lsp_add_document_symbol(zval *items, zend_string *name, zend_long kind, zend_long one_based_line)
{
	zval item, range, selection_range;

	array_init(&item);
	add_assoc_str(&item, "name", zend_string_copy(name));
	add_assoc_long(&item, "kind", kind);
	lsp_line_range(&range, zend_empty_string, one_based_line);
	lsp_line_range(&selection_range, zend_empty_string, one_based_line);
	add_assoc_zval(&item, "range", &range);
	add_assoc_zval(&item, "selectionRange", &selection_range);
	add_next_index_zval(items, &item);
}

static inline void lsp_document_symbols(zval *return_value, lsp_document *document)
{
	zend_long kind;
	zend_string *label;
	zval *tokens_zv, *token;
	HashTable *tokens;
	uint32_t i, count;

	array_init(return_value);
	tokens_zv = zend_hash_str_find(Z_ARRVAL(document->lsparrot), "tokens", sizeof("tokens") - 1);
	if (!tokens_zv || Z_TYPE_P(tokens_zv) != IS_ARRAY) {
		return;
	}

	tokens = Z_ARRVAL_P(tokens_zv);
	count = zend_hash_num_elements(tokens);
	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		label = NULL;
		kind = 5;

		if (!token || Z_TYPE_P(token) != IS_ARRAY) {
			continue;
		}

			if (lsp_token_name_equals(token, "T_FUNCTION")) {
				label = lsp_next_string_token(tokens, i + 1);
				kind = 12;
			} else if (lsp_token_is_class_like(token)) {
				label = lsp_next_string_token(tokens, i + 1);
				kind = lsp_token_name_equals(token, "T_INTERFACE") ? 11 : (lsp_token_name_equals(token, "T_ENUM") ? 10 : 5);
			}

		if (label) {
			lsp_add_document_symbol(return_value, label, kind, lsp_token_long(token, "line", 1));
		}
	}
}

static inline bool lsp_matches_query(const char *value, size_t value_length, zend_string *query)
{
	const char *query_value;
	size_t i, query_length, query_offset = 0;

	if (!query || ZSTR_LEN(query) == 0) {
		return true;
	}

	query_value = ZSTR_VAL(query);
	query_length = ZSTR_LEN(query);
	if (query_length > value_length) {
		return false;
	}

	for (i = 0; i + query_length <= value_length; i++) {
		if (strncasecmp(value + i, query_value, query_length) == 0) {
			return true;
		}
	}

	for (i = 0; i < value_length && query_offset < query_length; i++) {
		if (tolower((unsigned char) value[i]) == tolower((unsigned char) query_value[query_offset])) {
			query_offset++;
		}
	}

	return query_offset == query_length;
}

static inline void lsp_add_workspace_symbol(zval *items, zend_string *name, zend_long kind, zend_string *uri)
{
	zval item, location, range;

	array_init(&item);
	add_assoc_str(&item, "name", zend_string_copy(name));
	add_assoc_long(&item, "kind", kind);
	array_init(&location);
	add_assoc_str(&location, "uri", zend_string_copy(uri));
	lsp_default_range(&range);
	add_assoc_zval(&location, "range", &range);
	add_assoc_zval(&item, "location", &location);
	add_next_index_zval(items, &item);
}

static inline void lsp_add_workspace_symbols_from_index_pass(lsp_server *server, zval *items, zend_string *query, bool vendor_symbols)
{
	lsp_symbol_index_header *header;
	zend_string *name, *uri, *path_string;
	uint32_t i;
	size_t fqcn_length, path_length;
	char *cursor, *end, kind, *fqcn, *path;

	if (!server->symbol_index.available || !server->symbol_index.addr) {
		return;
	}
	header = (lsp_symbol_index_header *) server->symbol_index.addr;
	if (header->magic != LSP_SYMBOL_INDEX_MAGIC || header->used > header->capacity) {
		return;
	}

	cursor = ((char *) server->symbol_index.addr) + sizeof(lsp_symbol_index_header);
	end = ((char *) server->symbol_index.addr) + header->used;
	for (i = 0; i < header->symbol_count && cursor < end; i++) {
		kind = *cursor++;
		fqcn = cursor;
		fqcn_length = strlen(fqcn);
		path = fqcn + fqcn_length + 1;

		if (path >= end) {
			break;
		}

		path_length = strlen(path);
		cursor = path + path_length + 1;
		if (cursor > end ||
			lsp_path_value_contains_analysis_helper(path, path_length) ||
			lsp_path_value_contains_vendor(path, path_length) != vendor_symbols ||
			!lsp_matches_query(fqcn, fqcn_length, query)
		) {
			continue;
		}

		name = zend_string_init(fqcn, fqcn_length, 0);
		path_string = zend_string_init(path, path_length, 0);
		uri = lsp_path_to_uri(path_string);
		lsp_add_workspace_symbol(items, name, lsp_symbol_workspace_kind(kind), uri);
		zend_string_release(uri);
		zend_string_release(path_string);
		zend_string_release(name);
	}
}

static inline void lsp_add_workspace_symbols_from_index(lsp_server *server, zval *items, zend_string *query)
{
	lsp_add_workspace_symbols_from_index_pass(server, items, query, false);
	lsp_add_workspace_symbols_from_index_pass(server, items, query, true);
}

static inline void lsp_workspace_symbols(lsp_server *server, zval *params, zval *return_value)
{
	zend_string *query = lsp_array_string(params, "query");

	array_init(return_value);

	lsp_add_workspace_symbols_from_index(server, return_value, query);
}

static inline void lsp_server_handle(lsp_server *server, zend_string *method, zval *params, zval *return_value)
{
	if (zend_string_equals_literal(method, "initialize")) {
		lsp_initialize(server, params, return_value);

		return;
	}

	if (zend_string_equals_literal(method, "initialized")) {
		ZVAL_NULL(return_value);

		return;
	}

	if (zend_string_equals_literal(method, "lsparrot.php/status")) {
		lsp_server_status(server, return_value);

		return;
	}

	if (zend_string_equals_literal(method, "lsparrot.php/preloadThisMembers")) {
		lsp_document_request_no_position(server, params, lsp_preload_this_member_cache, return_value);

		return;
	}

	if (zend_string_equals_literal(method, "lsparrot.php/invalidateMemberCache") ||
		zend_string_equals_literal(method, "workspace/didChangeWatchedFiles")
	) {
		zend_hash_clean(&server->member_cache);
		ZVAL_NULL(return_value);

		return;
	}

	if (zend_string_equals_literal(method, "shutdown")) {
		server->shutdown = true;
		server->saw_shutdown = true;
		ZVAL_NULL(return_value);

		return;
	}

	if (zend_string_equals_literal(method, "exit")) {
		server->shutdown = true;
		ZVAL_NULL(return_value);

		return;
	}

	if (zend_string_equals_literal(method, "textDocument/didOpen")) {
		lsp_did_open(server, params);
		ZVAL_NULL(return_value);

		return;
	}

	if (zend_string_equals_literal(method, "textDocument/didChange")) {
		lsp_did_change(server, params);
		ZVAL_NULL(return_value);

		return;
	}

	if (zend_string_equals_literal(method, "textDocument/didSave")) {
		lsp_did_save(server, params);
		ZVAL_NULL(return_value);

		return;
	}

	if (zend_string_equals_literal(method, "textDocument/didClose")) {
		lsp_did_close(server, params);
		ZVAL_NULL(return_value);

		return;
	}

	if (zend_string_equals_literal(method, "textDocument/completion")) {
		lsp_document_request(server, params, lsp_lsparrot_completion, return_value);

		return;
	}

	if (zend_string_equals_literal(method, "textDocument/hover")) {
		lsp_document_request(server, params, lsp_lsparrot_hover, return_value);

		return;
	}

	if (zend_string_equals_literal(method, "textDocument/definition")) {
		lsp_document_request(server, params, lsp_lsparrot_definition, return_value);

		return;
	}

	if (zend_string_equals_literal(method, "textDocument/codeLens")) {
		lsp_document_request_no_position(server, params, lsp_lsparrot_code_lens, return_value);

		return;
	}

	if (zend_string_equals_literal(method, "textDocument/signatureHelp")) {
		lsp_document_request(server, params, lsp_lsparrot_signature_help, return_value);

		return;
	}

	if (zend_string_equals_literal(method, "textDocument/documentSymbol")) {
		lsp_document_request_no_server_no_position(server, params, lsp_document_symbols, return_value);

		return;
	}

	if (zend_string_equals_literal(method, "workspace/symbol")) {
		lsp_workspace_symbols(server, params, return_value);

		return;
	}

	ZVAL_NULL(return_value);
}

extern void lsp_server_loop(lsp_server *server)
{
	zval message, *method_zv, *params, *id, result, empty_params;
	bool has_id;

	while (!server->shutdown && lsp_protocol_read(&message)) {
		lsp_reap_analyzer_jobs(server);
		method_zv = lsp_array_find(&message, "method");
		params = lsp_array_find(&message, "params");
		id = zend_hash_str_find(Z_ARRVAL(message), "id", sizeof("id") - 1);
		has_id = zend_hash_str_exists(Z_ARRVAL(message), "id", sizeof("id") - 1);

		ZVAL_UNDEF(&result);

		if (!method_zv || Z_TYPE_P(method_zv) != IS_STRING) {
			if (has_id) {
				lsp_protocol_error(id, -32600, "Invalid Request");
			}

			zval_ptr_dtor(&message);

			continue;
		}

		if (!params || Z_TYPE_P(params) != IS_ARRAY) {
			array_init(&empty_params);
			lsp_server_handle(server, Z_STR_P(method_zv), &empty_params, &result);
			zval_ptr_dtor(&empty_params);
		} else {
			lsp_server_handle(server, Z_STR_P(method_zv), params, &result);
		}

		if (EG(exception)) {
			zend_clear_exception();
			if (has_id) {
				lsp_protocol_error(id, -32603, "Internal error");
			}
		} else if (has_id) {
			lsp_protocol_respond(id, &result);
		}
		if (!Z_ISUNDEF(result)) {
			zval_ptr_dtor(&result);
		}

		zval_ptr_dtor(&message);
	}
}
