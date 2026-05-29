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

static inline bool lsp_refactor_string_equals_ci(const char *left, size_t left_length, const char *right, size_t right_length)
{
	return left_length == right_length && strncasecmp(left, right, left_length) == 0;
}

static inline bool lsp_refactor_string_equals_zstr_ci(const char *left, size_t left_length, zend_string *right)
{
	const char *right_value;
	size_t right_length;

	right_value = ZSTR_VAL(right);
	right_length = ZSTR_LEN(right);
	if (right_length > 0 && right_value[0] == '\\') {
		right_value++;
		right_length--;
	}

	return lsp_refactor_string_equals_ci(left, left_length, right_value, right_length);
}

static inline int lsp_refactor_zstr_casecmp(zend_string *left, zend_string *right)
{
	unsigned char left_char, right_char;
	size_t i, min_length;

	min_length = ZSTR_LEN(left) < ZSTR_LEN(right) ? ZSTR_LEN(left) : ZSTR_LEN(right);
	for (i = 0; i < min_length; i++) {
		left_char = (unsigned char) tolower((unsigned char) ZSTR_VAL(left)[i]);
		right_char = (unsigned char) tolower((unsigned char) ZSTR_VAL(right)[i]);
		if (left_char < right_char) {
			return -1;
		}
		if (left_char > right_char) {
			return 1;
		}
	}

	if (ZSTR_LEN(left) < ZSTR_LEN(right)) {
		return -1;
	}
	if (ZSTR_LEN(left) > ZSTR_LEN(right)) {
		return 1;
	}

	return 0;
}

static inline bool lsp_refactor_zstr_equals_ci(zend_string *left, zend_string *right)
{
	return lsp_refactor_zstr_casecmp(left, right) == 0;
}

static inline bool lsp_refactor_word_is_variable(zend_string *word)
{
	return ZSTR_LEN(word) > 0 && ZSTR_VAL(word)[0] == '$';
}

static inline bool lsp_refactor_word_is_qualified(zend_string *word)
{
	return memchr(ZSTR_VAL(word), '\\', ZSTR_LEN(word)) != NULL;
}

static inline bool lsp_refactor_token_is_searchable(zval *token, bool variable)
{
	if (variable) {
		return lsp_token_name_equals(token, "T_VARIABLE");
	}

	return lsp_token_name_equals(token, "T_STRING") ||
		lsp_token_name_equals(token, "T_NAME_FULLY_QUALIFIED") ||
		lsp_token_name_equals(token, "T_NAME_RELATIVE") ||
		lsp_token_name_equals(token, "T_NAME_QUALIFIED")
	;
}

static inline bool lsp_refactor_match_word_at(const char *value, size_t value_length, size_t start, zend_string *word, bool variable, size_t *match_end)
{
	const char *word_value;
	size_t word_length;

	word_value = ZSTR_VAL(word);
	word_length = ZSTR_LEN(word);
	if (!variable && word_length > 0 && word_value[0] == '\\') {
		word_value++;
		word_length--;
	}

	if (word_length == 0 || start + word_length > value_length) {
		return false;
	}

	if (start > 0 && lsp_doc_is_identifier_char(value[start - 1])) {
		return false;
	}

	if (start + word_length < value_length && lsp_doc_is_identifier_char(value[start + word_length])) {
		return false;
	}

	if (variable) {
		if (memcmp(value + start, word_value, word_length) != 0) {
			return false;
		}
	} else if (strncasecmp(value + start, word_value, word_length) != 0) {
		return false;
	}

	*match_end = start + word_length;

	return true;
}

static inline bool lsp_refactor_next_word_match(zend_string *text, zend_string *word, bool variable, size_t *cursor, size_t *match_start, size_t *match_end)
{
	const char *value;
	size_t i, end;

	value = ZSTR_VAL(text);
	i = *cursor;
	while (i < ZSTR_LEN(text)) {
		if (lsp_refactor_match_word_at(value, ZSTR_LEN(text), i, word, variable, &end)) {
			*cursor = end;
			*match_start = i;
			*match_end = end;

			return true;
		}
		i++;
	}

	return false;
}

static inline bool lsp_refactor_token_match(zval *token, zend_string *word, bool variable, size_t *match_start, size_t *match_end)
{
	zend_string *text;
	size_t cursor, local_start, local_end, offset;

	text = lsp_token_string(token, "text");
	if (!text || !lsp_refactor_token_is_searchable(token, variable)) {
		return false;
	}

	cursor = 0;
	if (!lsp_refactor_next_word_match(text, word, variable, &cursor, &local_start, &local_end)) {
		return false;
	}

	offset = (size_t) lsp_token_long(token, "offset", 0);
	*match_start = offset + local_start;
	*match_end = offset + local_end;

	return true;
}

static inline bool lsp_refactor_word_bounds(zend_string *text, size_t offset, size_t *word_start, size_t *word_end)
{
	const char *value;
	size_t start, end, length;

	value = ZSTR_VAL(text);
	length = ZSTR_LEN(text);
	if (offset > length) {
		offset = length;
	}

	start = offset;
	while (start > 0 && (lsp_doc_is_identifier_char(value[start - 1]) || value[start - 1] == '$' || value[start - 1] == '\\')) {
		start--;
	}

	end = offset;
	while (end < length && (lsp_doc_is_identifier_char(value[end]) || value[end] == '$' || value[end] == '\\')) {
		end++;
	}

	if (end <= start) {
		return false;
	}

	*word_start = start;
	*word_end = end;

	return true;
}

static inline void lsp_refactor_location_from_offsets(zend_string *path, zend_string *contents, size_t start_offset, size_t end_offset, zval *location)
{
	zend_string *uri;
	zval range;

	uri = lsp_uri_from_path(path);
	array_init(location);
	add_assoc_str(location, "uri", uri);
	lsp_range_from_offsets(contents, start_offset, end_offset, &range);
	add_assoc_zval(location, "range", &range);
}

static inline void lsp_refactor_add_token_locations(zval *locations, zend_string *path, zend_string *contents, zend_string *word)
{
	zval tokens_zv, *token, location;
	HashTable *tokens;
	uint32_t i, count;
	size_t start, end;
	bool variable;

	variable = lsp_refactor_word_is_variable(word);
	ZVAL_UNDEF(&tokens_zv);
	lsp_lsparrot_tokens_to_zval(&tokens_zv, contents);
	if (Z_TYPE(tokens_zv) != IS_ARRAY) {
		if (!Z_ISUNDEF(tokens_zv)) {
			zval_ptr_dtor(&tokens_zv);
		}

		return;
	}

	tokens = Z_ARRVAL(tokens_zv);
	count = zend_hash_num_elements(tokens);
	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY || !lsp_refactor_token_match(token, word, variable, &start, &end)) {
			continue;
		}

		lsp_refactor_location_from_offsets(path, contents, start, end, &location);
		add_next_index_zval(locations, &location);
	}

	zval_ptr_dtor(&tokens_zv);
}

