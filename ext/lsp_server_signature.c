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

static inline bool lsp_signature_call_context(zend_string *text, size_t offset, size_t *paren, zend_string **name, size_t *active_parameter)
{
	const char *value = ZSTR_VAL(text);
	size_t i, name_start, name_end, depth = 0;
	char c;

	*paren = (size_t) -1;
	*name = NULL;
	*active_parameter = 0;

	if (offset == 0) {
		return false;
	}

	for (i = offset; i > 0; i--) {
		c = value[i - 1];
		if (c == ')') {
			depth++;
		} else if (c == '(') {
			if (depth == 0) {
				*paren = i - 1;
				break;
			}
			depth--;
		}
	}

	if (*paren == (size_t) -1 || *paren == 0) {
		return false;
	}

	for (i = *paren + 1, depth = 0; i < offset; i++) {
		if (value[i] == '(' || value[i] == '[' || value[i] == '{') {
			depth++;
		} else if ((value[i] == ')' || value[i] == ']' || value[i] == '}') && depth > 0) {
			depth--;
		} else if (value[i] == ',' && depth == 0) {
			(*active_parameter)++;
		}
	}

	name_end = *paren;
	while (name_end > 0 && isspace((unsigned char) value[name_end - 1])) {
		name_end--;
	}

	name_start = name_end;
	while (name_start > 0 && (isalnum((unsigned char) value[name_start - 1]) || value[name_start - 1] == '_' || value[name_start - 1] == '\\')) {
		name_start--;
	}

	if (name_start == name_end) {
		return false;
	}

	*name = zend_string_init(value + name_start, name_end - name_start, 0);

	return true;
}

static inline zend_string *lsp_cached_class_method_signature(zval *entry, zend_string *method_name)
{
	zend_string *label, *detail;
	zval *methods, *method;

	methods = zend_hash_str_find(Z_ARRVAL_P(entry), "methods", sizeof("methods") - 1);
	if (!methods || Z_TYPE_P(methods) != IS_ARRAY) {
		return NULL;
	}

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(methods), method) {
		if (Z_TYPE_P(method) != IS_ARRAY) {
			continue;
		}

		label = lsp_array_string(method, "label");
		detail = lsp_array_string(method, "detail");
		if (label && detail && zend_string_equals(label, method_name)) {
			return zend_string_copy(detail);
		}
	} ZEND_HASH_FOREACH_END();

	return NULL;
}

static inline zend_string *lsp_project_class_method_signature(lsp_server *server, zend_string *class_name, zend_string *method_name)
{
	zend_string *current, *next, *detail = NULL;
	zval *entry, *parent;
	HashTable visited;

	zend_hash_init(&visited, 8, NULL, NULL, 0);
	current = zend_string_copy(class_name);
	while (current) {
		if (zend_hash_exists(&visited, current)) {
			break;
		}

		zend_hash_add_empty_element(&visited, current);
		entry = lsp_class_member_cache_entry(server, current);
		if (!entry || Z_TYPE_P(entry) != IS_ARRAY) {
			break;
		}

		detail = lsp_cached_class_method_signature(entry, method_name);
		if (detail) {
			break;
		}

		parent = zend_hash_str_find(Z_ARRVAL_P(entry), "parent", sizeof("parent") - 1);
		next = parent && Z_TYPE_P(parent) == IS_STRING && Z_STRLEN_P(parent) > 0 ? zend_string_copy(Z_STR_P(parent)) : NULL;
		zend_string_release(current);
		current = next;
	}

	if (current) {
		zend_string_release(current);
	}

	zend_hash_destroy(&visited);

	return detail;
}

static inline zend_string *lsp_document_class_method_signature(lsp_document *document, size_t offset, zend_string *method_name)
{
	zend_long body_depth = 0;
	zend_string *label, *detail;
	zval *tokens_zv, *token, *name_token;
	HashTable *tokens;
	uint32_t i, count, name_index;
	size_t class_start = 0, body_start = 0, body_end = 0;

	if (!lsp_find_enclosing_class_header(document->text, offset, &class_start, &body_start, &body_end, &body_depth)) {
		return NULL;
	}

	tokens_zv = zend_hash_str_find(Z_ARRVAL(document->lsparrot), "tokens", sizeof("tokens") - 1);
	if (!tokens_zv || Z_TYPE_P(tokens_zv) != IS_ARRAY) {
		return NULL;
	}

	tokens = Z_ARRVAL_P(tokens_zv);
	count = zend_hash_num_elements(tokens);
	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY || !lsp_token_in_bounds(token, body_start, body_end)) {
			continue;
		}

		if (!lsp_token_name_equals(token, "T_FUNCTION") || !lsp_token_at_depth(document->text, token, body_depth)) {
			continue;
		}

		name_token = lsp_next_function_name_token_ex(tokens, i + 1, &name_index);
		label = lsp_token_string(name_token, "text");
		if (!label || !zend_string_equals(label, method_name)) {
			continue;
		}

		detail = lsp_function_signature_detail(document->text, name_token, tokens, name_index, body_depth, NULL);
		if (detail) {
			return detail;
		}

		return strpprintf(0, "%s(...)", ZSTR_VAL(method_name));
	}

	return NULL;
}

