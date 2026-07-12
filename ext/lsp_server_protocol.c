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

#include <Zend/zend_language_parser.h>

static inline void lsp_initialize(lsp_server *server, zval *params, zval *return_value)
{
	zend_string *root_uri = lsp_array_string(params, "rootUri"), *root_path = lsp_array_string(params, "rootPath");
	zval capabilities, sync, save, completion, triggers, code_lens, signature, signature_triggers, code_action, code_action_kinds, rename, server_info, semantic_tokens, legend;
	zval workspace, workspace_folders, file_operations, will_rename, filters, filter, pattern;

	if (root_uri) {
		zend_string_release(server->root);
		server->root = lsp_uri_to_path(root_uri);
	} else if (root_path) {
		zend_string_release(server->root);
		server->root = zend_string_copy(root_path);
	}

	/* Multi-root: every workspace folder is indexed; the first folder (or
	 * rootUri) anchors the index cache location. */
	lsp_workspace_parse_folders(server, params);

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

	add_assoc_string(&capabilities, "positionEncoding", "utf-16");
	add_assoc_bool(&capabilities, "hoverProvider", true);
	add_assoc_bool(&capabilities, "definitionProvider", true);
	add_assoc_bool(&capabilities, "declarationProvider", true);
	add_assoc_bool(&capabilities, "typeDefinitionProvider", true);
	add_assoc_bool(&capabilities, "referencesProvider", true);
	add_assoc_bool(&capabilities, "documentHighlightProvider", true);
	add_assoc_bool(&capabilities, "implementationProvider", true);
	add_assoc_bool(&capabilities, "foldingRangeProvider", true);
	add_assoc_bool(&capabilities, "callHierarchyProvider", true);
	add_assoc_bool(&capabilities, "typeHierarchyProvider", true);
	array_init(&code_lens);
	add_assoc_bool(&code_lens, "resolveProvider", false);
	add_assoc_zval(&capabilities, "codeLensProvider", &code_lens);
	array_init(&code_action);
	array_init(&code_action_kinds);
	add_next_index_string(&code_action_kinds, "quickfix");
	add_next_index_string(&code_action_kinds, "source.organizeImports");
	add_assoc_zval(&code_action, "codeActionKinds", &code_action_kinds);
	add_assoc_zval(&capabilities, "codeActionProvider", &code_action);
	array_init(&rename);
	add_assoc_bool(&rename, "prepareProvider", true);
	add_assoc_zval(&capabilities, "renameProvider", &rename);
	add_assoc_bool(&capabilities, "documentFormattingProvider", server->options.formatting_enabled);
	add_assoc_bool(&capabilities, "documentRangeFormattingProvider", server->options.formatting_enabled);
	add_assoc_bool(&capabilities, "inlayHintProvider", true);
	array_init(&semantic_tokens);
	lsp_semantic_token_legend(&legend);
	add_assoc_zval(&semantic_tokens, "legend", &legend);
	add_assoc_bool(&semantic_tokens, "full", true);
	add_assoc_zval(&capabilities, "semanticTokensProvider", &semantic_tokens);
	array_init(&signature);
	array_init(&signature_triggers);
	add_next_index_string(&signature_triggers, "(");
	add_next_index_string(&signature_triggers, ",");
	add_assoc_zval(&signature, "triggerCharacters", &signature_triggers);
	add_assoc_zval(&capabilities, "signatureHelpProvider", &signature);
	add_assoc_bool(&capabilities, "documentSymbolProvider", true);
	add_assoc_bool(&capabilities, "workspaceSymbolProvider", true);

	array_init(&workspace);
	array_init(&workspace_folders);
	add_assoc_bool(&workspace_folders, "supported", true);
	add_assoc_bool(&workspace_folders, "changeNotifications", true);
	add_assoc_zval(&workspace, "workspaceFolders", &workspace_folders);
	array_init(&file_operations);
	array_init(&will_rename);
	array_init(&filters);
	array_init(&filter);
	array_init(&pattern);
	add_assoc_string(&pattern, "glob", "**/*.php");
	add_assoc_string(&pattern, "matches", "file");
	add_assoc_zval(&filter, "pattern", &pattern);
	add_next_index_zval(&filters, &filter);
	add_assoc_zval(&will_rename, "filters", &filters);
	add_assoc_zval(&file_operations, "willRename", &will_rename);
	add_assoc_zval(&workspace, "fileOperations", &file_operations);
	add_assoc_zval(&capabilities, "workspace", &workspace);

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

	/* Full-document sync is advertised, so every change event carries the
	 * whole text and the LAST event in the batch is authoritative. A change
	 * carrying a range would be an incremental event from a non-conforming
	 * client; treating its fragment as the full document would corrupt the
	 * buffer, so such events are ignored. */
	if (changes && Z_TYPE_P(changes) == IS_ARRAY && zend_hash_num_elements(Z_ARRVAL_P(changes)) > 0) {
		change = zend_hash_index_find(Z_ARRVAL_P(changes), zend_hash_num_elements(Z_ARRVAL_P(changes)) - 1);
	}

	if (change && lsp_array_find(change, "range")) {
		return;
	}

	text = lsp_array_string(change, "text");
	if (!uri || !text) {
		return;
	}

	document = lsp_document_open_or_change(server, uri, version, text);
	lsp_psalm_ls_document_change(server, document);

	/* Coalesce typing bursts: when a newer change for this document is
	 * already queued (with no request in between), publishing diagnostics
	 * for this revision would be wasted work. */
	if (!lsp_protocol_queue_supersedes_change(uri)) {
		lsp_publish_document_diagnostics(server, document);
	}
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
	/* Saving is the natural refresh point for the version-less analyzer type
	 * cache entries of this document. */
	lsp_server_evict_document_caches(server, uri, true);
	/* Drop only the member-cache entries built from the saved file; ancestor
	 * entries re-validate themselves through version/mtime freshness checks. */
	lsp_member_cache_invalidate_path(server, document->path);
	lsp_index_refresh_file(server, document->path);
	lsp_psalm_ls_document_save(server, document);
	lsp_reschedule_project_analyzers(server, document);
	lsp_publish_document_diagnostics(server, document);
}

