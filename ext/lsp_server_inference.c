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

extern bool lsp_text_is_word_boundary(zend_string *text, size_t offset)
{
	const char *value = ZSTR_VAL(text);

	return offset >= ZSTR_LEN(text) || !lsp_doc_is_identifier_char(value[offset]);
}

extern zend_long lsp_brace_depth_at(zend_string *text, size_t offset)
{
	const char *value = ZSTR_VAL(text);
	zend_long depth = 0;
	size_t i, length = offset > ZSTR_LEN(text) ? ZSTR_LEN(text) : offset;

	for (i = 0; i < length; i++) {
		if (value[i] == '{') {
			depth++;
		} else if (value[i] == '}' && depth > 0) {
			depth--;
		}
	}

	return depth;
}

extern bool lsp_find_matching_brace(zend_string *text, size_t open_offset, size_t *close_offset)
{
	const char *value = ZSTR_VAL(text);
	zend_long depth = 0;
	size_t i;

	for (i = open_offset; i < ZSTR_LEN(text); i++) {
		if (value[i] == '{') {
			depth++;
		} else if (value[i] == '}') {
			depth--;
			if (depth == 0) {
				*close_offset = i;

				return true;
			}
		}
	}

	*close_offset = ZSTR_LEN(text);

	return true;
}

extern bool lsp_find_enclosing_class_bounds(zend_string *text, size_t offset, size_t *body_start, size_t *body_end, zend_long *body_depth)
{
	const char *value = ZSTR_VAL(text);
	size_t i, length = offset > ZSTR_LEN(text) ? ZSTR_LEN(text) : offset, p, close_offset;
	bool found = false;

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

		*body_start = p + 1;
		*body_end = close_offset;
		*body_depth = lsp_brace_depth_at(text, p + 1);

		found = true;
	}

	return found;
}

extern bool lsp_token_is_char(zval *token, char value)
{
	zend_string *text = lsp_token_string(token, "text");

	return lsp_token_name_equals(token, "CHAR") && text && ZSTR_LEN(text) == 1 && ZSTR_VAL(text)[0] == value;
}

static inline bool lsp_token_is_visibility(zval *token)
{
	return lsp_token_name_equals(token, "T_PUBLIC") ||
		lsp_token_name_equals(token, "T_PROTECTED") ||
		lsp_token_name_equals(token, "T_PRIVATE")
	;
}

extern bool lsp_token_in_bounds(zval *token, size_t start, size_t end)
{
	size_t offset = (size_t) lsp_token_long(token, "offset", 0);

	return offset >= start && offset < end;
}

extern bool lsp_token_at_depth(zend_string *text, zval *token, zend_long depth)
{
	return lsp_brace_depth_at(text, (size_t) lsp_token_long(token, "offset", 0)) == depth;
}

extern bool lsp_token_is_property_declaration(HashTable *tokens, uint32_t index, zend_string *text, zend_long body_depth)
{
	zend_long i;
	zval *token;
	bool saw_visibility = false, saw_open_paren = false;

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

		if (lsp_token_is_char(token, '(')) {
			saw_open_paren = true;
			break;
		}

		if (lsp_token_is_visibility(token)) {
			saw_visibility = true;
		}
	}

	return saw_visibility && !saw_open_paren;
}

static inline zend_string *lsp_method_declared_return_type(zend_string *text, zval *function_token)
{
	const char *value = ZSTR_VAL(text), *end = value + ZSTR_LEN(text), *p, *start;
	zend_long depth = 0;
	size_t offset = (size_t) lsp_token_long(function_token, "offset", 0);

	if (offset >= ZSTR_LEN(text)) {
		return NULL;
	}

	p = value + offset;
	while (p < end && *p != '(') {
		p++;
	}

	if (p >= end) {
		return NULL;
	}

	for (; p < end; p++) {
		if (*p == '(') {
			depth++;
		} else if (*p == ')' && --depth == 0) {
			p++;
			break;
		}
	}

	while (p < end && isspace((unsigned char) *p)) {
		p++;
	}

	if (p >= end || *p != ':') {
		return NULL;
	}

	p++;
	while (p < end && isspace((unsigned char) *p)) {
		p++;
	}

	start = p;
	while (p < end && (lsp_doc_is_identifier_char(*p) || *p == '\\' || *p == '?' || *p == '|' || *p == '&')) {
		p++;
	}

	if (p == start) {
		return NULL;
	}

	return zend_string_init(start, p - start, 0);
}

static inline bool lsp_type_segment_equals_name(const char *start, const char *end, zend_string *name)
{
	while (start < end && (*start == '\\' || *start == '?')) {
		start++;
	}

	while (end > start && isspace((unsigned char) end[-1])) {
		end--;
	}

	return (size_t) (end - start) == ZSTR_LEN(name) && strncasecmp(start, ZSTR_VAL(name), ZSTR_LEN(name)) == 0;
}

static inline bool lsp_type_contains_name(zend_string *type, zend_string *name)
{
	const char *value = ZSTR_VAL(type), *end = value + ZSTR_LEN(type), *segment_start, *p;

	segment_start = value;
	for (p = value; p < end; p++) {
		if (lsp_doc_is_identifier_char(*p) || *p == '\\' || *p == '?' || *p == '-') {
			continue;
		}

		if (lsp_type_segment_equals_name(segment_start, p, name)) {
			return true;
		}

		segment_start = p + 1;
	}

	return lsp_type_segment_equals_name(segment_start, end, name);
}

static inline const char *lsp_next_template_tag(const char *cursor, const char *end, size_t *tag_length)
{
	const char *tags[] = {"@template", "@phpstan-template", "@psalm-template"};
	const char *match, *best;
	size_t i, length;

	best = NULL;
	*tag_length = 0;
	for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
		match = strstr(cursor, tags[i]);
		if (!match || match >= end) {
			continue;
		}

		if (!best || match < best) {
			best = match;
			length = strlen(tags[i]);
			*tag_length = length;
		}
	}

	return best;
}

static inline bool lsp_method_phpdoc_type_uses_template(zend_string *comment, zend_string *type)
{
	const char *cursor, *end, *p, *start;
	zend_string *template_name;
	size_t tag_length;
	bool found;

	cursor = ZSTR_VAL(comment);
	end = cursor + ZSTR_LEN(comment);
	while ((cursor = lsp_next_template_tag(cursor, end, &tag_length)) != NULL && cursor < end) {
		p = cursor + tag_length;
		if (p < end && *p == '-') {
			p++;
			while (p < end && (islower((unsigned char) *p) || *p == '-')) {
				p++;
			}
		}

		while (p < end && isspace((unsigned char) *p)) {
			p++;
		}

		if (p >= end || !lsp_doc_is_identifier_start(*p)) {
			cursor = p;
			continue;
		}

		start = p++;
		while (p < end && lsp_doc_is_identifier_char(*p)) {
			p++;
		}

		template_name = zend_string_init(start, p - start, 0);
		found = lsp_type_contains_name(type, template_name);
		zend_string_release(template_name);
		if (found) {
			return true;
		}

		cursor = p;
	}

	return false;
}

static inline zend_string *lsp_method_phpdoc_return_type(HashTable *tokens, uint32_t function_index)
{
	zend_long i;
	zend_string *comment, *type;
	zval *token;

	for (i = (zend_long) function_index - 1; i >= 0; i--) {
		token = zend_hash_index_find(tokens, (zend_ulong) i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY) {
			continue;
		}

		if (lsp_token_name_equals(token, "T_DOC_COMMENT")) {
			comment = lsp_token_string(token, "text");
			type = comment ? lsp_phpdoc_return_type_from_comment(comment) : NULL;
			if (comment && type && lsp_method_phpdoc_type_uses_template(comment, type)) {
				zend_string_release(type);

				return zend_string_init("mixed", sizeof("mixed") - 1, 0);
			}

			return type;
		}

		if (lsp_token_is_char(token, ';') || lsp_token_is_char(token, '{') || lsp_token_is_char(token, '}')) {
			break;
		}
	}

	return NULL;
}

