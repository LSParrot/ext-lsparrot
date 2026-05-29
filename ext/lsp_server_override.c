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

static inline bool lsp_override_declaration_context(zend_string *text, size_t offset, zend_string *prefix, size_t *class_start, size_t *body_start, size_t *body_end, zend_long *body_depth, lsp_method_visibility *visibility, bool *is_static)
{
	const char *value, *word_start, *word_end;
	size_t prefix_start, statement_start, p;
	bool saw_function = false;

	value = ZSTR_VAL(text);

	if (!lsp_find_enclosing_class_header(text, offset, class_start, body_start, body_end, body_depth)) {
		return false;
	}

	prefix_start = offset >= ZSTR_LEN(prefix) ? offset - ZSTR_LEN(prefix) : 0;
	if (prefix_start < *body_start || prefix_start > *body_end || lsp_brace_depth_at(text, prefix_start) != *body_depth) {
		return false;
	}

	statement_start = prefix_start;
	while (statement_start > *body_start &&
		value[statement_start - 1] != '\n' &&
		value[statement_start - 1] != ';' &&
		value[statement_start - 1] != '{' &&
		value[statement_start - 1] != '}'
	) {
		statement_start--;
	}

	*visibility = LSP_METHOD_VISIBILITY_PUBLIC;
	*is_static = false;

	p = statement_start;
	while (p < prefix_start) {
		while (p < prefix_start && isspace((unsigned char) value[p])) {
			p++;
		}

		if (p >= prefix_start) {
			break;
		}

		if (!lsp_doc_is_identifier_start(value[p])) {
			return false;
		}

		word_start = value + p;

		p++;
		while (p < prefix_start && lsp_doc_is_identifier_char(value[p])) {
			p++;
		}

		word_end = value + p;

		if (lsp_word_at_slice_equals(word_start, word_end, "public")) {
			*visibility = LSP_METHOD_VISIBILITY_PUBLIC;
		} else if (lsp_word_at_slice_equals(word_start, word_end, "protected")) {
			*visibility = LSP_METHOD_VISIBILITY_PROTECTED;
		} else if (lsp_word_at_slice_equals(word_start, word_end, "private")) {
			return false;
		} else if (lsp_word_at_slice_equals(word_start, word_end, "static")) {
			*is_static = true;
		} else if (lsp_word_at_slice_equals(word_start, word_end, "abstract") ||
			lsp_word_at_slice_equals(word_start, word_end, "final")
		) {
			continue;
		} else if (lsp_word_at_slice_equals(word_start, word_end, "function")) {
			saw_function = true;
			break;
		} else {
			return false;
		}
	}

	if (!saw_function) {
		return false;
	}

	while (p < prefix_start && isspace((unsigned char) value[p])) {
		p++;
	}

	if (p < prefix_start && value[p] == '&') {
		p++;
		while (p < prefix_start && isspace((unsigned char) value[p])) {
			p++;
		}
	}

	return p == prefix_start;
}

static inline void lsp_method_modifiers(HashTable *tokens, uint32_t index, zend_string *text, zend_long body_depth, lsp_method_visibility *visibility, bool *is_static, bool *is_final)
{
	zend_long i;
	zval *token;

	*visibility = LSP_METHOD_VISIBILITY_PUBLIC;
	*is_static = false;
	*is_final = false;

	for (i = (zend_long) index - 1; i >= 0; i--) {
		token = zend_hash_index_find(tokens, (zend_ulong) i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY) {
			continue;
		}

		if (!lsp_token_at_depth(text, token, body_depth)) {
			continue;
		}

		if (lsp_token_is_char(token, ';') || lsp_token_is_char(token, '{') || lsp_token_is_char(token, '}')) {
			break;
		}

		if (lsp_token_name_equals(token, "T_PRIVATE")) {
			*visibility = LSP_METHOD_VISIBILITY_PRIVATE;
		} else if (lsp_token_name_equals(token, "T_PROTECTED")) {
			*visibility = LSP_METHOD_VISIBILITY_PROTECTED;
		} else if (lsp_token_name_equals(token, "T_PUBLIC")) {
			*visibility = LSP_METHOD_VISIBILITY_PUBLIC;
		} else if (lsp_token_name_equals(token, "T_STATIC")) {
			*is_static = true;
		} else if (lsp_token_text_equals(token, "final")) {
			*is_final = true;
		}
	}
}

