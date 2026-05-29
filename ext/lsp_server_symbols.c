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

static inline void lsp_symbol_index_header_init(lsp_symbol_index *region)
{
	lsp_symbol_index_header *header;

	if (!region->available || !region->addr || region->size < sizeof(lsp_symbol_index_header)) {
		return;
	}

	header = (lsp_symbol_index_header *) region->addr;
	header->magic = LSP_SYMBOL_INDEX_MAGIC;
	header->reserved = 0;
	header->generation = 1;
	header->capacity = region->size;
	header->used = sizeof(lsp_symbol_index_header);
	header->symbol_count = 0;
	header->flags = 0;
}

static inline zend_string *lsp_symbol_index_key(char kind, zend_string *fqcn)
{
	zend_string *key;
	size_t i;

	key = zend_string_alloc(ZSTR_LEN(fqcn) + 1, 0);
	ZSTR_VAL(key)[0] = kind;
	for (i = 0; i < ZSTR_LEN(fqcn); i++) {
		ZSTR_VAL(key)[i + 1] = (char) tolower((unsigned char) ZSTR_VAL(fqcn)[i]);
	}
	ZSTR_VAL(key)[ZSTR_LEN(fqcn) + 1] = '\0';

	return key;
}

static inline void lsp_symbol_index_add_key(lsp_symbol_index *region, char kind, zend_string *fqcn)
{
	zend_string *key;

	if (!region->keys_initialized) {
		return;
	}

	key = lsp_symbol_index_key(kind, fqcn);
	zend_hash_add_empty_element(&region->symbol_keys, key);
	zend_string_release(key);
}

static inline bool lsp_symbol_index_has_key(lsp_symbol_index *region, char kind, zend_string *fqcn)
{
	zend_string *key;
	bool exists;

	if (!region->keys_initialized) {
		return false;
	}

	key = lsp_symbol_index_key(kind, fqcn);
	exists = zend_hash_exists(&region->symbol_keys, key);
	zend_string_release(key);

	return exists;
}

extern void lsp_symbol_index_init(lsp_symbol_index *region, lsp_options *options)
{
	memset(region, 0, sizeof(*region));

	if (options->symbol_index_size < sizeof(lsp_symbol_index_header) + 1024) {
		options->symbol_index_size = sizeof(lsp_symbol_index_header) + 1024;
	}

	region->size = options->symbol_index_size;
	region->addr = ecalloc(1, region->size);
	region->available = region->addr != NULL;
	zend_hash_init(&region->symbol_keys, 1024, NULL, NULL, 0);
	region->keys_initialized = true;
	lsp_symbol_index_header_init(region);
}

extern void lsp_symbol_index_destroy(lsp_symbol_index *region)
{
	if (region->keys_initialized) {
		zend_hash_destroy(&region->symbol_keys);
	}

	if (region->addr) {
		efree(region->addr);
	}

	memset(region, 0, sizeof(*region));
}

extern void lsp_symbol_index_reset(lsp_symbol_index *region)
{
	lsp_symbol_index_header *header;

	if (!region->available || !region->addr) {
		return;
	}

	header = (lsp_symbol_index_header *) region->addr;
	header->magic = LSP_SYMBOL_INDEX_MAGIC;
	header->reserved = 0;
	header->generation++;
	header->capacity = region->size;
	header->used = sizeof(lsp_symbol_index_header);
	header->symbol_count = 0;
	header->flags = 0;
	if (region->keys_initialized) {
		zend_hash_clean(&region->symbol_keys);
	}
}

extern bool lsp_path_value_contains_vendor(const char *path, size_t path_length)
{
	return lsp_path_value_contains_segment(path, path_length, "vendor");
}

extern bool lsp_path_value_contains_analysis_helper(const char *path, size_t path_length)
{
	return lsp_path_value_contains_segment(path, path_length, ".lsparrot");
}

