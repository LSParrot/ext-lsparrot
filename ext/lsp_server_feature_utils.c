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

static inline zend_string *lsp_normalized_signature_slice(const char *start, size_t length)
{
	smart_str signature = {0};
	size_t i;
	bool pending_space = false;
	char c;

	for (i = 0; i < length; i++) {
		c = start[i];
		if (isspace((unsigned char) c)) {
			pending_space = true;
			continue;
		}

		if (pending_space && signature.s && ZSTR_LEN(signature.s) > 0 &&
			c != ')' && c != ',' && c != ':' && c != '|' && c != '&' &&
			ZSTR_VAL(signature.s)[ZSTR_LEN(signature.s) - 1] != '(' &&
			ZSTR_VAL(signature.s)[ZSTR_LEN(signature.s) - 1] != '|'
		) {
			smart_str_appendc(&signature, ' ');
		}

		smart_str_appendc(&signature, c);
		pending_space = false;
	}

	smart_str_0(&signature);

	return signature.s ? signature.s : zend_string_init("", 0, 0);
}

static inline zend_string *lsp_signature_phpdoc_return_type(HashTable *tokens, uint32_t name_index)
{
	zend_long i;
	zend_string *comment;
	zval *token;

	for (i = (zend_long) name_index - 1; i >= 0; i--) {
		token = zend_hash_index_find(tokens, (zend_ulong) i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY) {
			continue;
		}

		if (lsp_token_name_equals(token, "T_DOC_COMMENT")) {
			comment = lsp_token_string(token, "text");

			return comment ? lsp_phpdoc_return_type_from_comment(comment) : NULL;
		}

		if (lsp_token_is_char(token, ';') || lsp_token_is_char(token, '{') || lsp_token_is_char(token, '}')) {
			break;
		}
	}

	return NULL;
}

static inline bool lsp_signature_phpdoc_type_has_detail(zend_string *type)
{
	const char *value;
	size_t i;

	if (!type || ZSTR_LEN(type) == 0) {
		return false;
	}

	value = ZSTR_VAL(type);
	for (i = 0; i < ZSTR_LEN(type); i++) {
		if (
			value[i] == '<' ||
			value[i] == '{' ||
			value[i] == '|' ||
			value[i] == '&' ||
			value[i] == '[' ||
			value[i] == ']'
		) {
			return true;
		}
	}

	return strstr(value, "non-empty") != NULL ||
		strstr(value, "positive-") != NULL ||
		strstr(value, "class-string") != NULL
	;
}

static inline zend_string *lsp_signature_with_phpdoc_return(zend_string *signature, zend_string *phpdoc_return)
{
	const char *value;
	smart_str detail = {0};
	size_t i, close_paren, colon;
	bool found_close_paren, found_colon;

	value = ZSTR_VAL(signature);
	close_paren = 0;
	colon = 0;
	found_close_paren = false;
	found_colon = false;

	for (i = ZSTR_LEN(signature); i > 0; i--) {
		if (value[i - 1] == ')') {
			close_paren = i - 1;
			found_close_paren = true;
			break;
		}
	}

	if (!found_close_paren) {
		return NULL;
	}

	for (i = close_paren + 1; i < ZSTR_LEN(signature); i++) {
		if (isspace((unsigned char) value[i])) {
			continue;
		}

		if (value[i] == ':') {
			colon = i;
			found_colon = true;
		}

		break;
	}

	if (found_colon) {
		smart_str_appendl(&detail, value, colon + 1);
		smart_str_appendc(&detail, ' ');
		smart_str_append(&detail, phpdoc_return);
	} else {
		smart_str_append(&detail, signature);
		smart_str_appendl(&detail, ": ", sizeof(": ") - 1);
		smart_str_append(&detail, phpdoc_return);
	}

	smart_str_0(&detail);

	return detail.s;
}

