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

static inline void lsp_symbol_index_add_key(lsp_symbol_index *region, char kind, zend_string *fqcn, size_t record_offset)
{
	zend_string *key;
	zval offset_zv;

	if (!region->keys_initialized) {
		return;
	}

	key = lsp_symbol_index_key(kind, fqcn);
	ZVAL_LONG(&offset_zv, (zend_long) record_offset);
	zend_hash_add(&region->symbol_keys, key, &offset_zv);
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

extern void lsp_symbol_index_table_invalidate(lsp_symbol_index *region)
{
	if (region->entries) {
		efree(region->entries);
		region->entries = NULL;
	}

	if (region->segments) {
		efree(region->segments);
		region->segments = NULL;
	}

	region->entry_count = 0;
	region->segment_count = 0;
	region->table_valid = false;
}

extern void lsp_symbol_index_destroy(lsp_symbol_index *region)
{
	if (region->keys_initialized) {
		zend_hash_destroy(&region->symbol_keys);
	}

	lsp_symbol_index_table_invalidate(region);

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

	lsp_symbol_index_table_invalidate(region);
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
	lsp_symbol_index_add_key(region, kind, fqcn, header->used);
	header->used += need;
	header->symbol_count++;
	lsp_symbol_index_table_invalidate(region);

	return true;
}

extern const char *lsp_symbol_entry_fqcn(lsp_symbol_index *region, const lsp_symbol_entry *entry)
{
	return ((const char *) region->addr) + entry->record_offset + 1;
}

extern const char *lsp_symbol_entry_path(lsp_symbol_index *region, const lsp_symbol_entry *entry)
{
	return lsp_symbol_entry_fqcn(region, entry) + entry->fqcn_length + 1;
}

/* qsort(3) has no user-data parameter in C99, so the comparator reads the
 * region being sorted from this file-local pointer. The server is strictly
 * single-threaded, which makes this safe. */
static lsp_symbol_index *lsp_symbol_sort_region = NULL;

static inline int lsp_symbol_segment_compare(const void *left, const void *right)
{
	const lsp_symbol_segment *a = (const lsp_symbol_segment *) left, *b = (const lsp_symbol_segment *) right;
	const char *sa, *sb;
	int result;

	sa = lsp_symbol_entry_fqcn(lsp_symbol_sort_region, &lsp_symbol_sort_region->entries[a->entry_index]) + a->segment_offset;
	sb = lsp_symbol_entry_fqcn(lsp_symbol_sort_region, &lsp_symbol_sort_region->entries[b->entry_index]) + b->segment_offset;
	result = strcasecmp(sa, sb);
	if (result != 0) {
		return result;
	}

	if (a->entry_index != b->entry_index) {
		return a->entry_index < b->entry_index ? -1 : 1;
	}

	return a->segment_offset < b->segment_offset ? -1 : (a->segment_offset > b->segment_offset ? 1 : 0);
}

extern void lsp_symbol_index_table_ensure(lsp_symbol_index *region)
{
	lsp_symbol_index_header *header;
	lsp_symbol_entry *entry;
	uint32_t i, j, segment_capacity;
	size_t fqcn_length, path_length;
	char *cursor, *end, *base, *fqcn, *path, kind;

	if (region->table_valid) {
		return;
	}

	lsp_symbol_index_table_invalidate(region);
	region->table_valid = true;

	if (!region->available || !region->addr) {
		return;
	}

	header = (lsp_symbol_index_header *) region->addr;
	if (header->magic != LSP_SYMBOL_INDEX_MAGIC || header->used > header->capacity || header->symbol_count == 0) {
		return;
	}

	base = (char *) region->addr;
	region->entries = safe_emalloc(header->symbol_count, sizeof(lsp_symbol_entry), 0);
	cursor = base + sizeof(lsp_symbol_index_header);
	end = base + header->used;
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

		if (kind == LSP_SYMBOL_TOMBSTONE) {
			continue;
		}

		entry = &region->entries[region->entry_count++];
		entry->record_offset = (uint32_t) ((fqcn - 1) - base);
		entry->fqcn_length = (uint32_t) fqcn_length;
		entry->path_length = (uint32_t) path_length;
		entry->kind = (uint8_t) kind;
		entry->flags = lsp_path_value_contains_vendor(path, path_length) ? LSP_SYMBOL_ENTRY_VENDOR : 0;
	}

	if (region->entry_count == 0) {
		return;
	}

	segment_capacity = region->entry_count * 2;
	region->segments = safe_emalloc(segment_capacity, sizeof(lsp_symbol_segment), 0);
	for (i = 0; i < region->entry_count; i++) {
		entry = &region->entries[i];
		fqcn = (char *) lsp_symbol_entry_fqcn(region, entry);
		for (j = 0; j < entry->fqcn_length; j++) {
			if (j != 0 && fqcn[j - 1] != '\\' && fqcn[j - 1] != '_') {
				continue;
			}

			if (region->segment_count >= segment_capacity) {
				segment_capacity *= 2;
				region->segments = safe_erealloc(region->segments, segment_capacity, sizeof(lsp_symbol_segment), 0);
			}

			region->segments[region->segment_count].entry_index = i;
			region->segments[region->segment_count].segment_offset = j;
			region->segment_count++;
		}
	}

	lsp_symbol_sort_region = region;
	qsort(region->segments, region->segment_count, sizeof(lsp_symbol_segment), lsp_symbol_segment_compare);
	lsp_symbol_sort_region = NULL;
}