extern bool lsp_symbol_index_add_symbol_kind(lsp_symbol_index *region, char kind, zend_string *fqcn, zend_string *path)
{
	lsp_symbol_index_header *header;
	size_t need;
	char *cursor;

	if (!region->available || !region->addr) {
		return false;
	}

	header = (lsp_symbol_index_header *) region->addr;
	if (header->magic != LSP_SYMBOL_INDEX_MAGIC) {
		return false;
	}

	if (path && lsp_path_value_contains_analysis_helper(ZSTR_VAL(path), ZSTR_LEN(path))) {
		return false;
	}

	if (lsp_symbol_index_has_key(region, kind, fqcn)) {
		return false;
	}

	need = 1 + ZSTR_LEN(fqcn) + 1 + (path ? ZSTR_LEN(path) : 0) + 1;
	if (header->used + need > header->capacity) {
		return false;
	}

	cursor = ((char *) region->addr) + header->used;
	*cursor++ = kind;
	memcpy(cursor, ZSTR_VAL(fqcn), ZSTR_LEN(fqcn));
	cursor += ZSTR_LEN(fqcn);
	*cursor++ = '\0';

	if (path) {
		memcpy(cursor, ZSTR_VAL(path), ZSTR_LEN(path));
		cursor += ZSTR_LEN(path);
	}

	*cursor++ = '\0';
	header->used += need;
	header->symbol_count++;
	lsp_symbol_index_add_key(region, kind, fqcn);

	return true;
}

static inline bool lsp_symbol_index_add_symbol(lsp_symbol_index *region, zend_string *fqcn, zend_string *path)
{
	return lsp_symbol_index_add_symbol_kind(region, LSP_SYMBOL_CLASS, fqcn, path);
}

extern const char *lsp_basename_from_fqcn(const char *fqcn, size_t length, size_t *label_length)
{
	size_t i;

	for (i = length; i > 0; i--) {
		if (fqcn[i - 1] == '\\') {
			*label_length = length - i;
			return fqcn + i;
		}
	}

	for (i = length; i > 0; i--) {
		if (fqcn[i - 1] == '_') {
			*label_length = length - i;
			return fqcn + i;
		}
	}

	*label_length = length;

	return fqcn;
}

extern zend_string *lsp_token_string(zval *token, const char *key)
{
	return token && Z_TYPE_P(token) == IS_ARRAY ? lsp_array_string(token, key) : NULL;
}

extern zend_long lsp_token_long(zval *token, const char *key, zend_long fallback)
{
	return token && Z_TYPE_P(token) == IS_ARRAY ? lsp_array_long(token, key, fallback) : fallback;
}

extern bool lsp_token_name_equals(zval *token, const char *name)
{
	zend_string *token_name = lsp_token_string(token, "name");

	return token_name && zend_string_equals_cstr(token_name, name, strlen(name));
}

extern bool lsp_token_text_equals(zval *token, const char *text)
{
	zend_string *token_text = lsp_token_string(token, "text");

	return token_text && zend_string_equals_cstr(token_text, text, strlen(text));
}

extern bool lsp_token_is_class_like(zval *token)
{
	return lsp_token_name_equals(token, "T_CLASS") ||
		lsp_token_name_equals(token, "T_INTERFACE") ||
		lsp_token_name_equals(token, "T_TRAIT") ||
		lsp_token_name_equals(token, "T_ENUM")
	;
}

extern zend_string *lsp_next_string_token(HashTable *tokens, uint32_t start)
{
	zval *token;
	uint32_t i, count = zend_hash_num_elements(tokens);

	for (i = start; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY) {
			continue;
		}

		if (lsp_token_name_equals(token, "T_STRING")) {
			return lsp_token_string(token, "text");
		}

		if (!lsp_token_name_equals(token, "T_WHITESPACE")) {
			return NULL;
		}
	}

	return NULL;
}

extern zend_string *lsp_next_function_name_token(HashTable *tokens, uint32_t start)
{
	zend_string *text;
	zval *token;
	uint32_t i, count = zend_hash_num_elements(tokens);

	for (i = start; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY) {
			continue;
		}

		if (lsp_token_name_equals(token, "T_STRING")) {
			return lsp_token_string(token, "text");
		}

		if (lsp_token_name_equals(token, "T_WHITESPACE")) {
			continue;
		}

		if (lsp_token_name_equals(token, "CHAR")) {
			text = lsp_token_string(token, "text");
			if (text && ZSTR_LEN(text) == 1 && ZSTR_VAL(text)[0] == '&') {
				continue;
			}
		}

		if (lsp_token_name_equals(token, "T_AMPERSAND_FOLLOWED_BY_VAR_OR_VARARG") ||
			lsp_token_name_equals(token, "T_AMPERSAND_NOT_FOLLOWED_BY_VAR_OR_VARARG")
		) {
			continue;
		}

		return NULL;
	}

	return NULL;
}