static inline void lsp_refactor_add_document_highlights(zval *items, zend_string *contents, zend_string *word)
{
	zval tokens_zv, *token, item, range;
	HashTable *tokens;
	uint32_t i, count;
	size_t start, end;
	bool variable;

	variable = lsp_refactor_word_is_variable(word);
	ZVAL_UNDEF(&tokens_zv);
	lsp_lsparrot_tokens_to_zval(&tokens_zv, contents);
	if (Z_TYPE(tokens_zv) != IS_ARRAY) {
		if (!Z_ISUNDEF(tokens_zv)) {
			zval_ptr_dtor(&tokens_zv);
		}

		return;
	}

	tokens = Z_ARRVAL(tokens_zv);
	count = zend_hash_num_elements(tokens);
	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY || !lsp_refactor_token_match(token, word, variable, &start, &end)) {
			continue;
		}

		array_init(&item);
		lsp_range_from_offsets(contents, start, end, &range);
		add_assoc_zval(&item, "range", &range);
		add_assoc_long(&item, "kind", 1);
		add_next_index_zval(items, &item);
	}

	zval_ptr_dtor(&tokens_zv);
}

static inline void lsp_refactor_add_project_reference_locations(lsp_server *server, zval *locations, lsp_document *primary_document, zend_string *word)
{
	lsp_symbol_index_header *header;
	lsp_document *document;
	HashTable visited;
	zend_string *path_string, *contents;
	uint32_t i;
	size_t fqcn_length, path_length;
	char *cursor, *end, kind, *fqcn, *path;
	bool owns_contents;

	if (!server->symbol_index.available || !server->symbol_index.addr) {
		return;
	}

	header = (lsp_symbol_index_header *) server->symbol_index.addr;
	if (header->magic != LSP_SYMBOL_INDEX_MAGIC || header->used > header->capacity) {
		return;
	}

	zend_hash_init(&visited, 128, NULL, NULL, 0);
	zend_hash_add_empty_element(&visited, primary_document->path);
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
			lsp_path_value_contains_vendor(path, path_length) ||
			kind == '\0'
		) {
			continue;
		}

		path_string = zend_string_init(path, path_length, 0);
		if (zend_hash_exists(&visited, path_string)) {
			zend_string_release(path_string);
			continue;
		}

		zend_hash_add_empty_element(&visited, path_string);

		document = lsp_document_for_path(server, path_string);
		contents = document ? zend_string_copy(document->text) : lsp_read_file(path_string);
		owns_contents = document || contents != zend_empty_string;
		if (owns_contents) {
			lsp_refactor_add_token_locations(locations, path_string, contents, word);
			zend_string_release(contents);
		}

		zend_string_release(path_string);
	}

	zend_hash_destroy(&visited);
}

static inline bool lsp_refactor_range_start_offset(zend_string *text, zval *range, size_t *offset)
{
	zend_long line, character;
	zval *start;

	if (!range || Z_TYPE_P(range) != IS_ARRAY) {
		return false;
	}

	start = lsp_array_find(range, "start");
	if (!start || Z_TYPE_P(start) != IS_ARRAY) {
		return false;
	}

	lsp_position_from_zval(start, &line, &character);
	*offset = lsp_offset_at(text, line, character);

	return true;
}

static inline bool lsp_refactor_range_offsets(zend_string *text, zval *range, size_t *start_offset, size_t *end_offset)
{
	zend_long line, character;
	zval *start, *end;

	if (!range || Z_TYPE_P(range) != IS_ARRAY) {
		return false;
	}

	start = lsp_array_find(range, "start");
	end = lsp_array_find(range, "end");
	if (!start || Z_TYPE_P(start) != IS_ARRAY || !end || Z_TYPE_P(end) != IS_ARRAY) {
		return false;
	}

	lsp_position_from_zval(start, &line, &character);
	*start_offset = lsp_offset_at(text, line, character);
	lsp_position_from_zval(end, &line, &character);
	*end_offset = lsp_offset_at(text, line, character);
	if (*end_offset < *start_offset) {
		*end_offset = *start_offset;
	}

	return true;
}

static inline bool lsp_refactor_context_only_has(zval *params, const char *kind)
{
	zval *context, *only, *entry;

	context = lsp_array_find(params, "context");
	if (!context || Z_TYPE_P(context) != IS_ARRAY) {
		return true;
	}

	only = lsp_array_find(context, "only");
	if (!only || Z_TYPE_P(only) != IS_ARRAY || zend_hash_num_elements(Z_ARRVAL_P(only)) == 0) {
		return true;
	}

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(only), entry) {
		if (Z_TYPE_P(entry) == IS_STRING && php_ver_abstract.string_equals_cstr(Z_STR_P(entry), kind, strlen(kind))) {
			return true;
		}
	} ZEND_HASH_FOREACH_END();

	return false;
}

static inline void lsp_refactor_workspace_edit_single(zval *edit, zend_string *uri, zval *text_edits)
{
	zval changes;

	array_init(edit);
	array_init(&changes);

	add_assoc_zval_ex(&changes, ZSTR_VAL(uri), ZSTR_LEN(uri), text_edits);
	add_assoc_zval(edit, "changes", &changes);
}

static inline void lsp_refactor_add_single_text_edit(zval *text_edits, zend_string *contents, size_t start_offset, size_t end_offset, zend_string *new_text)
{
	zval edit, range;

	array_init(&edit);
	lsp_range_from_offsets(contents, start_offset, end_offset, &range);
	add_assoc_zval(&edit, "range", &range);
	add_assoc_str(&edit, "newText", zend_string_copy(new_text));
	add_next_index_zval(text_edits, &edit);
}

static inline zend_string *lsp_refactor_trim_line(const char *start, const char *end)
{
	while (start < end && isspace((unsigned char) *start)) {
		start++;
	}

	while (end > start && isspace((unsigned char) end[-1])) {
		end--;
	}

	return zend_string_init(start, end - start, 0);
}

static inline bool lsp_refactor_trimmed_line_starts_with(const char *start, const char *end, const char *keyword)
{
	size_t keyword_length;

	keyword_length = strlen(keyword);
	while (start < end && isspace((unsigned char) *start)) {
		start++;
	}

	return (size_t) (end - start) >= keyword_length &&
		strncasecmp(start, keyword, keyword_length) == 0 &&
		((size_t) (end - start) == keyword_length || isspace((unsigned char) start[keyword_length]))
	;
}