extern void lsp_symbol_index_rebuild_keys(lsp_symbol_index *region)
{
	lsp_symbol_index_header *header;
	zend_string *fqcn_string;
	char *cursor, *end, *base, *fqcn, *path, kind;
	uint32_t i;
	size_t fqcn_length, path_length, record_offset;

	if (!region->available || !region->addr || !region->keys_initialized) {
		return;
	}

	zend_hash_clean(&region->symbol_keys);
	lsp_symbol_index_table_invalidate(region);

	header = (lsp_symbol_index_header *) region->addr;
	if (header->magic != LSP_SYMBOL_INDEX_MAGIC || header->used > header->capacity) {
		return;
	}

	base = (char *) region->addr;
	cursor = base + sizeof(lsp_symbol_index_header);
	end = base + header->used;
	for (i = 0; i < header->symbol_count && cursor < end; i++) {
		record_offset = (size_t) (cursor - base);
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

		if (kind == LSP_SYMBOL_TOMBSTONE) {
			continue;
		}

		fqcn_string = zend_string_init(fqcn, fqcn_length, 0);
		lsp_symbol_index_add_key(region, kind, fqcn_string, record_offset);
		zend_string_release(fqcn_string);
	}
}

extern uint32_t lsp_symbol_index_remove_path(lsp_symbol_index *region, zend_string *path)
{
	lsp_symbol_entry *entry;
	zend_string *fqcn_string, *key;
	const char *fqcn, *entry_path;
	char *base;
	uint32_t i, removed = 0;

	if (!region->available || !region->addr) {
		return 0;
	}

	lsp_symbol_index_table_ensure(region);

	base = (char *) region->addr;
	for (i = 0; i < region->entry_count; i++) {
		entry = &region->entries[i];
		if ((entry->flags & LSP_SYMBOL_ENTRY_DELETED) != 0) {
			continue;
		}

		entry_path = lsp_symbol_entry_path(region, entry);
		if (entry->path_length != ZSTR_LEN(path) || memcmp(entry_path, ZSTR_VAL(path), ZSTR_LEN(path)) != 0) {
			continue;
		}

		/* Tombstone the arena record (kept in place so the record walk stays
		 * intact), drop the dedup/lookup key, and hide the derived entry. */
		fqcn = lsp_symbol_entry_fqcn(region, entry);
		if (region->keys_initialized) {
			fqcn_string = zend_string_init(fqcn, entry->fqcn_length, 0);
			key = lsp_symbol_index_key((char) entry->kind, fqcn_string);
			zend_hash_del(&region->symbol_keys, key);
			zend_string_release(key);
			zend_string_release(fqcn_string);
		}

		base[entry->record_offset] = LSP_SYMBOL_TOMBSTONE;
		entry->flags |= LSP_SYMBOL_ENTRY_DELETED;
		removed++;
	}

	return removed;
}

extern const char *lsp_symbol_index_lookup(lsp_symbol_index *region, char expected_kind, zend_string *fqcn, char *found_kind, const char **stored_fqcn, size_t *path_length)
{
	static const char class_like_kinds[] = { LSP_SYMBOL_CLASS, LSP_SYMBOL_INTERFACE, LSP_SYMBOL_TRAIT, LSP_SYMBOL_ENUM };
	lsp_symbol_index_header *header;
	zend_string *key;
	zval *offset_zv;
	const char *probe_kinds, *base, *stored, *path, *best_path = NULL;
	size_t probe_count, i, record_offset, best_offset = 0, stored_length;
	char single_kind[1];

	if (!region->available || !region->addr || !region->keys_initialized) {
		return NULL;
	}

	header = (lsp_symbol_index_header *) region->addr;
	if (header->magic != LSP_SYMBOL_INDEX_MAGIC || header->used > header->capacity) {
		return NULL;
	}

	if (expected_kind == LSP_SYMBOL_CLASS) {
		probe_kinds = class_like_kinds;
		probe_count = sizeof(class_like_kinds);
	} else {
		single_kind[0] = expected_kind;
		probe_kinds = single_kind;
		probe_count = 1;
	}

	base = (const char *) region->addr;
	for (i = 0; i < probe_count; i++) {
		key = lsp_symbol_index_key(probe_kinds[i], fqcn);
		offset_zv = zend_hash_find(&region->symbol_keys, key);
		zend_string_release(key);
		if (!offset_zv || Z_TYPE_P(offset_zv) != IS_LONG) {
			continue;
		}

		record_offset = (size_t) Z_LVAL_P(offset_zv);
		if (record_offset + 1 >= header->used) {
			continue;
		}

		/* Keep the earliest arena record to preserve historical first-match
		 * semantics when multiple class-like kinds share one FQCN. */
		if (best_path && record_offset >= best_offset) {
			continue;
		}

		stored = base + record_offset + 1;
		stored_length = strlen(stored);
		path = stored + stored_length + 1;
		if (path >= base + header->used) {
			continue;
		}

		best_path = path;
		best_offset = record_offset;
		if (found_kind) {
			*found_kind = base[record_offset];
		}
		if (stored_fqcn) {
			*stored_fqcn = stored;
		}
		if (path_length) {
			*path_length = strlen(path);
		}
	}

	return best_path;
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
	zend_string *left_label = lsp_array_string(left, "label"), *right_label = lsp_array_string(right, "label"),
		*left_qualified_name, *right_qualified_name;
	zend_long left_kind = lsp_array_long(left, "kind", 0), right_kind = lsp_array_long(right, "kind", 0);
	zval *left_data, *right_data;
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

	if (left_length != right_length || memcmp(left_value, right_value, left_length) != 0) {
		return false;
	}

	/* Pre-encoded entries carry qualifiedName at the top level; classic zval
	 * items nest it under data. */
	left_qualified_name = lsp_array_string(left, "qualifiedName");
	right_qualified_name = lsp_array_string(right, "qualifiedName");
	if (!left_qualified_name) {
		left_data = lsp_array_find(left, "data");
		left_qualified_name = left_data && Z_TYPE_P(left_data) == IS_ARRAY ? lsp_array_string(left_data, "qualifiedName") : NULL;
	}
	if (!right_qualified_name) {
		right_data = lsp_array_find(right, "data");
		right_qualified_name = right_data && Z_TYPE_P(right_data) == IS_ARRAY ? lsp_array_string(right_data, "qualifiedName") : NULL;
	}

	if (left_qualified_name && right_qualified_name) {
		return zend_string_equals(left_qualified_name, right_qualified_name);
	}

	return true;
}

