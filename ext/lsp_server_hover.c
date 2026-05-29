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

static inline zend_string *lsp_hover_generic_base_type(zend_string *type)
{
	const char *generic;
	size_t length;

	generic = memchr(ZSTR_VAL(type), '<', ZSTR_LEN(type));
	if (!generic) {
		return NULL;
	}

	length = (size_t) (generic - ZSTR_VAL(type));
	while (length > 0 && isspace((unsigned char) ZSTR_VAL(type)[length - 1])) {
		length--;
	}

	if (length == 0) {
		return NULL;
	}

	return zend_string_init(ZSTR_VAL(type), length, 0);
}

static inline void lsp_hover_add_type_source(zend_string **types, smart_str *sources, uint32_t *group_count, zend_string *type, const char *source)
{
	uint32_t i;

	if (!type || ZSTR_LEN(type) == 0) {
		return;
	}

	for (i = 0; i < *group_count; i++) {
		if (!zend_string_equals(types[i], type)) {
			continue;
		}

		if (sources[i].s && ZSTR_LEN(sources[i].s) > 0) {
			smart_str_appendl(&sources[i], " / ", sizeof(" / ") - 1);
		}

		smart_str_appends(&sources[i], source);

		return;
	}

	if (*group_count >= 4) {
		return;
	}

	types[*group_count] = zend_string_copy(type);
	smart_str_appends(&sources[*group_count], source);
	(*group_count)++;
}

static inline void lsp_hover_release_type_groups(zend_string **types, smart_str *sources, uint32_t group_count)
{
	uint32_t i;

	for (i = 0; i < group_count; i++) {
		if (types[i]) {
			zend_string_release(types[i]);
		}

		smart_str_free(&sources[i]);
	}
}

static inline void lsp_hover_append_type_groups(smart_str *markdown, zend_string **types, smart_str *sources, uint32_t group_count)
{
	uint32_t i;

	for (i = 0; i < group_count; i++) {
		if (i > 0) {
			smart_str_appendl(markdown, "\n\n", sizeof("\n\n") - 1);
		}

		smart_str_appendc(markdown, '`');
		smart_str_append(markdown, types[i]);
		smart_str_appendl(markdown, "`\n\n", sizeof("`\n\n") - 1);
		smart_str_0(&sources[i]);
		smart_str_append(markdown, sources[i].s);
	}
}

static inline bool lsp_hover_is_word_char(char c)
{
	return lsp_doc_is_identifier_char(c) || c == '$' || c == '\\';
}

static inline bool lsp_hover_word_is_variable(zend_string *word)
{
	return word && ZSTR_LEN(word) > 1 && ZSTR_VAL(word)[0] == '$' && lsp_doc_is_identifier_start(ZSTR_VAL(word)[1]);
}

static inline bool lsp_hover_word_bounds(zend_string *text, zend_string *word, size_t offset, size_t *word_start, size_t *word_end)
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
	while (start > 0 && lsp_hover_is_word_char(value[start - 1])) {
		start--;
	}

	end = offset;
	while (end < length && lsp_hover_is_word_char(value[end])) {
		end++;
	}

	if (end <= start || end - start != ZSTR_LEN(word) || memcmp(value + start, ZSTR_VAL(word), ZSTR_LEN(word)) != 0) {
		return false;
	}

	*word_start = start;
	*word_end = end;

	return true;
}