extern zval *lsp_next_function_name_token_ex(HashTable *tokens, uint32_t start, uint32_t *index)
{
	zval *token;
	uint32_t i, count = zend_hash_num_elements(tokens);

	for (i = start; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY) {
			continue;
		}

		if (lsp_token_name_equals(token, "T_STRING")) {
			*index = i;
			return token;
		}

		if (lsp_token_name_equals(token, "T_WHITESPACE")) {
			continue;
		}

		if ((lsp_token_name_equals(token, "CHAR") && lsp_token_text_equals(token, "&")) ||
			lsp_token_name_equals(token, "T_AMPERSAND_FOLLOWED_BY_VAR_OR_VARARG") ||
			lsp_token_name_equals(token, "T_AMPERSAND_NOT_FOLLOWED_BY_VAR_OR_VARARG")
		) {
			continue;
		}

		return NULL;
	}

	return NULL;
}

static inline bool lsp_completion_kind_needs_call_snippet(zend_long kind)
{
	return kind == 2 || kind == 3;
}

extern void lsp_add_completion_item_ex(zval *items, zend_string *label, zend_long kind, zend_string *detail, const char *source)
{
	const char *filter_text = ZSTR_VAL(label);
	zval item, data;
	size_t filter_text_length = ZSTR_LEN(label);

	array_init(&item);
	add_assoc_str(&item, "label", zend_string_copy(label));
	add_assoc_long(&item, "kind", kind);
	add_assoc_str(&item, "detail", zend_string_copy(detail));
	add_assoc_stringl(&item, "filterText", filter_text, filter_text_length);

	if (lsp_completion_kind_needs_call_snippet(kind)) {
		add_assoc_str(&item, "insertText", lsp_completion_call_snippet_for_detail(label, detail));
		add_assoc_long(&item, "insertTextFormat", 2);
	}

	array_init(&data);
	add_assoc_string(&data, "source", source);
	add_assoc_zval(&item, "data", &data);
	add_next_index_zval(items, &item);
}

extern void lsp_add_variable_completion_item_ex(zval *items, zend_string *label, zend_string *detail, const char *source, zend_string *text, size_t start_offset, size_t end_offset)
{
	zval item, data, text_edit, range;

	array_init(&item);
	add_assoc_str(&item, "label", zend_string_copy(label));
	add_assoc_long(&item, "kind", 6);
	add_assoc_str(&item, "detail", zend_string_copy(detail));
	add_assoc_str(&item, "filterText", zend_string_copy(label));
	array_init(&text_edit);
	lsp_range_from_offsets(text, start_offset, end_offset, &range);
	add_assoc_zval(&text_edit, "range", &range);
	add_assoc_str(&text_edit, "newText", zend_string_copy(label));
	add_assoc_zval(&item, "textEdit", &text_edit);
	array_init(&data);
	add_assoc_string(&data, "source", source);
	add_assoc_zval(&item, "data", &data);
	add_next_index_zval(items, &item);
}

extern void lsp_add_completion_item(zval *items, zend_string *label, zend_long kind, zend_string *detail)
{
	lsp_add_completion_item_ex(items, label, kind, detail, "lsparrot");
}

extern void lsp_add_keyword_completion(zval *items, const char *keyword, zend_string *prefix)
{
	zval item;

	if (!lsp_matches_prefix_literal(keyword, prefix)) {
		return;
	}

	array_init(&item);
	add_assoc_string(&item, "label", keyword);
	add_assoc_long(&item, "kind", 14);
	add_assoc_string(&item, "detail", "keyword");
	add_next_index_zval(items, &item);
}

static inline bool lsp_completion_same_label(zval *left, zval *right)
{
	const char *left_value, *right_value;
	zend_string *left_label = lsp_array_string(left, "label"), *right_label = lsp_array_string(right, "label");
	zend_long left_kind = lsp_array_long(left, "kind", 0), right_kind = lsp_array_long(right, "kind", 0);
	size_t left_length, right_length;

	if (!left_label || !right_label) {
		return false;
	}

	if (left_kind != 0 && right_kind != 0 && left_kind != right_kind) {
		return false;
	}

	left_value = ZSTR_VAL(left_label);
	left_length = ZSTR_LEN(left_label);
	right_value = ZSTR_VAL(right_label);
	right_length = ZSTR_LEN(right_label);

	if (left_length > 0 && left_value[0] == '$') {
		left_value++;
		left_length--;
	}

	if (right_length > 0 && right_value[0] == '$') {
		right_value++;
		right_length--;
	}

	return left_length == right_length && memcmp(left_value, right_value, left_length) == 0;
}