static inline bool lsp_refactor_line_contains_semicolon(const char *start, const char *end)
{
	while (start < end) {
		if (*start == ';') {
			return true;
		}
		start++;
	}

	return false;
}

static inline bool lsp_refactor_line_starts_declaration(const char *start, const char *end)
{
	return lsp_refactor_trimmed_line_starts_with(start, end, "class") ||
		lsp_refactor_trimmed_line_starts_with(start, end, "interface") ||
		lsp_refactor_trimmed_line_starts_with(start, end, "trait") ||
		lsp_refactor_trimmed_line_starts_with(start, end, "enum") ||
		lsp_refactor_trimmed_line_starts_with(start, end, "function") ||
		lsp_refactor_trimmed_line_starts_with(start, end, "final") ||
		lsp_refactor_trimmed_line_starts_with(start, end, "abstract")
	;
}

static inline void lsp_refactor_sorted_line_insert(zend_string ***lines, uint32_t *count, uint32_t *capacity, zend_string *line)
{
	uint32_t i, j;

	for (i = 0; i < *count; i++) {
		if (lsp_refactor_zstr_equals_ci((*lines)[i], line)) {
			zend_string_release(line);
			return;
		}
	}

	if (*count + 1 > *capacity) {
		*capacity = *capacity == 0 ? 8 : *capacity * 2;
		*lines = erealloc(*lines, sizeof(zend_string *) * (*capacity));
	}

	i = 0;
	while (i < *count && lsp_refactor_zstr_casecmp((*lines)[i], line) < 0) {
		i++;
	}

	for (j = *count; j > i; j--) {
		(*lines)[j] = (*lines)[j - 1];
	}

	(*lines)[i] = line;
	(*count)++;
}

static inline void lsp_refactor_lines_destroy(zend_string **lines, uint32_t count)
{
	uint32_t i;

	for (i = 0; i < count; i++) {
		zend_string_release(lines[i]);
	}

	if (lines) {
		efree(lines);
	}
}

static inline bool lsp_refactor_organize_imports_edit(lsp_document *document, zval *text_edit)
{
	const char *value, *line_start, *line_end;
	zend_string **lines, *line;
	smart_str new_text = {0};
	zval range;
	uint32_t count, capacity, i;
	size_t offset, next_offset, first_use_offset, last_use_end, original_length;
	bool has_newline;

	value = ZSTR_VAL(document->text);
	lines = NULL;
	count = 0;
	capacity = 0;
	offset = 0;
	first_use_offset = (size_t) -1;
	last_use_end = 0;
	while (offset < ZSTR_LEN(document->text)) {
		line_start = value + offset;
		line_end = memchr(line_start, '\n', ZSTR_LEN(document->text) - offset);
		has_newline = line_end != NULL;
		if (!line_end) {
			line_end = value + ZSTR_LEN(document->text);
			next_offset = ZSTR_LEN(document->text);
		} else {
			next_offset = (size_t) (line_end - value) + 1;
		}

		if (lsp_refactor_trimmed_line_starts_with(line_start, line_end, "use") && lsp_refactor_line_contains_semicolon(line_start, line_end)) {
			if (first_use_offset == (size_t) -1) {
				first_use_offset = offset;
			}
			last_use_end = next_offset;
			line = lsp_refactor_trim_line(line_start, line_end);
			lsp_refactor_sorted_line_insert(&lines, &count, &capacity, line);
		} else if (lsp_refactor_line_starts_declaration(line_start, line_end)) {
			break;
		}

		offset = next_offset;
		if (!has_newline) {
			break;
		}
	}

	if (count == 0 || first_use_offset == (size_t) -1 || last_use_end <= first_use_offset) {
		lsp_refactor_lines_destroy(lines, count);
		return false;
	}

	for (i = 0; i < count; i++) {
		smart_str_append(&new_text, lines[i]);
		smart_str_appendc(&new_text, '\n');
	}

	smart_str_0(&new_text);
	lsp_refactor_lines_destroy(lines, count);

	original_length = last_use_end - first_use_offset;
	if (new_text.s && ZSTR_LEN(new_text.s) == original_length && memcmp(ZSTR_VAL(new_text.s), value + first_use_offset, original_length) == 0) {
		smart_str_free(&new_text);
		return false;
	}

	array_init(text_edit);

	lsp_range_from_offsets(document->text, first_use_offset, last_use_end, &range);
	add_assoc_zval(text_edit, "range", &range);
	add_assoc_str(text_edit, "newText", new_text.s);

	return true;
}

static inline void lsp_refactor_add_organize_imports_action(zval *actions, lsp_document *document)
{
	zval action, edit, changes, text_edits, text_edit;

	ZVAL_UNDEF(&text_edit);
	if (!lsp_refactor_organize_imports_edit(document, &text_edit)) {
		return;
	}

	array_init(&text_edits);
	add_next_index_zval(&text_edits, &text_edit);
	array_init(&edit);
	array_init(&changes);
	add_assoc_zval_ex(&changes, ZSTR_VAL(document->uri), ZSTR_LEN(document->uri), &text_edits);
	add_assoc_zval(&edit, "changes", &changes);

	array_init(&action);
	add_assoc_string(&action, "title", "Organize Imports");
	add_assoc_string(&action, "kind", "source.organizeImports");
	add_assoc_zval(&action, "edit", &edit);
	add_next_index_zval(actions, &action);
}

static inline void lsp_refactor_add_import_action(zval *actions, lsp_document *document, char kind, const char *fqcn, size_t fqcn_length)
{
	zend_string *current_namespace, *import_text, *title;
	zval action, edit, changes, text_edits, text_edit, range;
	size_t insert_offset;
	bool after_existing_use;

	current_namespace = lsp_document_namespace(document->text);
	if (lsp_symbol_in_current_namespace(current_namespace, fqcn, fqcn_length) || lsp_document_has_import(document->text, kind, fqcn)) {
		if (current_namespace != zend_empty_string) {
			zend_string_release(current_namespace);
		}
		return;
	}
	if (current_namespace != zend_empty_string) {
		zend_string_release(current_namespace);
	}

	insert_offset = lsp_import_insert_offset(document->text, &after_existing_use);
	import_text = lsp_symbol_import_text(kind, fqcn, after_existing_use);
	array_init(&text_edit);
	lsp_range_from_offsets(document->text, insert_offset, insert_offset, &range);
	add_assoc_zval(&text_edit, "range", &range);
	add_assoc_str(&text_edit, "newText", import_text);
	array_init(&text_edits);
	add_next_index_zval(&text_edits, &text_edit);
	array_init(&changes);
	add_assoc_zval_ex(&changes, ZSTR_VAL(document->uri), ZSTR_LEN(document->uri), &text_edits);
	array_init(&edit);
	add_assoc_zval(&edit, "changes", &changes);
	title = strpprintf(0, "Import %.*s", (int) fqcn_length, fqcn);
	array_init(&action);
	add_assoc_str(&action, "title", title);
	add_assoc_string(&action, "kind", "quickfix");
	add_assoc_zval(&action, "edit", &edit);
	add_next_index_zval(actions, &action);
}