static inline zend_string *lsp_document_parent_class_at(lsp_document *document, size_t offset)
{
	zend_long body_depth = 0;
	size_t class_start = 0, body_start = 0, body_end = 0;

	if (!lsp_find_enclosing_class_header(document->text, offset, &class_start, &body_start, &body_end, &body_depth)) {
		return NULL;
	}

	return lsp_class_extends_name(document->text, class_start, body_start);
}

static inline zend_string *lsp_member_call_signature(lsp_server *server, lsp_document *document, size_t name_end, zend_string *method_name)
{
	zend_string *receiver, *member_prefix, *class_name, *parent_class, *detail;

	detail = NULL;
	if (lsp_member_access_context(document->text, name_end, method_name, &receiver, &member_prefix)) {
		if (zend_string_equals_literal(receiver, "$this")) {
			detail = lsp_document_class_method_signature(document, name_end, method_name);
			if (!detail) {
				parent_class = lsp_document_parent_class_at(document, name_end);
				if (parent_class) {
					detail = lsp_project_class_method_signature(server, parent_class, method_name);
					zend_string_release(parent_class);
				}
			}

			zend_string_release(receiver);
			zend_string_release(member_prefix);

			return detail;
		}

		zend_string_release(receiver);
		zend_string_release(member_prefix);
	}

	if (lsp_member_access_class_context(server, document, name_end, method_name, &class_name, &member_prefix)) {
		detail = lsp_project_class_method_signature(server, class_name, method_name);

		zend_string_release(class_name);
		zend_string_release(member_prefix);
	}

	return detail;
}

static inline bool lsp_signature_function_declaration_context(zend_string *text, size_t paren, zend_string *method_name)
{
	const char *value = ZSTR_VAL(text);
	size_t name_end, name_start, keyword_start, p, keyword_length = sizeof("function") - 1;

	name_end = paren;
	while (name_end > 0 && isspace((unsigned char) value[name_end - 1])) {
		name_end--;
	}

	if (name_end < ZSTR_LEN(method_name)) {
		return false;
	}

	name_start = name_end - ZSTR_LEN(method_name);
	if (strncasecmp(value + name_start, ZSTR_VAL(method_name), ZSTR_LEN(method_name)) != 0) {
		return false;
	}

	if (name_start > 0 && lsp_doc_is_identifier_char(value[name_start - 1])) {
		return false;
	}

	p = name_start;
	while (p > 0 && isspace((unsigned char) value[p - 1])) {
		p--;
	}

	if (p > 0 && value[p - 1] == '&') {
		p--;
		while (p > 0 && isspace((unsigned char) value[p - 1])) {
			p--;
		}
	}

	if (p < keyword_length) {
		return false;
	}

	keyword_start = p - keyword_length;
	if (strncasecmp(value + keyword_start, "function", keyword_length) != 0) {
		return false;
	}

	if (keyword_start > 0 && lsp_doc_is_identifier_char(value[keyword_start - 1])) {
		return false;
	}

	return true;
}

static inline zend_string *lsp_override_declaration_signature(lsp_server *server, lsp_document *document, size_t paren, zend_string *method_name)
{
	zend_string *parent_class, *detail;

	if (!lsp_signature_function_declaration_context(document->text, paren, method_name)) {
		return NULL;
	}

	parent_class = lsp_document_parent_class_at(document, paren);
	if (!parent_class) {
		return NULL;
	}

	detail = lsp_project_class_method_signature(server, parent_class, method_name);
	zend_string_release(parent_class);

	return detail;
}