static inline bool lsp_completion_detail_starts_with(zend_string *detail, const char *prefix)
{
	size_t prefix_length = strlen(prefix);

	return ZSTR_LEN(detail) >= prefix_length && memcmp(ZSTR_VAL(detail), prefix, prefix_length) == 0;
}

static inline zend_long lsp_completion_type_score(zval *item)
{
	zend_long score;
	zend_string *detail, *source, *sort_text;
	zval *data;
	size_t i;

	detail = lsp_array_string(item, "detail");
	sort_text = lsp_array_string(item, "sortText");
	data = lsp_array_find(item, "data");
	source = data && Z_TYPE_P(data) == IS_ARRAY ? lsp_array_string(data, "source") : NULL;
	score = 0;

	if (source && (zend_string_equals_literal(source, "phpstan") || zend_string_equals_literal(source, "psalm") || zend_string_equals_literal(source, "psalm-ls"))) {
		score += 10000;
	}

	if (sort_text && ZSTR_LEN(sort_text) >= 2 && ZSTR_VAL(sort_text)[1] == ':') {
		if (ZSTR_VAL(sort_text)[0] == '0') {
			score += 5000;
		} else if (ZSTR_VAL(sort_text)[0] == '1') {
			score -= 5000;
		}
	}

	if (!detail) {
		return score;
	}

	score += (zend_long) ZSTR_LEN(detail);
	if (lsp_completion_detail_starts_with(detail, "keyword")) {
		score -= 1000;
	} else if (lsp_completion_detail_starts_with(detail, "variable ") ||
		lsp_completion_detail_starts_with(detail, "function ") ||
		lsp_completion_detail_starts_with(detail, "class ") ||
		lsp_completion_detail_starts_with(detail, "interface ") ||
		lsp_completion_detail_starts_with(detail, "trait ") ||
		lsp_completion_detail_starts_with(detail, "enum ") ||
		lsp_completion_detail_starts_with(detail, "property ")
	) {
		score -= 200;
	}

	for (i = 0; i < ZSTR_LEN(detail); i++) {
		switch (ZSTR_VAL(detail)[i]) {
			case '\\':
			case '<':
			case '>':
			case '|':
			case '&':
			case '{':
			case '}':
			case '[':
			case ']':
			case ':':
				score += 25;
				break;
		}
	}

	if (strstr(ZSTR_VAL(detail), "array") != NULL || strstr(ZSTR_VAL(detail), "list") != NULL) {
		score += 75;
	}

	if (strstr(ZSTR_VAL(detail), "non-empty") != NULL || strstr(ZSTR_VAL(detail), "positive-") != NULL) {
		score += 100;
	}

	if (strstr(ZSTR_VAL(detail), "...") != NULL) {
		score += 25;
	}

	return score;
}

extern void lsp_deduplicate_completion_items(zval *items)
{
	zval deduped, *item, *existing, copy;
	bool handled;

	if (Z_TYPE_P(items) != IS_ARRAY || zend_hash_num_elements(Z_ARRVAL_P(items)) < 2) {
		return;
	}

	array_init(&deduped);
	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(items), item) {
		handled = false;

		if (Z_TYPE_P(item) != IS_ARRAY) {
			ZVAL_COPY(&copy, item);
			add_next_index_zval(&deduped, &copy);
			continue;
		}

		ZEND_HASH_FOREACH_VAL(Z_ARRVAL(deduped), existing) {
			if (Z_TYPE_P(existing) != IS_ARRAY || !lsp_completion_same_label(item, existing)) {
				continue;
			}

			if (lsp_completion_type_score(item) > lsp_completion_type_score(existing)) {
				zval_ptr_dtor(existing);
				ZVAL_COPY(existing, item);
			}

			handled = true;
			break;
		} ZEND_HASH_FOREACH_END();

		if (!handled) {
			ZVAL_COPY(&copy, item);
			add_next_index_zval(&deduped, &copy);
		}
	} ZEND_HASH_FOREACH_END();

	zval_ptr_dtor(items);
	ZVAL_COPY_VALUE(items, &deduped);
}

static inline zend_long lsp_symbol_completion_kind(char kind)
{
	switch (kind) {
		case LSP_SYMBOL_CLASS:
			return 7;
		case LSP_SYMBOL_INTERFACE:
			return 8;
		case LSP_SYMBOL_TRAIT:
			return 9;
		case LSP_SYMBOL_ENUM:
			return 13;
		case LSP_SYMBOL_FUNCTION:
			return 3;
		case LSP_SYMBOL_CONSTANT:
			return 21;
	}

	return 7;
}