static inline zend_string *lsp_hover_assignment_rhs_expression_at(zend_string *text, size_t offset, size_t target_start, size_t target_end)
{
	const char *value;
	size_t length, line_start, line_end, i, rhs_start, rhs_end;

	if (!text) {
		return NULL;
	}

	value = ZSTR_VAL(text);
	length = ZSTR_LEN(text);
	if (offset > length) {
		offset = length;
	}

	line_start = offset;
	while (line_start > 0 && value[line_start - 1] != '\n' && value[line_start - 1] != '\r') {
		line_start--;
	}

	line_end = offset;
	while (line_end < length && value[line_end] != '\n' && value[line_end] != '\r') {
		line_end++;
	}

	if (target_start < line_start || target_end > line_end || target_end <= target_start) {
		return NULL;
	}

	i = target_end;
	while (i < line_end && isspace((unsigned char) value[i])) {
		i++;
	}
	if (i >= line_end || value[i] != '=') {
		return NULL;
	}
	if ((i + 1 < line_end && (value[i + 1] == '=' || value[i + 1] == '>')) || (i > line_start && (value[i - 1] == '!' || value[i - 1] == '<' || value[i - 1] == '>'))) {
		return NULL;
	}

	rhs_start = i + 1;
	while (rhs_start < line_end && isspace((unsigned char) value[rhs_start])) {
		rhs_start++;
	}

	rhs_end = line_end;
	while (rhs_end > rhs_start && isspace((unsigned char) value[rhs_end - 1])) {
		rhs_end--;
	}
	if (rhs_end > rhs_start && value[rhs_end - 1] == ';') {
		rhs_end--;
		while (rhs_end > rhs_start && isspace((unsigned char) value[rhs_end - 1])) {
			rhs_end--;
		}
	}

	if (rhs_end <= rhs_start) {
		return NULL;
	}

	return zend_string_init(value + rhs_start, rhs_end - rhs_start, 0);
}

static inline zend_string *lsp_hover_assignment_rhs_expression(zend_string *text, zend_string *word, size_t offset)
{
	size_t word_start, word_end;

	if (!lsp_hover_word_bounds(text, word, offset, &word_start, &word_end)) {
		return NULL;
	}

	return lsp_hover_assignment_rhs_expression_at(text, offset, word_start, word_end);
}

static inline zend_string *lsp_hover_member_access_expression(zend_string *text, zend_string *word, size_t offset, size_t *expression_start, size_t *expression_end)
{
	const char *value;
	size_t word_start, word_end, i, receiver_end, start, depth;

	if (!lsp_hover_word_bounds(text, word, offset, &word_start, &word_end)) {
		return NULL;
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
		return NULL;
	}

	start = receiver_end;
	depth = 0;
	for (i = receiver_end; i > 0; i--) {
		if (value[i - 1] == ')' || value[i - 1] == ']' || value[i - 1] == '}') {
			depth++;
			continue;
		}

		if (value[i - 1] == '(' || value[i - 1] == '[' || value[i - 1] == '{') {
			if (depth > 0) {
				depth--;
				continue;
			}

			break;
		}

		if (depth == 0 && (value[i - 1] == '\n' || value[i - 1] == '\r' || value[i - 1] == ';' || value[i - 1] == '=' || value[i - 1] == ',')) {
			break;
		}
	}

	start = i;
	while (start < receiver_end && isspace((unsigned char) value[start])) {
		start++;
	}

	if (receiver_end >= start + sizeof("return") - 1 &&
		memcmp(value + start, "return", sizeof("return") - 1) == 0 &&
		!lsp_doc_is_identifier_char(value[start + sizeof("return") - 1])
	) {
		start += sizeof("return") - 1;
		while (start < receiver_end && isspace((unsigned char) value[start])) {
			start++;
		}
	}

	if (word_end <= start) {
		return NULL;
	}

	*expression_start = start;
	*expression_end = word_end;

	return zend_string_init(value + start, word_end - start, 0);
}

static inline zend_string *lsp_hover_analyzer_expression(zend_string *text, zend_string *word, size_t offset)
{
	zend_string *member_expression, *rhs_expression;
	size_t expression_start, expression_end;

	member_expression = lsp_hover_member_access_expression(text, word, offset, &expression_start, &expression_end);
	if (member_expression) {
		rhs_expression = lsp_hover_assignment_rhs_expression_at(text, offset, expression_start, expression_end);
		if (rhs_expression) {
			zend_string_release(member_expression);

			return rhs_expression;
		}

		return member_expression;
	}

	rhs_expression = lsp_hover_assignment_rhs_expression(text, word, offset);
	if (rhs_expression) {
		return rhs_expression;
	}

	if (lsp_hover_word_is_variable(word)) {
		return zend_string_copy(word);
	}

	return NULL;
}