static inline bool lsp_find_matching_close_paren(zend_string *text, size_t open_offset, size_t *close_offset)
{
	const char *value = ZSTR_VAL(text);
	size_t i, depth;

	if (open_offset >= ZSTR_LEN(text) || value[open_offset] != '(') {
		return false;
	}

	depth = 0;
	for (i = open_offset; i < ZSTR_LEN(text); i++) {
		if (value[i] == '(') {
			depth++;
		} else if (value[i] == ')') {
			if (depth == 0) {
				return false;
			}
			depth--;
			if (depth == 0) {
				*close_offset = i;
				return true;
			}
		}
	}

	return false;
}

static inline bool lsp_find_function_scope_at(HashTable *tokens, zend_string *text, size_t offset, size_t *param_start, size_t *param_end, size_t *body_start, size_t *body_end, zend_long *body_depth)
{
	const char *value = ZSTR_VAL(text);
	zval *token;
	uint32_t i, count;
	size_t token_offset, p, close_paren, open_brace, close_brace, length, best_body_start;
	bool found;

	count = zend_hash_num_elements(tokens);
	length = ZSTR_LEN(text);
	found = false;
	best_body_start = 0;
	*param_start = 0;
	*param_end = 0;
	*body_start = 0;
	*body_end = length;
	*body_depth = 0;

	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY || !lsp_token_name_equals(token, "T_FUNCTION")) {
			continue;
		}

		token_offset = (size_t) lsp_token_long(token, "offset", 0);
		if (token_offset >= offset || token_offset >= length) {
			continue;
		}

		p = token_offset;
		while (p < length && value[p] != '(' && value[p] != '{' && value[p] != ';') {
			p++;
		}

		if (p >= length || value[p] != '(' || !lsp_find_matching_close_paren(text, p, &close_paren)) {
			continue;
		}

		open_brace = close_paren + 1;
		while (open_brace < length && value[open_brace] != '{' && value[open_brace] != ';') {
			open_brace++;
		}

		if (open_brace >= length || value[open_brace] != '{' || offset <= open_brace) {
			continue;
		}

		if (!lsp_find_matching_brace(text, open_brace, &close_brace)) {
			close_brace = length;
		}

		if (offset > close_brace || open_brace + 1 <= best_body_start) {
			continue;
		}

		*param_start = p;
		*param_end = close_paren;
		*body_start = open_brace + 1;
		*body_end = close_brace;
		*body_depth = lsp_brace_depth_at(text, open_brace + 1);
		best_body_start = open_brace + 1;
		found = true;
	}

	return found;
}

static inline bool lsp_parameter_modifier_at(const char *value, size_t start, size_t end, const char *modifier, size_t *next)
{
	size_t length;

	length = strlen(modifier);
	if (start + length > end || strncasecmp(value + start, modifier, length) != 0) {
		return false;
	}

	if (start + length < end && lsp_doc_is_identifier_char(value[start + length])) {
		return false;
	}

	*next = start + length;

	return true;
}

static inline zend_string *lsp_parameter_type_before_variable(zend_string *text, size_t variable_offset, size_t param_start)
{
	const char *modifiers[6] = {"public", "protected", "private", "readonly", "static", "final"}, *value = ZSTR_VAL(text);
	size_t segment_start, type_start, type_end, i, next;
	uint32_t modifier_index;
	bool stripped;

	if (variable_offset > ZSTR_LEN(text) || param_start >= variable_offset) {
		return NULL;
	}

	segment_start = param_start + 1;
	for (i = param_start + 1; i < variable_offset; i++) {
		if (value[i] == ',') {
			segment_start = i + 1;
		}
	}

	type_start = segment_start;
	type_end = variable_offset;
	while (type_start < type_end && isspace((unsigned char) value[type_start])) {
		type_start++;
	}

	while (type_end > type_start && isspace((unsigned char) value[type_end - 1])) {
		type_end--;
	}

	while (type_end > type_start && (value[type_end - 1] == '&' || value[type_end - 1] == '.')) {
		type_end--;
		while (type_end > type_start && isspace((unsigned char) value[type_end - 1])) {
			type_end--;
		}
	}

	do {
		stripped = false;
		for (modifier_index = 0; modifier_index < sizeof(modifiers) / sizeof(modifiers[0]); modifier_index++) {
			if (!lsp_parameter_modifier_at(value, type_start, type_end, modifiers[modifier_index], &next)) {
				continue;
			}

			type_start = next;
			while (type_start < type_end && isspace((unsigned char) value[type_start])) {
				type_start++;
			}

			stripped = true;
			break;
		}
	} while (stripped);

	if (type_end <= type_start) {
		return NULL;
	}

	return zend_string_init(value + type_start, type_end - type_start, 0);
}