static inline void lsp_did_change_watched_files(lsp_server *server, zval *params)
{
	zend_string *uri, *path;
	zval *changes = lsp_array_find(params, "changes"), *change;

	if (!changes || Z_TYPE_P(changes) != IS_ARRAY) {
		zend_hash_clean(&server->member_cache);

		return;
	}

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(changes), change) {
		uri = lsp_array_string(change, "uri");
		if (!uri) {
			continue;
		}

		path = lsp_uri_to_path(uri);
		lsp_member_cache_invalidate_path(server, path);
		lsp_index_refresh_file(server, path);
		zend_string_release(path);
	} ZEND_HASH_FOREACH_END();
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
	lsp_server_evict_document_caches(server, uri, true);
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

static inline void lsp_document_request_params(lsp_server *server, zval *params, void (*handler)(lsp_server *, zval *, lsp_document *, zval *), zval *return_value)
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
	handler(server, return_value, document, params);
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

#define LSP_SYMBOL_FRAME_MAX 16

typedef struct _lsp_document_symbol_frame {
	zval item;       /* DocumentSymbol under construction */
	zval children;
	zend_long body_depth;
	size_t start_offset;
	bool is_enum;
	bool is_class_like;
} lsp_document_symbol_frame;

static inline void lsp_document_symbol_leaf(zval *target, lsp_document *document, zend_string *name, zend_long kind, size_t start_offset, size_t end_offset, size_t name_offset, size_t name_length)
{
	zval item, range, selection_range;

	array_init(&item);
	add_assoc_str(&item, "name", zend_string_copy(name));
	add_assoc_long(&item, "kind", kind);
	lsp_range_from_offsets(document->text, start_offset, end_offset, &range);
	add_assoc_zval(&item, "range", &range);
	lsp_range_from_offsets(document->text, name_offset, name_offset + name_length, &selection_range);
	add_assoc_zval(&item, "selectionRange", &selection_range);
	add_next_index_zval(target, &item);
}

static inline void lsp_document_symbol_frame_open(lsp_document_symbol_frame *frame, lsp_document *document, zend_string *name, zend_long kind, size_t start_offset, size_t name_offset, size_t name_length, zend_long body_depth, bool is_enum, bool is_class_like)
{
	zval selection_range;

	array_init(&frame->item);
	add_assoc_str(&frame->item, "name", zend_string_copy(name));
	add_assoc_long(&frame->item, "kind", kind);
	lsp_range_from_offsets(document->text, name_offset, name_offset + name_length, &selection_range);
	add_assoc_zval(&frame->item, "selectionRange", &selection_range);
	array_init(&frame->children);
	frame->body_depth = body_depth;
	frame->start_offset = start_offset;
	frame->is_enum = is_enum;
	frame->is_class_like = is_class_like;
}