static inline zend_string *lsp_document_function_signature(lsp_document *document, zend_string *function_name)
{
	zend_long body_depth;
	zend_string *label, *detail;
	zval *tokens_zv, *token, *name_token;
	HashTable *tokens;
	uint32_t i, count, name_index;

	tokens_zv = zend_hash_str_find(Z_ARRVAL(document->lsparrot), "tokens", sizeof("tokens") - 1);
	if (!tokens_zv || Z_TYPE_P(tokens_zv) != IS_ARRAY) {
		return NULL;
	}

	tokens = Z_ARRVAL_P(tokens_zv);
	count = zend_hash_num_elements(tokens);
	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY || !lsp_token_name_equals(token, "T_FUNCTION")) {
			continue;
		}

		name_token = lsp_next_function_name_token_ex(tokens, i + 1, &name_index);
		label = lsp_token_string(name_token, "text");
		if (!label || !zend_string_equals(label, function_name)) {
			continue;
		}

		body_depth = lsp_brace_depth_at(document->text, (size_t) lsp_token_long(token, "offset", 0));
		detail = lsp_function_signature_detail(document->text, name_token, tokens, name_index, body_depth, "function");
		if (detail) {
			return detail;
		}

		return strpprintf(0, "%s(...)", ZSTR_VAL(function_name));
	}

	return NULL;
}

static inline void lsp_signature_add_parameters(zval *signature, zend_string *label)
{
	const char *value = ZSTR_VAL(label);
	zval parameters, parameter;
	size_t length = ZSTR_LEN(label), open = (size_t) -1, close = (size_t) -1, i, start, end, paren_depth = 0, bracket_depth = 0, brace_depth = 0, angle_depth = 0;
	bool has_parameter = false;
	char c;

	for (i = 0; i < length; i++) {
		if (value[i] == '(') {
			open = i;
			break;
		}
	}

	if (open == (size_t) -1) {
		return;
	}

	for (i = open + 1; i < length; i++) {
		c = value[i];
		if (c == '(') {
			paren_depth++;
		} else if (c == ')' && paren_depth == 0) {
			close = i;
			break;
		} else if (c == ')' && paren_depth > 0) {
			paren_depth--;
		}
	}

	if (close == (size_t) -1 || close <= open + 1) {
		return;
	}

	array_init(&parameters);
	start = open + 1;
	for (i = start; i <= close; i++) {
		c = i < close ? value[i] : ',';
		if ((c == ',' && paren_depth == 0 && bracket_depth == 0 && brace_depth == 0 && angle_depth == 0) || i == close) {
			end = i;

			while (start < end && isspace((unsigned char) value[start])) {
				start++;
			}

			while (end > start && isspace((unsigned char) value[end - 1])) {
				end--;
			}

			if (end > start) {
				array_init(&parameter);
				add_assoc_stringl(&parameter, "label", value + start, end - start);
				add_next_index_zval(&parameters, &parameter);
				has_parameter = true;
			}
			start = i + 1;

			continue;
		}

		if (c == '(') {
			paren_depth++;
		} else if (c == ')' && paren_depth > 0) {
			paren_depth--;
		} else if (c == '[') {
			bracket_depth++;
		} else if (c == ']' && bracket_depth > 0) {
			bracket_depth--;
		} else if (c == '{') {
			brace_depth++;
		} else if (c == '}' && brace_depth > 0) {
			brace_depth--;
		} else if (c == '<') {
			angle_depth++;
		} else if (c == '>' && angle_depth > 0) {
			angle_depth--;
		}
	}

	if (has_parameter) {
		add_assoc_zval(signature, "parameters", &parameters);
	} else {
		zval_ptr_dtor(&parameters);
	}
}

static inline void lsp_signature_result(zval *return_value, zend_string *label, size_t active_parameter)
{
	zval signatures, signature;

	array_init(return_value);
	array_init(&signatures);
	array_init(&signature);
	add_assoc_str(&signature, "label", label);
	add_assoc_string(&signature, "documentation", "lsparrot signature context");
	lsp_signature_add_parameters(&signature, label);
	add_next_index_zval(&signatures, &signature);
	add_assoc_zval(return_value, "signatures", &signatures);
	add_assoc_long(return_value, "activeSignature", 0);
	add_assoc_long(return_value, "activeParameter", (zend_long) active_parameter);
}

extern void lsp_lsparrot_signature_help(lsp_server *server, zval *return_value, lsp_document *document, zval *position)
{
	zend_long line, character;
	zend_string *name, *label;
	size_t offset, paren = (size_t) -1, active_parameter = 0;

	lsp_position_from_zval(position, &line, &character);
	offset = lsp_offset_at(document->text, line, character);
	if (!lsp_signature_call_context(document->text, offset, &paren, &name, &active_parameter)) {
		ZVAL_NULL(return_value);

		return;
	}

	label = lsp_override_declaration_signature(server, document, paren, name);
	if (!label) {
		label = lsp_member_call_signature(server, document, paren, name);
	}

	if (!label) {
		label = lsp_document_function_signature(document, name);
	}

	if (!label) {
		label = strpprintf(0, "%s(...)", ZSTR_VAL(name));
	}

	lsp_signature_result(return_value, label, active_parameter);

	zend_string_release(name);
}
