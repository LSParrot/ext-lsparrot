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

static inline bool lsp_phpdoc_token_equals_literal_ci(const char *token, size_t token_length, const char *literal)
{
	size_t literal_length;

	literal_length = strlen(literal);

	return token_length == literal_length && strncasecmp(token, literal, token_length) == 0;
}

static inline bool lsp_phpdoc_token_is_lsparrot_int_alias(const char *token, size_t token_length)
{
	return lsp_phpdoc_token_equals_literal_ci(token, token_length, "positive-int") ||
		lsp_phpdoc_token_equals_literal_ci(token, token_length, "negative-int") ||
		lsp_phpdoc_token_equals_literal_ci(token, token_length, "non-positive-int") ||
		lsp_phpdoc_token_equals_literal_ci(token, token_length, "non-negative-int")
	;
}

static inline bool lsp_phpdoc_token_is_lsparrot_string_alias(const char *token, size_t token_length)
{
	return lsp_phpdoc_token_equals_literal_ci(token, token_length, "non-empty-string") ||
		lsp_phpdoc_token_equals_literal_ci(token, token_length, "literal-string") ||
		lsp_phpdoc_token_equals_literal_ci(token, token_length, "lowercase-string") ||
		lsp_phpdoc_token_equals_literal_ci(token, token_length, "numeric-string") ||
		lsp_phpdoc_token_equals_literal_ci(token, token_length, "class-string")
	;
}

static inline bool lsp_phpdoc_token_is_lsparrot_array_alias(const char *token, size_t token_length)
{
	return lsp_phpdoc_token_equals_literal_ci(token, token_length, "non-empty-array");
}

static inline const char *lsp_phpdoc_angle_expression_end(const char *open, const char *end)
{
	const char *cursor;
	uint32_t depth;
	char quote;
	bool escaped;

	cursor = open;
	depth = 0;
	quote = '\0';
	escaped = false;

	while (cursor < end) {
		if (quote != '\0') {
			if (escaped) {
				escaped = false;
			} else if (*cursor == '\\') {
				escaped = true;
			} else if (*cursor == quote) {
				quote = '\0';
			}
			cursor++;
			continue;
		}

		if (*cursor == '\'' || *cursor == '"') {
			quote = *cursor;
			cursor++;
			continue;
		}

		if (*cursor == '<') {
			depth++;
		} else if (*cursor == '>' && depth > 0 && --depth == 0) {
			return cursor + 1;
		}

		cursor++;
	}

	return cursor;
}

static inline bool lsp_phpdoc_token_starts_at_boundary(const char *value, const char *cursor)
{
	return cursor == value || (!lsp_doc_is_identifier_char(cursor[-1]) && cursor[-1] != '-');
}

static inline zend_string *lsp_phpdoc_normalize_lsparrot_type(zend_string *type)
{
	const char *value, *cursor, *end, *token_start, *token_end, *finish;
	smart_str buffer = {0};
	bool changed;

	value = ZSTR_VAL(type);
	cursor = value;
	end = value + ZSTR_LEN(type);
	changed = false;

	while (cursor < end) {
		if (cursor + sizeof("int<") - 1 <= end && lsp_phpdoc_token_starts_at_boundary(value, cursor) &&
			strncasecmp(cursor, "int<", sizeof("int<") - 1) == 0
		) {
			finish = lsp_phpdoc_angle_expression_end(cursor + sizeof("int") - 1, end);
			smart_str_appendl(&buffer, "int", sizeof("int") - 1);
			cursor = finish;
			changed = true;
			continue;
		}

		if (isalpha((unsigned char) *cursor) || *cursor == '_' || *cursor == '\\') {
			token_start = cursor;
			cursor++;
			while (cursor < end && (lsp_doc_is_identifier_char(*cursor) || *cursor == '-' || *cursor == '\\')) {
				cursor++;
			}
			token_end = cursor;

			if (lsp_phpdoc_token_is_lsparrot_int_alias(token_start, token_end - token_start)) {
				smart_str_appendl(&buffer, "int", sizeof("int") - 1);
				changed = true;
			} else if (lsp_phpdoc_token_is_lsparrot_string_alias(token_start, token_end - token_start)) {
				smart_str_appendl(&buffer, "string", sizeof("string") - 1);
				changed = true;
			} else if (lsp_phpdoc_token_is_lsparrot_array_alias(token_start, token_end - token_start)) {
				smart_str_appendl(&buffer, "array", sizeof("array") - 1);
				changed = true;
			} else {
				smart_str_appendl(&buffer, token_start, token_end - token_start);
			}
			continue;
		}

		smart_str_appendc(&buffer, *cursor);
		cursor++;
	}

	if (!changed) {
		smart_str_free(&buffer);

		return zend_string_copy(type);
	}

	smart_str_0(&buffer);

	return buffer.s ? buffer.s : zend_string_init("", 0, 0);
}

static inline zend_string *lsp_phpdoc_type_slice(const char *start, const char *end, bool normalize)
{
	zend_string *raw, *normalized;

	raw = zend_string_init(start, end - start, 0);
	if (!normalize) {
		return raw;
	}

	normalized = lsp_phpdoc_normalize_lsparrot_type(raw);
	zend_string_release(raw);

	return normalized;
}

static inline bool lsp_phpdoc_context_at(zend_string *text, size_t offset)
{
	const char *value;
	size_t i, comment_start;
	bool found_start;

	if (offset > ZSTR_LEN(text)) {
		offset = ZSTR_LEN(text);
	}

	value = ZSTR_VAL(text);
	i = offset;
	found_start = false;
	comment_start = 0;
	while (i > 0) {
		if (i >= 2 && value[i - 2] == '*' && value[i - 1] == '/') {
			return false;
		}

		if (i >= 2 && value[i - 2] == '/' && value[i - 1] == '*') {
			comment_start = i - 2;
			found_start = true;
			break;
		}

		i--;
	}

	return found_start && comment_start + 2 < ZSTR_LEN(text) && value[comment_start + 2] == '*';
}

static inline bool lsp_phpdoc_completion_char(char c)
{
	return lsp_doc_is_identifier_char(c) || c == '-' || c == '\\' || c == '@';
}

static inline zend_string *lsp_phpdoc_completion_prefix_at(zend_string *text, size_t offset, size_t *start_offset)
{
	const char *value;
	size_t start;

	if (offset > ZSTR_LEN(text)) {
		offset = ZSTR_LEN(text);
	}

	value = ZSTR_VAL(text);
	start = offset;
	while (start > 0 && lsp_phpdoc_completion_char(value[start - 1])) {
		start--;
	}

	*start_offset = start;

	return zend_string_init(value + start, offset - start, 0);
}

static inline bool lsp_phpdoc_matches_completion_prefix(const char *label, zend_string *prefix)
{
	const char *prefix_value;
	size_t label_length, prefix_length;

	prefix_value = ZSTR_VAL(prefix);
	prefix_length = ZSTR_LEN(prefix);
	label_length = strlen(label);
	if (prefix_length == 0) {
		return true;
	}

	if (prefix_length <= label_length && strncasecmp(label, prefix_value, prefix_length) == 0) {
		return true;
	}

	if (prefix_length > 0 && prefix_value[0] == '@') {
		return prefix_length - 1 <= label_length && strncasecmp(label, prefix_value + 1, prefix_length - 1) == 0;
	}

	return false;
}

static inline void lsp_phpdoc_add_completion_edit(zval *items, zend_string *text, size_t start_offset, size_t end_offset, const char *label, zend_long kind, const char *detail)
{
	zval item, data, text_edit, range;

	array_init(&item);
	add_assoc_string(&item, "label", label);
	add_assoc_long(&item, "kind", kind);
	add_assoc_string(&item, "detail", detail);
	add_assoc_string(&item, "filterText", label);
	array_init(&text_edit);
	lsp_range_from_offsets(text, start_offset, end_offset, &range);
	add_assoc_zval(&text_edit, "range", &range);
	add_assoc_string(&text_edit, "newText", label);
	add_assoc_zval(&item, "textEdit", &text_edit);
	array_init(&data);
	add_assoc_string(&data, "source", "lsparrot");
	add_assoc_zval(&item, "data", &data);
	add_next_index_zval(items, &item);
}