static inline zend_string *lsp_hover_type_markdown(lsp_server *server, lsp_document *document, zval *position, zend_string *word, size_t offset, zend_string *primary_type, zend_string *analyzer_expression)
{
	zend_string *types[4], *psalm_type, *array_shape_analyzer_type, *phpstan_type, *phpstan_query_type, *phpstan_query_expression, *psalm_query_type, *psalm_ls_query_type, *markdown;
	smart_str sources[4], buffer = {0};
	uint32_t group_count;

	memset(types, 0, sizeof(types));
	memset(sources, 0, sizeof(sources));
	group_count = 0;
	psalm_type = NULL;
	array_shape_analyzer_type = NULL;
	phpstan_type = NULL;
	phpstan_query_type = NULL;
	phpstan_query_expression = NULL;
	psalm_query_type = NULL;
	psalm_ls_query_type = NULL;

	lsp_hover_add_type_source(types, sources, &group_count, primary_type, "LSParrot Engine");

	if (server->phpstan_enabled || server->psalm_enabled || server->psalm_ls_enabled) {
		array_shape_analyzer_type = lsp_infer_variable_phpdoc_type(document, word, offset);
		if (array_shape_analyzer_type && !lsp_phpdoc_type_has_array_shape(array_shape_analyzer_type)) {
			zend_string_release(array_shape_analyzer_type);
			array_shape_analyzer_type = NULL;
		}
	}

	if (server->phpstan_enabled) {
		phpstan_query_expression = analyzer_expression ? zend_string_copy(analyzer_expression) : zend_string_copy(word);
		phpstan_query_type = lsp_phpstan_type_for_expression(server, document, phpstan_query_expression, offset);
		zend_string_release(phpstan_query_expression);
		if (lsp_type_is_unhelpful(phpstan_query_type)) {
			if (phpstan_query_type) {
				zend_string_release(phpstan_query_type);
			}
			phpstan_query_type = NULL;
		}
		phpstan_type = phpstan_query_type ? phpstan_query_type : (array_shape_analyzer_type ? array_shape_analyzer_type : primary_type);
		lsp_hover_add_type_source(types, sources, &group_count, phpstan_type, "PHPStan");
	}

	if (server->psalm_enabled || server->psalm_ls_enabled) {
		if (array_shape_analyzer_type) {
			psalm_type = zend_string_copy(array_shape_analyzer_type);
		} else {
			psalm_type = lsp_infer_variable_declared_type(server, document, word, offset);
		}
		if (!psalm_type && !array_shape_analyzer_type) {
			psalm_type = lsp_hover_generic_base_type(primary_type);
		}
	}

	if (server->psalm_enabled) {
		psalm_query_type = lsp_psalm_type_for_expression(server, document, word, offset);
		if (lsp_type_is_unhelpful(psalm_query_type)) {
			if (psalm_query_type) {
				zend_string_release(psalm_query_type);
			}
			psalm_query_type = NULL;
		}
		lsp_hover_add_type_source(types, sources, &group_count, psalm_query_type ? psalm_query_type : (psalm_type ? psalm_type : primary_type), "Psalm");
	}

	if (server->psalm_ls_enabled) {
		psalm_ls_query_type = lsp_psalm_ls_type_for_position(server, document, position, offset, word);
		if (lsp_type_is_unhelpful(psalm_ls_query_type)) {
			if (psalm_ls_query_type) {
				zend_string_release(psalm_ls_query_type);
			}
			psalm_ls_query_type = NULL;
		}
		lsp_hover_add_type_source(types, sources, &group_count, psalm_ls_query_type ? psalm_ls_query_type : (psalm_type ? psalm_type : primary_type), "Psalm-LS");
	}

	lsp_hover_append_type_groups(&buffer, types, sources, group_count);
	smart_str_0(&buffer);
	markdown = buffer.s ? buffer.s : zend_string_init("", 0, 0);
	lsp_hover_release_type_groups(types, sources, group_count);

	if (psalm_type) {
		zend_string_release(psalm_type);
	}

	if (array_shape_analyzer_type) {
		zend_string_release(array_shape_analyzer_type);
	}

	if (phpstan_query_type) {
		zend_string_release(phpstan_query_type);
	}

	if (psalm_query_type) {
		zend_string_release(psalm_query_type);
	}

	if (psalm_ls_query_type) {
		zend_string_release(psalm_ls_query_type);
	}

	return markdown;
}