static inline zend_string *lsp_method_phpdoc_return_type_raw(HashTable *tokens, uint32_t function_index)
{
	zend_long i;
	zend_string *comment, *type;
	zval *token;

	for (i = (zend_long) function_index - 1; i >= 0; i--) {
		token = zend_hash_index_find(tokens, (zend_ulong) i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY) {
			continue;
		}

		if (lsp_token_name_equals(token, "T_DOC_COMMENT")) {
			comment = lsp_token_string(token, "text");
			type = comment ? lsp_phpdoc_return_type_from_comment_raw(comment) : NULL;

			return type;
		}

		if (lsp_token_is_char(token, ';') || lsp_token_is_char(token, '{') || lsp_token_is_char(token, '}')) {
			break;
		}
	}

	return NULL;
}

static inline zend_string *lsp_find_method_return_type(lsp_document *document, zend_string *method_name)
{
	zend_string *label, *type;
	zval *tokens_zv, *token;
	HashTable *tokens;
	uint32_t i, count;

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

		label = lsp_next_string_token(tokens, i + 1);
		if (!label || !zend_string_equals(label, method_name)) {
			continue;
		}

		type = lsp_method_phpdoc_return_type(tokens, i);
		if (type) {
			return type;
		}

		return lsp_method_declared_return_type(document->text, token);
	}

	return NULL;
}

static inline zend_string *lsp_find_method_phpdoc_return_type(lsp_document *document, zend_string *method_name)
{
	zend_string *label, *type;
	zval *tokens_zv, *token;
	HashTable *tokens;
	uint32_t i, count;

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

		label = lsp_next_string_token(tokens, i + 1);
		if (!label || !zend_string_equals(label, method_name)) {
			continue;
		}

		type = lsp_method_phpdoc_return_type_raw(tokens, i);
		if (type) {
			return type;
		}

		return NULL;
	}

	return NULL;
}

static inline zend_string *lsp_find_method_declared_return_type(lsp_document *document, zend_string *method_name)
{
	zend_string *label;
	zval *tokens_zv, *token;
	HashTable *tokens;
	uint32_t i, count;

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

		label = lsp_next_string_token(tokens, i + 1);
		if (!label || !zend_string_equals(label, method_name)) {
			continue;
		}

		return lsp_method_declared_return_type(document->text, token);
	}

	return NULL;
}

static inline zend_string *lsp_find_method_return_type_in_text(zend_string *text, zend_string *method_name)
{
	zend_string *label, *type;
	zval tokens_zv, *token;
	HashTable *tokens;
	uint32_t i, count;

	ZVAL_UNDEF(&tokens_zv);
	lsp_lsparrot_tokens_to_zval(&tokens_zv, text);
	if (Z_TYPE(tokens_zv) != IS_ARRAY) {
		if (!Z_ISUNDEF(tokens_zv)) {
			zval_ptr_dtor(&tokens_zv);
		}

		return NULL;
	}

	tokens = Z_ARRVAL(tokens_zv);
	count = zend_hash_num_elements(tokens);

	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY || !lsp_token_name_equals(token, "T_FUNCTION")) {
			continue;
		}

		label = lsp_next_string_token(tokens, i + 1);
		if (!label || !zend_string_equals(label, method_name)) {
			continue;
		}

		type = lsp_method_phpdoc_return_type(tokens, i);
		if (!type) {
			type = lsp_method_declared_return_type(text, token);
		}

		zval_ptr_dtor(&tokens_zv);

		return type;
	}

	zval_ptr_dtor(&tokens_zv);

	return NULL;
}

static inline const char *lsp_next_param_tag(const char *cursor, const char *end, size_t *tag_length)
{
	const char *tags[] = {"@param", "@phpstan-param", "@psalm-param"};
	const char *match, *best;
	size_t i, length;

	best = NULL;
	*tag_length = 0;
	for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
		match = strstr(cursor, tags[i]);
		if (!match || match >= end) {
			continue;
		}

		if (!best || match < best) {
			best = match;
			length = strlen(tags[i]);
			*tag_length = length;
		}
	}

	return best;
}

static inline bool lsp_type_text_is_exact_template(zend_string *type, zend_string *template_name)
{
	const char *start, *end;

	start = ZSTR_VAL(type);
	end = start + ZSTR_LEN(type);
	while (start < end && isspace((unsigned char) *start)) {
		start++;
	}
	while (end > start && isspace((unsigned char) end[-1])) {
		end--;
	}
	while (start < end && (*start == '?' || *start == '(')) {
		start++;
		while (start < end && isspace((unsigned char) *start)) {
			start++;
		}
	}
	while (end > start && end[-1] == ')') {
		end--;
		while (end > start && isspace((unsigned char) end[-1])) {
			end--;
		}
	}

	return (size_t) (end - start) == ZSTR_LEN(template_name) &&
		strncasecmp(start, ZSTR_VAL(template_name), ZSTR_LEN(template_name)) == 0
	;
}

static inline zend_string *lsp_phpdoc_return_template_from_comment(zend_string *comment)
{
	const char *cursor, *end, *p, *start;
	zend_string *return_type, *template_name;
	size_t tag_length;
	bool matches;

	return_type = lsp_phpdoc_return_type_from_comment_raw(comment);
	if (!return_type) {
		return NULL;
	}

	cursor = ZSTR_VAL(comment);
	end = cursor + ZSTR_LEN(comment);
	while ((cursor = lsp_next_template_tag(cursor, end, &tag_length)) != NULL && cursor < end) {
		p = cursor + tag_length;
		if (p < end && *p == '-') {
			p++;
			while (p < end && (islower((unsigned char) *p) || *p == '-')) {
				p++;
			}
		}

		while (p < end && isspace((unsigned char) *p)) {
			p++;
		}

		if (p >= end || !lsp_doc_is_identifier_start(*p)) {
			cursor = p;
			continue;
		}

		start = p++;
		while (p < end && lsp_doc_is_identifier_char(*p)) {
			p++;
		}

		template_name = zend_string_init(start, p - start, 0);
		matches = lsp_type_text_is_exact_template(return_type, template_name);
		if (matches) {
			zend_string_release(return_type);

			return template_name;
		}
		zend_string_release(template_name);
		cursor = p;
	}

	zend_string_release(return_type);

	return NULL;
}

static inline bool lsp_phpdoc_param_index_for_template(zend_string *comment, zend_string *template_name, uint32_t *param_index)
{
	const char *cursor, *end, *p, *type_start, *type_end;
	zend_string *type;
	size_t tag_length;
	uint32_t index;
	bool found_variable, found;

	cursor = ZSTR_VAL(comment);
	end = cursor + ZSTR_LEN(comment);
	index = 0;
	while ((cursor = lsp_next_param_tag(cursor, end, &tag_length)) != NULL && cursor < end) {
		p = cursor + tag_length;
		while (p < end && isspace((unsigned char) *p)) {
			p++;
		}

		type_start = p;
		found_variable = false;
		while (p < end && *p != '\n' && *p != '\r') {
			if (*p == '$' && p + 1 < end && lsp_doc_is_identifier_start(p[1])) {
				type_end = p;
				while (type_end > type_start && isspace((unsigned char) type_end[-1])) {
					type_end--;
				}
				found_variable = true;
				break;
			}
			p++;
		}

		if (found_variable && type_end > type_start) {
			type = zend_string_init(type_start, type_end - type_start, 0);
			found = lsp_type_contains_name(type, template_name);
			zend_string_release(type);
			if (found) {
				*param_index = index;

				return true;
			}
			index++;
		}

		cursor = p;
	}

	return false;
}