static inline void lsp_phpdoc_add_completion_edit_if_matches(zval *items, zend_string *text, size_t start_offset, size_t end_offset, zend_string *prefix, const char *label, zend_long kind, const char *detail)
{
	if (!lsp_phpdoc_matches_completion_prefix(label, prefix)) {
		return;
	}

	lsp_phpdoc_add_completion_edit(items, text, start_offset, end_offset, label, kind, detail);
}

static inline void lsp_phpdoc_add_tag_completion_edits(zval *items, zend_string *text, size_t start_offset, size_t end_offset, zend_string *prefix)
{
	static const char *tags[] = {
		"@api", "@deprecated", "@extends", "@implements", "@method", "@mixin", "@param", "@param-out",
		"@property", "@property-read", "@property-write", "@return", "@self-out", "@template",
		"@template-covariant", "@template-contravariant", "@throws", "@use", "@var",
		"@phpstan-assert", "@phpstan-assert-if-false", "@phpstan-assert-if-true", "@phpstan-consistent-constructor",
		"@phpstan-extends", "@phpstan-implements", "@phpstan-import-type", "@phpstan-method", "@phpstan-param",
		"@phpstan-param-out", "@phpstan-property", "@phpstan-property-read", "@phpstan-property-write",
		"@phpstan-pure", "@phpstan-require-extends", "@phpstan-require-implements", "@phpstan-return",
		"@phpstan-self-out", "@phpstan-template", "@phpstan-template-covariant", "@phpstan-this-out",
		"@phpstan-throws", "@phpstan-type", "@phpstan-use", "@phpstan-var",
		"@psalm-api", "@psalm-assert", "@psalm-assert-if-false", "@psalm-assert-if-true",
		"@psalm-check-type", "@psalm-check-type-exact", "@psalm-consistent-constructor",
		"@psalm-consistent-templates", "@psalm-external-mutation-free", "@psalm-immutable",
		"@psalm-import-type", "@psalm-implements", "@psalm-inheritors", "@psalm-internal",
		"@psalm-method", "@psalm-mutation-free", "@psalm-param", "@psalm-param-out",
		"@psalm-property", "@psalm-property-read", "@psalm-property-write", "@psalm-pure",
		"@psalm-readonly", "@psalm-readonly-allow-private-mutation", "@psalm-require-extends",
		"@psalm-require-implements", "@psalm-return", "@psalm-seal-methods", "@psalm-seal-properties",
		"@psalm-suppress", "@psalm-taint-escape", "@psalm-taint-sink", "@psalm-taint-source",
		"@psalm-template", "@psalm-template-covariant", "@psalm-this-out", "@psalm-trace",
		"@psalm-type", "@psalm-var", "@psalm-yield"
	};
	size_t i;

	for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
		lsp_phpdoc_add_completion_edit_if_matches(items, text, start_offset, end_offset, prefix, tags[i], 14, "PHPDoc annotation");
	}
}

static inline void lsp_phpdoc_add_type_completion_edits(zval *items, zend_string *text, size_t start_offset, size_t end_offset, zend_string *prefix)
{
	static const char *types[] = {
		"array", "array-key", "bool", "callable", "callable-string", "class-string",
		"class-string-map", "closed-resource", "conditional", "decimal-int-string",
		"empty", "enum-string", "false", "float", "int", "interface-string", "iterable",
		"key-of", "list", "literal-string", "lowercase-string", "mixed", "negative-int",
		"never", "non-decimal-int-string", "non-empty-array", "non-empty-list",
		"non-empty-lowercase-string", "non-empty-string", "non-empty-uppercase-string",
		"non-falsy-string", "non-negative-int", "non-positive-int", "null", "numeric",
		"numeric-string", "object", "parent", "positive-int", "private-properties-of",
		"protected-properties-of", "public-properties-of", "pure-callable", "pure-Closure",
		"resource", "scalar", "self", "static", "string", "template-type", "trait-string",
		"true", "truthy-string", "uppercase-string", "value-of", "void"
	};
	size_t i;

	for (i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
		lsp_phpdoc_add_completion_edit_if_matches(items, text, start_offset, end_offset, prefix, types[i], 25, "PHPDoc type");
	}
}

extern void lsp_phpdoc_add_annotation_completions(zval *items, zend_string *text, size_t offset)
{
	size_t start_offset;
	zend_string *prefix;

	if (!lsp_phpdoc_context_at(text, offset)) {
		return;
	}

	prefix = lsp_phpdoc_completion_prefix_at(text, offset, &start_offset);
	lsp_phpdoc_add_tag_completion_edits(items, text, start_offset, offset, prefix);
	lsp_phpdoc_add_type_completion_edits(items, text, start_offset, offset, prefix);
	zend_string_release(prefix);
}

static inline bool lsp_doc_find_next_type_ex(const char **cursor, const char *end, zend_string **variable, zend_string **type, bool normalize)
{
	const char *var_tag = NULL, *param_tag = NULL, *tag, *p,
		*type_start, *type_end, *var_start, *var_end
	;

	*variable = NULL;
	*type = NULL;
	var_tag = strstr(*cursor, "@var");
	param_tag = strstr(*cursor, "@param");

	if (var_tag && var_tag < end && (!param_tag || var_tag < param_tag)) {
		tag = var_tag + sizeof("@var") - 1;
	} else if (param_tag && param_tag < end) {
		tag = param_tag + sizeof("@param") - 1;
	} else {
		*cursor = end;

		return false;
	}

	p = tag;
	while (p < end && isspace((unsigned char) *p)) {
		p++;
	}

	type_start = p;
	while (p < end && *p != '\n' && *p != '\r') {
		if (*p == '$' && p + 1 < end && lsp_doc_is_identifier_start(p[1])) {
			var_start = p;
			type_end = p;
			while (type_end > type_start && isspace((unsigned char) type_end[-1])) {
				type_end--;
			}

			var_end = p + 2;
			while (var_end < end && lsp_doc_is_identifier_char(*var_end)) {
				var_end++;
			}

			if (type_end > type_start) {
				*variable = zend_string_init(var_start, var_end - var_start, 0);
				*type = lsp_phpdoc_type_slice(type_start, type_end, normalize);
				*cursor = var_end;

				return true;
			}
		}

		p++;
	}

	*cursor = p;

	return false;
}

static inline bool lsp_doc_find_next_type(const char **cursor, const char *end, zend_string **variable, zend_string **type)
{
	return lsp_doc_find_next_type_ex(cursor, end, variable, type, true);
}

static inline zend_string *lsp_type_class_name(zend_string *type)
{
	const char *p = ZSTR_VAL(type), *end = p + ZSTR_LEN(type), *start;

	while (p < end && isspace((unsigned char) *p)) {
		p++;
	}

	while (p < end && (*p == '?' || *p == '(')) {
		p++;
	}

	while (p < end && isspace((unsigned char) *p)) {
		p++;
	}

	if (p < end && *p == '\\') {
		p++;
	}

	start = p;
	while (p < end && (lsp_doc_is_identifier_char(*p) || *p == '\\')) {
		p++;
	}

	if (p == start) {
		return NULL;
	}

	return zend_string_init(start, p - start, 0);
}

static inline zend_string *lsp_string_trim_slice(const char *start, const char *end)
{
	while (start < end && isspace((unsigned char) *start)) {
		start++;
	}

	while (end > start && isspace((unsigned char) end[-1])) {
		end--;
	}

	return zend_string_init(start, end - start, 0);
}

static inline bool lsp_phpdoc_whitespace_continues_type(const char *type_start, const char *cursor, const char *end)
{
	const char *previous, *next;

	previous = cursor;
	while (previous > type_start && isspace((unsigned char) previous[-1])) {
		previous--;
	}

	next = cursor;
	while (next < end && isspace((unsigned char) *next) && *next != '\n' && *next != '\r') {
		next++;
	}

	if (next < end && (*next == '|' || *next == '&')) {
		return true;
	}

	return previous > type_start && (previous[-1] == '|' || previous[-1] == '&');
}

static inline bool lsp_phpdoc_tag_at(const char *cursor, const char *end, const char *tag)
{
	size_t tag_length;

	tag_length = strlen(tag);
	if (cursor + tag_length > end || memcmp(cursor, tag, tag_length) != 0) {
		return false;
	}

	return cursor + tag_length == end ||
		isspace((unsigned char) cursor[tag_length]) ||
		cursor[tag_length] == '*'
	;
}