static inline void lsp_refactor_add_import_actions(lsp_server *server, zval *actions, lsp_document *document, zend_string *word)
{
	const char *label_value;
	lsp_symbol_index_header *header;
	uint32_t i, added;
	size_t fqcn_length, path_length, label_length;
	char *cursor, *end, kind, *fqcn, *path;

	if (!server->symbol_index.available || !server->symbol_index.addr || ZSTR_LEN(word) == 0 || lsp_refactor_word_is_variable(word)) {
		return;
	}

	header = (lsp_symbol_index_header *) server->symbol_index.addr;
	if (header->magic != LSP_SYMBOL_INDEX_MAGIC || header->used > header->capacity) {
		return;
	}

	added = 0;
	cursor = ((char *) server->symbol_index.addr) + sizeof(lsp_symbol_index_header);
	end = ((char *) server->symbol_index.addr) + header->used;
	for (i = 0; i < header->symbol_count && cursor < end && added < 20; i++) {
		kind = *cursor++;
		fqcn = cursor;
		fqcn_length = strlen(fqcn);
		path = fqcn + fqcn_length + 1;
		if (path >= end) {
			break;
		}

		path_length = strlen(path);
		cursor = path + path_length + 1;
		if (cursor > end || lsp_path_value_contains_analysis_helper(path, path_length)) {
			continue;
		}

		label_value = lsp_basename_from_fqcn(fqcn, fqcn_length, &label_length);
		if (!lsp_refactor_string_equals_zstr_ci(label_value, label_length, word)) {
			continue;
		}

		lsp_refactor_add_import_action(actions, document, kind, fqcn, fqcn_length);
		added++;
	}
}

static inline bool lsp_refactor_header_has_name(zend_string *contents, size_t class_start, size_t body_start, const char *keyword, zend_string *target)
{
	const char *value, *header_end, *p, *name_start, *name_end;
	zend_string *raw, *resolved;
	size_t keyword_end;
	bool found;

	value = ZSTR_VAL(contents);
	header_end = value + (body_start > 0 ? body_start - 1 : body_start);
	p = value + class_start;
	found = false;
	while (p < header_end) {
		if (!lsp_keyword_at_slice(value, (size_t) (p - value), (size_t) (header_end - value), keyword, &keyword_end)) {
			p++;
			continue;
		}

		p = value + keyword_end;
		while (p < header_end && *p != '{') {
			while (p < header_end && (isspace((unsigned char) *p) || *p == ',')) {
				p++;
			}

			name_start = p;
			while (p < header_end && (lsp_doc_is_identifier_char(*p) || *p == '\\')) {
				p++;
			}
			name_end = p;
			if (name_end > name_start) {
				raw = zend_string_init(name_start, name_end - name_start, 0);
				resolved = lsp_resolve_class_name(contents, raw);
				zend_string_release(raw);
				if (resolved && lsp_refactor_zstr_equals_ci(resolved, target)) {
					zend_string_release(resolved);
					found = true;
					break;
				}
				if (resolved) {
					zend_string_release(resolved);
				}
			}

			while (p < header_end && *p != ',' && *p != '{') {
				p++;
			}
			if (p < header_end && *p == '{') {
				break;
			}
		}
		break;
	}

	return found;
}

static inline bool lsp_refactor_class_matches_implementation(zend_string *contents, zend_string *candidate, zend_string *target)
{
	zend_long body_depth;
	size_t class_start, body_start, body_end;

	body_depth = 0;
	class_start = 0;
	body_start = 0;
	body_end = 0;
	if (!lsp_find_class_header_for_name(contents, candidate, &class_start, &body_start, &body_end, &body_depth)) {
		return false;
	}

	return lsp_refactor_header_has_name(contents, class_start, body_start, "extends", target) ||
		lsp_refactor_header_has_name(contents, class_start, body_start, "implements", target)
	;
}

static inline bool lsp_refactor_class_location(zend_string *path, zend_string *contents, zend_string *class_name, zval *location)
{
	zend_long body_depth;
	zval tokens_zv, *token, *name_token;
	HashTable *tokens;
	uint32_t i, count, name_index;
	size_t class_start, body_start, body_end, name_start, name_end;
	bool found;

	body_depth = 0;
	class_start = 0;
	body_start = 0;
	body_end = 0;
	found = false;
	ZVAL_UNDEF(&tokens_zv);
	lsp_lsparrot_tokens_to_zval(&tokens_zv, contents);
	if (Z_TYPE(tokens_zv) == IS_ARRAY && lsp_find_class_header_for_name(contents, class_name, &class_start, &body_start, &body_end, &body_depth)) {
		tokens = Z_ARRVAL(tokens_zv);
		count = zend_hash_num_elements(tokens);
		for (i = 0; i < count; i++) {
			token = zend_hash_index_find(tokens, i);
			if (!token || Z_TYPE_P(token) != IS_ARRAY || !lsp_token_is_class_like(token)) {
				continue;
			}

			if (!lsp_token_in_bounds(token, class_start, body_start)) {
				continue;
			}

			name_token = lsp_next_function_name_token_ex(tokens, i + 1, &name_index);
			if (!name_token) {
				continue;
			}

			name_start = (size_t) lsp_token_long(name_token, "offset", lsp_token_long(token, "offset", 0));
			name_end = name_start + (size_t) lsp_token_long(name_token, "length", 1);
			lsp_refactor_location_from_offsets(path, contents, name_start, name_end, location);
			found = true;
			break;
		}
	}

	if (!Z_ISUNDEF(tokens_zv)) {
		zval_ptr_dtor(&tokens_zv);
	}

	return found;
}