static inline zend_string *lsp_property_type_before_variable(zend_string *text, size_t variable_offset)
{
	const char *modifiers[7] = {"public", "protected", "private", "readonly", "static", "final", "var"}, *value = ZSTR_VAL(text);
	size_t declaration_start, type_start, type_end, modifier_index, next, scan;
	bool stripped;

	if (variable_offset > ZSTR_LEN(text)) {
		return NULL;
	}

	declaration_start = variable_offset;
	while (declaration_start > 0 &&
		value[declaration_start - 1] != ';' &&
		value[declaration_start - 1] != '{' &&
		value[declaration_start - 1] != '}'
	) {
		declaration_start--;
	}

	type_start = declaration_start;
	type_end = variable_offset;
	while (type_start < type_end && isspace((unsigned char) value[type_start])) {
		type_start++;
	}

	while (type_end > type_start && isspace((unsigned char) value[type_end - 1])) {
		type_end--;
	}

	for (scan = type_start; scan + 1 < type_end; scan++) {
		if (value[scan] == '*' && value[scan + 1] == '/') {
			type_start = scan + 2;
		}
	}

	while (type_start < type_end && isspace((unsigned char) value[type_start])) {
		type_start++;
	}

	do {
		stripped = false;
		for (modifier_index = 0; modifier_index < sizeof(modifiers) / sizeof(modifiers[0]); modifier_index++) {
			if (!lsp_parameter_modifier_at(value, type_start, type_end, modifiers[modifier_index], &next)) {
				continue;
			}

			type_start = next;
			while (type_start < type_end && isspace((unsigned char) value[type_start])) {
				type_start++;
			}

			stripped = true;
			break;
		}
	} while (stripped);

	if (type_end <= type_start || (type_end - type_start == 1 && value[type_start] == '?')) {
		return NULL;
	}

	return zend_string_init(value + type_start, type_end - type_start, 0);
}

extern zend_string *lsp_parameter_declared_type_for_variable(lsp_document *document, zend_string *variable, size_t offset)
{
	zend_long body_depth;
	zend_string *label;
	zval *tokens_zv, *token;
	HashTable *tokens;
	uint32_t i, count;
	size_t param_start, param_end, body_start, body_end, token_offset;

	if (Z_TYPE(document->lsparrot) != IS_ARRAY) {
		return NULL;
	}

	tokens_zv = zend_hash_str_find(Z_ARRVAL(document->lsparrot), "tokens", sizeof("tokens") - 1);
	if (!tokens_zv || Z_TYPE_P(tokens_zv) != IS_ARRAY) {
		return NULL;
	}

	tokens = Z_ARRVAL_P(tokens_zv);
	if (!lsp_find_function_scope_at(tokens, document->text, offset, &param_start, &param_end, &body_start, &body_end, &body_depth)) {
		return NULL;
	}

	count = zend_hash_num_elements(tokens);
	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY || !lsp_token_name_equals(token, "T_VARIABLE")) {
			continue;
		}

		token_offset = (size_t) lsp_token_long(token, "offset", 0);
		if (token_offset <= param_start || token_offset >= param_end) {
			continue;
		}

		label = lsp_token_string(token, "text");
		if (!label || !zend_string_equals(label, variable)) {
			continue;
		}

		return lsp_parameter_type_before_variable(document->text, token_offset, param_start);
	}

	return NULL;
}