static inline const char *lsp_phpdoc_type_expression_end(const char *type_start, const char *end)
{
	const char *cursor;
	uint32_t brace_depth, angle_depth, paren_depth, bracket_depth;
	char quote;
	bool escaped;

	cursor = type_start;
	brace_depth = 0;
	angle_depth = 0;
	paren_depth = 0;
	bracket_depth = 0;
	quote = '\0';
	escaped = false;

	while (cursor < end) {
		if (quote != '\0') {
			if (escaped) {
				escaped = false;
			} else if (*cursor == '\\') {
				escaped = true;
			} else if (*cursor == quote) {
				quote = '\0';
			}
			cursor++;
			continue;
		}

		if (*cursor == '\'' || *cursor == '"') {
			quote = *cursor;
			cursor++;
			continue;
		}

		if (*cursor == '\n' || *cursor == '\r') {
			break;
		}

		if (isspace((unsigned char) *cursor) && brace_depth == 0 && angle_depth == 0 && paren_depth == 0 && bracket_depth == 0) {
			if (!lsp_phpdoc_whitespace_continues_type(type_start, cursor, end)) {
				break;
			}
		} else if (*cursor == '{') {
			brace_depth++;
		} else if (*cursor == '}' && brace_depth > 0) {
			brace_depth--;
		} else if (*cursor == '<') {
			angle_depth++;
		} else if (*cursor == '>' && angle_depth > 0) {
			angle_depth--;
		} else if (*cursor == '(') {
			paren_depth++;
		} else if (*cursor == ')' && paren_depth > 0) {
			paren_depth--;
		} else if (*cursor == '[') {
			bracket_depth++;
		} else if (*cursor == ']' && bracket_depth > 0) {
			bracket_depth--;
		}

		cursor++;
	}

	return cursor;
}

static inline zend_string *lsp_phpdoc_var_type_from_comment_ex(zend_string *comment, bool normalize)
{
	const char *tags[3] = {"@var", "@phpstan-var", "@psalm-var"};
	const char *value, *end, *cursor, *match, *best, *p, *start, *finish;
	size_t tag_length, best_length;
	uint32_t i;

	value = ZSTR_VAL(comment);
	end = value + ZSTR_LEN(comment);
	best = NULL;
	best_length = 0;

	for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
		tag_length = strlen(tags[i]);
		cursor = value;
		while ((match = strstr(cursor, tags[i])) != NULL && match < end) {
			if (lsp_phpdoc_tag_at(match, end, tags[i]) && (!best || match < best)) {
				best = match;
				best_length = tag_length;
				break;
			}

			cursor = match + tag_length;
		}
	}

	if (!best) {
		return NULL;
	}

	p = best + best_length;
	while (p < end && isspace((unsigned char) *p)) {
		p++;
	}

	start = p;
	finish = lsp_phpdoc_type_expression_end(start, end);
	while (finish > start && isspace((unsigned char) finish[-1])) {
		finish--;
	}

	if (finish - start >= 2 && finish[-2] == '*' && finish[-1] == '/') {
		finish -= 2;
		while (finish > start && isspace((unsigned char) finish[-1])) {
			finish--;
		}
	}

	if (finish <= start) {
		return NULL;
	}

	return lsp_phpdoc_type_slice(start, finish, normalize);
}

static inline zend_string *lsp_phpdoc_property_var_type_before(zend_string *text, size_t variable_offset, bool normalize)
{
	const char *value;
	zend_string *comment, *type;
	size_t declaration_start, scan, doc_end, doc_start, base;
	bool found_doc_start;

	if (variable_offset > ZSTR_LEN(text)) {
		return NULL;
	}

	value = ZSTR_VAL(text);
	declaration_start = variable_offset;
	while (declaration_start > 0 &&
		value[declaration_start - 1] != ';' &&
		value[declaration_start - 1] != '{' &&
		value[declaration_start - 1] != '}'
	) {
		declaration_start--;
	}

	doc_end = 0;
	for (scan = declaration_start; scan + 1 < variable_offset; scan++) {
		if (value[scan] == '*' && value[scan + 1] == '/') {
			doc_end = scan + 2;
		}
	}

	if (doc_end == 0 || doc_end < 3) {
		return NULL;
	}

	doc_start = 0;
	base = declaration_start + 2;
	found_doc_start = false;
	for (scan = doc_end - 2; scan >= base; scan--) {
		if (value[scan - 2] == '/' && value[scan - 1] == '*' && value[scan] == '*') {
			doc_start = scan - 2;
			found_doc_start = true;
			break;
		}

		if (scan == base) {
			break;
		}
	}

	if (!found_doc_start || doc_end <= doc_start) {
		return NULL;
	}

	comment = zend_string_init(value + doc_start, doc_end - doc_start, 0);
	type = lsp_phpdoc_var_type_from_comment_ex(comment, normalize);
	zend_string_release(comment);

	return type;
}

static inline bool lsp_phpdoc_word_char(char c)
{
	return lsp_doc_is_identifier_char(c) || c == '$' || c == '\\';
}

static inline bool lsp_phpdoc_word_bounds(zend_string *text, zend_string *word, size_t offset, size_t *word_start, size_t *word_end)
{
	const char *value;
	size_t length, start, end;

	if (!text || !word || ZSTR_LEN(word) == 0) {
		return false;
	}

	value = ZSTR_VAL(text);
	length = ZSTR_LEN(text);
	if (offset > length) {
		offset = length;
	}

	start = offset;
	while (start > 0 && lsp_phpdoc_word_char(value[start - 1])) {
		start--;
	}

	end = offset;
	while (end < length && lsp_phpdoc_word_char(value[end])) {
		end++;
	}

	if (end <= start || end - start != ZSTR_LEN(word) || memcmp(value + start, ZSTR_VAL(word), ZSTR_LEN(word)) != 0) {
		return false;
	}

	*word_start = start;
	*word_end = end;

	return true;
}

static inline bool lsp_phpdoc_this_member_access_at(zend_string *text, zend_string *word, size_t offset)
{
	const char *value;
	size_t word_start, word_end, i, receiver_end, receiver_start;

	if (!word || ZSTR_LEN(word) == 0 || ZSTR_VAL(word)[0] == '$') {
		return false;
	}

	if (!lsp_phpdoc_word_bounds(text, word, offset, &word_start, &word_end)) {
		return false;
	}

	value = ZSTR_VAL(text);
	i = word_start;
	while (i > 0 && isspace((unsigned char) value[i - 1])) {
		i--;
	}

	if (i >= 3 && value[i - 3] == '?' && value[i - 2] == '-' && value[i - 1] == '>') {
		receiver_end = i - 3;
	} else if (i >= 2 && value[i - 2] == '-' && value[i - 1] == '>') {
		receiver_end = i - 2;
	} else {
		return false;
	}

	i = receiver_end;
	while (i > 0 && isspace((unsigned char) value[i - 1])) {
		i--;
	}
	receiver_end = i;

	while (i > 0 && (lsp_doc_is_identifier_char(value[i - 1]) || value[i - 1] == '$' || value[i - 1] == '\\')) {
		i--;
	}
	receiver_start = i;

	return receiver_end - receiver_start == sizeof("$this") - 1 &&
		memcmp(value + receiver_start, "$this", sizeof("$this") - 1) == 0
	;
}

static inline bool lsp_phpdoc_property_declaration_before_variable(zend_string *text, size_t variable_offset)
{
	const char *visibility[4] = {"public", "protected", "private", "var"};
	const char *value;
	size_t declaration_start, scan, token_start, token_end;
	uint32_t i;

	if (variable_offset > ZSTR_LEN(text)) {
		return false;
	}

	value = ZSTR_VAL(text);
	declaration_start = variable_offset;
	while (declaration_start > 0 &&
		value[declaration_start - 1] != ';' &&
		value[declaration_start - 1] != '{' &&
		value[declaration_start - 1] != '}'
	) {
		declaration_start--;
	}

	scan = declaration_start;
	while (scan < variable_offset) {
		if (!lsp_doc_is_identifier_start(value[scan])) {
			scan++;
			continue;
		}

		token_start = scan;
		scan++;
		while (scan < variable_offset && lsp_doc_is_identifier_char(value[scan])) {
			scan++;
		}
		token_end = scan;

		for (i = 0; i < sizeof(visibility) / sizeof(visibility[0]); i++) {
			if (token_end - token_start == strlen(visibility[i]) &&
				strncasecmp(value + token_start, visibility[i], token_end - token_start) == 0
			) {
				return true;
			}
		}
	}

	return false;
}