static inline void lsp_refactor_add_implementation_locations(lsp_server *server, zval *locations, zend_string *target)
{
	lsp_symbol_index_header *header;
	lsp_document *document;
	zend_string *candidate, *path_string, *contents;
	zval location;
	uint32_t i;
	size_t fqcn_length, path_length;
	char *cursor, *end, kind, *fqcn, *path;
	bool owns_contents;

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
			(kind != LSP_SYMBOL_CLASS && kind != LSP_SYMBOL_INTERFACE) ||
			lsp_path_value_contains_analysis_helper(path, path_length)
		) {
			continue;
		}

		candidate = zend_string_init(fqcn, fqcn_length, 0);
		if (lsp_refactor_zstr_equals_ci(candidate, target)) {
			zend_string_release(candidate);
			continue;
		}

		path_string = zend_string_init(path, path_length, 0);
		document = lsp_document_for_path(server, path_string);
		contents = document ? zend_string_copy(document->text) : lsp_read_file(path_string);
		owns_contents = document || contents != zend_empty_string;
		if (owns_contents && lsp_refactor_class_matches_implementation(contents, candidate, target)) {
			ZVAL_UNDEF(&location);
			if (lsp_refactor_class_location(path_string, contents, candidate, &location)) {
				add_next_index_zval(locations, &location);
			} else if (!Z_ISUNDEF(location)) {
				zval_ptr_dtor(&location);
			}
		}

		if (owns_contents) {
			zend_string_release(contents);
		}
		zend_string_release(path_string);
		zend_string_release(candidate);
	}
}

static inline uint32_t lsp_refactor_add_text_edits_for_word(zval *text_edits, zend_string *contents, zend_string *word, zend_string *new_text)
{
	zval tokens_zv, *token, edit, range;
	HashTable *tokens;
	uint32_t i, count, added;
	size_t start, end;
	bool variable;

	variable = lsp_refactor_word_is_variable(word);
	added = 0;
	ZVAL_UNDEF(&tokens_zv);
	lsp_lsparrot_tokens_to_zval(&tokens_zv, contents);
	if (Z_TYPE(tokens_zv) != IS_ARRAY) {
		if (!Z_ISUNDEF(tokens_zv)) {
			zval_ptr_dtor(&tokens_zv);
		}

		return 0;
	}

	tokens = Z_ARRVAL(tokens_zv);
	count = zend_hash_num_elements(tokens);
	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY || !lsp_refactor_token_match(token, word, variable, &start, &end)) {
			continue;
		}

		array_init(&edit);
		lsp_range_from_offsets(contents, start, end, &range);
		add_assoc_zval(&edit, "range", &range);
		add_assoc_str(&edit, "newText", zend_string_copy(new_text));
		add_next_index_zval(text_edits, &edit);
		added++;
	}

	zval_ptr_dtor(&tokens_zv);

	return added;
}

static inline void lsp_refactor_add_file_rename_changes(zval *changes, zend_string *uri, zend_string *contents, zend_string *word, zend_string *new_text)
{
	zval text_edits;

	array_init(&text_edits);
	if (lsp_refactor_add_text_edits_for_word(&text_edits, contents, word, new_text) == 0) {
		zval_ptr_dtor(&text_edits);
		return;
	}

	add_assoc_zval_ex(changes, ZSTR_VAL(uri), ZSTR_LEN(uri), &text_edits);
}

static inline void lsp_refactor_add_project_rename_changes(lsp_server *server, zval *changes, lsp_document *primary_document, zend_string *word, zend_string *new_text)
{
	lsp_symbol_index_header *header;
	lsp_document *document;
	zend_string *path_string, *uri, *contents;
	HashTable visited;
	uint32_t i;
	size_t fqcn_length, path_length;
	char *cursor, *end, kind, *fqcn, *path;
	bool owns_contents;

	if (!server->symbol_index.available || !server->symbol_index.addr) {
		return;
	}

	header = (lsp_symbol_index_header *) server->symbol_index.addr;
	if (header->magic != LSP_SYMBOL_INDEX_MAGIC || header->used > header->capacity) {
		return;
	}

	zend_hash_init(&visited, 128, NULL, NULL, 0);
	zend_hash_add_empty_element(&visited, primary_document->path);
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
		if (cursor > end || lsp_path_value_contains_analysis_helper(path, path_length) || lsp_path_value_contains_vendor(path, path_length)) {
			continue;
		}

		path_string = zend_string_init(path, path_length, 0);
		if (zend_hash_exists(&visited, path_string)) {
			zend_string_release(path_string);
			continue;
		}

		zend_hash_add_empty_element(&visited, path_string);

		document = lsp_document_for_path(server, path_string);
		contents = document ? zend_string_copy(document->text) : lsp_read_file(path_string);
		owns_contents = document || contents != zend_empty_string;
		if (owns_contents) {
			uri = lsp_uri_from_path(path_string);
			lsp_refactor_add_file_rename_changes(changes, uri, contents, word, new_text);
			zend_string_release(uri);
			zend_string_release(contents);
		}

		zend_string_release(path_string);
	}

	zend_hash_destroy(&visited);
}

static inline zend_string *lsp_refactor_rename_text(zend_string *word, zend_string *new_name)
{
	if (lsp_refactor_word_is_variable(word) && (ZSTR_LEN(new_name) == 0 || ZSTR_VAL(new_name)[0] != '$')) {
		return strpprintf(0, "$%s", ZSTR_VAL(new_name));
	}

	return zend_string_copy(new_name);
}

static inline zend_string *lsp_refactor_formatted_text(zend_string *text)
{
	const char *value, *line_start, *line_end, *trim_end;
	smart_str formatted = {0};
	size_t offset, next_offset;
	bool has_newline;

	value = ZSTR_VAL(text);
	offset = 0;
	while (offset < ZSTR_LEN(text)) {
		line_start = value + offset;
		line_end = memchr(line_start, '\n', ZSTR_LEN(text) - offset);
		has_newline = line_end != NULL;
		if (!line_end) {
			line_end = value + ZSTR_LEN(text);
			next_offset = ZSTR_LEN(text);
		} else {
			next_offset = (size_t) (line_end - value) + 1;
		}

		trim_end = line_end;
		if (trim_end > line_start && trim_end[-1] == '\r') {
			trim_end--;
		}

		while (trim_end > line_start && (trim_end[-1] == ' ' || trim_end[-1] == '\t')) {
			trim_end--;
		}

		smart_str_appendl(&formatted, line_start, trim_end - line_start);

		if (has_newline) {
			smart_str_appendc(&formatted, '\n');
		}

		offset = next_offset;

		if (!has_newline) {
			break;
		}
	}

	if (!formatted.s || ZSTR_LEN(formatted.s) == 0 || ZSTR_VAL(formatted.s)[ZSTR_LEN(formatted.s) - 1] != '\n') {
		smart_str_appendc(&formatted, '\n');
	}

	smart_str_0(&formatted);

	return formatted.s;
}