static inline zend_string *lsp_hover_zval_type(zval *value)
{
	if (!value) {
		return NULL;
	}

	switch (Z_TYPE_P(value)) {
		case IS_NULL:
			return zend_string_init("null", sizeof("null") - 1, 0);
		case IS_FALSE:
		case IS_TRUE:
			return zend_string_init("bool", sizeof("bool") - 1, 0);
		case IS_LONG:
			return zend_string_init("int", sizeof("int") - 1, 0);
		case IS_DOUBLE:
			return zend_string_init("float", sizeof("float") - 1, 0);
		case IS_STRING:
			return zend_string_init("string", sizeof("string") - 1, 0);
		case IS_ARRAY:
			return zend_string_init("array", sizeof("array") - 1, 0);
		case IS_OBJECT:
			if (Z_OBJCE_P(value) && Z_OBJCE_P(value)->name) {
				return zend_string_copy(Z_OBJCE_P(value)->name);
			}

			return zend_string_init("object", sizeof("object") - 1, 0);
		case IS_RESOURCE:
			return zend_string_init("resource", sizeof("resource") - 1, 0);
		default:
			return zend_string_init("mixed", sizeof("mixed") - 1, 0);
	}
}

static inline zend_string *lsp_hover_normalized_global_name(zend_string *word)
{
	if (ZSTR_LEN(word) > 0 && ZSTR_VAL(word)[0] == '\\') {
		return zend_string_init(ZSTR_VAL(word) + 1, ZSTR_LEN(word) - 1, 0);
	}

	return zend_string_copy(word);
}

static inline zend_string *lsp_hover_builtin_constant_type(zend_string *word)
{
	zend_string *name;
	zend_constant *constant;

	if (!EG(zend_constants) || ZSTR_LEN(word) == 0) {
		return NULL;
	}

	name = lsp_hover_normalized_global_name(word);
	constant = zend_hash_find_ptr(EG(zend_constants), name);
	zend_string_release(name);
	if (!constant) {
		return NULL;
	}

	return lsp_hover_zval_type(&constant->value);
}

static inline zend_string *lsp_hover_literal_token_type(zval *token)
{
	zend_string *text;

	if (!token || Z_TYPE_P(token) != IS_ARRAY) {
		return NULL;
	}

	if (lsp_token_name_equals(token, "T_LNUMBER")) {
		return zend_string_init("int", sizeof("int") - 1, 0);
	}

	if (lsp_token_name_equals(token, "T_DNUMBER")) {
		return zend_string_init("float", sizeof("float") - 1, 0);
	}

	if (lsp_token_name_equals(token, "T_CONSTANT_ENCAPSED_STRING")) {
		return zend_string_init("string", sizeof("string") - 1, 0);
	}

	if (lsp_token_is_char(token, '[')) {
		return zend_string_init("array", sizeof("array") - 1, 0);
	}

	text = lsp_token_string(token, "text");
	if (!text) {
		return NULL;
	}

	if (zend_string_equals_literal_ci(text, "true") || zend_string_equals_literal_ci(text, "false")) {
		return zend_string_init("bool", sizeof("bool") - 1, 0);
	}

	if (zend_string_equals_literal_ci(text, "null")) {
		return zend_string_init("null", sizeof("null") - 1, 0);
	}

	if (zend_string_equals_literal_ci(text, "array")) {
		return zend_string_init("array", sizeof("array") - 1, 0);
	}

	return NULL;
}