static inline zend_string *lsp_phpdoc_property_type_for_word_ex(zend_string *text, zend_string *word, size_t offset, bool normalize)
{
	const char *value, *target;
	zend_string *type;
	size_t class_start, body_start, body_end, search, target_length, variable_end, word_start, word_end;
	zend_long body_depth;
	bool has_dollar, require_same_offset;

	if (!text || !word || ZSTR_LEN(word) == 0) {
		return NULL;
	}

	value = ZSTR_VAL(text);
	has_dollar = ZSTR_VAL(word)[0] == '$';
	require_same_offset = has_dollar;
	word_start = 0;
	word_end = 0;
	target = ZSTR_VAL(word) + (has_dollar ? 1 : 0);
	target_length = ZSTR_LEN(word) - (has_dollar ? 1 : 0);
	if (target_length == 0) {
		return NULL;
	}

	if (has_dollar) {
		if (!lsp_phpdoc_word_bounds(text, word, offset, &word_start, &word_end)) {
			return NULL;
		}
	} else if (!lsp_phpdoc_this_member_access_at(text, word, offset)) {
		return NULL;
	}

	if (!lsp_find_enclosing_class_header(text, offset, &class_start, &body_start, &body_end, &body_depth)) {
		return NULL;
	}

	for (search = body_start; search + target_length < body_end; search++) {
		if (value[search] != '$') {
			continue;
		}

		variable_end = search + 1 + target_length;
		if (variable_end > body_end || strncmp(value + search + 1, target, target_length) != 0) {
			continue;
		}

		if (variable_end < ZSTR_LEN(text) && lsp_doc_is_identifier_char(value[variable_end])) {
			continue;
		}

		if (lsp_brace_depth_at(text, search) != body_depth) {
			continue;
		}

		if (require_same_offset && (word_start != search || word_end != variable_end)) {
			continue;
		}

		if (!lsp_phpdoc_property_declaration_before_variable(text, search)) {
			continue;
		}

		type = lsp_phpdoc_property_var_type_before(text, search, normalize);
		if (type) {
			return type;
		}
	}

	return NULL;
}

static inline zend_string *lsp_phpdoc_array_shape_from_type(zend_string *type)
{
	const char *value, *end, *cursor, *open;
	uint32_t depth;
	char quote;
	bool escaped;

	value = ZSTR_VAL(type);
	end = value + ZSTR_LEN(type);
	open = NULL;

	for (cursor = value; cursor + sizeof("array") - 1 <= end; cursor++) {
		if (strncasecmp(cursor, "array", sizeof("array") - 1) != 0) {
			continue;
		}

		open = cursor + sizeof("array") - 1;
		while (open < end && isspace((unsigned char) *open)) {
			open++;
		}

		if (open < end && *open == '{') {
			break;
		}

		open = NULL;
	}

	if (!open || open >= end || *open != '{') {
		return NULL;
	}

	depth = 1;
	quote = '\0';
	escaped = false;
	for (cursor = open + 1; cursor < end; cursor++) {
		if (quote != '\0') {
			if (escaped) {
				escaped = false;
			} else if (*cursor == '\\') {
				escaped = true;
			} else if (*cursor == quote) {
				quote = '\0';
			}
			continue;
		}

		if (*cursor == '\'' || *cursor == '"') {
			quote = *cursor;
			continue;
		}

		if (*cursor == '{') {
			depth++;
		} else if (*cursor == '}' && --depth == 0) {
			return zend_string_init(open + 1, cursor - open - 1, 0);
		}
	}

	return NULL;
}

static inline const char *lsp_phpdoc_shape_value_end(const char *value_start, const char *end)
{
	const char *cursor;
	uint32_t brace_depth, angle_depth, paren_depth, bracket_depth;
	char quote;
	bool escaped;

	cursor = value_start;
	brace_depth = 0;
	angle_depth = 0;
	paren_depth = 0;
	bracket_depth = 0;
	quote = '\0';
	escaped = false;

	while (cursor < end) {
		if (quote != '\0') {
			if (escaped) {
				escaped = false;
			} else if (*cursor == '\\') {
				escaped = true;
			} else if (*cursor == quote) {
				quote = '\0';
			}
			cursor++;
			continue;
		}

		if (*cursor == '\'' || *cursor == '"') {
			quote = *cursor;
			cursor++;
			continue;
		}

		if (*cursor == ',' && brace_depth == 0 && angle_depth == 0 && paren_depth == 0 && bracket_depth == 0) {
			break;
		}

		if (*cursor == '{') {
			brace_depth++;
		} else if (*cursor == '}' && brace_depth > 0) {
			brace_depth--;
		} else if (*cursor == '<') {
			angle_depth++;
		} else if (*cursor == '>' && angle_depth > 0) {
			angle_depth--;
		} else if (*cursor == '(') {
			paren_depth++;
		} else if (*cursor == ')' && paren_depth > 0) {
			paren_depth--;
		} else if (*cursor == '[') {
			bracket_depth++;
		} else if (*cursor == ']' && bracket_depth > 0) {
			bracket_depth--;
		}

		cursor++;
	}

	return cursor;
}

extern void lsp_phpdoc_add_variable_type_completions(lsp_server *server, zval *items, zend_string *text, zend_string *prefix)
{
	const char *cursor = ZSTR_VAL(text), *end = ZSTR_VAL(text) + ZSTR_LEN(text), *source = lsp_primary_analyzer_source(server);
	zend_string *variable, *type;

	while (lsp_doc_find_next_type(&cursor, end, &variable, &type)) {
		if (lsp_matches_prefix_string(variable, prefix)) {
			lsp_add_completion_item_ex(items, variable, 6, type, source);
		}

		zend_string_release(variable);
		zend_string_release(type);
	}
}

extern void lsp_phpdoc_add_variable_type_completion_edits(lsp_server *server, zval *items, zend_string *text, zend_string *prefix, size_t prefix_start, size_t offset)
{
	const char *cursor = ZSTR_VAL(text), *end = ZSTR_VAL(text) + ZSTR_LEN(text), *source = lsp_primary_analyzer_source(server);
	zend_string *variable, *type, *detail;

	while (lsp_doc_find_next_type(&cursor, end, &variable, &type)) {
		if (lsp_matches_prefix_string(variable, prefix)) {
			detail = strpprintf(0, "variable %s: %s", ZSTR_VAL(variable), ZSTR_VAL(type));
			lsp_add_variable_completion_item_ex(items, variable, detail, source, text, prefix_start, offset);
			zend_string_release(detail);
		}

		zend_string_release(variable);
		zend_string_release(type);
	}
}

extern zend_string *lsp_phpdoc_type_for_word(zend_string *text, zend_string *word)
{
	const char *word_value = ZSTR_VAL(word), *cursor = ZSTR_VAL(text), *end = ZSTR_VAL(text) + ZSTR_LEN(text);
	zend_string *variable, *type;
	size_t word_length = ZSTR_LEN(word);
	bool match;

	while (lsp_doc_find_next_type_ex(&cursor, end, &variable, &type, true)) {
		match = ZSTR_LEN(variable) == word_length && strncasecmp(ZSTR_VAL(variable), word_value, word_length) == 0;
		if (!match && word_length > 0 && word_value[0] != '$') {
			match = ZSTR_LEN(variable) == word_length + 1 && ZSTR_VAL(variable)[0] == '$' &&
				strncasecmp(ZSTR_VAL(variable) + 1, word_value, word_length) == 0
			;
		}

		zend_string_release(variable);

		if (match) {
			return type;
		}

		zend_string_release(type);
	}

	return NULL;
}