extern zend_long lsp_symbol_workspace_kind(char kind)
{
	switch (kind) {
		case LSP_SYMBOL_INTERFACE:
			return 11;
		case LSP_SYMBOL_ENUM:
			return 10;
		case LSP_SYMBOL_FUNCTION:
			return 12;
		case LSP_SYMBOL_CONSTANT:
			return 14;
	}

	return 5;
}

static inline const char *lsp_symbol_detail_prefix(char kind)
{
	switch (kind) {
		case LSP_SYMBOL_INTERFACE:
			return "interface ";
		case LSP_SYMBOL_TRAIT:
			return "trait ";
		case LSP_SYMBOL_ENUM:
			return "enum ";
		case LSP_SYMBOL_FUNCTION:
			return "function ";
		case LSP_SYMBOL_CONSTANT:
			return "constant ";
		case LSP_SYMBOL_CLASS:
		default:
			return "class ";
	}
}

static inline bool lsp_symbol_kind_is_class_like(char kind)
{
	return kind == LSP_SYMBOL_CLASS ||
		kind == LSP_SYMBOL_INTERFACE ||
		kind == LSP_SYMBOL_TRAIT ||
		kind == LSP_SYMBOL_ENUM
	;
}

static inline const char *lsp_symbol_completion_label(char kind, const char *fqcn, size_t fqcn_length, size_t *label_length)
{
	size_t i;

	if (kind == LSP_SYMBOL_FUNCTION || kind == LSP_SYMBOL_CONSTANT) {
		for (i = fqcn_length; i > 0; i--) {
			if (fqcn[i - 1] == '\\') {
				*label_length = fqcn_length - i;

				return fqcn + i;
			}
		}

		*label_length = fqcn_length;

		return fqcn;
	}

	return lsp_basename_from_fqcn(fqcn, fqcn_length, label_length);
}

extern bool lsp_symbol_kind_matches(char expected_kind, char actual_kind)
{
	if (expected_kind == LSP_SYMBOL_CLASS) {
		return lsp_symbol_kind_is_class_like(actual_kind);
	}

	return expected_kind == actual_kind;
}

static inline void lsp_offset_to_position(zend_string *text, size_t offset, zval *position)
{
	const char *value = ZSTR_VAL(text);
	zend_long line = 0;
	size_t i, line_start = 0,
		length = offset > ZSTR_LEN(text) ? ZSTR_LEN(text) : offset
	;

	for (i = 0; i < length; i++) {
		if (value[i] == '\n') {
			line++;
			line_start = i + 1;
		}
	}

	array_init(position);
	add_assoc_long(position, "line", line);
	add_assoc_long(position, "character", (zend_long) (length - line_start));
}

extern void lsp_range_from_offsets(zend_string *text, size_t start_offset, size_t end_offset, zval *range)
{
	zval start, end;

	array_init(range);
	lsp_offset_to_position(text, start_offset, &start);
	lsp_offset_to_position(text, end_offset, &end);
	add_assoc_zval(range, "start", &start);
	add_assoc_zval(range, "end", &end);
}

static inline bool lsp_symbol_fuzzy_match(const char *value, size_t value_length, const char *prefix, size_t prefix_length)
{
	size_t value_index, prefix_index;

	if (prefix_length < 2 || value_length == 0) {
		return false;
	}

	prefix_index = 0;
	for (value_index = 0; value_index < value_length && prefix_index < prefix_length; value_index++) {
		if (tolower((unsigned char) value[value_index]) == tolower((unsigned char) prefix[prefix_index])) {
			prefix_index++;
		}
	}

	return prefix_index == prefix_length;
}

static inline bool lsp_symbol_prefix_matches(const char *fqcn, size_t fqcn_length, const char *label, size_t label_length, zend_string *prefix, bool fuzzy)
{
	const char *prefix_value = ZSTR_VAL(prefix);
	size_t prefix_length = ZSTR_LEN(prefix), i;

	if (prefix_length > 0 && prefix_value[0] == '\\') {
		prefix_value++;
		prefix_length--;
	}

	if (prefix_length == 0) {
		return true;
	}

	if (prefix_length <= label_length && strncasecmp(label, prefix_value, prefix_length) == 0) {
		return true;
	}

	if (prefix_length <= fqcn_length && strncasecmp(fqcn, prefix_value, prefix_length) == 0) {
		return true;
	}

	for (i = 0; i < fqcn_length; i++) {
		if ((i == 0 || fqcn[i - 1] == '\\' || fqcn[i - 1] == '_') &&
			i + prefix_length <= fqcn_length &&
			strncasecmp(fqcn + i, prefix_value, prefix_length) == 0
		) {
			return true;
		}
	}

	return fuzzy && (
		lsp_symbol_fuzzy_match(label, label_length, prefix_value, prefix_length) ||
		lsp_symbol_fuzzy_match(fqcn, fqcn_length, prefix_value, prefix_length)
	);
}