static inline bool lsp_completion_detail_starts_with(zend_string *detail, const char *prefix)
{
	size_t prefix_length = strlen(prefix);

	return ZSTR_LEN(detail) >= prefix_length && memcmp(ZSTR_VAL(detail), prefix, prefix_length) == 0;
}

/* Detail-derived part of the completion ranking, shared between the classic
 * zval scorer and the pre-encoded item builder. */
static inline zend_long lsp_completion_detail_score(zend_string *detail)
{
	zend_long score;
	size_t i;

	score = (zend_long) ZSTR_LEN(detail);
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

static inline zend_long lsp_completion_type_score(zval *item)
{
	zend_long score;
	zend_string *detail, *source, *sort_text;
	zval *data, *precomputed;

	/* Pre-encoded entries carry their score, computed at build time. */
	precomputed = lsp_array_find(item, "score");
	if (precomputed && Z_TYPE_P(precomputed) == IS_LONG) {
		return Z_LVAL_P(precomputed);
	}

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

	score += lsp_completion_detail_score(detail);

	return score;
}

static inline zend_string *lsp_completion_dedup_bucket_key(zval *item)
{
	zend_string *label = lsp_array_string(item, "label");
	const char *value;
	size_t length;

	if (!label) {
		return NULL;
	}

	value = ZSTR_VAL(label);
	length = ZSTR_LEN(label);
	if (length > 0 && value[0] == '$') {
		value++;
		length--;
	}

	return zend_string_init(value, length, 0);
}

extern void lsp_deduplicate_completion_items(zval *items)
{
	HashTable label_buckets;
	zend_string *bucket_key;
	zval deduped, new_bucket, *item, *existing, *bucket, *index_zv, copy;
	zend_long next_index = 0;
	bool handled;

	if (Z_TYPE_P(items) != IS_ARRAY || zend_hash_num_elements(Z_ARRVAL_P(items)) < 2) {
		return;
	}

	/* Items with different labels can never merge, so candidates are
	 * bucketed by normalized label and the pairwise comparison only runs
	 * within a bucket. This keeps the historical merge semantics while
	 * turning the old O(n^2) full scan into O(n) for typical lists. */
	zend_hash_init(&label_buckets, 64, NULL, ZVAL_PTR_DTOR, 0);
	array_init(&deduped);

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(items), item) {
		handled = false;

		if (Z_TYPE_P(item) != IS_ARRAY) {
			ZVAL_COPY(&copy, item);
			add_next_index_zval(&deduped, &copy);
			next_index++;
			continue;
		}

		bucket_key = lsp_completion_dedup_bucket_key(item);
		bucket = bucket_key ? zend_hash_find(&label_buckets, bucket_key) : NULL;

		if (bucket) {
			ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(bucket), index_zv) {
				existing = zend_hash_index_find(Z_ARRVAL(deduped), (zend_ulong) Z_LVAL_P(index_zv));
				if (!existing || Z_TYPE_P(existing) != IS_ARRAY || !lsp_completion_same_label(item, existing)) {
					continue;
				}

				if (lsp_completion_type_score(item) > lsp_completion_type_score(existing)) {
					zval_ptr_dtor(existing);
					ZVAL_COPY(existing, item);
				}

				handled = true;
				break;
			} ZEND_HASH_FOREACH_END();
		}

		if (!handled) {
			ZVAL_COPY(&copy, item);
			add_next_index_zval(&deduped, &copy);

			if (bucket_key) {
				if (!bucket) {
					array_init(&new_bucket);
					bucket = zend_hash_add(&label_buckets, bucket_key, &new_bucket);
				}
				add_next_index_long(bucket, next_index);
			}

			next_index++;
		}

		if (bucket_key) {
			zend_string_release(bucket_key);
		}
	} ZEND_HASH_FOREACH_END();

	zend_hash_destroy(&label_buckets);
	zval_ptr_dtor(items);
	ZVAL_COPY_VALUE(items, &deduped);
}

/* Assemble the completion response JSON directly: pre-encoded entries are
 * spliced verbatim, classic zval items are encoded individually. The result
 * is handed to the transport as a raw JSON payload, skipping the full
 * zval-tree encode of potentially thousands of items. */
extern bool lsp_completion_attach_items(zval *return_value, zval *items)
{
	smart_str json = {0};
	zend_string *fragment;
	zval *item, *incomplete;
	bool first = true, ok = true;

	incomplete = zend_hash_str_find(Z_ARRVAL_P(return_value), "isIncomplete", sizeof("isIncomplete") - 1);
	smart_str_appendl(&json, "{\"isIncomplete\":", sizeof("{\"isIncomplete\":") - 1);
	if (incomplete && Z_TYPE_P(incomplete) == IS_TRUE) {
		smart_str_appendl(&json, "true", 4);
	} else {
		smart_str_appendl(&json, "false", 5);
	}
	smart_str_appendl(&json, ",\"items\":[", sizeof(",\"items\":[") - 1);

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(items), item) {
		if (!first) {
			smart_str_appendc(&json, ',');
		}

		fragment = Z_TYPE_P(item) == IS_ARRAY ? lsp_array_string(item, "__json") : NULL;
		if (fragment) {
			smart_str_append(&json, fragment);
		} else if (php_json_encode(&json, item, PHP_JSON_UNESCAPED_SLASHES | PHP_JSON_UNESCAPED_UNICODE) != SUCCESS) {
			ok = false;
			break;
		}

		first = false;
	} ZEND_HASH_FOREACH_END();

	if (!ok) {
		/* Pathological data (e.g. invalid UTF-8 from an analyzer): fall back
		 * to attaching the structured items so the generic encoder reports
		 * the failure the same way it always has. */
		smart_str_free(&json);
		add_assoc_zval(return_value, "items", items);

		return false;
	}

	smart_str_appendl(&json, "]}", 2);
	smart_str_0(&json);

	zval_ptr_dtor(items);
	zval_ptr_dtor(return_value);
	array_init(return_value);
	add_assoc_str(return_value, "__raw_json", json.s);

	return true;
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