extern zend_string *lsp_phpdoc_type_for_word_raw(zend_string *text, zend_string *word)
{
	const char *word_value = ZSTR_VAL(word), *cursor = ZSTR_VAL(text), *end = ZSTR_VAL(text) + ZSTR_LEN(text);
	zend_string *variable, *type;
	size_t word_length = ZSTR_LEN(word);
	bool match;

	while (lsp_doc_find_next_type_ex(&cursor, end, &variable, &type, false)) {
		match = ZSTR_LEN(variable) == word_length && strncasecmp(ZSTR_VAL(variable), word_value, word_length) == 0;
		if (!match && word_length > 0 && word_value[0] != '$') {
			match = ZSTR_LEN(variable) == word_length + 1 && ZSTR_VAL(variable)[0] == '$' &&
				strncasecmp(ZSTR_VAL(variable) + 1, word_value, word_length) == 0
			;
		}

		zend_string_release(variable);

		if (match) {
			return type;
		}

		zend_string_release(type);
	}

	return NULL;
}

extern zend_string *lsp_phpdoc_property_type_for_word(zend_string *text, zend_string *word, size_t offset)
{
	return lsp_phpdoc_property_type_for_word_ex(text, word, offset, true);
}

extern zend_string *lsp_phpdoc_property_type_for_word_raw(zend_string *text, zend_string *word, size_t offset)
{
	return lsp_phpdoc_property_type_for_word_ex(text, word, offset, false);
}

static inline zend_string *lsp_phpdoc_return_type_from_comment_ex(zend_string *comment, bool normalize)
{
	const char *tags[] = {"@return", "@phpstan-return", "@psalm-return"};
	const char *cursor, *end, *p, *start, *finish, *match, *best;
	size_t i, tag_length;

	cursor = ZSTR_VAL(comment);
	end = cursor + ZSTR_LEN(comment);
	best = NULL;
	tag_length = 0;
	for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
		match = strstr(cursor, tags[i]);
		if (!match || match >= end) {
			continue;
		}
		if (!best || match < best) {
			best = match;
			tag_length = strlen(tags[i]);
		}
	}
	if (!best) {
		return NULL;
	}

	p = best + tag_length;
	while (p < end && isspace((unsigned char) *p)) {
		p++;
	}

	start = p;
	finish = lsp_phpdoc_type_expression_end(start, end);
	while (finish > start && isspace((unsigned char) finish[-1])) {
		finish--;
	}

	if (finish <= start) {
		return NULL;
	}

	return lsp_phpdoc_type_slice(start, finish, normalize);
}

extern zend_string *lsp_phpdoc_return_type_from_comment(zend_string *comment)
{
	return lsp_phpdoc_return_type_from_comment_ex(comment, true);
}

extern zend_string *lsp_phpdoc_return_type_from_comment_raw(zend_string *comment)
{
	return lsp_phpdoc_return_type_from_comment_ex(comment, false);
}

extern bool lsp_phpdoc_type_has_array_shape(zend_string *type)
{
	zend_string *shape;

	shape = lsp_phpdoc_array_shape_from_type(type);
	if (!shape) {
		return false;
	}

	zend_string_release(shape);

	return true;
}

extern zend_string *lsp_phpdoc_array_shape_key_type(zend_string *type, zend_string *key)
{
	const char *p, *end, *key_start, *key_end, *value_start, *value_end;
	zend_string *shape, *key_type;
	char quote;

	shape = lsp_phpdoc_array_shape_from_type(type);
	if (!shape) {
		return NULL;
	}

	p = ZSTR_VAL(shape);
	end = p + ZSTR_LEN(shape);
	while (p < end) {
		while (p < end && (isspace((unsigned char) *p) || *p == ',')) {
			p++;
		}

		if (p >= end) {
			break;
		}

		if (*p == '\'' || *p == '"') {
			quote = *p++;
			key_start = p;
			while (p < end && *p != quote) {
				p++;
			}
			key_end = p;
			if (p < end) {
				p++;
			}
		} else {
			key_start = p;
			while (p < end && !isspace((unsigned char) *p) && *p != ':' && *p != ',' && *p != '?') {
				p++;
			}
			key_end = p;
		}

		while (p < end && isspace((unsigned char) *p)) {
			p++;
		}

		if (p < end && *p == '?') {
			p++;
			while (p < end && isspace((unsigned char) *p)) {
				p++;
			}
		}

		if (p >= end || *p != ':') {
			p = lsp_phpdoc_shape_value_end(p, end);
			if (p < end && *p == ',') {
				p++;
			}
			continue;
		}

		p++;
		value_start = p;
		value_end = lsp_phpdoc_shape_value_end(value_start, end);
		if (
			key_end > key_start &&
			ZSTR_LEN(key) == (size_t) (key_end - key_start) &&
			memcmp(ZSTR_VAL(key), key_start, ZSTR_LEN(key)) == 0
		) {
			key_type = lsp_phpdoc_type_slice(value_start, value_end, true);
			zend_string_release(shape);

			return key_type;
		}

		p = value_end;
		if (p < end && *p == ',') {
			p++;
		}
	}

	zend_string_release(shape);

	return NULL;
}

extern void lsp_phpdoc_add_template_completions(lsp_server *server, zval *items, zend_string *text, zend_string *prefix)
{
	const char *cursor = ZSTR_VAL(text), *end = ZSTR_VAL(text) + ZSTR_LEN(text), *source = lsp_primary_analyzer_source(server), *p, *start, *finish;
	zend_string *label, *detail;

	while ((cursor = strstr(cursor, "@template")) != NULL && cursor < end) {
		p = cursor + sizeof("@template") - 1;

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
		finish = p;

		label = zend_string_init(start, finish - start, 0);
		if (lsp_matches_prefix_string(label, prefix)) {
			detail = zend_string_init("template type", sizeof("template type") - 1, 0);
			lsp_add_completion_item_ex(items, label, 25, detail, source);
			zend_string_release(detail);
		}

		zend_string_release(label);
		cursor = p;
	}
}

static inline bool lsp_array_access_at(zend_string *text, size_t offset, zend_string **variable, zend_string **key_prefix, size_t *edit_start, size_t *edit_end, char *quote, bool *has_opening_quote)
{
	const char *value;
	size_t i, j, key_start, bracket, var_end, var_start;
	char opening_quote;

	*variable = NULL;
	*key_prefix = NULL;
	*edit_start = 0;
	*edit_end = offset;
	*quote = '\'';
	*has_opening_quote = false;

	if (offset > ZSTR_LEN(text)) {
		offset = ZSTR_LEN(text);
	}

	value = ZSTR_VAL(text);
	i = offset;
	while (i > 0 && (isalnum((unsigned char) value[i - 1]) || value[i - 1] == '_' || value[i - 1] == '-')) {
		i--;
	}

	key_start = i;
	opening_quote = '\0';
	if (i > 0 && (value[i - 1] == '\'' || value[i - 1] == '"')) {
		opening_quote = value[i - 1];
		i--;
	}

	while (i > 0 && isspace((unsigned char) value[i - 1])) {
		i--;
	}

	if (i == 0 || value[i - 1] != '[') {
		return false;
	}
	bracket = i - 1;

	i = bracket;
	while (i > 0 && isspace((unsigned char) value[i - 1])) {
		i--;
	}

	var_end = i;
	while (i > 0 && lsp_doc_is_identifier_char(value[i - 1])) {
		i--;
	}

	if (i == 0 || value[i - 1] != '$') {
		return false;
	}

	var_start = i - 1;

	*variable = zend_string_init(value + var_start, var_end - var_start, 0);
	*key_prefix = zend_string_init(value + key_start, offset - key_start, 0);
	*edit_start = key_start;
	*quote = opening_quote ? opening_quote : '\'';
	*has_opening_quote = opening_quote != '\0';

	j = offset;
	if (opening_quote) {
		if (j < ZSTR_LEN(text) && value[j] == opening_quote) {
			j++;
			while (j < ZSTR_LEN(text) && isspace((unsigned char) value[j])) {
				j++;
			}
			if (j < ZSTR_LEN(text) && value[j] == ']') {
				j++;
			}
		}
	} else {
		while (j < ZSTR_LEN(text) && isspace((unsigned char) value[j])) {
			j++;
		}
		if (j < ZSTR_LEN(text) && value[j] == ']') {
			j++;
		}
	}

	*edit_end = j;

	return true;
}

static inline zend_string *lsp_array_shape_key_new_text(zend_string *label, char quote, bool has_opening_quote)
{
	smart_str text = {0};
	size_t i;
	char c;

	if (!has_opening_quote) {
		smart_str_appendc(&text, quote);
	}

	for (i = 0; i < ZSTR_LEN(label); i++) {
		c = ZSTR_VAL(label)[i];
		if (c == quote || c == '\\') {
			smart_str_appendc(&text, '\\');
		}
		smart_str_appendc(&text, c);
	}

	smart_str_appendc(&text, quote);
	smart_str_appendc(&text, ']');
	smart_str_0(&text);

	return text.s;
}