static inline void lsp_document_symbol_frame_close(lsp_document_symbol_frame *frame, lsp_document *document, zval *target, size_t end_offset)
{
	zval range;

	lsp_range_from_offsets(document->text, frame->start_offset, end_offset, &range);
	add_assoc_zval(&frame->item, "range", &range);

	if (zend_hash_num_elements(Z_ARRVAL(frame->children)) > 0) {
		add_assoc_zval(&frame->item, "children", &frame->children);
	} else {
		zval_ptr_dtor(&frame->children);
	}

	add_next_index_zval(target, &frame->item);
}

/* Hierarchical DocumentSymbol tree from the token stream: classes own their
 * methods and enum cases as children, with real ranges (declaration through
 * closing brace) and name-based selection ranges. Token-driven so it also
 * works on documents that do not currently parse. */
static inline void lsp_document_symbols_uncached(zval *return_value, lsp_document *document);

/* Text pointer identity keys the cache the same way the token cache does:
 * didChange swaps document->text, naturally invalidating the memo. */
static inline void lsp_document_symbols(zval *return_value, lsp_document *document)
{
	if (document->outline_cache_text == document->text && Z_TYPE(document->outline_cache) == IS_ARRAY) {
		ZVAL_COPY(return_value, &document->outline_cache);

		return;
	}

	lsp_document_symbols_uncached(return_value, document);

	if (document->outline_cache_text) {
		zend_string_release(document->outline_cache_text);
		zval_ptr_dtor(&document->outline_cache);
	}

	document->outline_cache_text = zend_string_copy(document->text);
	ZVAL_COPY(&document->outline_cache, return_value);
}