static inline bool lsp_namespace_token_is_name(zval *token)
{
	return lsp_token_name_equals(token, "T_STRING") ||
		lsp_token_name_equals(token, "T_NAME_QUALIFIED") ||
		lsp_token_name_equals(token, "T_NAME_FULLY_QUALIFIED") ||
		lsp_token_name_equals(token, "T_NAME_RELATIVE") ||
		lsp_token_name_equals(token, "T_NS_SEPARATOR")
	;
}

static inline zend_string *lsp_document_namespace_from_tokens(HashTable *tokens, uint32_t start)
{
	zend_string *text;
	zval *token;
	smart_str namespace_name = {0};
	uint32_t i, count;

	count = zend_hash_num_elements(tokens);
	for (i = start; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY) {
			continue;
		}

		if (lsp_token_name_equals(token, "T_WHITESPACE")) {
			continue;
		}

		if (lsp_token_is_char(token, ';') || lsp_token_is_char(token, '{')) {
			break;
		}

		if (!lsp_namespace_token_is_name(token)) {
			continue;
		}

		text = lsp_token_string(token, "text");
		if (!text) {
			continue;
		}

		if (ZSTR_LEN(text) > 0 && ZSTR_VAL(text)[0] == '\\') {
			smart_str_appendl(&namespace_name, ZSTR_VAL(text) + 1, ZSTR_LEN(text) - 1);
		} else {
			smart_str_append(&namespace_name, text);
		}
	}

	if (!namespace_name.s) {
		return zend_empty_string;
	}

	smart_str_0(&namespace_name);

	return namespace_name.s;
}

extern zend_string *lsp_document_namespace(zend_string *text)
{
	zval tokens_zv, *token;
	HashTable *tokens;
	zend_string *namespace_name;
	uint32_t i, count;

	ZVAL_UNDEF(&tokens_zv);
	lsp_lsparrot_tokens_to_zval(&tokens_zv, text);
	if (Z_TYPE(tokens_zv) != IS_ARRAY) {
		if (!Z_ISUNDEF(tokens_zv)) {
			zval_ptr_dtor(&tokens_zv);
		}

		return zend_empty_string;
	}

	tokens = Z_ARRVAL(tokens_zv);
	count = zend_hash_num_elements(tokens);
	namespace_name = zend_empty_string;
	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY || !lsp_token_name_equals(token, "T_NAMESPACE")) {
			continue;
		}

		namespace_name = lsp_document_namespace_from_tokens(tokens, i + 1);
		break;
	}

	zval_ptr_dtor(&tokens_zv);

	return namespace_name;
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

extern bool lsp_symbol_in_current_namespace(zend_string *current_namespace, const char *fqcn, size_t fqcn_length)
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

static inline uint32_t lsp_symbol_import_ast_kind(char kind)
{
	if (kind == LSP_SYMBOL_FUNCTION) {
		return ZEND_SYMBOL_FUNCTION;
	}

	if (kind == LSP_SYMBOL_CONSTANT) {
		return ZEND_SYMBOL_CONST;
	}

	return ZEND_SYMBOL_CLASS;
}

static inline bool lsp_import_name_equals(zend_string *name, char kind, const char *fqcn)
{
	if (kind == LSP_SYMBOL_CONSTANT) {
		return strcmp(ZSTR_VAL(name), fqcn) == 0;
	}

	return strcasecmp(ZSTR_VAL(name), fqcn) == 0;
}

static inline zend_string *lsp_import_full_name(zend_string *prefix, zend_string *name)
{
	if (prefix && ZSTR_LEN(prefix) > 0) {
		return strpprintf(0, "%s\\%s", ZSTR_VAL(prefix), ZSTR_VAL(name));
	}

	return zend_string_copy(name);
}

static inline bool lsp_use_statement_has_import(zend_ast *use_ast, zend_string *prefix, uint32_t target_kind, char symbol_kind, const char *fqcn)
{
	zend_ast_list *list;
	zend_ast *elem;
	zend_string *name, *full_name;
	uint32_t i, elem_kind;
	bool found;

	if (!use_ast || use_ast->kind != ZEND_AST_USE || !zend_ast_is_list(use_ast)) {
		return false;
	}

	list = zend_ast_get_list(use_ast);
	for (i = 0; i < list->children; i++) {
		elem = list->child[i];
		if (!elem || elem->kind != ZEND_AST_USE_ELEM) {
			continue;
		}

		elem_kind = elem->attr != 0 ? elem->attr : use_ast->attr;
		if (elem_kind == 0) {
			elem_kind = ZEND_SYMBOL_CLASS;
		}
		if (elem_kind != target_kind) {
			continue;
		}

		name = lsp_ast_string_value(elem->child[0]);
		if (!name) {
			continue;
		}

		full_name = lsp_import_full_name(prefix, name);
		found = lsp_import_name_equals(full_name, symbol_kind, fqcn);
		zend_string_release(full_name);
		if (found) {
			return true;
		}
	}

	return false;
}