static inline zend_string *lsp_scope_variable_detail(lsp_server *server, lsp_document *document, zend_string *label, size_t offset, zend_string *declared_type, bool parameter)
{
	zend_string *type, *detail;

	type = declared_type ? zend_string_copy(declared_type) : lsp_infer_variable_type(server, document, label, offset);
	if (!type) {
		type = lsp_infer_variable_declared_type(server, document, label, offset);
	}

	if (type && ZSTR_LEN(type) > 0) {
		detail = strpprintf(0, "%s %s: %s", parameter ? "parameter" : "variable", ZSTR_VAL(label), ZSTR_VAL(type));
		zend_string_release(type);

		return detail;
	}

	if (type) {
		zend_string_release(type);
	}

	return strpprintf(0, "%s %s", parameter ? "parameter" : "variable", ZSTR_VAL(label));
}

static inline void lsp_add_scope_this_completion(zval *items, lsp_document *document, size_t offset, zend_string *prefix, size_t prefix_start)
{
	zend_long body_depth;
	zend_string *label, *class_name, *detail;
	size_t class_start, body_start, body_end;

	if (!lsp_matches_prefix_literal("$this", prefix)) {
		return;
	}

	if (!lsp_find_enclosing_class_header(document->text, offset, &class_start, &body_start, &body_end, &body_depth)) {
		return;
	}

	label = zend_string_init("$this", sizeof("$this") - 1, 0);
	class_name = lsp_class_declared_name(document->text, class_start, body_start);
	detail = class_name ? strpprintf(0, "variable $this: %s", ZSTR_VAL(class_name)) : zend_string_init("variable $this", sizeof("variable $this") - 1, 0);
	lsp_add_variable_completion_item_ex(items, label, detail, "lsparrot", document->text, prefix_start, offset);
	zend_string_release(label);
	zend_string_release(detail);

	if (class_name) {
		zend_string_release(class_name);
	}
}

static inline bool lsp_string_equals_ci(zend_string *left, zend_string *right)
{
	return ZSTR_LEN(left) == ZSTR_LEN(right) && strncasecmp(ZSTR_VAL(left), ZSTR_VAL(right), ZSTR_LEN(left)) == 0;
}

extern bool lsp_find_class_header_from(zend_string *text, size_t search_start, size_t *class_start, size_t *body_start, size_t *body_end, zend_long *body_depth)
{
	const char *keywords[4] = {"class", "interface", "trait", "enum"}, *value = ZSTR_VAL(text);
	size_t i, p, close_offset, keyword_index, keyword_length;

	for (i = search_start; i < ZSTR_LEN(text); i++) {
		for (keyword_index = 0; keyword_index < sizeof(keywords) / sizeof(keywords[0]); keyword_index++) {
			keyword_length = strlen(keywords[keyword_index]);
			if (i + keyword_length >= ZSTR_LEN(text) || strncasecmp(value + i, keywords[keyword_index], keyword_length) != 0) {
				continue;
			}

			if (i > 0 && (lsp_doc_is_identifier_char(value[i - 1]) || value[i - 1] == '$')) {
				continue;
			}

			if (!lsp_text_is_word_boundary(text, i + keyword_length)) {
				continue;
			}

			p = i + keyword_length;
			while (p < ZSTR_LEN(text) && value[p] != '{') {
				p++;
			}

			if (p >= ZSTR_LEN(text) || !lsp_find_matching_brace(text, p, &close_offset)) {
				return false;
			}

			*class_start = i;
			*body_start = p + 1;
			*body_end = close_offset;
			*body_depth = lsp_brace_depth_at(text, p + 1);

			return true;
		}
	}

	return false;
}