static inline bool lsp_current_statement_starts_with_use(zend_string *text, size_t offset)
{
	const char *value = ZSTR_VAL(text);
	size_t start, p;

	if (offset > ZSTR_LEN(text)) {
		offset = ZSTR_LEN(text);
	}

	start = offset;
	while (start > 0 && value[start - 1] != '\n' && value[start - 1] != ';' && value[start - 1] != '{' && value[start - 1] != '}') {
		start--;
	}

	p = start;
	while (p < offset && isspace((unsigned char) value[p])) {
		p++;
	}

	return p + sizeof("use") - 1 <= offset &&
		memcmp(value + p, "use", sizeof("use") - 1) == 0 &&
		(p + sizeof("use") - 1 == offset || isspace((unsigned char) value[p + sizeof("use") - 1]))
	;
}

extern zend_string *lsp_document_namespace(zend_string *text)
{
	const char *value = ZSTR_VAL(text), *end = value + ZSTR_LEN(text), *p = value, *name_start, *name_end;

	while ((p = strstr(p, "namespace")) != NULL && p < end) {
		if (p > value && (isalnum((unsigned char) p[-1]) || p[-1] == '_')) {
			p++;
			continue;
		}

		if (p + sizeof("namespace") - 1 < end && (isalnum((unsigned char) p[sizeof("namespace") - 1]) || p[sizeof("namespace") - 1] == '_')) {
			p++;
			continue;
		}

		p += sizeof("namespace") - 1;
		while (p < end && isspace((unsigned char) *p)) {
			p++;
		}

		if (p < end && *p == '\\') {
			p++;
		}

		name_start = p;
		while (p < end && (isalnum((unsigned char) *p) || *p == '_' || *p == '\\')) {
			p++;
		}
		name_end = p;

		if (name_end > name_start) {
			return zend_string_init(name_start, name_end - name_start, 0);
		}
	}

	return zend_empty_string;
}

static inline bool lsp_symbol_has_namespace(const char *fqcn, size_t fqcn_length, const char **namespace_end)
{
	size_t i;

	for (i = fqcn_length; i > 0; i--) {
		if (fqcn[i - 1] == '\\') {
			*namespace_end = fqcn + i - 1;

			return true;
		}
	}

	return false;
}

static inline bool lsp_symbol_in_current_namespace(zend_string *current_namespace, const char *fqcn, size_t fqcn_length)
{
	const char *namespace_end;
	size_t namespace_length;

	if (!lsp_symbol_has_namespace(fqcn, fqcn_length, &namespace_end)) {
		return true;
	}

	namespace_length = namespace_end - fqcn;

	return ZSTR_LEN(current_namespace) == namespace_length &&
		strncasecmp(ZSTR_VAL(current_namespace), fqcn, namespace_length) == 0
	;
}

static inline bool lsp_document_has_import(zend_string *text, char kind, const char *fqcn)
{
	zend_string *needle;
	bool found;

	if (kind == LSP_SYMBOL_FUNCTION) {
		needle = strpprintf(0, "use function %s;", fqcn);
	} else if (kind == LSP_SYMBOL_CONSTANT) {
		needle = strpprintf(0, "use const %s;", fqcn);
	} else {
		needle = strpprintf(0, "use %s;", fqcn);
	}

	found = strstr(ZSTR_VAL(text), ZSTR_VAL(needle)) != NULL;
	zend_string_release(needle);

	return found;
}

static inline bool lsp_line_trimmed_starts_with(const char *start, const char *end, const char *keyword)
{
	size_t keyword_length = strlen(keyword);

	while (start < end && isspace((unsigned char) *start)) {
		start++;
	}

	return (size_t) (end - start) >= keyword_length &&
		memcmp(start, keyword, keyword_length) == 0 &&
		((size_t) (end - start) == keyword_length || isspace((unsigned char) start[keyword_length]) || start[keyword_length] == '(')
	;
}

static inline bool lsp_line_contains_char(const char *start, const char *end, char needle)
{
	while (start < end) {
		if (*start == needle) {
			return true;
		}

		start++;
	}

	return false;
}