static inline zend_string *lsp_refactor_formatted_slice(zend_string *text, size_t start_offset, size_t end_offset)
{
	const char *value, *line_start, *line_end, *trim_end, *slice_end;
	smart_str formatted = {0};
	size_t offset, next_offset;
	bool has_newline;

	value = ZSTR_VAL(text);
	if (start_offset > ZSTR_LEN(text)) {
		start_offset = ZSTR_LEN(text);
	}

	if (end_offset > ZSTR_LEN(text)) {
		end_offset = ZSTR_LEN(text);
	}

	if (end_offset < start_offset) {
		end_offset = start_offset;
	}

	offset = start_offset;
	slice_end = value + end_offset;
	while (offset < end_offset) {
		line_start = value + offset;
		line_end = memchr(line_start, '\n', end_offset - offset);
		has_newline = line_end != NULL;

		if (!line_end) {
			line_end = slice_end;
			next_offset = end_offset;
		} else {
			next_offset = (size_t) (line_end - value) + 1;
		}

		trim_end = line_end;
		if (trim_end > line_start && trim_end[-1] == '\r') {
			trim_end--;
		}

		while (trim_end > line_start && (trim_end[-1] == ' ' || trim_end[-1] == '\t')) {
			trim_end--;
		}

		smart_str_appendl(&formatted, line_start, trim_end - line_start);

		if (has_newline) {
			smart_str_appendc(&formatted, '\n');
		}

		offset = next_offset;

		if (!has_newline) {
			break;
		}
	}

	smart_str_0(&formatted);

	if (!formatted.s) {
		return zend_empty_string;
	}

	return formatted.s;
}

static inline zend_string *lsp_inlay_key(zend_string *name)
{
	zend_string *key;
	size_t i;

	key = zend_string_alloc(ZSTR_LEN(name), 0);

	for (i = 0; i < ZSTR_LEN(name); i++) {
		ZSTR_VAL(key)[i] = (char) tolower((unsigned char) ZSTR_VAL(name)[i]);
	}

	ZSTR_VAL(key)[ZSTR_LEN(name)] = '\0';

	return key;
}

static inline zval *lsp_inlay_next_non_whitespace(HashTable *tokens, uint32_t start, uint32_t *index)
{
	zval *token;
	uint32_t i, count;

	count = zend_hash_num_elements(tokens);
	for (i = start; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY || lsp_token_name_equals(token, "T_WHITESPACE")) {
			continue;
		}

		*index = i;

		return token;
	}

	return NULL;
}

static inline bool lsp_inlay_token_is_call_name(HashTable *tokens, uint32_t index)
{
	zval *previous;
	uint32_t previous_index;

	if (index == 0) {
		return true;
	}

	previous = NULL;
	previous_index = 0;
	while (index > 0) {
		index--;

		previous = zend_hash_index_find(tokens, index);
		if (!previous || Z_TYPE_P(previous) != IS_ARRAY || lsp_token_name_equals(previous, "T_WHITESPACE")) {
			continue;
		}

		previous_index = index;
		break;
	}

	if (!previous) {
		return true;
	}

	(void) previous_index;
	return !lsp_token_name_equals(previous, "T_FUNCTION") &&
		!lsp_token_name_equals(previous, "T_CLASS") &&
		!lsp_token_name_equals(previous, "T_INTERFACE") &&
		!lsp_token_name_equals(previous, "T_TRAIT") &&
		!lsp_token_name_equals(previous, "T_ENUM") &&
		!lsp_token_name_equals(previous, "T_NEW") &&
		!lsp_token_name_equals(previous, "T_NS_SEPARATOR")
	;
}

static inline bool lsp_inlay_find_call_parens(HashTable *tokens, uint32_t name_index, uint32_t *open_index, size_t *open_offset, size_t *close_offset)
{
	zend_string *text;
	zval *token;
	uint32_t next_index;

	token = lsp_inlay_next_non_whitespace(tokens, name_index + 1, &next_index);
	if (!token || !lsp_token_is_char(token, '(')) {
		return false;
	}

	text = lsp_token_string(token, "text");
	if (!text || ZSTR_LEN(text) != 1) {
		return false;
	}

	*open_index = next_index;
	*open_offset = (size_t) lsp_token_long(token, "offset", 0);
	*close_offset = 0;

	return true;
}

static inline bool lsp_inlay_find_close_paren(zend_string *text, size_t open_offset, size_t *close_offset)
{
	const char *value;
	uint32_t depth;
	size_t i;
	bool escaped;
	char quote;

	value = ZSTR_VAL(text);
	depth = 0;
	quote = '\0';
	escaped = false;
	for (i = open_offset; i < ZSTR_LEN(text); i++) {
		if (quote != '\0') {
			if (escaped) {
				escaped = false;
			} else if (value[i] == '\\') {
				escaped = true;
			} else if (value[i] == quote) {
				quote = '\0';
			}
			continue;
		}

		if (value[i] == '\'' || value[i] == '"') {
			quote = value[i];
			continue;
		}

		if (value[i] == '(') {
			depth++;
		} else if (value[i] == ')' && depth > 0) {
			depth--;
			if (depth == 0) {
				*close_offset = i;

				return true;
			}
		}
	}

	return false;
}

static inline void lsp_inlay_add_param_map_entry(HashTable *param_map, zend_string *name, zval *params)
{
	zend_string *key;

	key = lsp_inlay_key(name);
	zend_hash_update(param_map, key, params);
	zend_string_release(key);
}

static inline void lsp_inlay_collect_function_params(HashTable *tokens, uint32_t function_index, uint32_t name_index, HashTable *param_map)
{
	zend_string *name, *variable, *label;
	zval params, *token;
	uint32_t i, count;
	size_t open_offset, close_offset;
	bool saw_open;

	name = lsp_token_string(zend_hash_index_find(tokens, name_index), "text");
	if (!name) {
		return;
	}

	count = zend_hash_num_elements(tokens);
	open_offset = 0;
	close_offset = 0;
	saw_open = false;
	array_init(&params);

	for (i = function_index + 1; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY) {
			continue;
		}

		if (!saw_open) {
			if (lsp_token_is_char(token, '(')) {
				open_offset = (size_t) lsp_token_long(token, "offset", 0);
				saw_open = true;
			}
			continue;
		}

		if (lsp_token_is_char(token, ')')) {
			close_offset = (size_t) lsp_token_long(token, "offset", 0);
			break;
		}

		if (!lsp_token_name_equals(token, "T_VARIABLE")) {
			continue;
		}

		if ((size_t) lsp_token_long(token, "offset", 0) <= open_offset) {
			continue;
		}

		variable = lsp_token_string(token, "text");
		if (!variable || ZSTR_LEN(variable) <= 1) {
			continue;
		}

		label = strpprintf(0, "%.*s:", (int) (ZSTR_LEN(variable) - 1), ZSTR_VAL(variable) + 1);

		add_next_index_str(&params, label);
	}

	if (zend_hash_num_elements(Z_ARRVAL(params)) == 0 || close_offset <= open_offset) {
		zval_ptr_dtor(&params);

		return;
	}

	lsp_inlay_add_param_map_entry(param_map, name, &params);
}