static inline void lsp_add_array_shape_key_completion_item(zval *items, zend_string *text, size_t edit_start, size_t edit_end, zend_string *label, zend_string *detail, const char *source, char quote, bool has_opening_quote)
{
	zend_string *new_text;
	zval item, data, text_edit, range;

	new_text = lsp_array_shape_key_new_text(label, quote, has_opening_quote);

	array_init(&item);
	add_assoc_str(&item, "label", zend_string_copy(label));
	add_assoc_long(&item, "kind", 10);
	add_assoc_str(&item, "detail", zend_string_copy(detail));
	add_assoc_str(&item, "filterText", zend_string_copy(label));
	array_init(&text_edit);
	lsp_range_from_offsets(text, edit_start, edit_end, &range);
	add_assoc_zval(&text_edit, "range", &range);
	add_assoc_str(&text_edit, "newText", new_text);
	add_assoc_zval(&item, "textEdit", &text_edit);
	array_init(&data);
	add_assoc_string(&data, "source", source);
	add_assoc_zval(&item, "data", &data);
	add_next_index_zval(items, &item);
}

static inline bool lsp_phpdoc_shape_for_variable(zend_string *text, zend_string *variable, zend_string **shape)
{
	const char *cursor = ZSTR_VAL(text), *end = ZSTR_VAL(text) + ZSTR_LEN(text);
	zend_string *doc_variable, *type, *extracted_shape;

	*shape = NULL;

	while (lsp_doc_find_next_type(&cursor, end, &doc_variable, &type)) {
		if (zend_string_equals(doc_variable, variable)) {
			extracted_shape = lsp_phpdoc_array_shape_from_type(type);
			if (extracted_shape) {
				*shape = extracted_shape;
				zend_string_release(doc_variable);
				zend_string_release(type);

				return true;
			}
		}

		zend_string_release(doc_variable);
		zend_string_release(type);
	}

	return false;
}

extern bool lsp_phpdoc_add_array_shape_completions(lsp_server *server, zval *items, lsp_document *document, size_t offset)
{
	const char *source = lsp_primary_analyzer_source(server), *p, *end, *key_start, *key_end, *value_start, *value_end;
	zend_string *variable, *key_prefix, *shape, *label, *detail, *inferred_type;
	size_t edit_start, edit_end;
	char quote, access_quote;
	bool has_opening_quote;

	if (!lsp_array_access_at(document->text, offset, &variable, &key_prefix, &edit_start, &edit_end, &access_quote, &has_opening_quote)) {
		return false;
	}

	shape = NULL;
	inferred_type = NULL;
	if (!lsp_phpdoc_shape_for_variable(document->text, variable, &shape)) {
		inferred_type = lsp_infer_variable_type(server, document, variable, offset);
		if (inferred_type) {
			shape = lsp_phpdoc_array_shape_from_type(inferred_type);
			zend_string_release(inferred_type);
		}
	}

	if (!shape) {
		zend_string_release(variable);
		zend_string_release(key_prefix);
		return false;
	}

	p = ZSTR_VAL(shape);
	end = p + ZSTR_LEN(shape);
	while (p < end) {
		while (p < end && (isspace((unsigned char) *p) || *p == ',')) {
			p++;
		}

		if (p >= end) {
			break;
		}

		if (*p == '\'' || *p == '"') {
			quote = *p++;

			key_start = p;
			while (p < end && *p != quote) {
				p++;
			}
			key_end = p;

			if (p < end) {
				p++;
			}
		} else {
			key_start = p;
			while (p < end && !isspace((unsigned char) *p) && *p != ':' && *p != ',' && *p != '?') {
				p++;
			}
			key_end = p;
		}

		while (p < end && isspace((unsigned char) *p)) {
			p++;
		}

		if (p < end && *p == '?') {
			p++;
			while (p < end && isspace((unsigned char) *p)) {
				p++;
			}
		}

		if (p >= end || *p != ':') {
			p = lsp_phpdoc_shape_value_end(p, end);
			if (p < end && *p == ',') {
				p++;
			}
			continue;
		}

		if (p < end && *p == ':') {
			p++;
		}

		value_start = p;
		value_end = lsp_phpdoc_shape_value_end(value_start, end);
		if (key_end <= key_start) {
			p = value_end;
			if (p < end && *p == ',') {
				p++;
			}
			continue;
		}

		label = zend_string_init(key_start, key_end - key_start, 0);
		if (lsp_matches_prefix_string(label, key_prefix)) {
			detail = zend_string_init("array-shape key", sizeof("array-shape key") - 1, 0);
			lsp_add_array_shape_key_completion_item(items, document->text, edit_start, edit_end, label, detail, source, access_quote, has_opening_quote);
			zend_string_release(detail);
		}

		zend_string_release(label);
		p = value_end;
		if (p < end && *p == ',') {
			p++;
		}
	}

	zend_string_release(shape);
	zend_string_release(variable);
	zend_string_release(key_prefix);

	return true;
}

extern bool lsp_member_access_arrow(zend_string *text, size_t offset, zend_string *prefix, size_t *arrow_start, size_t *receiver_end)
{
	const char *value = ZSTR_VAL(text);
	size_t i, start;

	if (offset > ZSTR_LEN(text)) {
		offset = ZSTR_LEN(text);
	}

	i = offset >= ZSTR_LEN(prefix) ? offset - ZSTR_LEN(prefix) : 0;
	while (i > 0 && isspace((unsigned char) value[i - 1])) {
		i--;
	}

	if (i >= 2 && value[i - 2] == '-' && value[i - 1] == '>') {
		start = i - 2;
	} else if (i >= 3 && value[i - 3] == '?' && value[i - 2] == '-' && value[i - 1] == '>') {
		start = i - 3;
	} else {
		return false;
	}

	if (arrow_start) {
		*arrow_start = start;
	}

	i = start;
	while (i > 0 && isspace((unsigned char) value[i - 1])) {
		i--;
	}

	*receiver_end = i;

	return true;
}

extern bool lsp_member_access_context(zend_string *text, size_t offset, zend_string *prefix, zend_string **receiver, zend_string **member_prefix)
{
	const char *value = ZSTR_VAL(text);
	size_t i, receiver_end, receiver_start;

	*receiver = NULL;
	*member_prefix = NULL;

	if (!lsp_member_access_arrow(text, offset, prefix, NULL, &receiver_end)) {
		return false;
	}

	i = receiver_end;
	while (i > 0 && lsp_doc_is_identifier_char(value[i - 1])) {
		i--;
	}

	if (i == 0 || value[i - 1] != '$') {
		return false;
	}

	receiver_start = i - 1;

	*receiver = zend_string_init(value + receiver_start, receiver_end - receiver_start, 0);
	*member_prefix = zend_string_copy(prefix);

	return true;
}

extern bool lsp_find_matching_open_paren(zend_string *text, size_t close_offset, size_t *open_offset)
{
	const char *value = ZSTR_VAL(text);
	size_t i, depth = 0;

	if (close_offset >= ZSTR_LEN(text) || value[close_offset] != ')') {
		return false;
	}

	for (i = close_offset + 1; i > 0; i--) {
		if (value[i - 1] == ')') {
			depth++;
		} else if (value[i - 1] == '(') {
			if (depth == 0) {
				return false;
			}

			depth--;

			if (depth == 0) {
				*open_offset = i - 1;

				return true;
			}
		}
	}

	return false;
}

extern bool lsp_find_matching_open_bracket(zend_string *text, size_t close_offset, size_t *open_offset)
{
	const char *value = ZSTR_VAL(text);
	size_t i, depth = 0;

	if (close_offset >= ZSTR_LEN(text) || value[close_offset] != ']') {
		return false;
	}

	for (i = close_offset + 1; i > 0; i--) {
		if (value[i - 1] == ']') {
			depth++;
		} else if (value[i - 1] == '[') {
			if (depth == 0) {
				return false;
			}

			depth--;

			if (depth == 0) {
				*open_offset = i - 1;

				return true;
			}
		}
	}

	return false;
}