static inline size_t lsp_import_insert_offset(zend_string *text, bool *after_existing_use)
{
	const char *value = ZSTR_VAL(text), *line_start, *line_end;
	size_t offset = 0, insert_offset = 0, next_offset;

	*after_existing_use = false;
	while (offset < ZSTR_LEN(text)) {
		line_start = value + offset;
		line_end = memchr(line_start, '\n', ZSTR_LEN(text) - offset);

		if (!line_end) {
			line_end = value + ZSTR_LEN(text);
			next_offset = ZSTR_LEN(text);
		} else {
			next_offset = (size_t) (line_end - value) + 1;
		}

		if (lsp_line_trimmed_starts_with(line_start, line_end, "<?php") ||
			lsp_line_trimmed_starts_with(line_start, line_end, "declare") ||
			lsp_line_trimmed_starts_with(line_start, line_end, "namespace")
		) {
			insert_offset = next_offset;
		} else if (lsp_line_trimmed_starts_with(line_start, line_end, "use") && lsp_line_contains_char(line_start, line_end, ';')) {
			insert_offset = next_offset;
			*after_existing_use = true;
		} else if (lsp_line_trimmed_starts_with(line_start, line_end, "class") ||
			lsp_line_trimmed_starts_with(line_start, line_end, "final") ||
			lsp_line_trimmed_starts_with(line_start, line_end, "abstract") ||
			lsp_line_trimmed_starts_with(line_start, line_end, "interface") ||
			lsp_line_trimmed_starts_with(line_start, line_end, "trait") ||
			lsp_line_trimmed_starts_with(line_start, line_end, "enum") ||
			lsp_line_trimmed_starts_with(line_start, line_end, "function")
		) {
			break;
		}

		offset = next_offset;

		if (next_offset == ZSTR_LEN(text)) {
			break;
		}
	}

	return insert_offset;
}

static inline zend_string *lsp_symbol_import_text(char kind, const char *fqcn, bool compact)
{
	if (kind == LSP_SYMBOL_FUNCTION) {
		return strpprintf(0, "%suse function %s;\n", compact ? "" : "\n", fqcn);
	}

	if (kind == LSP_SYMBOL_CONSTANT) {
		return strpprintf(0, "%suse const %s;\n", compact ? "" : "\n", fqcn);
	}

	return strpprintf(0, "%suse %s;\n", compact ? "" : "\n", fqcn);
}

static inline void lsp_add_project_symbol_completion_item(zval *items, lsp_document *document, size_t offset, zend_string *prefix, char kind, const char *fqcn, size_t fqcn_length, const char *label_value, size_t label_length, const char *path, size_t path_length)
{
	zend_string *label = zend_string_init(label_value, label_length, 0),
		*detail = strpprintf(0, "%s%s", lsp_symbol_detail_prefix(kind), fqcn),
		*current_namespace, *new_text, *import_text, *sort_text
	;
	zval item, data, text_edit, edit_range, additional, edit, range;
	size_t prefix_start = offset >= ZSTR_LEN(prefix) ? offset - ZSTR_LEN(prefix) : 0, insert_offset;
	bool import_context = lsp_current_statement_starts_with_use(document->text, offset), prefix_is_qualified = memchr(ZSTR_VAL(prefix), '\\', ZSTR_LEN(prefix)) != NULL,
		after_existing_use, call_snippet = kind == LSP_SYMBOL_FUNCTION && !import_context;

	if (import_context || prefix_is_qualified) {
		if (ZSTR_LEN(prefix) > 0 && ZSTR_VAL(prefix)[0] == '\\') {
			new_text = strpprintf(0, "\\%s", fqcn);
		} else {
			new_text = zend_string_init(fqcn, fqcn_length, 0);
		}
	} else {
		new_text = zend_string_copy(label);
	}

	array_init(&item);
	add_assoc_str(&item, "label", zend_string_copy(label));
	add_assoc_long(&item, "kind", lsp_symbol_completion_kind(kind));
	add_assoc_str(&item, "detail", zend_string_copy(detail));
	add_assoc_str(&item, "filterText", zend_string_copy(label));
	sort_text = strpprintf(0, "%c:%s", lsp_path_value_contains_vendor(path, path_length) ? '1' : '0', ZSTR_VAL(label));
	add_assoc_str(&item, "sortText", sort_text);
	array_init(&data);
	add_assoc_string(&data, "source", "lsparrot");
	add_assoc_zval(&item, "data", &data);
	array_init(&text_edit);
	lsp_range_from_offsets(document->text, prefix_start, offset, &edit_range);
	add_assoc_zval(&text_edit, "range", &edit_range);

	if (call_snippet) {
		add_assoc_str(&text_edit, "newText", lsp_completion_call_snippet(new_text));
		add_assoc_long(&item, "insertTextFormat", 2);
	} else {
		add_assoc_str(&text_edit, "newText", zend_string_copy(new_text));
	}

	add_assoc_zval(&item, "textEdit", &text_edit);

	current_namespace = lsp_document_namespace(document->text);

	if (!import_context &&
		!prefix_is_qualified &&
		!lsp_symbol_in_current_namespace(current_namespace, fqcn, fqcn_length) &&
		!lsp_document_has_import(document->text, kind, fqcn)
	) {
		insert_offset = lsp_import_insert_offset(document->text, &after_existing_use);
		import_text = lsp_symbol_import_text(kind, fqcn, after_existing_use);

		array_init(&additional);
		array_init(&edit);
		lsp_range_from_offsets(document->text, insert_offset, insert_offset, &range);
		add_assoc_zval(&edit, "range", &range);
		add_assoc_str(&edit, "newText", import_text);
		add_next_index_zval(&additional, &edit);
		add_assoc_zval(&item, "additionalTextEdits", &additional);
	}

	if (current_namespace != zend_empty_string) {
		zend_string_release(current_namespace);
	}

	add_next_index_zval(items, &item);
	zend_string_release(new_text);
	zend_string_release(detail);
	zend_string_release(label);
}