static inline void lsp_inlay_collect_param_map(lsp_document *document, HashTable *param_map)
{
	zval *tokens_zv, *token, *name_token;
	HashTable *tokens;
	uint32_t i, count, name_index;

	tokens_zv = zend_hash_str_find(Z_ARRVAL(document->lsparrot), "tokens", sizeof("tokens") - 1);
	if (!tokens_zv || Z_TYPE_P(tokens_zv) != IS_ARRAY) {
		return;
	}

	tokens = Z_ARRVAL_P(tokens_zv);
	count = zend_hash_num_elements(tokens);
	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY || !lsp_token_name_equals(token, "T_FUNCTION")) {
			continue;
		}

		name_token = lsp_next_function_name_token_ex(tokens, i + 1, &name_index);
		if (!name_token) {
			continue;
		}

		lsp_inlay_collect_function_params(tokens, i, name_index, param_map);
	}
}

static inline void lsp_inlay_add_hint(zval *items, zend_string *text, size_t offset, zend_string *label)
{
	zval hint, range, position, *start;

	array_init(&hint);
	lsp_range_from_offsets(text, offset, offset, &range);
	start = zend_hash_str_find(Z_ARRVAL(range), "start", sizeof("start") - 1);
	if (start) {
		ZVAL_COPY(&position, start);
		add_assoc_zval(&hint, "position", &position);
	}

	add_assoc_str(&hint, "label", zend_string_copy(label));
	add_assoc_long(&hint, "kind", 2);
	add_assoc_bool(&hint, "paddingRight", true);
	add_next_index_zval(items, &hint);

	zval_ptr_dtor(&range);
}

static inline void lsp_inlay_add_call_hints(zval *items, zend_string *text, size_t open_offset, size_t close_offset, zval *params)
{
	const char *value;
	zval *label;
	size_t i, hint_offset;
	uint32_t depth, param_index;
	bool escaped, need_hint;
	char quote;

	value = ZSTR_VAL(text);
	depth = 0;
	param_index = 0;
	quote = '\0';
	escaped = false;
	need_hint = true;
	hint_offset = open_offset + 1;
	for (i = open_offset + 1; i < close_offset; i++) {
		if (quote != '\0') {
			if (escaped) {
				escaped = false;
			} else if (value[i] == '\\') {
				escaped = true;
			} else if (value[i] == quote) {
				quote = '\0';
			}
			continue;
		}

		if (value[i] == '\'' || value[i] == '"') {
			quote = value[i];
			continue;
		}

		if (need_hint && !isspace((unsigned char) value[i])) {
			label = zend_hash_index_find(Z_ARRVAL_P(params), param_index);
			if (label && Z_TYPE_P(label) == IS_STRING) {
				lsp_inlay_add_hint(items, text, hint_offset, Z_STR_P(label));
			}
			need_hint = false;
		}

		if (value[i] == '(' || value[i] == '[' || value[i] == '{') {
			depth++;
		} else if ((value[i] == ')' || value[i] == ']' || value[i] == '}') && depth > 0) {
			depth--;
		} else if (value[i] == ',' && depth == 0) {
			param_index++;
			hint_offset = i + 1;
			while (hint_offset < close_offset && isspace((unsigned char) value[hint_offset])) {
				hint_offset++;
			}
			need_hint = true;
		}
	}
}

static inline void lsp_inlay_add_document_hints(lsp_document *document, HashTable *param_map, zval *items)
{
	zend_string *name, *key;
	zval *tokens_zv, *token, *params;
	HashTable *tokens;
	uint32_t i, count, open_index;
	size_t open_offset, close_offset;

	tokens_zv = zend_hash_str_find(Z_ARRVAL(document->lsparrot), "tokens", sizeof("tokens") - 1);
	if (!tokens_zv || Z_TYPE_P(tokens_zv) != IS_ARRAY) {
		return;
	}

	tokens = Z_ARRVAL_P(tokens_zv);
	count = zend_hash_num_elements(tokens);
	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY || !lsp_token_name_equals(token, "T_STRING") || !lsp_inlay_token_is_call_name(tokens, i)) {
			continue;
		}

		name = lsp_token_string(token, "text");
		if (!name) {
			continue;
		}

		key = lsp_inlay_key(name);
		params = zend_hash_find(param_map, key);
		zend_string_release(key);
		if (!params || Z_TYPE_P(params) != IS_ARRAY || zend_hash_num_elements(Z_ARRVAL_P(params)) == 0) {
			continue;
		}

		open_index = 0;
		open_offset = 0;
		close_offset = 0;
		if (!lsp_inlay_find_call_parens(tokens, i, &open_index, &open_offset, &close_offset) ||
			!lsp_inlay_find_close_paren(document->text, open_offset, &close_offset)
		) {
			continue;
		}

		(void) open_index;
		lsp_inlay_add_call_hints(items, document->text, open_offset, close_offset, params);
	}
}

extern void lsp_lsparrot_references(lsp_server *server, zval *return_value, lsp_document *document, zval *params)
{
	zend_long line, character;
	zend_string *word;
	zval *position;
	size_t offset;

	array_init(return_value);
	position = lsp_array_find(params, "position");
	lsp_position_from_zval(position, &line, &character);
	offset = lsp_offset_at(document->text, line, character);
	word = lsp_word_at(document->text, offset);
	if (ZSTR_LEN(word) == 0) {
		zend_string_release(word);
		return;
	}

	lsp_refactor_add_token_locations(return_value, document->path, document->text, word);
	if (!lsp_refactor_word_is_variable(word)) {
		lsp_refactor_add_project_reference_locations(server, return_value, document, word);
	}

	zend_string_release(word);
}