static inline void lsp_document_symbols_uncached(zval *return_value, lsp_document *document)
{
	lsp_document_symbol_frame frames[LSP_SYMBOL_FRAME_MAX];
	zend_long depth = 0, kind, pending_kind = 0;
	zend_string *label, *pending_name = NULL;
	zval *tokens_zv, *token, *name_token;
	zval *target;
	HashTable *tokens;
	uint32_t i, count, name_index;
	int frame_count = 0;
	size_t token_offset, pending_start = 0, pending_name_offset = 0, pending_name_length = 0;
	bool pending_is_class = false, pending_active = false, previous_is_new = false;

	array_init(return_value);
	tokens_zv = zend_hash_str_find(Z_ARRVAL(document->lsparrot), "tokens", sizeof("tokens") - 1);
	if (!tokens_zv || Z_TYPE_P(tokens_zv) != IS_ARRAY) {
		return;
	}

	tokens = Z_ARRVAL_P(tokens_zv);
	count = zend_hash_num_elements(tokens);
	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY) {
			continue;
		}

		if (lsp_token_name_equals(token, "T_WHITESPACE") || lsp_token_name_equals(token, "T_COMMENT") || lsp_token_name_equals(token, "T_DOC_COMMENT")) {
			continue;
		}

		token_offset = (size_t) lsp_token_long(token, "offset", 0);

		if (lsp_token_is_char(token, '{') || lsp_token_name_equals(token, "T_CURLY_OPEN") || lsp_token_name_equals(token, "T_DOLLAR_OPEN_CURLY_BRACES")) {
			depth++;
			if (pending_active && frame_count < LSP_SYMBOL_FRAME_MAX) {
				lsp_document_symbol_frame_open(&frames[frame_count], document, pending_name, pending_kind, pending_start, pending_name_offset, pending_name_length, depth, pending_is_class && pending_kind == 10, pending_is_class);
				frame_count++;
				pending_active = false;
				pending_name = NULL;
			}
			previous_is_new = false;
			continue;
		}

		if (lsp_token_is_char(token, '}')) {
			if (frame_count > 0 && frames[frame_count - 1].body_depth == depth) {
				frame_count--;
				target = frame_count > 0 ? &frames[frame_count - 1].children : return_value;
				lsp_document_symbol_frame_close(&frames[frame_count], document, target, token_offset + 1);
			}
			if (depth > 0) {
				depth--;
			}
			previous_is_new = false;
			continue;
		}

		if (pending_active && lsp_token_is_char(token, ';')) {
			/* Bodyless declaration (abstract/interface method). */
			target = frame_count > 0 ? &frames[frame_count - 1].children : return_value;
			lsp_document_symbol_leaf(target, document, pending_name, pending_kind, pending_start, token_offset + 1, pending_name_offset, pending_name_length);
			pending_active = false;
			pending_name = NULL;
			continue;
		}

		if (lsp_token_is_class_like(token)) {
			if (previous_is_new) {
				/* Anonymous class: no symbol, braces tracked by depth. */
				previous_is_new = false;
				continue;
			}

			label = lsp_next_string_token(tokens, i + 1);
			if (label) {
				kind = lsp_token_name_equals(token, "T_INTERFACE") ? 11 : (lsp_token_name_equals(token, "T_ENUM") ? 10 : 5);
				name_token = lsp_next_function_name_token_ex(tokens, i + 1, &name_index);
				pending_active = true;
				pending_is_class = true;
				pending_kind = kind;
				pending_name = label;
				pending_start = token_offset;
				pending_name_offset = name_token ? (size_t) lsp_token_long(name_token, "offset", 0) : token_offset;
				pending_name_length = ZSTR_LEN(label);
			}
			previous_is_new = false;
			continue;
		}

		if (lsp_token_name_equals(token, "T_FUNCTION")) {
			name_token = lsp_next_function_name_token_ex(tokens, i + 1, &name_index);
			label = name_token ? lsp_token_string(name_token, "text") : NULL;
			if (label) {
				kind = frame_count > 0 && frames[frame_count - 1].body_depth == depth && frames[frame_count - 1].is_class_like
					? (zend_string_equals_literal(label, "__construct") ? 9 : 6)
					: 12
				;
				pending_active = true;
				pending_is_class = false;
				pending_kind = kind;
				pending_name = label;
				pending_start = token_offset;
				pending_name_offset = (size_t) lsp_token_long(name_token, "offset", 0);
				pending_name_length = ZSTR_LEN(label);
			}
			previous_is_new = false;
			continue;
		}

		if (lsp_token_name_equals(token, "T_CASE") &&
			frame_count > 0 &&
			frames[frame_count - 1].is_enum &&
			frames[frame_count - 1].body_depth == depth
		) {
			label = lsp_next_string_token(tokens, i + 1);
			if (label) {
				name_token = lsp_next_function_name_token_ex(tokens, i + 1, &name_index);
				lsp_document_symbol_leaf(&frames[frame_count - 1].children, document, label, 22,
					token_offset,
					(name_token ? (size_t) lsp_token_long(name_token, "offset", 0) : token_offset) + ZSTR_LEN(label),
					name_token ? (size_t) lsp_token_long(name_token, "offset", 0) : token_offset,
					ZSTR_LEN(label));
			}
			previous_is_new = false;
			continue;
		}

		previous_is_new = lsp_token_name_equals(token, "T_NEW");
	}

	/* Close frames left open by an unfinished document. */
	while (frame_count > 0) {
		frame_count--;
		target = frame_count > 0 ? &frames[frame_count - 1].children : return_value;
		lsp_document_symbol_frame_close(&frames[frame_count], document, target, ZSTR_LEN(document->text));
	}
}