extern bool lsp_parse_method_call_before_offset(zend_string *text, size_t offset, zend_string **method_name, size_t *receiver_end)
{
	const char *value = ZSTR_VAL(text);
	size_t i, close_offset, open_offset, method_start, method_end;

	*method_name = NULL;
	*receiver_end = 0;

	if (offset > ZSTR_LEN(text)) {
		offset = ZSTR_LEN(text);
	}

	i = offset;
	while (i > 0 && isspace((unsigned char) value[i - 1])) {
		i--;
	}

	if (i == 0 || value[i - 1] != ')') {
		return false;
	}

	close_offset = i - 1;
	if (!lsp_find_matching_open_paren(text, close_offset, &open_offset)) {
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

	if (i >= 2 && value[i - 2] == '-' && value[i - 1] == '>') {
		i -= 2;
	} else if (i >= 3 && value[i - 3] == '?' && value[i - 2] == '-' && value[i - 1] == '>') {
		i -= 3;
	} else {
		return false;
	}

	while (i > 0 && isspace((unsigned char) value[i - 1])) {
		i--;
	}

	*method_name = zend_string_init(value + method_start, method_end - method_start, 0);
	*receiver_end = i;

	return true;
}

extern bool lsp_parse_this_method_call_ending_at(zend_string *text, size_t offset, zend_string **method_name)
{
	const char *value = ZSTR_VAL(text);
	zend_string *parsed_method;
	size_t receiver_end, i, receiver_start;

	*method_name = NULL;

	if (!lsp_parse_method_call_before_offset(text, offset, &parsed_method, &receiver_end)) {
		return false;
	}

	i = receiver_end;
	while (i > 0 && lsp_doc_is_identifier_char(value[i - 1])) {
		i--;
	}

	if (i == 0 || value[i - 1] != '$') {
		zend_string_release(parsed_method);

		return false;
	}

	receiver_start = i - 1;
	if (receiver_end - receiver_start != sizeof("$this") - 1 ||
		memcmp(value + receiver_start, "$this", sizeof("$this") - 1) != 0
	) {
		zend_string_release(parsed_method);

		return false;
	}

	*method_name = parsed_method;

	return true;
}

extern bool lsp_this_method_call_member_access_context(zend_string *text, size_t offset, zend_string *prefix, zend_string **method_name, zend_string **member_prefix)
{
	const char *value = ZSTR_VAL(text);
	size_t receiver_end, close_offset, open_offset, i, method_start, method_end, call_receiver_start, call_receiver_end;

	*method_name = NULL;
	*member_prefix = NULL;

	if (!lsp_member_access_arrow(text, offset, prefix, NULL, &receiver_end)) {
		return false;
	}

	i = receiver_end;
	while (i > 0 && isspace((unsigned char) value[i - 1])) {
		i--;
	}

	if (i == 0 || value[i - 1] != ')') {
		return false;
	}

	close_offset = i - 1;
	if (!lsp_find_matching_open_paren(text, close_offset, &open_offset)) {
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

	if (i >= 2 && value[i - 2] == '-' && value[i - 1] == '>') {
		i -= 2;
	} else if (i >= 3 && value[i - 3] == '?' && value[i - 2] == '-' && value[i - 1] == '>') {
		i -= 3;
	} else {
		return false;
	}

	while (i > 0 && isspace((unsigned char) value[i - 1])) {
		i--;
	}

	call_receiver_end = i;

	while (i > 0 && lsp_doc_is_identifier_char(value[i - 1])) {
		i--;
	}

	if (i == 0 || value[i - 1] != '$') {
		return false;
	}

	call_receiver_start = i - 1;
	if (call_receiver_end - call_receiver_start != sizeof("$this") - 1 ||
		memcmp(value + call_receiver_start, "$this", sizeof("$this") - 1) != 0
	) {
		return false;
	}

	*method_name = zend_string_init(value + method_start, method_end - method_start, 0);
	*member_prefix = zend_string_copy(prefix);

	return true;
}

extern zend_string *lsp_infer_new_assignment_class(zend_string *text, zend_string *receiver, size_t offset)
{
	const char *value = ZSTR_VAL(text), *end = value + (offset > ZSTR_LEN(text) ? ZSTR_LEN(text) : offset),
		*p = value, *match, *q, *class_start, *class_end
	;
	zend_string *class_name = NULL;

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

		if (q + 3 > end || strncasecmp(q, "new", 3) != 0 || (q + 3 < end && lsp_doc_is_identifier_char(q[3]))) {
			p = match + 1;
			continue;
		}

		q += 3;
		while (q < end && isspace((unsigned char) *q)) {
			q++;
		}

		class_start = q;
		while (q < end && (lsp_doc_is_identifier_char(*q) || *q == '\\')) {
			q++;
		}

		class_end = q;
		if (class_end > class_start) {
			if (class_name) {
				zend_string_release(class_name);
			}
			class_name = zend_string_init(class_start, class_end - class_start, 0);
		}
		p = q;
	}

	return class_name;
}

extern size_t lsp_current_statement_scan_limit(zend_string *text, size_t offset)
{
	const char *value = ZSTR_VAL(text);
	size_t limit, length = ZSTR_LEN(text);

	if (offset > length) {
		offset = length;
	}

	limit = offset;
	while (limit < length && value[limit] != ';' && value[limit] != '\n' && value[limit] != '{' && value[limit] != '}') {
		limit++;
	}

	return limit;
}

extern zend_string *lsp_type_generic_argument(zend_string *type, uint32_t target_index)
{
	const char *value = ZSTR_VAL(type), *end = value + ZSTR_LEN(type), *open, *p, *arg_start;
	uint32_t depth = 0, index = 0;

	open = memchr(value, '<', ZSTR_LEN(type));
	if (!open) {
		return NULL;
	}

	arg_start = open + 1;
	for (p = open + 1; p < end; p++) {
		if (*p == '<') {
			depth++;
		} else if (*p == '>') {
			if (depth == 0) {
				return index == target_index ? lsp_string_trim_slice(arg_start, p) : NULL;
			}
			depth--;
		} else if (*p == ',' && depth == 0) {
			if (index == target_index) {
				return lsp_string_trim_slice(arg_start, p);
			}
			index++;
			arg_start = p + 1;
		}
	}

	return NULL;
}

static inline bool lsp_type_name_equals_literal(const char *start, const char *end, const char *literal)
{
	size_t literal_length = strlen(literal);

	while (start < end && isspace((unsigned char) *start)) {
		start++;
	}

	while (end > start && isspace((unsigned char) end[-1])) {
		end--;
	}

	return (size_t) (end - start) == literal_length &&
		strncasecmp(start, literal, literal_length) == 0
	;
}

extern zend_string *lsp_type_array_element_type(zend_string *type)
{
	const char *value = ZSTR_VAL(type), *end = value + ZSTR_LEN(type), *open;
	zend_string *argument;

	while (end > value && isspace((unsigned char) end[-1])) {
		end--;
	}
	if (end - value > 2 && end[-1] == ']' && end[-2] == ']') {
		return lsp_string_trim_slice(value, end - 2);
	}

	open = memchr(value, '<', end - value);
	if (!open) {
		return NULL;
	}

	if (lsp_type_name_equals_literal(value, open, "array") ||
		lsp_type_name_equals_literal(value, open, "non-empty-array")
	) {
		argument = lsp_type_generic_argument(type, 1);
		if (argument) {
			return argument;
		}

		return lsp_type_generic_argument(type, 0);
	}

	if (lsp_type_name_equals_literal(value, open, "list") ||
		lsp_type_name_equals_literal(value, open, "non-empty-list")
	) {
		return lsp_type_generic_argument(type, 0);
	}

	return NULL;
}

extern bool lsp_type_equals(zend_string *left, zend_string *right)
{
	return ZSTR_LEN(left) == ZSTR_LEN(right) &&
		strncasecmp(ZSTR_VAL(left), ZSTR_VAL(right), ZSTR_LEN(left)) == 0
	;
}

static inline bool lsp_type_is_builtin(zend_string *type)
{
	const char *value = ZSTR_VAL(type);
	size_t length = ZSTR_LEN(type);

	return (length == sizeof("array") - 1 && strncasecmp(value, "array", length) == 0) ||
		(length == sizeof("callable") - 1 && strncasecmp(value, "callable", length) == 0) ||
		(length == sizeof("bool") - 1 && strncasecmp(value, "bool", length) == 0) ||
		(length == sizeof("boolean") - 1 && strncasecmp(value, "boolean", length) == 0) ||
		(length == sizeof("float") - 1 && strncasecmp(value, "float", length) == 0) ||
		(length == sizeof("double") - 1 && strncasecmp(value, "double", length) == 0) ||
		(length == sizeof("int") - 1 && strncasecmp(value, "int", length) == 0) ||
		(length == sizeof("integer") - 1 && strncasecmp(value, "integer", length) == 0) ||
		(length == sizeof("iterable") - 1 && strncasecmp(value, "iterable", length) == 0) ||
		(length == sizeof("mixed") - 1 && strncasecmp(value, "mixed", length) == 0) ||
		(length == sizeof("never") - 1 && strncasecmp(value, "never", length) == 0) ||
		(length == sizeof("null") - 1 && strncasecmp(value, "null", length) == 0) ||
		(length == sizeof("object") - 1 && strncasecmp(value, "object", length) == 0) ||
		(length == sizeof("resource") - 1 && strncasecmp(value, "resource", length) == 0) ||
		(length == sizeof("self") - 1 && strncasecmp(value, "self", length) == 0) ||
		(length == sizeof("static") - 1 && strncasecmp(value, "static", length) == 0) ||
		(length == sizeof("string") - 1 && strncasecmp(value, "string", length) == 0) ||
		(length == sizeof("void") - 1 && strncasecmp(value, "void", length) == 0)
	;
}

static inline bool lsp_import_alias_matches(const char *alias_start, const char *alias_end, const char *type_start, size_t type_length)
{
	while (alias_start < alias_end && isspace((unsigned char) *alias_start)) {
		alias_start++;
	}

	while (alias_end > alias_start && isspace((unsigned char) alias_end[-1])) {
		alias_end--;
	}

	return (size_t) (alias_end - alias_start) == type_length &&
		strncasecmp(alias_start, type_start, type_length) == 0
	;
}

static inline const char *lsp_use_statement_alias_start(const char *use_start, const char *use_end)
{
	const char *p;

	for (p = use_start; p + sizeof(" as ") - 1 <= use_end; p++) {
		if (strncasecmp(p, " as ", sizeof(" as ") - 1) == 0) {
			return p + sizeof(" as ") - 1;
		}
	}

	for (p = use_end; p > use_start; p--) {
		if (p[-1] == '\\') {
			return p;
		}
	}

	return use_start;
}

static inline zend_string *lsp_resolve_imported_class_name(zend_string *text, zend_string *type)
{
	const char *value = ZSTR_VAL(text), *end = value + ZSTR_LEN(text), *cursor = value,
		*type_value = ZSTR_VAL(type), *type_segment_end, *statement_end, *use_start, *use_end,
		*alias_start, *alias_keyword, *import_end, *p
	;
	zend_string *resolved;
	size_t type_segment_length, import_length;

	type_segment_end = memchr(type_value, '\\', ZSTR_LEN(type));
	type_segment_length = type_segment_end ? (size_t) (type_segment_end - type_value) : ZSTR_LEN(type);
	while ((cursor = strstr(cursor, "use")) != NULL && cursor < end) {
		if (cursor > value && lsp_doc_is_identifier_char(cursor[-1])) {
			cursor++;

			continue;
		}
		if (cursor + sizeof("use") - 1 < end && lsp_doc_is_identifier_char(cursor[sizeof("use") - 1])) {
			cursor++;

			continue;
		}

		use_start = cursor + sizeof("use") - 1;
		while (use_start < end && isspace((unsigned char) *use_start)) {
			use_start++;
		}

		if ((use_start + sizeof("function") - 1 < end && strncasecmp(use_start, "function", sizeof("function") - 1) == 0) ||
			(use_start + sizeof("const") - 1 < end && strncasecmp(use_start, "const", sizeof("const") - 1) == 0)
		) {
			cursor = use_start;
			continue;
		}

		statement_end = memchr(use_start, ';', end - use_start);
		if (!statement_end) {
			return NULL;
		}

		use_end = statement_end;
		while (use_end > use_start && isspace((unsigned char) use_end[-1])) {
			use_end--;
		}

		alias_keyword = NULL;
		for (p = use_start; p + sizeof(" as ") - 1 <= use_end; p++) {
			if (strncasecmp(p, " as ", sizeof(" as ") - 1) == 0) {
				alias_keyword = p;
				break;
			}
		}

		alias_start = lsp_use_statement_alias_start(use_start, use_end);
		if (lsp_import_alias_matches(alias_start, use_end, type_value, type_segment_length)) {
			import_end = alias_keyword ? alias_keyword : use_end;
			while (import_end > use_start && isspace((unsigned char) import_end[-1])) {
				import_end--;
			}

			import_length = import_end - use_start;
			resolved = zend_string_alloc(import_length + (type_segment_end ? ZSTR_LEN(type) - type_segment_length : 0), 0);
			memcpy(ZSTR_VAL(resolved), use_start, import_length);
			if (type_segment_end) {
				memcpy(ZSTR_VAL(resolved) + import_length, type_segment_end, ZSTR_LEN(type) - type_segment_length);
			}

			ZSTR_VAL(resolved)[ZSTR_LEN(resolved)] = '\0';

			return resolved;
		}

		cursor = statement_end + 1;
	}

	return NULL;
}

extern zend_string *lsp_resolve_class_name(zend_string *text, zend_string *type)
{
	zend_string *base = lsp_type_class_name(type), *resolved, *namespace_name;

	if (!base || lsp_type_is_builtin(base)) {
		if (base) {
			zend_string_release(base);
		}

		return NULL;
	}

	if (zend_lookup_class(base)) {
		return base;
	}

	resolved = lsp_resolve_imported_class_name(text, base);
	if (resolved) {
		zend_string_release(base);

		return resolved;
	}

	namespace_name = lsp_document_namespace(text);
	if (namespace_name != zend_empty_string) {
		resolved = strpprintf(0, "%s\\%s", ZSTR_VAL(namespace_name), ZSTR_VAL(base));
		zend_string_release(namespace_name);
		zend_string_release(base);

		return resolved;
	}

	return base;
}

extern const char *lsp_primary_analyzer_source(lsp_server *server)
{
	if (server->phpstan_enabled) {
		return "phpstan";
	}

	if (server->psalm_enabled) {
		return "psalm";
	}

	if (server->psalm_ls_enabled) {
		return "psalm-ls";
	}

	return "lsparrot";
}

extern bool lsp_doc_is_identifier_start(char c)
{
	return isalpha((unsigned char) c) || c == '_';
}

extern bool lsp_doc_is_identifier_char(char c)
{
	return isalnum((unsigned char) c) || c == '_';
}

extern char lsp_type_constraint_completion_kind(zend_string *text, size_t offset, zend_string *prefix)
{
	const char *value = ZSTR_VAL(text), *cursor, *end, *token_start;
	size_t prefix_start, start, token_length;
	char filter_kind = '\0';

	if (offset > ZSTR_LEN(text)) {
		offset = ZSTR_LEN(text);
	}

	prefix_start = offset >= ZSTR_LEN(prefix) ? offset - ZSTR_LEN(prefix) : 0;
	start = prefix_start;
	while (start > 0 && value[start - 1] != '\n' && value[start - 1] != ';' && value[start - 1] != '{' && value[start - 1] != '(' && value[start - 1] != ')') {
		start--;
	}

	cursor = value + start;
	end = value + prefix_start;
	while (cursor < end) {
		while (cursor < end && !lsp_doc_is_identifier_start(*cursor)) {
			cursor++;
		}

		if (cursor >= end) {
			break;
		}

		token_start = cursor++;
		while (cursor < end && lsp_doc_is_identifier_char(*cursor)) {
			cursor++;
		}

		token_length = cursor - token_start;
		if ((token_length == sizeof("extends") - 1 && strncasecmp(token_start, "extends", token_length) == 0) ||
			(token_length == sizeof("implements") - 1 && strncasecmp(token_start, "implements", token_length) == 0)
		) {
			filter_kind = token_length == sizeof("extends") - 1 ? LSP_SYMBOL_CLASS : LSP_SYMBOL_INTERFACE;
		}
	}

	return filter_kind;
}