static inline void lsp_add_project_symbol_completions_pass(lsp_server *server, zval *items, lsp_document *document, size_t offset, zend_string *prefix, char filter_kind, bool class_like_only, bool vendor_symbols)
{
	const char *label_value;
	lsp_symbol_index_header *header;
	uint32_t i;
	size_t fqcn_length, label_length, path_length;
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
		if (cursor > end) {
			break;
		}

		if (lsp_path_value_contains_analysis_helper(path, path_length) ||
			lsp_path_value_contains_vendor(path, path_length) != vendor_symbols
		) {
			continue;
		}

		if (filter_kind != '\0' && kind != filter_kind) {
			continue;
		}

		if (class_like_only && !lsp_symbol_kind_is_class_like(kind)) {
			continue;
		}

		label_value = lsp_symbol_completion_label(kind, fqcn, fqcn_length, &label_length);
		if (!lsp_symbol_prefix_matches(fqcn, fqcn_length, label_value, label_length, prefix, lsp_symbol_kind_is_class_like(kind))) {
			continue;
		}

		lsp_add_project_symbol_completion_item(items, document, offset, prefix, kind, fqcn, fqcn_length, label_value, label_length, path, path_length);
	}
}

static inline void lsp_add_project_symbol_completions_ex(lsp_server *server, zval *items, lsp_document *document, size_t offset, zend_string *prefix, char filter_kind, bool class_like_only)
{
	lsp_add_project_symbol_completions_pass(server, items, document, offset, prefix, filter_kind, class_like_only, false);
	lsp_add_project_symbol_completions_pass(server, items, document, offset, prefix, filter_kind, class_like_only, true);
}

extern void lsp_add_project_symbol_completions(lsp_server *server, zval *items, lsp_document *document, size_t offset, zend_string *prefix)
{
	lsp_add_project_symbol_completions_ex(server, items, document, offset, prefix, '\0', false);
}

extern void lsp_add_project_class_like_completions(lsp_server *server, zval *items, lsp_document *document, size_t offset, zend_string *prefix)
{
	lsp_add_project_symbol_completions_ex(server, items, document, offset, prefix, '\0', true);
}

extern void lsp_add_project_symbol_kind_completions(lsp_server *server, zval *items, lsp_document *document, size_t offset, zend_string *prefix, char filter_kind)
{
	lsp_add_project_symbol_completions_ex(server, items, document, offset, prefix, filter_kind, false);
}

extern void lsp_add_class_like_symbol_completion_item(zval *items, lsp_document *document, size_t offset, zend_string *prefix, char kind, zend_string *fqcn)
{
	const char *label_value;
	size_t label_length;

	label_value = lsp_symbol_completion_label(kind, ZSTR_VAL(fqcn), ZSTR_LEN(fqcn), &label_length);
	lsp_add_project_symbol_completion_item(items, document, offset, prefix, kind, ZSTR_VAL(fqcn), ZSTR_LEN(fqcn), label_value, label_length, "", 0);
}