static inline bool lsp_matches_query(const char *value, size_t value_length, zend_string *query)
{
	/* Case-insensitive subsequence match (every substring match is also a
	 * subsequence match, so one pass decides). The query arrives pre-lowered
	 * by the caller so the per-entry cost is a single tolower per byte. */
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

	for (i = 0; i < value_length && query_offset < query_length; i++) {
		if (tolower((unsigned char) value[i]) == (unsigned char) query_value[query_offset]) {
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

/* Editors re-issue workspace/symbol on every keystroke of the picker, so the
 * scan is capped and prioritized: one pass over the index emits project
 * symbols directly (stopping at the cap) while vendor matches queue in a side
 * list that only tops up remaining slots. */
#define LSP_WORKSPACE_SYMBOL_LIMIT 256

static inline void lsp_add_workspace_symbol_entry(lsp_symbol_index *region, const lsp_symbol_entry *entry, zval *items)
{
	zend_string *name, *uri, *path_string;
	const char *fqcn, *path;

	fqcn = lsp_symbol_entry_fqcn(region, entry);
	path = lsp_symbol_entry_path(region, entry);
	name = zend_string_init(fqcn, entry->fqcn_length, 0);
	path_string = zend_string_init(path, entry->path_length, 0);
	uri = lsp_path_to_uri(path_string);
	lsp_add_workspace_symbol(items, name, lsp_symbol_workspace_kind((char) entry->kind), uri);
	zend_string_release(uri);
	zend_string_release(path_string);
	zend_string_release(name);
}

static inline void lsp_add_workspace_symbols_from_index(lsp_server *server, zval *items, zend_string *query)
{
	const lsp_symbol_entry *entry;
	lsp_symbol_index *region = &server->symbol_index;
	const char *fqcn;
	uint32_t i, project_count, vendor_count, vendor_take;
	uint32_t *vendor_matches;

	lsp_symbol_index_table_ensure(region);

	vendor_matches = emalloc(sizeof(uint32_t) * LSP_WORKSPACE_SYMBOL_LIMIT);
	project_count = 0;
	vendor_count = 0;

	for (i = 0; i < region->entry_count && project_count < LSP_WORKSPACE_SYMBOL_LIMIT; i++) {
		entry = &region->entries[i];
		if ((entry->flags & LSP_SYMBOL_ENTRY_DELETED) != 0) {
			continue;
		}

		if ((entry->flags & LSP_SYMBOL_ENTRY_VENDOR) != 0) {
			if (vendor_count >= LSP_WORKSPACE_SYMBOL_LIMIT) {
				continue;
			}
		}

		fqcn = lsp_symbol_entry_fqcn(region, entry);
		if (!lsp_matches_query(fqcn, entry->fqcn_length, query)) {
			continue;
		}

		if ((entry->flags & LSP_SYMBOL_ENTRY_VENDOR) != 0) {
			vendor_matches[vendor_count++] = i;
		} else {
			lsp_add_workspace_symbol_entry(region, entry, items);
			project_count++;
		}
	}

	vendor_take = LSP_WORKSPACE_SYMBOL_LIMIT - project_count;
	if (vendor_take > vendor_count) {
		vendor_take = vendor_count;
	}
	for (i = 0; i < vendor_take; i++) {
		lsp_add_workspace_symbol_entry(region, &region->entries[vendor_matches[i]], items);
	}

	efree(vendor_matches);
}

static inline void lsp_workspace_symbols(lsp_server *server, zval *params, zval *return_value)
{
	zend_string *query, *lowered;

	query = lsp_array_string(params, "query");
	lowered = query ? zend_string_tolower(query) : NULL;

	array_init(return_value);

	lsp_index_join_worker(server);
	lsp_add_workspace_symbols_from_index(server, return_value, lowered);

	if (lowered) {
		zend_string_release(lowered);
	}
}

#define LSP_FOLDING_STACK_MAX 128

static inline void lsp_folding_add_range(zval *items, zend_long start_line, zend_long end_line, const char *kind)
{
	zval range;

	if (end_line <= start_line) {
		return;
	}

	array_init(&range);
	add_assoc_long(&range, "startLine", start_line);
	add_assoc_long(&range, "endLine", end_line);
	if (kind) {
		add_assoc_string(&range, "kind", kind);
	}
	add_next_index_zval(items, &range);
}

static inline zend_long lsp_folding_token_newlines(zend_string *text)
{
	const char *p = ZSTR_VAL(text), *end = p + ZSTR_LEN(text);
	zend_long count = 0;

	while (p < end) {
		p = memchr(p, '\n', (size_t) (end - p));
		if (!p) {
			break;
		}
		count++;
		p++;
	}

	return count;
}

/* Brace/bracket/paren blocks, multi-line doc comments, heredocs, and grouped
 * use statements fold. The closing-delimiter line stays visible (endLine =
 * closer line - 1), matching how VSCode folds braces; comments collapse onto
 * their first line. */
extern void lsp_lsparrot_folding_range(lsp_server *server, zval *return_value, lsp_document *document)
{
	zval *tokens_zv, *token;
	zend_string *token_text;
	HashTable *tokens;
	zend_long open_lines[LSP_FOLDING_STACK_MAX];
	zend_long token_line, newlines, heredoc_start_line;
	uint32_t i, count, depth;
	zend_long id;

	(void) server;

	array_init(return_value);

	tokens_zv = zend_hash_str_find(Z_ARRVAL(document->lsparrot), "tokens", sizeof("tokens") - 1);
	if (!tokens_zv || Z_TYPE_P(tokens_zv) != IS_ARRAY) {
		return;
	}

	tokens = Z_ARRVAL_P(tokens_zv);
	count = zend_hash_num_elements(tokens);
	depth = 0;
	heredoc_start_line = -1;

	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY) {
			continue;
		}

		id = lsp_token_long(token, "id", 0);
		token_line = lsp_token_long(token, "line", 1) - 1;

		if (id == T_COMMENT || id == T_DOC_COMMENT) {
			token_text = lsp_token_string(token, "text");
			newlines = token_text ? lsp_folding_token_newlines(token_text) : 0;
			if (newlines > 0) {
				lsp_folding_add_range(return_value, token_line, token_line + newlines, "comment");
			}
			continue;
		}

		if (id == T_START_HEREDOC) {
			heredoc_start_line = token_line;
			continue;
		}

		if (id == T_END_HEREDOC) {
			if (heredoc_start_line >= 0) {
				lsp_folding_add_range(return_value, heredoc_start_line, token_line - 1, NULL);
				heredoc_start_line = -1;
			}
			continue;
		}

		if (id == '{' || id == '(' || id == '[' || id == T_ATTRIBUTE || id == T_CURLY_OPEN || id == T_DOLLAR_OPEN_CURLY_BRACES) {
			if (depth < LSP_FOLDING_STACK_MAX) {
				open_lines[depth] = token_line;
			}
			depth++;
			continue;
		}

		if (id == '}' || id == ')' || id == ']') {
			if (depth > 0) {
				depth--;
				if (depth < LSP_FOLDING_STACK_MAX) {
					lsp_folding_add_range(return_value, open_lines[depth], token_line - 1, NULL);
				}
			}
			continue;
		}
	}
}

static inline bool lsp_server_handle(lsp_server *server, zend_string *method, zval *params, zval *return_value)
{
	if (zend_string_equals_literal(method, "initialize")) {
		lsp_initialize(server, params, return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "initialized")) {
		ZVAL_NULL(return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "lsparrot.php/status")) {
		lsp_server_status(server, return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "workspace/didChangeWatchedFiles")) {
		lsp_did_change_watched_files(server, params);
		ZVAL_NULL(return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "shutdown")) {
		/* Per the spec the server stays alive after shutdown and only the
		 * exit notification (or EOF) ends the loop. */
		server->saw_shutdown = true;
		ZVAL_NULL(return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "exit")) {
		server->shutdown = true;
		ZVAL_NULL(return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "textDocument/didOpen")) {
		lsp_did_open(server, params);
		ZVAL_NULL(return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "textDocument/didChange")) {
		lsp_did_change(server, params);
		ZVAL_NULL(return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "textDocument/didSave")) {
		lsp_did_save(server, params);
		ZVAL_NULL(return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "textDocument/didClose")) {
		lsp_did_close(server, params);
		ZVAL_NULL(return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "textDocument/completion")) {
		lsp_document_request(server, params, lsp_lsparrot_completion, return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "textDocument/hover")) {
		lsp_document_request(server, params, lsp_lsparrot_hover, return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "textDocument/definition") ||
		zend_string_equals_literal(method, "textDocument/declaration")
	) {
		/* PHP has no declaration/definition split; declaration aliases the
		 * definition lookup. */
		lsp_document_request(server, params, lsp_lsparrot_definition, return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "textDocument/typeDefinition")) {
		lsp_document_request(server, params, lsp_lsparrot_type_definition, return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "textDocument/foldingRange")) {
		lsp_document_request_no_position(server, params, lsp_lsparrot_folding_range, return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "textDocument/prepareCallHierarchy")) {
		lsp_document_request(server, params, lsp_lsparrot_prepare_call_hierarchy, return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "callHierarchy/incomingCalls")) {
		lsp_lsparrot_call_hierarchy_incoming(server, return_value, params);

		return true;
	}

	if (zend_string_equals_literal(method, "callHierarchy/outgoingCalls")) {
		lsp_lsparrot_call_hierarchy_outgoing(server, return_value, params);

		return true;
	}

	if (zend_string_equals_literal(method, "textDocument/prepareTypeHierarchy")) {
		lsp_document_request(server, params, lsp_lsparrot_prepare_type_hierarchy, return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "typeHierarchy/supertypes")) {
		lsp_lsparrot_type_hierarchy_supertypes(server, return_value, params);

		return true;
	}

	if (zend_string_equals_literal(method, "typeHierarchy/subtypes")) {
		lsp_lsparrot_type_hierarchy_subtypes(server, return_value, params);

		return true;
	}

	if (zend_string_equals_literal(method, "workspace/didChangeConfiguration")) {
		lsp_options_apply_runtime(&server->options, params);
		ZVAL_NULL(return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "workspace/didChangeWorkspaceFolders")) {
		lsp_workspace_did_change_folders(server, params);
		ZVAL_NULL(return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "workspace/willRenameFiles")) {
		lsp_lsparrot_will_rename_files(server, return_value, params);

		return true;
	}

	if (zend_string_equals_literal(method, "textDocument/references")) {
		lsp_document_request_params(server, params, lsp_lsparrot_references, return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "textDocument/documentHighlight")) {
		lsp_document_request(server, params, lsp_lsparrot_document_highlight, return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "textDocument/implementation")) {
		lsp_document_request(server, params, lsp_lsparrot_implementation, return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "textDocument/codeAction")) {
		lsp_document_request_params(server, params, lsp_lsparrot_code_action, return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "textDocument/prepareRename")) {
		lsp_document_request(server, params, lsp_lsparrot_prepare_rename, return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "textDocument/rename")) {
		lsp_document_request_params(server, params, lsp_lsparrot_rename, return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "textDocument/formatting")) {
		lsp_document_request_params(server, params, lsp_lsparrot_formatting, return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "textDocument/rangeFormatting")) {
		lsp_document_request_params(server, params, lsp_lsparrot_range_formatting, return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "textDocument/inlayHint")) {
		lsp_document_request_params(server, params, lsp_lsparrot_inlay_hint, return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "textDocument/codeLens")) {
		lsp_document_request_no_position(server, params, lsp_lsparrot_code_lens, return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "textDocument/semanticTokens/full")) {
		lsp_document_request_no_position(server, params, lsp_lsparrot_semantic_tokens, return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "textDocument/signatureHelp")) {
		lsp_document_request(server, params, lsp_lsparrot_signature_help, return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "textDocument/documentSymbol")) {
		lsp_document_request_no_server_no_position(server, params, lsp_document_symbols, return_value);

		return true;
	}

	if (zend_string_equals_literal(method, "workspace/symbol")) {
		lsp_workspace_symbols(server, params, return_value);

		return true;
	}

	ZVAL_NULL(return_value);

	return false;
}

extern void lsp_server_loop(lsp_server *server)
{
	double started_at;
	zval message, *method_zv, *params, *id, result, empty_params;
	bool has_id, handled;

	while (!server->shutdown && lsp_protocol_next_message(server, &message)) {
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

		if (has_id && lsp_protocol_request_is_cancelled(id)) {
			lsp_protocol_error(id, -32800, "Request cancelled");
			zval_ptr_dtor(&message);

			continue;
		}

		/* After shutdown only the exit notification is honored; requests get
		 * InvalidRequest and other notifications are dropped. */
		if (server->saw_shutdown && !zend_string_equals_literal(Z_STR_P(method_zv), "exit")) {
			if (has_id) {
				lsp_protocol_error(id, -32600, "Invalid Request");
			}

			zval_ptr_dtor(&message);

			continue;
		}

		started_at = lsp_now_seconds();

		if (!params || Z_TYPE_P(params) != IS_ARRAY) {
			array_init(&empty_params);
			handled = lsp_server_handle(server, Z_STR_P(method_zv), &empty_params, &result);
			zval_ptr_dtor(&empty_params);
		} else {
			handled = lsp_server_handle(server, Z_STR_P(method_zv), params, &result);
		}

		lsp_perf_stats_record(server, Z_STR_P(method_zv), lsp_now_seconds() - started_at);

		if (EG(exception)) {
			zend_clear_exception();
			if (has_id) {
				lsp_protocol_error(id, -32603, "Internal error");
			}
		} else if (has_id) {
			if (handled) {
				lsp_protocol_respond(id, &result);
			} else {
				lsp_protocol_error(id, -32601, "Method not found");
			}
		}
		if (!Z_ISUNDEF(result)) {
			zval_ptr_dtor(&result);
		}

		zval_ptr_dtor(&message);
	}
}