static inline bool lsp_class_declares_method(zend_string *text, HashTable *tokens, size_t body_start, size_t body_end, zend_long body_depth, zend_string *method_name)
{
	zend_string *label;
	zval *token;
	uint32_t i, count = zend_hash_num_elements(tokens);

	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY || !lsp_token_in_bounds(token, body_start, body_end)) {
			continue;
		}

		if (!lsp_token_name_equals(token, "T_FUNCTION") || !lsp_token_at_depth(text, token, body_depth)) {
			continue;
		}

		label = lsp_next_string_token(tokens, i + 1);
		if (label && zend_string_equals(label, method_name)) {
			return true;
		}
	}

	return false;
}

static inline bool lsp_completion_items_contain_method(zval *items, zend_string *label)
{
	zend_long kind;
	zend_string *item_label;
	zval *item;

	if (Z_TYPE_P(items) != IS_ARRAY) {
		return false;
	}

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(items), item) {
		if (Z_TYPE_P(item) != IS_ARRAY) {
			continue;
		}

		kind = lsp_array_long(item, "kind", 0);
		item_label = lsp_array_string(item, "label");

		if (kind == 2 && item_label && zend_string_equals(item_label, label)) {
			return true;
		}
	} ZEND_HASH_FOREACH_END();

	return false;
}

static inline void lsp_snippet_append_escaped(smart_str *snippet, const char *value, size_t length)
{
	size_t i;

	for (i = 0; i < length; i++) {
		if (value[i] == '\\' || value[i] == '$' || value[i] == '}') {
			smart_str_appendc(snippet, '\\');
		}

		smart_str_appendc(snippet, value[i]);
	}
}

static inline zend_string *lsp_override_method_snippet(zend_string *text, zval *name_token, HashTable *tokens, uint32_t name_index, zend_long body_depth)
{
	const char *value = ZSTR_VAL(text);
	smart_str snippet = {0};
	size_t start, end;

	start = (size_t) lsp_token_long(name_token, "offset", 0);
	end = lsp_method_signature_end(tokens, name_index, text, body_depth);

	if (end > ZSTR_LEN(text)) {
		end = ZSTR_LEN(text);
	}

	while (end > start && isspace((unsigned char) value[end - 1])) {
		end--;
	}

	lsp_snippet_append_escaped(&snippet, value + start, end - start);
	smart_str_appendc(&snippet, '\n');
	smart_str_appendc(&snippet, '{');
	smart_str_appendc(&snippet, '\n');
	smart_str_appendl(&snippet, "    $0", sizeof("    $0") - 1);
	smart_str_appendc(&snippet, '\n');
	smart_str_appendc(&snippet, '}');
	smart_str_0(&snippet);

	return snippet.s;
}

static inline void lsp_add_override_completion_item(zval *items, lsp_document *document, size_t prefix_start, size_t offset, zend_string *label, zend_string *parent_class, zend_string *snippet, zend_string *signature)
{
	zend_string *detail, *sort_text;
	zval item, data, text_edit, edit_range;

	detail = signature
		? strpprintf(0, "override %s::%s", ZSTR_VAL(parent_class), ZSTR_VAL(signature))
		: strpprintf(0, "override %s::%s", ZSTR_VAL(parent_class), ZSTR_VAL(label))
	;

	sort_text = strpprintf(0, "0000:%s", ZSTR_VAL(label));

	array_init(&item);
	add_assoc_str(&item, "label", zend_string_copy(label));
	add_assoc_long(&item, "kind", 2);
	add_assoc_str(&item, "detail", detail);
	add_assoc_str(&item, "filterText", zend_string_copy(label));
	add_assoc_str(&item, "sortText", sort_text);
	add_assoc_long(&item, "insertTextFormat", 2);
	array_init(&text_edit);
	lsp_range_from_offsets(document->text, prefix_start, offset, &edit_range);
	add_assoc_zval(&text_edit, "range", &edit_range);
	add_assoc_str(&text_edit, "newText", zend_string_copy(snippet));
	add_assoc_zval(&item, "textEdit", &text_edit);
	array_init(&data);
	add_assoc_string(&data, "source", "lsparrot");
	add_assoc_zval(&item, "data", &data);
	add_next_index_zval(items, &item);
}