static inline zend_string *lsp_find_method_doc_comment(lsp_document *document, zend_string *method_name)
{
	zend_string *label, *comment;
	zval tokens_zv, *token;
	HashTable *tokens;
	uint32_t i, count;
	zend_long j;

	ZVAL_UNDEF(&tokens_zv);
	lsp_lsparrot_tokens_to_zval(&tokens_zv, document->text);
	if (Z_TYPE(tokens_zv) != IS_ARRAY) {
		if (!Z_ISUNDEF(tokens_zv)) {
			zval_ptr_dtor(&tokens_zv);
		}

		return NULL;
	}

	tokens = Z_ARRVAL(tokens_zv);
	count = zend_hash_num_elements(tokens);
	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY || !lsp_token_name_equals(token, "T_FUNCTION")) {
			continue;
		}

		label = lsp_next_string_token(tokens, i + 1);
		if (!label || !zend_string_equals(label, method_name)) {
			continue;
		}

		for (j = (zend_long) i - 1; j >= 0; j--) {
			token = zend_hash_index_find(tokens, (zend_ulong) j);
			if (!token || Z_TYPE_P(token) != IS_ARRAY) {
				continue;
			}
			if (lsp_token_name_equals(token, "T_DOC_COMMENT")) {
				comment = lsp_token_string(token, "text");
				if (comment) {
					comment = zend_string_copy(comment);
				}
				zval_ptr_dtor(&tokens_zv);

				return comment;
			}
			if (lsp_token_is_char(token, ';') || lsp_token_is_char(token, '{') || lsp_token_is_char(token, '}')) {
				break;
			}
		}

		zval_ptr_dtor(&tokens_zv);

		return NULL;
	}

	zval_ptr_dtor(&tokens_zv);

	return NULL;
}

static inline zend_string *lsp_method_call_argument_variable_at(zend_string *text, size_t open_paren_offset, uint32_t target_index)
{
	const char *value;
	size_t length, p, arg_start, arg_end;
	uint32_t index;
	int32_t paren_depth, bracket_depth, brace_depth;

	value = ZSTR_VAL(text);
	length = ZSTR_LEN(text);
	if (open_paren_offset >= length || value[open_paren_offset] != '(') {
		return NULL;
	}

	p = open_paren_offset + 1;
	arg_start = p;
	index = 0;
	paren_depth = 0;
	bracket_depth = 0;
	brace_depth = 0;
	while (p < length) {
		if (value[p] == '(') {
			paren_depth++;
		} else if (value[p] == ')' && paren_depth > 0) {
			paren_depth--;
		} else if (value[p] == '[') {
			bracket_depth++;
		} else if (value[p] == ']' && bracket_depth > 0) {
			bracket_depth--;
		} else if (value[p] == '{') {
			brace_depth++;
		} else if (value[p] == '}' && brace_depth > 0) {
			brace_depth--;
		} else if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 0 && (value[p] == ',' || value[p] == ')')) {
			if (index == target_index) {
				arg_end = p;
				while (arg_start < arg_end && isspace((unsigned char) value[arg_start])) {
					arg_start++;
				}
				while (arg_end > arg_start && isspace((unsigned char) value[arg_end - 1])) {
					arg_end--;
				}
				if (arg_end > arg_start + 1 && value[arg_start] == '$' && lsp_doc_is_identifier_start(value[arg_start + 1])) {
					p = arg_start + 2;
					while (p < arg_end && lsp_doc_is_identifier_char(value[p])) {
						p++;
					}
					if (p == arg_end) {
						return zend_string_init(value + arg_start, arg_end - arg_start, 0);
					}
				}

				return NULL;
			}
			if (value[p] == ')') {
				return NULL;
			}
			index++;
			arg_start = p + 1;
		}
		p++;
	}

	return NULL;
}

static inline zend_string *lsp_resolve_template_method_call_assignment_type(lsp_server *server, lsp_document *document, zend_string *method_name, size_t open_paren_offset, size_t offset)
{
	zend_string *comment, *template_name, *argument, *type;
	uint32_t param_index;

	comment = lsp_find_method_doc_comment(document, method_name);
	if (!comment) {
		return NULL;
	}

	template_name = lsp_phpdoc_return_template_from_comment(comment);
	if (!template_name) {
		zend_string_release(comment);

		return NULL;
	}

	if (!lsp_phpdoc_param_index_for_template(comment, template_name, &param_index)) {
		zend_string_release(comment);
		zend_string_release(template_name);

		return NULL;
	}
	zend_string_release(comment);

	argument = lsp_method_call_argument_variable_at(document->text, open_paren_offset, param_index);
	if (!argument) {
		zend_string_release(template_name);

		return NULL;
	}

	type = lsp_infer_variable_type(server, document, argument, offset);
	zend_string_release(argument);
	zend_string_release(template_name);
	if (lsp_type_is_unhelpful(type)) {
		if (type) {
			zend_string_release(type);
		}

		return NULL;
	}

	return type;
}

static inline zend_string *lsp_infer_method_call_assignment_type(lsp_server *server, lsp_document *document, zend_string *receiver, size_t offset)
{
	const char *value = ZSTR_VAL(document->text), *end = value + lsp_current_statement_scan_limit(document->text, offset),
		*p = value, *match, *q, *method_start, *method_end;
	zend_string *method_name, *return_type = NULL;

	while (p < end) {
		match = strstr(p, ZSTR_VAL(receiver));
		if (!match || match >= end) {
			break;
		}

		q = match + ZSTR_LEN(receiver);
		while (q < end && isspace((unsigned char) *q)) {
			q++;
		}

		if (q >= end || *q != '=') {
			p = match + 1;
			continue;
		}

		q++;
		while (q < end && isspace((unsigned char) *q)) {
			q++;
		}

		if (q + sizeof("$this") - 1 >= end || memcmp(q, "$this", sizeof("$this") - 1) != 0) {
			p = match + 1;
			continue;
		}

		q += sizeof("$this") - 1;
		while (q < end && isspace((unsigned char) *q)) {
			q++;
		}

		if (q + 2 >= end || q[0] != '-' || q[1] != '>') {
			p = match + 1;
			continue;
		}

		q += 2;
		while (q < end && isspace((unsigned char) *q)) {
			q++;
		}

		if (q >= end || !lsp_doc_is_identifier_start(*q)) {
			p = match + 1;
			continue;
		}

		method_start = q++;
		while (q < end && lsp_doc_is_identifier_char(*q)) {
			q++;
		}

		method_end = q;
		while (q < end && isspace((unsigned char) *q)) {
			q++;
		}

		if (q >= end || *q != '(') {
			p = match + 1;
			continue;
		}

		method_name = zend_string_init(method_start, method_end - method_start, 0);

		if (return_type) {
			zend_string_release(return_type);
		}

		return_type = lsp_resolve_template_method_call_assignment_type(server, document, method_name, (size_t) (q - value), (size_t) (match - value));
		if (!return_type) {
			return_type = lsp_find_method_return_type(document, method_name);
		}

		zend_string_release(method_name);

		p = q + 1;
	}

	return return_type;
}

static inline zend_string *lsp_infer_method_call_assignment_phpdoc_type(lsp_document *document, zend_string *receiver, size_t offset)
{
	const char *value = ZSTR_VAL(document->text), *end = value + lsp_current_statement_scan_limit(document->text, offset),
		*p = value, *match, *q, *method_start, *method_end;
	zend_string *method_name, *return_type = NULL;

	while (p < end) {
		match = strstr(p, ZSTR_VAL(receiver));
		if (!match || match >= end) {
			break;
		}

		q = match + ZSTR_LEN(receiver);
		while (q < end && isspace((unsigned char) *q)) {
			q++;
		}

		if (q >= end || *q != '=') {
			p = match + 1;
			continue;
		}

		q++;
		while (q < end && isspace((unsigned char) *q)) {
			q++;
		}

		if (q + sizeof("$this") - 1 >= end || memcmp(q, "$this", sizeof("$this") - 1) != 0) {
			p = match + 1;
			continue;
		}

		q += sizeof("$this") - 1;
		while (q < end && isspace((unsigned char) *q)) {
			q++;
		}

		if (q + 2 >= end || q[0] != '-' || q[1] != '>') {
			p = match + 1;
			continue;
		}

		q += 2;
		while (q < end && isspace((unsigned char) *q)) {
			q++;
		}

		if (q >= end || !lsp_doc_is_identifier_start(*q)) {
			p = match + 1;
			continue;
		}

		method_start = q++;
		while (q < end && lsp_doc_is_identifier_char(*q)) {
			q++;
		}

		method_end = q;
		while (q < end && isspace((unsigned char) *q)) {
			q++;
		}

		if (q >= end || *q != '(') {
			p = match + 1;
			continue;
		}

		method_name = zend_string_init(method_start, method_end - method_start, 0);

		if (return_type) {
			zend_string_release(return_type);
		}

		return_type = lsp_find_method_phpdoc_return_type(document, method_name);

		zend_string_release(method_name);

		p = q + 1;
	}

	return return_type;
}