static inline bool lsp_document_has_import_in_ast(zend_ast *ast, uint32_t target_kind, char symbol_kind, const char *fqcn)
{
	zend_ast_list *list;
	zend_string *prefix;
	uint32_t i, count;

	if (!ast) {
		return false;
	}

	if (ast->kind == ZEND_AST_USE) {
		return lsp_use_statement_has_import(ast, NULL, target_kind, symbol_kind, fqcn);
	}

	if (ast->kind == ZEND_AST_GROUP_USE) {
		prefix = lsp_ast_string_value(ast->child[0]);

		return lsp_use_statement_has_import(ast->child[1], prefix, target_kind, symbol_kind, fqcn);
	}

	if (zend_ast_is_list(ast)) {
		list = zend_ast_get_list(ast);
		for (i = 0; i < list->children; i++) {
			if (lsp_document_has_import_in_ast(list->child[i], target_kind, symbol_kind, fqcn)) {
				return true;
			}
		}

		return false;
	}

	if (zend_ast_is_special(ast) || php_ver_abstract.ast_is_opaque_node(ast->kind)) {
		return false;
	}

	count = zend_ast_get_num_children(ast);
	for (i = 0; i < count; i++) {
		if (lsp_document_has_import_in_ast(ast->child[i], target_kind, symbol_kind, fqcn)) {
			return true;
		}
	}

	return false;
}

extern bool lsp_document_has_import(zend_string *text, char kind, const char *fqcn)
{
	zend_arena *ast_arena;
	zend_ast *ast;
	uint32_t target_kind;
	bool found;

	if (!strstr(ZSTR_VAL(text), "use")) {
		return false;
	}

	ast = lsp_compile_string_to_ast_silent(text, ZSTR_EMPTY_ALLOC(), &ast_arena);
	if (!ast) {
		lsp_compiled_ast_destroy(ast, ast_arena);

		return false;
	}

	target_kind = lsp_symbol_import_ast_kind(kind);
	found = lsp_document_has_import_in_ast(ast, target_kind, kind, fqcn);
	lsp_compiled_ast_destroy(ast, ast_arena);

	return found;
}

static inline zend_string *lsp_import_set_key(uint32_t target_kind, const char *name, size_t length)
{
	zend_string *key;
	size_t i;

	key = zend_string_alloc(length + 2, 0);
	ZSTR_VAL(key)[0] = (char) ('0' + (target_kind % 10));
	ZSTR_VAL(key)[1] = ':';

	if (target_kind == ZEND_SYMBOL_CONST) {
		/* Constant imports compare case-sensitively. */
		memcpy(ZSTR_VAL(key) + 2, name, length);
	} else {
		for (i = 0; i < length; i++) {
			ZSTR_VAL(key)[i + 2] = (char) tolower((unsigned char) name[i]);
		}
	}

	ZSTR_VAL(key)[length + 2] = '\0';

	return key;
}

static inline void lsp_collect_use_statement_imports(zend_ast *use_ast, zend_string *prefix, HashTable *imports)
{
	zend_ast_list *list;
	zend_ast *elem;
	zend_string *name, *full_name, *key;
	uint32_t i, elem_kind;

	if (!use_ast || use_ast->kind != ZEND_AST_USE || !zend_ast_is_list(use_ast)) {
		return;
	}

	list = zend_ast_get_list(use_ast);
	for (i = 0; i < list->children; i++) {
		elem = list->child[i];
		if (!elem || elem->kind != ZEND_AST_USE_ELEM) {
			continue;
		}

		elem_kind = elem->attr != 0 ? elem->attr : use_ast->attr;
		if (elem_kind == 0) {
			elem_kind = ZEND_SYMBOL_CLASS;
		}

		name = lsp_ast_string_value(elem->child[0]);
		if (!name) {
			continue;
		}

		full_name = lsp_import_full_name(prefix, name);
		key = lsp_import_set_key(elem_kind, ZSTR_VAL(full_name), ZSTR_LEN(full_name));
		zend_hash_add_empty_element(imports, key);
		zend_string_release(key);
		zend_string_release(full_name);
	}
}

static inline void lsp_collect_imports_in_ast(zend_ast *ast, HashTable *imports)
{
	zend_ast_list *list;
	zend_string *prefix;
	uint32_t i, count;

	if (!ast) {
		return;
	}

	if (ast->kind == ZEND_AST_USE) {
		lsp_collect_use_statement_imports(ast, NULL, imports);

		return;
	}

	if (ast->kind == ZEND_AST_GROUP_USE) {
		prefix = lsp_ast_string_value(ast->child[0]);
		lsp_collect_use_statement_imports(ast->child[1], prefix, imports);

		return;
	}

	if (zend_ast_is_list(ast)) {
		list = zend_ast_get_list(ast);
		for (i = 0; i < list->children; i++) {
			lsp_collect_imports_in_ast(list->child[i], imports);
		}

		return;
	}

	if (zend_ast_is_special(ast) || php_ver_abstract.ast_is_opaque_node(ast->kind)) {
		return;
	}

	count = zend_ast_get_num_children(ast);
	for (i = 0; i < count; i++) {
		lsp_collect_imports_in_ast(ast->child[i], imports);
	}
}

extern void lsp_document_collect_imports(zend_string *text, HashTable *imports)
{
	zend_arena *ast_arena;
	zend_ast *ast;

	if (!strstr(ZSTR_VAL(text), "use")) {
		return;
	}

	ast = lsp_compile_string_to_ast_silent(text, ZSTR_EMPTY_ALLOC(), &ast_arena);
	if (!ast) {
		lsp_compiled_ast_destroy(ast, ast_arena);

		return;
	}

	lsp_collect_imports_in_ast(ast, imports);
	lsp_compiled_ast_destroy(ast, ast_arena);
}