static inline void lsp_add_override_method_completions_for_class(lsp_server *server, zval *items, lsp_document *document, size_t prefix_start, size_t offset, zend_string *prefix, zend_string *class_name, lsp_method_visibility requested_visibility, bool requested_static, HashTable *current_tokens, size_t current_body_start, size_t current_body_end, zend_long current_body_depth, uint32_t depth)
{
	lsp_method_visibility visibility;
	zend_long body_depth = 0;
	zend_string *path, *contents, *label, *snippet, *signature, *parent_class;
	zval tokens_zv, *token, *name_token;
	HashTable *tokens;
	uint32_t i, count, name_index;
	size_t class_start = 0, body_start = 0, body_end = 0;
	bool method_static, method_final;

	if (depth > 8) {
		return;
	}

	path = lsp_find_project_symbol_path(server, LSP_SYMBOL_CLASS, class_name);
	if (!path) {
		return;
	}

	contents = lsp_read_file(path);
	zend_string_release(path);
	if (contents == zend_empty_string) {
		return;
	}

	ZVAL_UNDEF(&tokens_zv);
	lsp_lsparrot_tokens_to_zval(&tokens_zv, contents);

	if (Z_TYPE(tokens_zv) != IS_ARRAY || !lsp_find_first_class_header(contents, &class_start, &body_start, &body_end, &body_depth)) {
		if (!Z_ISUNDEF(tokens_zv)) {
			zval_ptr_dtor(&tokens_zv);
		}

		zend_string_release(contents);

		return;
	}

	tokens = Z_ARRVAL(tokens_zv);
	count = zend_hash_num_elements(tokens);
	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY || !lsp_token_in_bounds(token, body_start, body_end)) {
			continue;
		}

		if (!lsp_token_name_equals(token, "T_FUNCTION") || !lsp_token_at_depth(contents, token, body_depth)) {
			continue;
		}

		name_token = lsp_next_function_name_token_ex(tokens, i + 1, &name_index);
		label = lsp_token_string(name_token, "text");
		if (!label ||
			!lsp_matches_prefix_string(label, prefix) ||
			zend_string_equals_literal(label, "__construct") ||
			lsp_class_declares_method(document->text, current_tokens, current_body_start, current_body_end, current_body_depth, label) ||
			lsp_completion_items_contain_method(items, label)
		) {
			continue;
		}

		lsp_method_modifiers(tokens, i, contents, body_depth, &visibility, &method_static, &method_final);

		if (visibility == LSP_METHOD_VISIBILITY_PRIVATE || method_final || visibility != requested_visibility || method_static != requested_static) {
			continue;
		}

		snippet = lsp_override_method_snippet(contents, name_token, tokens, name_index, body_depth);
		signature = lsp_function_signature_detail(contents, name_token, tokens, name_index, body_depth, NULL);
		lsp_add_override_completion_item(items, document, prefix_start, offset, label, class_name, snippet, signature);

		if (signature) {
			zend_string_release(signature);
		}

		zend_string_release(snippet);
	}

	parent_class = lsp_class_extends_name(contents, class_start, body_start);
	if (parent_class) {
		lsp_add_override_method_completions_for_class(server, items, document, prefix_start, offset, prefix, parent_class, requested_visibility, requested_static, current_tokens, current_body_start, current_body_end, current_body_depth, depth + 1);
		zend_string_release(parent_class);
	}

	zval_ptr_dtor(&tokens_zv);
	zend_string_release(contents);
}

extern bool lsp_add_override_method_completions(lsp_server *server, zval *items, lsp_document *document, HashTable *tokens, size_t offset, zend_string *prefix)
{
	lsp_method_visibility visibility;
	zend_long body_depth = 0;
	zend_string *parent_class;
	size_t class_start = 0, body_start = 0, body_end = 0, prefix_start;
	bool is_static;

	if (!lsp_override_declaration_context(document->text, offset, prefix, &class_start, &body_start, &body_end, &body_depth, &visibility, &is_static)) {
		return false;
	}

	parent_class = lsp_class_extends_name(document->text, class_start, body_start);
	if (!parent_class) {
		return true;
	}

	prefix_start = offset >= ZSTR_LEN(prefix) ? offset - ZSTR_LEN(prefix) : 0;
	lsp_add_override_method_completions_for_class(server, items, document, prefix_start, offset, prefix, parent_class, visibility, is_static, tokens, body_start, body_end, body_depth, 0);
	zend_string_release(parent_class);

	return true;
}