extern void lsp_lsparrot_document_highlight(lsp_server *server, zval *return_value, lsp_document *document, zval *position)
{
	zend_long line, character;
	zend_string *word;
	size_t offset;

	(void) server;
	array_init(return_value);
	lsp_position_from_zval(position, &line, &character);
	offset = lsp_offset_at(document->text, line, character);
	word = lsp_word_at(document->text, offset);
	if (ZSTR_LEN(word) > 0) {
		lsp_refactor_add_document_highlights(return_value, document->text, word);
	}
	zend_string_release(word);
}

extern void lsp_lsparrot_implementation(lsp_server *server, zval *return_value, lsp_document *document, zval *position)
{
	zend_long line, character;
	zend_string *word, *target;
	size_t offset;

	array_init(return_value);
	lsp_position_from_zval(position, &line, &character);
	offset = lsp_offset_at(document->text, line, character);
	word = lsp_word_at(document->text, offset);
	if (ZSTR_LEN(word) == 0) {
		zend_string_release(word);
		return;
	}

	target = lsp_resolve_class_name(document->text, word);
	if (!target) {
		target = zend_string_copy(word);
	}

	lsp_refactor_add_implementation_locations(server, return_value, target);
	zend_string_release(target);
	zend_string_release(word);
}

extern void lsp_lsparrot_code_action(lsp_server *server, zval *return_value, lsp_document *document, zval *params)
{
	zend_string *word;
	zval *range;
	size_t offset, word_start, word_end;

	array_init(return_value);
	if (lsp_refactor_context_only_has(params, "source.organizeImports")) {
		lsp_refactor_add_organize_imports_action(return_value, document);
	}

	if (!lsp_refactor_context_only_has(params, "quickfix")) {
		return;
	}

	range = lsp_array_find(params, "range");
	if (!lsp_refactor_range_start_offset(document->text, range, &offset)) {
		return;
	}

	if (!lsp_refactor_word_bounds(document->text, offset, &word_start, &word_end)) {
		return;
	}

	word = zend_string_init(ZSTR_VAL(document->text) + word_start, word_end - word_start, 0);
	lsp_refactor_add_import_actions(server, return_value, document, word);

	zend_string_release(word);
}

extern void lsp_lsparrot_prepare_rename(lsp_server *server, zval *return_value, lsp_document *document, zval *position)
{
	zend_long line, character;
	zend_string *word;
	zval range;
	size_t offset, word_start, word_end;

	(void) server;
	lsp_position_from_zval(position, &line, &character);
	offset = lsp_offset_at(document->text, line, character);
	word = lsp_word_at(document->text, offset);
	if (ZSTR_LEN(word) == 0 || lsp_refactor_word_is_qualified(word) || !lsp_refactor_word_bounds(document->text, offset, &word_start, &word_end)) {
		zend_string_release(word);
		ZVAL_NULL(return_value);
		return;
	}

	lsp_range_from_offsets(document->text, word_start, word_end, &range);
	ZVAL_COPY_VALUE(return_value, &range);
	zend_string_release(word);
}

extern void lsp_lsparrot_rename(lsp_server *server, zval *return_value, lsp_document *document, zval *params)
{
	zend_long line, character;
	zend_string *word, *new_name, *new_text;
	zval *position, changes;
	size_t offset;
	bool variable;

	position = lsp_array_find(params, "position");
	new_name = lsp_array_string(params, "newName");

	array_init(return_value);
	array_init(&changes);

	if (!new_name || ZSTR_LEN(new_name) == 0) {
		add_assoc_zval(return_value, "changes", &changes);
		return;
	}

	lsp_position_from_zval(position, &line, &character);
	offset = lsp_offset_at(document->text, line, character);
	word = lsp_word_at(document->text, offset);
	if (ZSTR_LEN(word) == 0 || lsp_refactor_word_is_qualified(word)) {
		zend_string_release(word);
		add_assoc_zval(return_value, "changes", &changes);
		return;
	}

	variable = lsp_refactor_word_is_variable(word);
	new_text = lsp_refactor_rename_text(word, new_name);
	lsp_refactor_add_file_rename_changes(&changes, document->uri, document->text, word, new_text);
	if (!variable) {
		lsp_refactor_add_project_rename_changes(server, &changes, document, word, new_text);
	}

	zend_string_release(new_text);
	zend_string_release(word);
	add_assoc_zval(return_value, "changes", &changes);
}

extern void lsp_lsparrot_formatting(lsp_server *server, zval *return_value, lsp_document *document)
{
	zend_string *formatted;
	zval edit, range;

	(void) server;

	array_init(return_value);
	formatted = lsp_refactor_formatted_text(document->text);
	if (ZSTR_LEN(formatted) == ZSTR_LEN(document->text) && memcmp(ZSTR_VAL(formatted), ZSTR_VAL(document->text), ZSTR_LEN(formatted)) == 0) {
		zend_string_release(formatted);
		return;
	}

	array_init(&edit);
	lsp_range_from_offsets(document->text, 0, ZSTR_LEN(document->text), &range);
	add_assoc_zval(&edit, "range", &range);
	add_assoc_str(&edit, "newText", formatted);
	add_next_index_zval(return_value, &edit);
}

extern void lsp_lsparrot_range_formatting(lsp_server *server, zval *return_value, lsp_document *document, zval *params)
{
	zend_string *formatted;
	zval *range_param, edit, range;
	size_t start_offset, end_offset, original_length;

	(void) server;

	array_init(return_value);
	range_param = lsp_array_find(params, "range");
	if (!lsp_refactor_range_offsets(document->text, range_param, &start_offset, &end_offset)) {
		return;
	}

	formatted = lsp_refactor_formatted_slice(document->text, start_offset, end_offset);
	original_length = end_offset - start_offset;
	if (ZSTR_LEN(formatted) == original_length && memcmp(ZSTR_VAL(formatted), ZSTR_VAL(document->text) + start_offset, original_length) == 0) {
		zend_string_release(formatted);
		return;
	}

	array_init(&edit);
	lsp_range_from_offsets(document->text, start_offset, end_offset, &range);
	add_assoc_zval(&edit, "range", &range);
	add_assoc_str(&edit, "newText", formatted);
	add_next_index_zval(return_value, &edit);
}

extern void lsp_lsparrot_inlay_hint(lsp_server *server, zval *return_value, lsp_document *document, zval *params)
{
	HashTable param_map;

	(void) server;
	(void) params;

	array_init(return_value);
	zend_hash_init(&param_map, 32, NULL, ZVAL_PTR_DTOR, 0);
	lsp_inlay_collect_param_map(document, &param_map);
	lsp_inlay_add_document_hints(document, &param_map, return_value);
	zend_hash_destroy(&param_map);
}