static inline zend_string *lsp_hover_current_document_constant_type(lsp_document *document, zend_string *word)
{
	zend_string *label, *name;
	zval *tokens_zv, *token, *value_token;
	HashTable *tokens;
	uint32_t i, j, count;
	size_t token_offset;
	bool found_equals;

	tokens_zv = zend_hash_str_find(Z_ARRVAL(document->lsparrot), "tokens", sizeof("tokens") - 1);
	if (!tokens_zv || Z_TYPE_P(tokens_zv) != IS_ARRAY) {
		return NULL;
	}

	tokens = Z_ARRVAL_P(tokens_zv);
	count = zend_hash_num_elements(tokens);
	name = lsp_hover_normalized_global_name(word);
	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY || !lsp_token_name_equals(token, "T_CONST")) {
			continue;
		}

		token_offset = (size_t) lsp_token_long(token, "offset", 0);
		if (lsp_offset_is_inside_class_body(document->text, token_offset)) {
			continue;
		}

		label = lsp_next_string_token(tokens, i + 1);
		if (!label || !zend_string_equals(label, name)) {
			continue;
		}

		found_equals = false;
		for (j = i + 1; j < count; j++) {
			value_token = zend_hash_index_find(tokens, j);
			if (!value_token || Z_TYPE_P(value_token) != IS_ARRAY) {
				continue;
			}

			if (lsp_token_is_char(value_token, ';') || lsp_token_is_char(value_token, ',')) {
				break;
			}

			if (!found_equals) {
				if (lsp_token_is_char(value_token, '=')) {
					found_equals = true;
				}

				continue;
			}

			if (lsp_token_name_equals(value_token, "T_WHITESPACE")) {
				continue;
			}

			zend_string_release(name);

			return lsp_hover_literal_token_type(value_token);
		}
	}

	zend_string_release(name);

	return NULL;
}

static inline zend_string *lsp_hover_constant_type(lsp_document *document, zend_string *word)
{
	zend_string *type;

	type = lsp_hover_builtin_constant_type(word);
	if (type) {
		return type;
	}

	return lsp_hover_current_document_constant_type(document, word);
}

static inline zend_string *lsp_hover_constant_markdown(zend_string *word, zend_string *type)
{
	return strpprintf(0, "`%s`\n\nLSParrot Engine constant %s", ZSTR_VAL(type), ZSTR_VAL(word));
}

static inline zend_string *lsp_hover_builtin_symbol_markdown(zend_string *word)
{
	zend_string *name, *lookup, *markdown;
	zend_function *function;
	zend_class_entry *ce;

	if (ZSTR_LEN(word) == 0) {
		return NULL;
	}

	name = lsp_hover_normalized_global_name(word);
	if (ZSTR_LEN(name) == 0) {
		zend_string_release(name);

		return NULL;
	}

	lookup = zend_string_tolower(name);
	function = CG(function_table) ? zend_hash_find_ptr(CG(function_table), lookup) : NULL;
	zend_string_release(lookup);
	if (function && function->common.function_name) {
		markdown = strpprintf(0, "`function %s(...)`\n\nLSParrot Engine", ZSTR_VAL(function->common.function_name));
		zend_string_release(name);

		return markdown;
	}

	ce = zend_lookup_class(name);
	if (ce && ce->name) {
		markdown = strpprintf(0, "`%s%s`\n\nLSParrot Engine", lsp_builtin_class_detail_prefix(ce), ZSTR_VAL(ce->name));
		zend_string_release(name);

		return markdown;
	}

	zend_string_release(name);

	return NULL;
}