static inline zend_string *lsp_infer_method_call_assignment_declared_type(lsp_document *document, zend_string *receiver, size_t offset)
{
	const char *value = ZSTR_VAL(document->text), *end = value + lsp_current_statement_scan_limit(document->text, offset),
		*p = value, *match, *q, *method_start, *method_end;
	zend_string *method_name, *return_type = NULL;

	while (p < end) {
		match = strstr(p, ZSTR_VAL(receiver));
		if (!match || match >= end) {
			break;
		}

		q = match + ZSTR_LEN(receiver);
		while (q < end && isspace((unsigned char) *q)) {
			q++;
		}

		if (q >= end || *q != '=') {
			p = match + 1;
			continue;
		}

		q++;
		while (q < end && isspace((unsigned char) *q)) {
			q++;
		}

		if (q + sizeof("$this") - 1 >= end || memcmp(q, "$this", sizeof("$this") - 1) != 0) {
			p = match + 1;
			continue;
		}

		q += sizeof("$this") - 1;
		while (q < end && isspace((unsigned char) *q)) {
			q++;
		}

		if (q + 2 >= end || q[0] != '-' || q[1] != '>') {
			p = match + 1;
			continue;
		}

		q += 2;
		while (q < end && isspace((unsigned char) *q)) {
			q++;
		}

		if (q >= end || !lsp_doc_is_identifier_start(*q)) {
			p = match + 1;
			continue;
		}

		method_start = q++;
		while (q < end && lsp_doc_is_identifier_char(*q)) {
			q++;
		}

		method_end = q;
		while (q < end && isspace((unsigned char) *q)) {
			q++;
		}

		if (q >= end || *q != '(') {
			p = match + 1;
			continue;
		}

		method_name = zend_string_init(method_start, method_end - method_start, 0);

		if (return_type) {
			zend_string_release(return_type);
		}

		return_type = lsp_find_method_declared_return_type(document, method_name);

		zend_string_release(method_name);

		p = q + 1;
	}

	return return_type;
}

static inline zend_string *lsp_infer_method_call_assignment_class(lsp_server *server, lsp_document *document, zend_string *receiver, size_t offset)
{
	zend_string *return_type, *class_name;
	const char *slash;

	return_type = lsp_infer_method_call_assignment_type(server, document, receiver, offset);

	if (!return_type) {
		return NULL;
	}

	slash = memchr(ZSTR_VAL(return_type), '\\', ZSTR_LEN(return_type));
	class_name = slash ? zend_string_copy(return_type) : lsp_resolve_class_name(document->text, return_type);
	zend_string_release(return_type);

	return class_name;
}

extern bool lsp_find_first_class_bounds(zend_string *text, size_t *body_start, size_t *body_end, zend_long *body_depth)
{
	const char *keywords[4] = {"class", "interface", "trait", "enum"}, *value = ZSTR_VAL(text);
	size_t i, p, close_offset, keyword_index, keyword_length;

	for (i = 0; i < ZSTR_LEN(text); i++) {
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

			*body_start = p + 1;
			*body_end = close_offset;
			*body_depth = lsp_brace_depth_at(text, p + 1);

			return true;
		}
	}

	return false;
}

extern bool lsp_method_is_public(HashTable *tokens, uint32_t index, zend_string *text, zend_long body_depth)
{
	zend_long i;
	zval *token;
	bool saw_visibility = false, public_visibility = true;

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

		if (lsp_token_name_equals(token, "T_PRIVATE") || lsp_token_name_equals(token, "T_PROTECTED")) {
			saw_visibility = true;
			public_visibility = false;
		} else if (lsp_token_name_equals(token, "T_PUBLIC")) {
			saw_visibility = true;
			public_visibility = true;
		}
	}

	return !saw_visibility || public_visibility;
}