extern size_t lsp_method_signature_end(HashTable *tokens, uint32_t name_index, zend_string *text, zend_long body_depth)
{
	zval *token;
	uint32_t i, count = zend_hash_num_elements(tokens);
	size_t fallback = (size_t) lsp_token_long(zend_hash_index_find(tokens, name_index), "offset", 0);

	for (i = name_index; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY) {
			continue;
		}

		if (!lsp_token_at_depth(text, token, body_depth)) {
			continue;
		}

		if (lsp_token_is_char(token, '{') || lsp_token_is_char(token, ';')) {
			return (size_t) lsp_token_long(token, "offset", (zend_long) fallback);
		}

		fallback = (size_t) lsp_token_long(token, "offset", (zend_long) fallback) + (size_t) lsp_token_long(token, "length", 0);
	}

	return fallback;
}

extern zend_string *lsp_function_signature_detail(zend_string *text, zval *name_token, HashTable *tokens, uint32_t name_index, zend_long body_depth, const char *prefix)
{
	const char *value = ZSTR_VAL(text);
	zend_string *signature, *detail, *phpdoc_return, *phpdoc_signature;
	size_t start, end;

	if (!name_token || Z_TYPE_P(name_token) != IS_ARRAY) {
		return NULL;
	}

	start = (size_t) lsp_token_long(name_token, "offset", 0);
	end = lsp_method_signature_end(tokens, name_index, text, body_depth);

	if (start >= ZSTR_LEN(text) || end <= start) {
		return NULL;
	}

	if (end > ZSTR_LEN(text)) {
		end = ZSTR_LEN(text);
	}

	while (end > start && isspace((unsigned char) value[end - 1])) {
		end--;
	}

	signature = lsp_normalized_signature_slice(value + start, end - start);
	if (!signature || ZSTR_LEN(signature) == 0) {
		if (signature) {
			zend_string_release(signature);
		}

		return NULL;
	}

	phpdoc_return = lsp_signature_phpdoc_return_type(tokens, name_index);
	if (lsp_signature_phpdoc_type_has_detail(phpdoc_return)) {
		phpdoc_signature = lsp_signature_with_phpdoc_return(signature, phpdoc_return);
		if (phpdoc_signature && ZSTR_LEN(phpdoc_signature) > 0) {
			zend_string_release(signature);
			signature = phpdoc_signature;
		} else if (phpdoc_signature) {
			zend_string_release(phpdoc_signature);
		}
	}

	if (phpdoc_return) {
		zend_string_release(phpdoc_return);
	}

	if (!prefix) {
		return signature;
	}

	detail = strpprintf(0, "%s %s", prefix, ZSTR_VAL(signature));
	zend_string_release(signature);

	return detail;
}

extern bool lsp_find_enclosing_class_header(zend_string *text, size_t offset, size_t *class_start, size_t *body_start, size_t *body_end, zend_long *body_depth)
{
	const char *value = ZSTR_VAL(text);
	size_t i, length, p, close_offset;
	bool found = false;

	length = offset > ZSTR_LEN(text) ? ZSTR_LEN(text) : offset;

	for (i = 0; i + sizeof("class") - 1 < length; i++) {
		if (memcmp(value + i, "class", sizeof("class") - 1) != 0) {
			continue;
		}

		if (i > 0 && (lsp_doc_is_identifier_char(value[i - 1]) || value[i - 1] == '$')) {
			continue;
		}

		if (!lsp_text_is_word_boundary(text, i + sizeof("class") - 1)) {
			continue;
		}

		p = i + sizeof("class") - 1;
		while (p < ZSTR_LEN(text) && value[p] != '{') {
			p++;
		}

		if (p >= ZSTR_LEN(text) || p >= offset) {
			continue;
		}

		if (!lsp_find_matching_brace(text, p, &close_offset) || offset > close_offset) {
			continue;
		}

		*class_start = i;
		*body_start = p + 1;
		*body_end = close_offset;
		*body_depth = lsp_brace_depth_at(text, p + 1);
		found = true;
	}

	return found;
}