extern void lsp_lsparrot_hover(lsp_server *server, zval *return_value, lsp_document *document, zval *position)
{
	zend_long line, character;
	zend_string *word, *detail, *markdown, *inferred_type, *constant_type, *analyzer_expression;
	zval contents;
	size_t offset;

	lsp_position_from_zval(position, &line, &character);
	offset = lsp_offset_at(document->text, line, character);
	word = lsp_word_at(document->text, offset);
	analyzer_expression = NULL;
	if (server->phpstan_enabled) {
		analyzer_expression = lsp_hover_analyzer_expression(document->text, word, offset);
	}

	inferred_type = lsp_infer_variable_type(server, document, word, offset);
	if (inferred_type) {
		array_init(return_value);
		array_init(&contents);
		markdown = lsp_hover_type_markdown(server, document, position, word, offset, inferred_type, analyzer_expression);
		add_assoc_string(&contents, "kind", "markdown");
		add_assoc_str(&contents, "value", markdown);
		add_assoc_zval(return_value, "contents", &contents);
		zend_string_release(inferred_type);
		if (analyzer_expression) {
			zend_string_release(analyzer_expression);
		}
		zend_string_release(word);

		return;
	}

	if ((analyzer_expression || lsp_hover_word_is_variable(word)) && (server->phpstan_enabled || server->psalm_enabled || server->psalm_ls_enabled)) {
		inferred_type = zend_string_init("mixed", sizeof("mixed") - 1, 0);
		array_init(return_value);
		array_init(&contents);
		markdown = lsp_hover_type_markdown(server, document, position, word, offset, inferred_type, analyzer_expression);
		add_assoc_string(&contents, "kind", "markdown");
		add_assoc_str(&contents, "value", markdown);
		add_assoc_zval(return_value, "contents", &contents);
		zend_string_release(inferred_type);
		if (analyzer_expression) {
			zend_string_release(analyzer_expression);
		}
		zend_string_release(word);

		return;
	}

	constant_type = lsp_hover_constant_type(document, word);
	if (constant_type) {
		array_init(return_value);
		array_init(&contents);
		markdown = lsp_hover_constant_markdown(word, constant_type);
		add_assoc_string(&contents, "kind", "markdown");
		add_assoc_str(&contents, "value", markdown);
		add_assoc_zval(return_value, "contents", &contents);
		zend_string_release(constant_type);
		if (analyzer_expression) {
			zend_string_release(analyzer_expression);
		}
		zend_string_release(word);

		return;
	}

	markdown = lsp_hover_builtin_symbol_markdown(word);
	if (markdown) {
		array_init(return_value);
		array_init(&contents);
		add_assoc_string(&contents, "kind", "markdown");
		add_assoc_str(&contents, "value", markdown);
		add_assoc_zval(return_value, "contents", &contents);
		if (analyzer_expression) {
			zend_string_release(analyzer_expression);
		}
		zend_string_release(word);

		return;
	}

	if (ZSTR_LEN(word) == 0 || !lsp_find_symbol(&document->lsparrot, word, NULL, NULL, &detail)) {
		if (analyzer_expression) {
			zend_string_release(analyzer_expression);
		}
		zend_string_release(word);
		ZVAL_NULL(return_value);

		return;
	}

	array_init(return_value);
	array_init(&contents);
	markdown = strpprintf(0, "`%s`", ZSTR_VAL(detail));
	add_assoc_string(&contents, "kind", "markdown");
	add_assoc_str(&contents, "value", markdown);
	add_assoc_zval(return_value, "contents", &contents);
	zend_string_release(detail);
	if (analyzer_expression) {
		zend_string_release(analyzer_expression);
	}
	zend_string_release(word);
}