static inline zend_string *lsp_inference_class_name_after_keyword(zend_string *text, size_t header_start, size_t header_end, const char *keyword)
{
	const char *value, *name_start, *name_end;
	zend_string *raw, *resolved;
	size_t p, keyword_end;

	value = ZSTR_VAL(text);
	for (p = header_start; p < header_end; p++) {
		if (!lsp_keyword_at_slice(value, p, header_end, keyword, &keyword_end)) {
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

static inline zend_string *lsp_inference_declared_class_name(zend_string *text)
{
	const char *keywords[4] = {"class", "interface", "trait", "enum"};
	zend_long body_depth;
	zend_string *resolved;
	size_t body_start, body_end, i;

	body_start = 0;
	body_end = 0;
	body_depth = 0;

	if (!lsp_find_first_class_bounds(text, &body_start, &body_end, &body_depth)) {
		return NULL;
	}

	for (i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
		resolved = lsp_inference_class_name_after_keyword(text, 0, body_start > 0 ? body_start - 1 : body_start, keywords[i]);
		if (resolved) {
			return resolved;
		}
	}

	return NULL;
}

static inline zend_string *lsp_inference_class_extends_name(zend_string *text)
{
	zend_long body_depth;
	size_t body_start, body_end;

	body_start = 0;
	body_end = 0;
	body_depth = 0;

	if (!lsp_find_first_class_bounds(text, &body_start, &body_end, &body_depth)) {
		return NULL;
	}

	return lsp_inference_class_name_after_keyword(text, 0, body_start > 0 ? body_start - 1 : body_start, "extends");
}

static inline bool lsp_type_segment_is_fluent_this(const char *start, const char *end)
{
	while (start < end && isspace((unsigned char) *start)) {
		start++;
	}

	while (end > start && isspace((unsigned char) end[-1])) {
		end--;
	}

	while (start < end && (*start == '?' || *start == '\\')) {
		start++;
	}

	return ((size_t) (end - start) == sizeof("static") - 1 && strncasecmp(start, "static", sizeof("static") - 1) == 0) ||
		((size_t) (end - start) == sizeof("$this") - 1 && strncmp(start, "$this", sizeof("$this") - 1) == 0)
	;
}

static inline bool lsp_type_is_fluent_this(zend_string *type)
{
	const char *value, *end, *segment_start, *p;
	uint32_t depth = 0;

	value = ZSTR_VAL(type);
	end = value + ZSTR_LEN(type);
	segment_start = value;

	for (p = value; p < end; p++) {
		if (*p == '<' || *p == '(') {
			depth++;
		} else if ((*p == '>' || *p == ')') && depth > 0) {
			depth--;
		} else if ((*p == '|' || *p == '&') && depth == 0) {
			if (lsp_type_segment_is_fluent_this(segment_start, p)) {
				return true;
			}
			segment_start = p + 1;
		}
	}

	return lsp_type_segment_is_fluent_this(segment_start, end);
}

static inline zend_string *lsp_project_class_contents(lsp_server *server, zend_string *class_name)
{
	zend_string *path, *contents;

	path = lsp_find_project_symbol_path(server, LSP_SYMBOL_CLASS, class_name);
	if (!path) {
		return NULL;
	}

	contents = lsp_read_file(path);

	zend_string_release(path);

	if (contents == zend_empty_string) {
		return NULL;
	}

	return contents;
}

static inline zend_string *lsp_project_method_return_type_in_class(lsp_server *server, zend_string *class_name, zend_string *method_name, zend_string **class_text, zend_string **origin_class)
{
	zend_string *contents, *return_type;

	*class_text = NULL;

	if (origin_class) {
		*origin_class = NULL;
	}

	contents = lsp_project_class_contents(server, class_name);
	if (!contents) {
		return NULL;
	}

	return_type = lsp_find_method_return_type_in_text(contents, method_name);
	if (!return_type) {
		zend_string_release(contents);
		return NULL;
	}

	*class_text = contents;
	if (origin_class) {
		*origin_class = zend_string_copy(class_name);
	}

	return return_type;
}

static inline zend_string *lsp_project_method_return_type_recursive(lsp_server *server, zend_string *class_name, zend_string *method_name, zend_string **class_text, zend_string **origin_class, HashTable *visited, uint32_t depth)
{
	zend_string *contents, *parent_class, *return_type = NULL;
	zval traits, *trait_zv;

	*class_text = NULL;

	if (origin_class) {
		*origin_class = NULL;
	}

	if (depth > 64 || zend_hash_exists(visited, class_name)) {
		return NULL;
	}

	zend_hash_add_empty_element(visited, class_name);
	return_type = lsp_project_method_return_type_in_class(server, class_name, method_name, class_text, origin_class);
	if (return_type) {
		return return_type;
	}

	contents = lsp_project_class_contents(server, class_name);
	if (!contents) {
		return NULL;
	}

	lsp_collect_class_trait_names(contents, &traits);

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL(traits), trait_zv) {
		if (Z_TYPE_P(trait_zv) != IS_STRING) {
			continue;
		}

		return_type = lsp_project_method_return_type_recursive(server, Z_STR_P(trait_zv), method_name, class_text, origin_class, visited, depth + 1);
		if (return_type) {
			break;
		}
	} ZEND_HASH_FOREACH_END();

	zval_ptr_dtor(&traits);

	if (return_type) {
		zend_string_release(contents);

		return return_type;
	}

	parent_class = lsp_inference_class_extends_name(contents);

	zend_string_release(contents);

	if (!parent_class) {
		return NULL;
	}

	return_type = lsp_project_method_return_type_recursive(server, parent_class, method_name, class_text, origin_class, visited, depth + 1);
	zend_string_release(parent_class);

	return return_type;
}

static inline zend_string *lsp_project_method_return_type(lsp_server *server, zend_string *class_name, zend_string *method_name, zend_string **class_text, zend_string **origin_class)
{
	zend_string *return_type;
	HashTable visited;

	zend_hash_init(&visited, 8, NULL, NULL, 0);
	return_type = lsp_project_method_return_type_recursive(server, class_name, method_name, class_text, origin_class, &visited, 0);
	zend_hash_destroy(&visited);

	return return_type;
}

static inline bool lsp_type_segment_equals_template(const char *start, const char *end, zend_string *template_name)
{
	while (start < end && isspace((unsigned char) *start)) {
		start++;
	}

	while (end > start && isspace((unsigned char) end[-1])) {
		end--;
	}

	return (size_t) (end - start) == ZSTR_LEN(template_name) &&
		strncasecmp(start, ZSTR_VAL(template_name), ZSTR_LEN(template_name)) == 0
	;
}

static inline bool lsp_type_contains_template(zend_string *type, zend_string *template_name)
{
	const char *value = ZSTR_VAL(type), *end = value + ZSTR_LEN(type), *segment_start, *p;
	uint32_t depth = 0;

	segment_start = value;
	for (p = value; p < end; p++) {
		if (*p == '<' || *p == '(') {
			depth++;
		} else if ((*p == '>' || *p == ')') && depth > 0) {
			depth--;
		} else if (*p == '|' && depth == 0) {
			if (lsp_type_segment_equals_template(segment_start, p, template_name)) {
				return true;
			}
			segment_start = p + 1;
		}
	}

	return lsp_type_segment_equals_template(segment_start, end, template_name);
}

static inline zend_string *lsp_resolve_generic_argument_at(lsp_document *document, zend_string *container_type, uint32_t target_index)
{
	zend_string *argument, *resolved;

	argument = lsp_type_generic_argument(container_type, target_index);
	if (!argument && target_index > 0) {
		argument = lsp_type_generic_argument(container_type, 0);
	}

	if (!argument) {
		return NULL;
	}

	resolved = lsp_resolve_class_name(document->text, argument);
	zend_string_release(argument);

	return resolved;
}

static inline zend_string *lsp_resolve_trait_use_template_type(lsp_server *server, zend_string *container_class, zend_string *trait_name, uint32_t target_index)
{
	zend_string *current, *contents, *parent_class, *resolved;
	HashTable visited;

	zend_hash_init(&visited, 8, NULL, NULL, 0);
	current = zend_string_copy(container_class);
	while (current) {
		if (zend_hash_exists(&visited, current)) {
			break;
		}

		zend_hash_add_empty_element(&visited, current);

		contents = lsp_project_class_contents(server, current);
		if (!contents) {
			break;
		}

		resolved = lsp_phpdoc_trait_use_generic_argument(contents, trait_name, target_index);
		if (resolved) {
			zend_string_release(contents);
			zend_string_release(current);
			zend_hash_destroy(&visited);

			return resolved;
		}

		parent_class = lsp_inference_class_extends_name(contents);
		zend_string_release(contents);
		zend_string_release(current);
		current = parent_class;
	}

	if (current) {
		zend_string_release(current);
	}

	zend_hash_destroy(&visited);

	return NULL;
}

static inline zend_string *lsp_resolve_project_template_type(lsp_server *server, lsp_document *document, zend_string *class_text, zend_string *container_class, zend_string *container_type, zend_string *origin_class, zend_string *type)
{
	zend_string *template_name, *resolved;
	uint32_t index;

	for (index = 0; ; index++) {
		template_name = lsp_phpdoc_template_type_at(class_text, index);
		if (!template_name) {
			break;
		}

		if (lsp_type_contains_template(type, template_name)) {
			resolved = lsp_resolve_generic_argument_at(document, container_type, index);
			if (!resolved && origin_class) {
				resolved = lsp_resolve_trait_use_template_type(server, container_class, origin_class, index);
			}

			zend_string_release(template_name);

			if (resolved) {
				return resolved;
			}
		} else {
			zend_string_release(template_name);
		}
	}

	return NULL;
}

static inline zend_string *lsp_resolve_this_method_return_class(lsp_server *server, lsp_document *document, zend_string *container_class, zend_string *method_name)
{
	zend_string *class_text, *return_type, *origin_class, *resolved;

	return_type = lsp_project_method_return_type(server, container_class, method_name, &class_text, &origin_class);
	if (!return_type) {
		return NULL;
	}

	if (lsp_type_is_fluent_this(return_type)) {
		zend_string_release(return_type);
		zend_string_release(class_text);

		if (origin_class) {
			zend_string_release(origin_class);
		}

		return zend_string_copy(container_class);
	}

	resolved = lsp_resolve_project_template_type(server, document, class_text, container_class, container_class, origin_class, return_type);
	if (!resolved) {
		resolved = lsp_resolve_class_name(class_text, return_type);
	}

	zend_string_release(return_type);
	zend_string_release(class_text);

	if (origin_class) {
		zend_string_release(origin_class);
	}

	return resolved;
}

static inline zend_string *lsp_resolve_project_method_return_class(lsp_server *server, lsp_document *document, zend_string *container_class, zend_string *container_type, zend_string *method_name)
{
	zend_string *class_text, *return_type, *origin_class, *resolved;

	return_type = lsp_project_method_return_type(server, container_class, method_name, &class_text, &origin_class);
	if (!return_type) {
		return NULL;
	}

	if (lsp_type_is_fluent_this(return_type)) {
		zend_string_release(return_type);
		zend_string_release(class_text);

		if (origin_class) {
			zend_string_release(origin_class);
		}

		return zend_string_copy(container_class);
	}

	resolved = lsp_resolve_project_template_type(server, document, class_text, container_class, container_type, origin_class, return_type);
	if (!resolved) {
		resolved = lsp_resolve_class_name(class_text, return_type);
	}

	zend_string_release(return_type);
	zend_string_release(class_text);

	if (origin_class) {
		zend_string_release(origin_class);
	}

	return resolved;
}

static inline zend_string *lsp_resolve_project_method_array_element_class(lsp_server *server, lsp_document *document, zend_string *container_class, zend_string *container_type, zend_string *method_name)
{
	zend_string *class_text, *return_type, *element_type, *origin_class, *resolved;

	return_type = lsp_project_method_return_type(server, container_class, method_name, &class_text, &origin_class);
	if (!return_type) {
		return NULL;
	}

	element_type = lsp_type_array_element_type(return_type);
	zend_string_release(return_type);
	if (!element_type) {
		zend_string_release(class_text);
		if (origin_class) {
			zend_string_release(origin_class);
		}

		return NULL;
	}

	resolved = lsp_resolve_project_template_type(server, document, class_text, container_class, container_type, origin_class, element_type);
	if (!resolved) {
		resolved = lsp_resolve_class_name(class_text, element_type);
	}

	zend_string_release(element_type);
	zend_string_release(class_text);

	if (origin_class) {
		zend_string_release(origin_class);
	}

	return resolved;
}

static inline bool lsp_variable_ending_at(zend_string *text, size_t offset, zend_string **variable)
{
	const char *value = ZSTR_VAL(text);
	size_t i, variable_start, variable_end;

	*variable = NULL;
	if (offset > ZSTR_LEN(text)) {
		offset = ZSTR_LEN(text);
	}

	i = offset;
	while (i > 0 && isspace((unsigned char) value[i - 1])) {
		i--;
	}

	variable_end = i;

	while (i > 0 && lsp_doc_is_identifier_char(value[i - 1])) {
		i--;
	}

	if (i == 0 || value[i - 1] != '$' || variable_end == i) {
		return false;
	}

	variable_start = i - 1;
	*variable = zend_string_init(value + variable_start, variable_end - variable_start, 0);

	return true;
}

static inline zend_string *lsp_infer_variable_container_type(lsp_server *server, lsp_document *document, zend_string *variable, size_t offset)
{
	zend_string *type;

	type = lsp_phpdoc_type_for_word(document->text, variable);
	if (type) {
		return type;
	}

	type = lsp_phpdoc_property_type_for_word(document->text, variable, offset);
	if (type) {
		return type;
	}

	return lsp_infer_method_call_assignment_type(server, document, variable, offset);
}

static inline zend_string *lsp_resolve_variable_method_return_class(lsp_server *server, lsp_document *document, zend_string *variable, zend_string *method_name, size_t offset)
{
	zend_string *container_type, *container_class, *resolved;

	container_type = lsp_infer_variable_container_type(server, document, variable, offset);
	if (!container_type) {
		return NULL;
	}

	container_class = lsp_resolve_class_name(document->text, container_type);
	if (!container_class) {
		zend_string_release(container_type);

		return NULL;
	}

	resolved = lsp_resolve_project_method_return_class(server, document, container_class, container_type, method_name);

	zend_string_release(container_class);
	zend_string_release(container_type);

	return resolved;
}

static inline zend_string *lsp_resolve_variable_method_array_element_class(lsp_server *server, lsp_document *document, zend_string *variable, zend_string *method_name, size_t offset)
{
	zend_string *container_type, *container_class, *resolved;

	container_type = lsp_infer_variable_container_type(server, document, variable, offset);
	if (!container_type) {
		return NULL;
	}

	container_class = lsp_resolve_class_name(document->text, container_type);
	if (!container_class) {
		zend_string_release(container_type);

		return NULL;
	}

	resolved = lsp_resolve_project_method_array_element_class(server, document, container_class, container_type, method_name);

	zend_string_release(container_class);
	zend_string_release(container_type);

	return resolved;
}

static inline zend_string *lsp_infer_method_array_access_assignment_class(lsp_server *server, lsp_document *document, zend_string *receiver, size_t offset)
{
	const char *value = ZSTR_VAL(document->text), *end = value + lsp_current_statement_scan_limit(document->text, offset), *p = value, *match, *q, *statement_end, *scan;
	zend_string *container_method, *container_variable, *chain_method, *container_return_type, *container_class, *element_class;
	size_t close_bracket, open_bracket, chain_receiver_end;

	while (p < end) {
		match = strstr(p, ZSTR_VAL(receiver));
		if (!match || match >= end) {
			break;
		}

		q = match + ZSTR_LEN(receiver);
		while (q < end && isspace((unsigned char) *q)) {
			q++;
		}

		if (q >= end || *q != '=') {
			p = match + 1;
			continue;
		}

		q++;
		while (q < end && isspace((unsigned char) *q)) {
			q++;
		}

		statement_end = q;
		while (statement_end < end && *statement_end != ';' && *statement_end != '{' && *statement_end != '}') {
			statement_end++;
		}

		close_bracket = (size_t) -1;
		for (scan = statement_end; scan > q; scan--) {
			if (scan[-1] == ']') {
				close_bracket = (size_t) ((scan - 1) - value);
				break;
			}
		}

		if (close_bracket == (size_t) -1 || !lsp_find_matching_open_bracket(document->text, close_bracket, &open_bracket)) {
			p = match + 1;
			continue;
		}

		if (!lsp_parse_method_call_before_offset(document->text, open_bracket, &chain_method, &chain_receiver_end)) {
			p = match + 1;
			continue;
		}

		element_class = NULL;

		if (lsp_parse_this_method_call_ending_at(document->text, chain_receiver_end, &container_method)) {
			container_return_type = lsp_find_method_return_type(document, container_method);
			zend_string_release(container_method);
			if (container_return_type) {
				container_class = lsp_resolve_class_name(document->text, container_return_type);
				if (container_class) {
					element_class = lsp_resolve_project_method_array_element_class(server, document, container_class, container_return_type, chain_method);
					zend_string_release(container_class);
				}

				zend_string_release(container_return_type);
			}
		} else if (lsp_variable_ending_at(document->text, chain_receiver_end, &container_variable)) {
			element_class = lsp_resolve_variable_method_array_element_class(server, document, container_variable, chain_method, (size_t) (match - value));
			zend_string_release(container_variable);
		}

		zend_string_release(chain_method);

		if (element_class) {
			return element_class;
		}

		p = match + 1;
	}

	return NULL;
}

static inline zend_string *lsp_infer_receiver_class(lsp_server *server, lsp_document *document, zend_string *receiver, size_t offset)
{
	zend_string *type, *class_name, *resolved;

	type = lsp_phpdoc_type_for_word(document->text, receiver);
	if (type) {
		class_name = lsp_resolve_class_name(document->text, type);
		zend_string_release(type);

		if (class_name) {
			return class_name;
		}
	}

	type = lsp_phpdoc_property_type_for_word(document->text, receiver, offset);
	if (type) {
		class_name = lsp_resolve_class_name(document->text, type);
		zend_string_release(type);

		if (class_name) {
			return class_name;
		}
	}

	class_name = lsp_infer_new_assignment_class(document->text, receiver, offset);
	if (class_name) {
		if (zend_lookup_class(class_name)) {
			return class_name;
		}

		resolved = lsp_resolve_class_name(document->text, class_name);
		zend_string_release(class_name);

		if (resolved) {
			return resolved;
		}
	}

	class_name = lsp_infer_method_array_access_assignment_class(server, document, receiver, offset);
	if (class_name) {
		return class_name;
	}

	return lsp_infer_method_call_assignment_class(server, document, receiver, offset);
}

static inline bool lsp_static_method_call_before_offset(lsp_document *document, size_t offset, zend_string **method_name, zend_string **class_name)
{
	const char *value = ZSTR_VAL(document->text);
	zend_string *raw, *resolved;
	size_t i, close_offset, open_offset, method_start, method_end, class_start, class_end;

	*method_name = NULL;
	*class_name = NULL;

	if (offset > ZSTR_LEN(document->text)) {
		offset = ZSTR_LEN(document->text);
	}

	i = offset;
	while (i > 0 && isspace((unsigned char) value[i - 1])) {
		i--;
	}

	if (i == 0 || value[i - 1] != ')') {
		return false;
	}

	close_offset = i - 1;
	if (!lsp_find_matching_open_paren(document->text, close_offset, &open_offset)) {
		return false;
	}

	i = open_offset;
	while (i > 0 && isspace((unsigned char) value[i - 1])) {
		i--;
	}

	method_end = i;
	while (i > 0 && lsp_doc_is_identifier_char(value[i - 1])) {
		i--;
	}

	method_start = i;
	if (method_start == method_end) {
		return false;
	}

	while (i > 0 && isspace((unsigned char) value[i - 1])) {
		i--;
	}

	if (i < 2 || value[i - 2] != ':' || value[i - 1] != ':') {
		return false;
	}

	i -= 2;

	while (i > 0 && isspace((unsigned char) value[i - 1])) {
		i--;
	}

	class_end = i;
	while (i > 0 && (lsp_doc_is_identifier_char(value[i - 1]) || value[i - 1] == '\\')) {
		i--;
	}

	class_start = i;
	if (class_start == class_end) {
		return false;
	}

	raw = zend_string_init(value + class_start, class_end - class_start, 0);
	if ((ZSTR_LEN(raw) == sizeof("self") - 1 && strncasecmp(ZSTR_VAL(raw), "self", ZSTR_LEN(raw)) == 0) ||
		(ZSTR_LEN(raw) == sizeof("static") - 1 && strncasecmp(ZSTR_VAL(raw), "static", ZSTR_LEN(raw)) == 0)
	) {
		zend_string_release(raw);
		resolved = lsp_inference_declared_class_name(document->text);
	} else {
		resolved = lsp_resolve_class_name(document->text, raw);
		zend_string_release(raw);
	}

	if (!resolved) {
		return false;
	}

	*class_name = resolved;
	*method_name = zend_string_init(value + method_start, method_end - method_start, 0);

	return true;
}

static inline zend_string *lsp_array_access_literal_key(zend_string *text, size_t open_bracket, size_t close_bracket)
{
	const char *value;
	size_t start, end;
	char quote;

	if (open_bracket >= close_bracket || close_bracket > ZSTR_LEN(text)) {
		return NULL;
	}

	value = ZSTR_VAL(text);
	start = open_bracket + 1;
	end = close_bracket;
	while (start < end && isspace((unsigned char) value[start])) {
		start++;
	}

	while (end > start && isspace((unsigned char) value[end - 1])) {
		end--;
	}

	if (start >= end) {
		return NULL;
	}

	if (value[start] == '\'' || value[start] == '"') {
		quote = value[start++];
		if (end <= start || value[end - 1] != quote) {
			return NULL;
		}
		end--;
	}

	if (start >= end) {
		return NULL;
	}

	return zend_string_init(value + start, end - start, 0);
}

static inline zend_string *lsp_resolve_array_shape_key_class(lsp_document *document, zend_string *container_type, zend_string *key)
{
	zend_string *key_type, *class_name;

	key_type = lsp_phpdoc_array_shape_key_type(container_type, key);
	if (!key_type) {
		return NULL;
	}

	class_name = lsp_resolve_class_name(document->text, key_type);
	zend_string_release(key_type);

	return class_name;
}

static inline zend_string *lsp_project_method_return_type_for_receiver(lsp_server *server, lsp_document *document, size_t receiver_end, zend_string *method_name, size_t context_offset)
{
	zend_string *variable, *receiver_class, *class_text, *origin_class, *return_type;

	class_text = NULL;
	origin_class = NULL;
	if (!lsp_variable_ending_at(document->text, receiver_end, &variable)) {
		return NULL;
	}

	if (zend_string_equals_literal(variable, "$this")) {
		receiver_class = lsp_inference_declared_class_name(document->text);
	} else {
		receiver_class = lsp_infer_receiver_class(server, document, variable, context_offset);
	}

	zend_string_release(variable);
	if (!receiver_class) {
		return NULL;
	}

	return_type = lsp_project_method_return_type(server, receiver_class, method_name, &class_text, &origin_class);
	zend_string_release(receiver_class);
	if (class_text) {
		zend_string_release(class_text);
	}

	if (origin_class) {
		zend_string_release(origin_class);
	}

	return return_type;
}

static inline zend_string *lsp_resolve_array_access_expression_class(lsp_server *server, lsp_document *document, size_t close_bracket, size_t context_offset)
{
	zend_string *key, *variable, *container_type, *method_name, *class_name;
	size_t open_bracket, receiver_end;

	if (!lsp_find_matching_open_bracket(document->text, close_bracket, &open_bracket)) {
		return NULL;
	}

	key = lsp_array_access_literal_key(document->text, open_bracket, close_bracket);
	if (!key) {
		return NULL;
	}

	container_type = NULL;
	if (lsp_variable_ending_at(document->text, open_bracket, &variable)) {
		container_type = lsp_infer_variable_type(server, document, variable, context_offset);
		zend_string_release(variable);
	} else if (lsp_parse_method_call_before_offset(document->text, open_bracket, &method_name, &receiver_end)) {
		container_type = lsp_project_method_return_type_for_receiver(server, document, receiver_end, method_name, context_offset);
		zend_string_release(method_name);
	}

	if (!container_type) {
		zend_string_release(key);

		return NULL;
	}

	class_name = lsp_resolve_array_shape_key_class(document, container_type, key);
	zend_string_release(container_type);
	zend_string_release(key);

	return class_name;
}

static inline zend_string *lsp_resolve_expression_class_ending_at(lsp_server *server, lsp_document *document, size_t expression_end, size_t context_offset, uint32_t depth)
{
	const char *value;
	zend_string *method_name, *receiver_class, *resolved, *variable;
	size_t i, receiver_end;

	if (depth > 32) {
		return NULL;
	}

	value = ZSTR_VAL(document->text);
	if (expression_end > ZSTR_LEN(document->text)) {
		expression_end = ZSTR_LEN(document->text);
	}

	i = expression_end;
	while (i > 0 && isspace((unsigned char) value[i - 1])) {
		i--;
	}

	if (i > 0 && value[i - 1] == ']') {
		resolved = lsp_resolve_array_access_expression_class(server, document, i - 1, context_offset);
		if (resolved) {
			return resolved;
		}

		return NULL;
	}

	if (lsp_parse_method_call_before_offset(document->text, i, &method_name, &receiver_end)) {
		if (lsp_variable_ending_at(document->text, receiver_end, &variable)) {
			if (zend_string_equals_literal(variable, "$this")) {
				receiver_class = lsp_inference_declared_class_name(document->text);
				resolved = receiver_class ? lsp_resolve_project_method_return_class(server, document, receiver_class, receiver_class, method_name) : NULL;
				if (receiver_class) {
					zend_string_release(receiver_class);
				}
			} else {
				resolved = lsp_resolve_variable_method_return_class(server, document, variable, method_name, context_offset);
			}

			zend_string_release(variable);
			zend_string_release(method_name);

			if (resolved) {
				return resolved;
			}

			return NULL;
		}

		receiver_class = lsp_resolve_expression_class_ending_at(server, document, receiver_end, context_offset, depth + 1);
		if (!receiver_class) {
			zend_string_release(method_name);

			return NULL;
		}

		resolved = lsp_resolve_project_method_return_class(server, document, receiver_class, receiver_class, method_name);
		zend_string_release(receiver_class);
		zend_string_release(method_name);

		return resolved;
	}

	if (lsp_static_method_call_before_offset(document, i, &method_name, &receiver_class)) {
		resolved = lsp_resolve_project_method_return_class(server, document, receiver_class, receiver_class, method_name);
		zend_string_release(receiver_class);
		zend_string_release(method_name);

		return resolved;
	}

	if (lsp_variable_ending_at(document->text, i, &variable)) {
		if (zend_string_equals_literal(variable, "$this")) {
			zend_string_release(variable);

			return lsp_inference_declared_class_name(document->text);
		}

		resolved = lsp_infer_receiver_class(server, document, variable, context_offset);
		zend_string_release(variable);

		return resolved;
	}

	return NULL;
}

static inline bool lsp_method_chain_member_context(lsp_server *server, lsp_document *document, size_t offset, zend_string *prefix, zend_string **class_name, zend_string **member_prefix)
{
	size_t receiver_end;

	*class_name = NULL;
	*member_prefix = NULL;
	if (!lsp_member_access_arrow(document->text, offset, prefix, NULL, &receiver_end)) {
		return false;
	}

	*class_name = lsp_resolve_expression_class_ending_at(server, document, receiver_end, offset, 0);
	if (!*class_name) {
		return false;
	}

	*member_prefix = zend_string_copy(prefix);

	return true;
}

extern zend_string *lsp_infer_variable_type(lsp_server *server, lsp_document *document, zend_string *variable, size_t offset)
{
	zend_string *type;

	type = lsp_phpdoc_type_for_word(document->text, variable);
	if (type) {
		return type;
	}

	type = lsp_phpdoc_property_type_for_word(document->text, variable, offset);
	if (type) {
		return type;
	}

	type = lsp_infer_method_array_access_assignment_class(server, document, variable, offset);
	if (type) {
		return type;
	}

	type = lsp_infer_method_call_assignment_type(server, document, variable, offset);
	if (type) {
		return type;
	}

	return lsp_infer_new_assignment_class(document->text, variable, lsp_current_statement_scan_limit(document->text, offset));
}

extern zend_string *lsp_infer_variable_phpdoc_type(lsp_document *document, zend_string *variable, size_t offset)
{
	zend_string *type;

	type = lsp_phpdoc_type_for_word_raw(document->text, variable);
	if (type) {
		return type;
	}

	type = lsp_phpdoc_property_type_for_word_raw(document->text, variable, offset);
	if (type) {
		return type;
	}

	return lsp_infer_method_call_assignment_phpdoc_type(document, variable, offset);
}

extern zend_string *lsp_infer_variable_declared_type(lsp_server *server, lsp_document *document, zend_string *variable, size_t offset)
{
	zend_string *type;

	type = lsp_infer_method_array_access_assignment_class(server, document, variable, offset);
	if (type) {
		return type;
	}

	type = lsp_infer_method_call_assignment_declared_type(document, variable, offset);
	if (type) {
		return type;
	}

	return lsp_infer_new_assignment_class(document->text, variable, lsp_current_statement_scan_limit(document->text, offset));
}

static inline bool lsp_this_method_array_access_member_context(lsp_server *server, lsp_document *document, size_t offset, zend_string *prefix, zend_string **class_name, zend_string **member_prefix)
{
	const char *value = ZSTR_VAL(document->text);
	zend_string *container_method, *chain_method, *container_return_type, *container_class;
	size_t receiver_end, i, close_bracket, open_bracket, chain_receiver_end;

	*class_name = NULL;
	*member_prefix = NULL;

	if (!lsp_member_access_arrow(document->text, offset, prefix, NULL, &receiver_end)) {
		return false;
	}

	i = receiver_end;
	while (i > 0 && isspace((unsigned char) value[i - 1])) {
		i--;
	}

	if (i == 0 || value[i - 1] != ']') {
		return false;
	}

	close_bracket = i - 1;
	if (!lsp_find_matching_open_bracket(document->text, close_bracket, &open_bracket)) {
		return false;
	}

	if (!lsp_parse_method_call_before_offset(document->text, open_bracket, &chain_method, &chain_receiver_end)) {
		return false;
	}

	if (!lsp_parse_this_method_call_ending_at(document->text, chain_receiver_end, &container_method)) {
		zend_string_release(chain_method);

		return false;
	}

	container_return_type = lsp_find_method_return_type(document, container_method);
	zend_string_release(container_method);
	if (!container_return_type) {
		zend_string_release(chain_method);

		return false;
	}

	container_class = lsp_resolve_class_name(document->text, container_return_type);
	if (!container_class) {
		zend_string_release(container_return_type);
		zend_string_release(chain_method);

		return false;
	}

	*class_name = lsp_resolve_project_method_array_element_class(server, document, container_class, container_return_type, chain_method);

	zend_string_release(container_class);
	zend_string_release(container_return_type);
	zend_string_release(chain_method);

	if (!*class_name) {
		return false;
	}

	*member_prefix = zend_string_copy(prefix);

	return true;
}

static inline bool lsp_variable_method_call_member_context(lsp_server *server, lsp_document *document, size_t offset, zend_string *prefix, zend_string **class_name, zend_string **member_prefix)
{
	zend_string *method_name, *variable;
	size_t receiver_end, call_receiver_end;

	*class_name = NULL;
	*member_prefix = NULL;

	if (!lsp_member_access_arrow(document->text, offset, prefix, NULL, &receiver_end)) {
		return false;
	}

	if (!lsp_parse_method_call_before_offset(document->text, receiver_end, &method_name, &call_receiver_end)) {
		return false;
	}

	if (!lsp_variable_ending_at(document->text, call_receiver_end, &variable)) {
		zend_string_release(method_name);

		return false;
	}

	*class_name = lsp_resolve_variable_method_return_class(server, document, variable, method_name, offset);

	zend_string_release(variable);
	zend_string_release(method_name);

	if (!*class_name) {
		return false;
	}

	*member_prefix = zend_string_copy(prefix);

	return true;
}

static inline bool lsp_variable_method_array_access_member_context(lsp_server *server, lsp_document *document, size_t offset, zend_string *prefix, zend_string **class_name, zend_string **member_prefix)
{
	const char *value = ZSTR_VAL(document->text);
	zend_string *chain_method, *variable;
	size_t receiver_end, i, close_bracket, open_bracket, chain_receiver_end;

	*class_name = NULL;
	*member_prefix = NULL;

	if (!lsp_member_access_arrow(document->text, offset, prefix, NULL, &receiver_end)) {
		return false;
	}

	i = receiver_end;
	while (i > 0 && isspace((unsigned char) value[i - 1])) {
		i--;
	}

	if (i == 0 || value[i - 1] != ']') {
		return false;
	}

	close_bracket = i - 1;
	if (!lsp_find_matching_open_bracket(document->text, close_bracket, &open_bracket)) {
		return false;
	}

	if (!lsp_parse_method_call_before_offset(document->text, open_bracket, &chain_method, &chain_receiver_end)) {
		return false;
	}

	if (!lsp_variable_ending_at(document->text, chain_receiver_end, &variable)) {
		zend_string_release(chain_method);

		return false;
	}

	*class_name = lsp_resolve_variable_method_array_element_class(server, document, variable, chain_method, offset);

	zend_string_release(variable);
	zend_string_release(chain_method);

	if (!*class_name) {
		return false;
	}

	*member_prefix = zend_string_copy(prefix);

	return true;
}

extern bool lsp_member_access_class_context(lsp_server *server, lsp_document *document, size_t offset, zend_string *prefix, zend_string **class_name, zend_string **member_prefix)
{
	zend_string *receiver, *method_name, *current_class;

	*class_name = NULL;
	*member_prefix = NULL;

	if (lsp_member_access_context(document->text, offset, prefix, &receiver, member_prefix)) {
		*class_name = lsp_infer_receiver_class(server, document, receiver, offset);

		zend_string_release(receiver);

		if (*class_name) {
			return true;
		}

		zend_string_release(*member_prefix);
		*member_prefix = NULL;

		return false;
	}

	if (lsp_method_chain_member_context(server, document, offset, prefix, class_name, member_prefix)) {
		return true;
	}

	if (lsp_variable_method_call_member_context(server, document, offset, prefix, class_name, member_prefix)) {
		return true;
	}

	if (lsp_this_method_call_member_access_context(document->text, offset, prefix, &method_name, member_prefix)) {
		current_class = lsp_inference_declared_class_name(document->text);

		*class_name = current_class ? lsp_resolve_this_method_return_class(server, document, current_class, method_name) : NULL;
		if (current_class) {
			zend_string_release(current_class);
		}

		zend_string_release(method_name);

		if (*class_name) {
			return true;
		}

		zend_string_release(*member_prefix);
		*member_prefix = NULL;

		return false;
	}

	if (lsp_variable_method_array_access_member_context(server, document, offset, prefix, class_name, member_prefix)) {
		return true;
	}

	if (lsp_this_method_array_access_member_context(server, document, offset, prefix, class_name, member_prefix)) {
		return true;
	}

	return false;
}