extern bool lsp_find_first_class_header(zend_string *text, size_t *class_start, size_t *body_start, size_t *body_end, zend_long *body_depth)
{
	return lsp_find_class_header_from(text, 0, class_start, body_start, body_end, body_depth);
}

extern bool lsp_word_at_slice_equals(const char *start, const char *end, const char *word)
{
	size_t length = strlen(word);

	return (size_t) (end - start) == length && strncasecmp(start, word, length) == 0;
}

extern zend_string *lsp_class_extends_name(zend_string *text, size_t class_start, size_t body_start)
{
	const char *value = ZSTR_VAL(text), *name_start, *name_end;
	zend_string *raw, *resolved;
	size_t p, header_end, keyword_end;

	header_end = body_start > 0 ? body_start - 1 : body_start;

	for (p = class_start; p < header_end; p++) {
		if (!lsp_keyword_at_slice(value, p, header_end, "extends", &keyword_end)) {
			continue;
		}

		p = keyword_end;
		while (p < header_end && isspace((unsigned char) value[p])) {
			p++;
		}

		name_start = value + p;
		while (p < header_end && (lsp_doc_is_identifier_char(value[p]) || value[p] == '\\')) {
			p++;
		}

		name_end = value + p;
		if (name_end <= name_start) {
			return NULL;
		}

		raw = zend_string_init(name_start, name_end - name_start, 0);
		resolved = lsp_resolve_class_name(text, raw);
		zend_string_release(raw);

		return resolved;
	}

	return NULL;
}

extern zend_string *lsp_class_declared_name(zend_string *text, size_t class_start, size_t body_start)
{
	const char *keywords[4] = {"class", "interface", "trait", "enum"}, *value = ZSTR_VAL(text), *name_start, *name_end;
	zend_string *raw, *resolved;
	size_t p, header_end, keyword_end, i;

	header_end = body_start > 0 ? body_start - 1 : body_start;
	for (p = class_start; p < header_end; p++) {
		for (i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
			if (!lsp_keyword_at_slice(value, p, header_end, keywords[i], &keyword_end)) {
				continue;
			}

			p = keyword_end;
			while (p < header_end && isspace((unsigned char) value[p])) {
				p++;
			}

			name_start = value + p;
			while (p < header_end && (lsp_doc_is_identifier_char(value[p]) || value[p] == '\\')) {
				p++;
			}

			name_end = value + p;
			if (name_end <= name_start) {
				return NULL;
			}

			raw = zend_string_init(name_start, name_end - name_start, 0);
			resolved = lsp_resolve_class_name(text, raw);
			zend_string_release(raw);

			return resolved;
		}
	}

	return NULL;
}

extern zend_string *lsp_property_completion_detail(zend_string *text, zval *variable_token, bool is_static)
{
	zend_string *variable, *type, *detail;
	size_t variable_offset;

	variable = lsp_token_string(variable_token, "text");
	if (!variable) {
		return NULL;
	}

	variable_offset = (size_t) lsp_token_long(variable_token, "offset", 0);
	type = lsp_property_type_before_variable(text, variable_offset);
	if (type && ZSTR_LEN(type) > 0) {
		detail = is_static
			? strpprintf(0, "static property %s %s", ZSTR_VAL(type), ZSTR_VAL(variable))
			: strpprintf(0, "property %s %s", ZSTR_VAL(type), ZSTR_VAL(variable))
		;
		zend_string_release(type);

		return detail;
	}

	if (type) {
		zend_string_release(type);
	}

	return is_static
		? strpprintf(0, "static property %s", ZSTR_VAL(variable))
		: strpprintf(0, "property %s", ZSTR_VAL(variable))
	;
}