extern bool lsp_document_has_import_cached(lsp_document *document, char kind, const char *fqcn, size_t fqcn_length)
{
	zend_string *key;
	uint32_t target_kind;
	bool exists;

	lsp_document_derived_ensure(document);
	if (!document->derived_imports || zend_hash_num_elements(document->derived_imports) == 0) {
		return false;
	}

	target_kind = lsp_symbol_import_ast_kind(kind);
	key = lsp_import_set_key(target_kind, fqcn, fqcn_length);
	exists = zend_hash_exists(document->derived_imports, key);
	zend_string_release(key);

	return exists;
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

extern size_t lsp_import_insert_offset(zend_string *text, bool *after_existing_use)
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

extern zend_string *lsp_symbol_import_text(char kind, const char *fqcn, bool compact)
{
	if (kind == LSP_SYMBOL_FUNCTION) {
		return strpprintf(0, "%suse function %s;\n", compact ? "" : "\n", fqcn);
	}

	if (kind == LSP_SYMBOL_CONSTANT) {
		return strpprintf(0, "%suse const %s;\n", compact ? "" : "\n", fqcn);
	}

	return strpprintf(0, "%suse %s;\n", compact ? "" : "\n", fqcn);
}

/* ------------------------------------------------------------------------
 * Pre-encoded completion items. Project-symbol candidates are the highest
 * volume completion source (thousands per keystroke on broad prefixes); each
 * one used to be built as a deeply nested zval array and JSON-encoded again
 * at response time. Instead the item JSON is emitted once here, and a small
 * carrier zval {__json, label, kind, score, qualifiedName} keeps exactly the
 * fields deduplication needs. The response assembler splices the fragments.
 * ------------------------------------------------------------------------ */

static inline void lsp_json_append_escaped(smart_str *out, const char *value, size_t length)
{
	static const char hex_digits[] = "0123456789abcdef";
	size_t i;
	unsigned char c;

	smart_str_appendc(out, '"');
	for (i = 0; i < length; i++) {
		c = (unsigned char) value[i];
		switch (c) {
			case '"':
				smart_str_appendl(out, "\\\"", 2);
				break;
			case '\\':
				smart_str_appendl(out, "\\\\", 2);
				break;
			case '\b':
				smart_str_appendl(out, "\\b", 2);
				break;
			case '\f':
				smart_str_appendl(out, "\\f", 2);
				break;
			case '\n':
				smart_str_appendl(out, "\\n", 2);
				break;
			case '\r':
				smart_str_appendl(out, "\\r", 2);
				break;
			case '\t':
				smart_str_appendl(out, "\\t", 2);
				break;
			default:
				if (c < 0x20) {
					smart_str_appendl(out, "\\u00", 4);
					smart_str_appendc(out, hex_digits[(c >> 4) & 0xf]);
					smart_str_appendc(out, hex_digits[c & 0xf]);
				} else {
					smart_str_appendc(out, (char) c);
				}
				break;
		}
	}
	smart_str_appendc(out, '"');
}

static inline void lsp_json_append_position(smart_str *out, zend_string *text, size_t offset)
{
	const char *value = ZSTR_VAL(text);
	zend_long line = 0;
	size_t i, line_start = 0, length = offset > ZSTR_LEN(text) ? ZSTR_LEN(text) : offset;

	for (i = 0; i < length; i++) {
		if (value[i] == '\n') {
			line++;
			line_start = i + 1;
		}
	}

	smart_str_appendl(out, "{\"line\":", sizeof("{\"line\":") - 1);
	smart_str_append_long(out, line);
	smart_str_appendl(out, ",\"character\":", sizeof(",\"character\":") - 1);
	smart_str_append_long(out, (zend_long) (length - line_start));
	smart_str_appendc(out, '}');
}

static inline zend_string *lsp_range_json_from_offsets(zend_string *text, size_t start_offset, size_t end_offset)
{
	smart_str json = {0};

	smart_str_appendl(&json, "{\"start\":", sizeof("{\"start\":") - 1);
	lsp_json_append_position(&json, text, start_offset);
	smart_str_appendl(&json, ",\"end\":", sizeof(",\"end\":") - 1);
	lsp_json_append_position(&json, text, end_offset);
	smart_str_appendc(&json, '}');
	smart_str_0(&json);

	return json.s;
}

typedef struct _lsp_symbol_item_ctx {
	lsp_document *document;
	zend_string *prefix;
	zend_string *current_namespace;
	zend_string *edit_range_json;
	zend_string *import_range_json;
	size_t offset;
	bool import_after_use;
	bool import_context;
	bool prefix_is_qualified;
} lsp_symbol_item_ctx;

static inline void lsp_symbol_item_ctx_init(lsp_symbol_item_ctx *ctx, lsp_document *document, size_t offset, zend_string *prefix)
{
	size_t prefix_start = offset >= ZSTR_LEN(prefix) ? offset - ZSTR_LEN(prefix) : 0;

	ctx->document = document;
	ctx->prefix = prefix;
	ctx->current_namespace = lsp_document_namespace_cached(document);
	ctx->edit_range_json = lsp_range_json_from_offsets(document->text, prefix_start, offset);
	ctx->import_range_json = NULL;
	ctx->offset = offset;
	ctx->import_after_use = false;
	ctx->import_context = lsp_current_statement_starts_with_use(document->text, offset);
	ctx->prefix_is_qualified = memchr(ZSTR_VAL(prefix), '\\', ZSTR_LEN(prefix)) != NULL;
}

static inline void lsp_symbol_item_ctx_release(lsp_symbol_item_ctx *ctx)
{
	if (ctx->edit_range_json) {
		zend_string_release(ctx->edit_range_json);
	}

	if (ctx->import_range_json) {
		zend_string_release(ctx->import_range_json);
	}
}

static inline zend_string *lsp_symbol_item_ctx_import_range(lsp_symbol_item_ctx *ctx)
{
	size_t insert_offset;

	if (!ctx->import_range_json) {
		insert_offset = lsp_document_import_insert_offset_cached(ctx->document, &ctx->import_after_use);
		ctx->import_range_json = lsp_range_json_from_offsets(ctx->document->text, insert_offset, insert_offset);
	}

	return ctx->import_range_json;
}

static inline void lsp_add_project_symbol_completion_item(zval *items, lsp_symbol_item_ctx *ctx, char kind, const char *fqcn, size_t fqcn_length, const char *label_value, size_t label_length, const char *path, size_t path_length)
{
	smart_str json = {0};
	zend_string *new_text, *import_text, *detail, *encoded;
	zval entry;
	zend_long score;
	char sort_prefix;
	bool call_snippet, needs_import;

	call_snippet = kind == LSP_SYMBOL_FUNCTION && !ctx->import_context;
	sort_prefix = lsp_path_value_contains_vendor(path, path_length) ? '1' : '0';
	detail = strpprintf(0, "%s%s", lsp_symbol_detail_prefix(kind), fqcn);

	if (ctx->import_context || ctx->prefix_is_qualified) {
		if (ZSTR_LEN(ctx->prefix) > 0 && ZSTR_VAL(ctx->prefix)[0] == '\\') {
			new_text = strpprintf(0, "\\%s", fqcn);
		} else {
			new_text = zend_string_init(fqcn, fqcn_length, 0);
		}
	} else {
		new_text = zend_string_init(label_value, label_length, 0);
	}

	smart_str_appendl(&json, "{\"label\":", sizeof("{\"label\":") - 1);
	lsp_json_append_escaped(&json, label_value, label_length);
	smart_str_appendl(&json, ",\"kind\":", sizeof(",\"kind\":") - 1);
	smart_str_append_long(&json, lsp_symbol_completion_kind(kind));
	smart_str_appendl(&json, ",\"detail\":", sizeof(",\"detail\":") - 1);
	lsp_json_append_escaped(&json, ZSTR_VAL(detail), ZSTR_LEN(detail));
	smart_str_appendl(&json, ",\"filterText\":", sizeof(",\"filterText\":") - 1);
	lsp_json_append_escaped(&json, label_value, label_length);
	smart_str_appendl(&json, ",\"sortText\":\"", sizeof(",\"sortText\":\"") - 1);
	smart_str_appendc(&json, sort_prefix);
	smart_str_appendc(&json, ':');
	/* Labels are identifier characters; reuse the escaper minus the quotes
	 * by emitting the already-safe label directly. Identifier characters
	 * never require JSON escaping. */
	smart_str_appendl(&json, label_value, label_length);
	smart_str_appendc(&json, '"');
	smart_str_appendl(&json, ",\"data\":{\"source\":\"lsparrot\",\"qualifiedName\":", sizeof(",\"data\":{\"source\":\"lsparrot\",\"qualifiedName\":") - 1);
	lsp_json_append_escaped(&json, fqcn, fqcn_length);
	smart_str_appendc(&json, '}');

	if (call_snippet) {
		smart_str_appendl(&json, ",\"insertTextFormat\":2", sizeof(",\"insertTextFormat\":2") - 1);
	}

	smart_str_appendl(&json, ",\"textEdit\":{\"range\":", sizeof(",\"textEdit\":{\"range\":") - 1);
	smart_str_append(&json, ctx->edit_range_json);
	smart_str_appendl(&json, ",\"newText\":", sizeof(",\"newText\":") - 1);

	if (call_snippet) {
		encoded = lsp_completion_call_snippet(new_text);
		lsp_json_append_escaped(&json, ZSTR_VAL(encoded), ZSTR_LEN(encoded));
		zend_string_release(encoded);
	} else {
		lsp_json_append_escaped(&json, ZSTR_VAL(new_text), ZSTR_LEN(new_text));
	}

	smart_str_appendc(&json, '}');

	needs_import = !ctx->import_context &&
		!ctx->prefix_is_qualified &&
		!lsp_symbol_in_current_namespace(ctx->current_namespace, fqcn, fqcn_length) &&
		!lsp_document_has_import_cached(ctx->document, kind, fqcn, fqcn_length)
	;

	if (needs_import) {
		smart_str_appendl(&json, ",\"additionalTextEdits\":[{\"range\":", sizeof(",\"additionalTextEdits\":[{\"range\":") - 1);
		smart_str_append(&json, lsp_symbol_item_ctx_import_range(ctx));
		import_text = lsp_symbol_import_text(kind, fqcn, ctx->import_after_use);
		smart_str_appendl(&json, ",\"newText\":", sizeof(",\"newText\":") - 1);
		lsp_json_append_escaped(&json, ZSTR_VAL(import_text), ZSTR_LEN(import_text));
		zend_string_release(import_text);
		smart_str_appendl(&json, "}]", 2);
	}

	smart_str_appendc(&json, '}');
	smart_str_0(&json);

	score = (sort_prefix == '0' ? 5000 : -5000) + lsp_completion_detail_score(detail);

	array_init(&entry);
	add_assoc_str(&entry, "__json", json.s);
	add_assoc_stringl(&entry, "label", label_value, label_length);
	add_assoc_long(&entry, "kind", lsp_symbol_completion_kind(kind));
	add_assoc_long(&entry, "score", score);
	add_assoc_stringl(&entry, "qualifiedName", fqcn, fqcn_length);
	add_next_index_zval(items, &entry);

	zend_string_release(new_text);
	zend_string_release(detail);
}

static inline uint32_t lsp_symbol_segment_lower_bound(lsp_symbol_index *region, const char *prefix_value, size_t prefix_length)
{
	const lsp_symbol_segment *segment;
	const char *seg;
	uint32_t lo = 0, hi = region->segment_count, mid;

	while (lo < hi) {
		mid = lo + (hi - lo) / 2;
		segment = &region->segments[mid];
		seg = lsp_symbol_entry_fqcn(region, &region->entries[segment->entry_index]) + segment->segment_offset;
		if (strncasecmp(seg, prefix_value, prefix_length) < 0) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}

	return lo;
}

static inline void lsp_symbol_match_prefix(lsp_symbol_index *region, zend_string *prefix, uint8_t *matched)
{
	const lsp_symbol_entry *entry;
	const lsp_symbol_segment *segment;
	const char *prefix_value = ZSTR_VAL(prefix), *seg;
	uint32_t i;
	size_t prefix_length = ZSTR_LEN(prefix);

	if (prefix_length > 0 && prefix_value[0] == '\\') {
		prefix_value++;
		prefix_length--;
	}

	if (prefix_length == 0) {
		memset(matched, 1, region->entry_count);

		return;
	}

	/* Strict prefix matching at FQCN segment starts (offset 0, after a
	 * namespace separator, or after an underscore) via binary search over
	 * the sorted segment array. */
	for (i = lsp_symbol_segment_lower_bound(region, prefix_value, prefix_length); i < region->segment_count; i++) {
		segment = &region->segments[i];
		seg = lsp_symbol_entry_fqcn(region, &region->entries[segment->entry_index]) + segment->segment_offset;
		if (strncasecmp(seg, prefix_value, prefix_length) != 0) {
			break;
		}

		matched[segment->entry_index] = 1;
	}

	if (prefix_length < 2) {
		return;
	}

	/* Fuzzy subsequence fallback for class-like symbols. A subsequence of
	 * the basename label is always a subsequence of the FQCN, so matching
	 * the FQCN alone preserves the historical label-or-FQCN semantics. */
	for (i = 0; i < region->entry_count; i++) {
		entry = &region->entries[i];
		if (matched[i] || !lsp_symbol_kind_is_class_like((char) entry->kind)) {
			continue;
		}

		if (lsp_symbol_fuzzy_match(lsp_symbol_entry_fqcn(region, entry), entry->fqcn_length, prefix_value, prefix_length)) {
			matched[i] = 1;
		}
	}
}

static inline void lsp_add_project_symbol_completions_pass(lsp_server *server, zval *items, lsp_symbol_item_ctx *ctx, char filter_kind, bool class_like_only, bool vendor_symbols, const uint8_t *matched)
{
	const char *label_value, *fqcn, *path;
	const lsp_symbol_entry *entry;
	lsp_symbol_index *region = &server->symbol_index;
	uint32_t i;
	size_t label_length;

	for (i = 0; i < region->entry_count; i++) {
		if (!matched[i]) {
			continue;
		}

		entry = &region->entries[i];
		if ((entry->flags & LSP_SYMBOL_ENTRY_DELETED) != 0 ||
			((entry->flags & LSP_SYMBOL_ENTRY_VENDOR) != 0) != vendor_symbols
		) {
			continue;
		}

		if (filter_kind != '\0' && (char) entry->kind != filter_kind) {
			continue;
		}

		if (class_like_only && !lsp_symbol_kind_is_class_like((char) entry->kind)) {
			continue;
		}

		fqcn = lsp_symbol_entry_fqcn(region, entry);
		path = lsp_symbol_entry_path(region, entry);
		label_value = lsp_symbol_completion_label((char) entry->kind, fqcn, entry->fqcn_length, &label_length);
		lsp_add_project_symbol_completion_item(items, ctx, (char) entry->kind, fqcn, entry->fqcn_length, label_value, label_length, path, entry->path_length);
	}
}

static inline void lsp_add_project_symbol_completions_ex(lsp_server *server, zval *items, lsp_document *document, size_t offset, zend_string *prefix, char filter_kind, bool class_like_only)
{
	lsp_symbol_item_ctx ctx;
	uint8_t *matched;

	lsp_index_join_worker(server);
	lsp_symbol_index_table_ensure(&server->symbol_index);
	if (server->symbol_index.entry_count == 0) {
		return;
	}

	matched = ecalloc(server->symbol_index.entry_count, 1);
	lsp_symbol_match_prefix(&server->symbol_index, prefix, matched);
	lsp_symbol_item_ctx_init(&ctx, document, offset, prefix);
	lsp_add_project_symbol_completions_pass(server, items, &ctx, filter_kind, class_like_only, false, matched);
	lsp_add_project_symbol_completions_pass(server, items, &ctx, filter_kind, class_like_only, true, matched);
	lsp_symbol_item_ctx_release(&ctx);
	efree(matched);
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
	lsp_symbol_item_ctx ctx;
	const char *label_value;
	size_t label_length;

	label_value = lsp_symbol_completion_label(kind, ZSTR_VAL(fqcn), ZSTR_LEN(fqcn), &label_length);
	lsp_symbol_item_ctx_init(&ctx, document, offset, prefix);
	lsp_add_project_symbol_completion_item(items, &ctx, kind, ZSTR_VAL(fqcn), ZSTR_LEN(fqcn), label_value, label_length, "", 0);
	lsp_symbol_item_ctx_release(&ctx);
}