extern zend_string *lsp_promoted_property_completion_detail(zend_string *text, zval *variable_token, size_t param_start)
{
	zend_string *variable, *type, *detail;
	size_t variable_offset;

	variable = lsp_token_string(variable_token, "text");
	if (!variable) {
		return NULL;
	}

	variable_offset = (size_t) lsp_token_long(variable_token, "offset", 0);
	type = lsp_parameter_type_before_variable(text, variable_offset, param_start);
	if (type && ZSTR_LEN(type) > 0) {
		detail = strpprintf(0, "property %s %s", ZSTR_VAL(type), ZSTR_VAL(variable));
		zend_string_release(type);

		return detail;
	}

	if (type) {
		zend_string_release(type);
	}

	return strpprintf(0, "property %s", ZSTR_VAL(variable));
}

extern void lsp_add_scope_variable_completions(lsp_server *server, zval *items, lsp_document *document, HashTable *tokens, size_t offset, zend_string *prefix, size_t prefix_start)
{
	zend_long body_depth;
	zend_string *label, *detail, *declared_type;
	zval *token;
	uint32_t i, count;
	size_t param_start, param_end, body_start, body_end, token_offset;
	bool has_scope;

	has_scope = lsp_find_function_scope_at(tokens, document->text, offset, &param_start, &param_end, &body_start, &body_end, &body_depth);
	if (!has_scope) {
		param_start = 0;
		param_end = 0;
		body_start = 0;
		body_end = ZSTR_LEN(document->text);
		body_depth = 0;
	}

	lsp_add_scope_this_completion(items, document, offset, prefix, prefix_start);

	count = zend_hash_num_elements(tokens);
	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY || !lsp_token_name_equals(token, "T_VARIABLE")) {
			continue;
		}

		token_offset = (size_t) lsp_token_long(token, "offset", 0);
		if (token_offset >= prefix_start) {
			continue;
		}

		if (has_scope && ((token_offset < param_start || token_offset > param_end) && (token_offset < body_start || token_offset > body_end))) {
			continue;
		}

		if (!has_scope && token_offset > body_end) {
			continue;
		}

		label = lsp_token_string(token, "text");
		if (!label || !lsp_matches_prefix_string(label, prefix)) {
			continue;
		}

		declared_type = NULL;
		if (has_scope && token_offset > param_start && token_offset < param_end) {
			declared_type = lsp_parameter_type_before_variable(document->text, token_offset, param_start);
		}

		detail = lsp_scope_variable_detail(server, document, label, prefix_start, declared_type, declared_type != NULL);
		lsp_add_variable_completion_item_ex(items, label, detail, "lsparrot", document->text, prefix_start, offset);
		zend_string_release(detail);

		if (declared_type) {
			zend_string_release(declared_type);
		}
	}
}

extern bool lsp_find_class_header_for_name(zend_string *text, zend_string *class_name, size_t *class_start, size_t *body_start, size_t *body_end, zend_long *body_depth)
{
	zend_long found_body_depth;
	zend_string *declared;
	size_t search_start, found_class_start, found_body_start, found_body_end;

	search_start = 0;
	while (lsp_find_class_header_from(text, search_start, &found_class_start, &found_body_start, &found_body_end, &found_body_depth)) {
		declared = lsp_class_declared_name(text, found_class_start, found_body_start);
		if (declared && lsp_string_equals_ci(declared, class_name)) {
			zend_string_release(declared);
			*class_start = found_class_start;
			*body_start = found_body_start;
			*body_end = found_body_end;
			*body_depth = found_body_depth;

			return true;
		}

		if (declared) {
			zend_string_release(declared);
		}

		search_start = found_body_end + 1;
	}

	return false;
}

extern lsp_document *lsp_document_for_path(lsp_server *server, zend_string *path)
{
	lsp_document *document = NULL, *candidate;
	zval *value;

	ZEND_HASH_FOREACH_VAL(&server->documents, value) {
		if (Z_TYPE_P(value) != IS_PTR) {
			continue;
		}

		candidate = (lsp_document *) Z_PTR_P(value);
		if (candidate && candidate->path && zend_string_equals(candidate->path, path)) {
			document = candidate;
			break;
		}
	} ZEND_HASH_FOREACH_END();

	return document;
}
