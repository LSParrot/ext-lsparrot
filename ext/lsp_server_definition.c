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

static inline bool lsp_definition_string_equals_ci(zend_string *left, zend_string *right)
{
	return ZSTR_LEN(left) == ZSTR_LEN(right) && strncasecmp(ZSTR_VAL(left), ZSTR_VAL(right), ZSTR_LEN(left)) == 0;
}

static inline void lsp_definition_position_from_offset(zend_string *text, size_t offset, zval *position)
{
	const char *value;
	zend_long line;
	size_t i, line_start, length;

	value = ZSTR_VAL(text);
	line = 0;
	line_start = 0;
	length = offset > ZSTR_LEN(text) ? ZSTR_LEN(text) : offset;

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

static inline void lsp_definition_location_from_offsets(zend_string *path, zend_string *contents, size_t start_offset, size_t end_offset, zval *location)
{
	zend_string *uri;
	zval range;

	if (end_offset < start_offset) {
		end_offset = start_offset;
	}

	uri = lsp_uri_from_path(path);
	array_init(location);
	add_assoc_str(location, "uri", uri);
	lsp_range_from_offsets(contents, start_offset, end_offset, &range);
	add_assoc_zval(location, "range", &range);
}

static inline bool lsp_method_definition_in_contents(zend_string *path, zend_string *contents, zend_string *member_name, zval *return_value, zend_string **parent_class)
{
	zend_long body_depth = 0;
	zend_string *label;
	zval tokens_zv, *token, *name_token;
	HashTable *tokens;
	uint32_t i, count, name_index;
	size_t class_start = 0, body_start = 0, body_end = 0, name_offset;

	*parent_class = NULL;

	ZVAL_UNDEF(&tokens_zv);
	lsp_lsparrot_tokens_to_zval(&tokens_zv, contents);

	if (Z_TYPE(tokens_zv) != IS_ARRAY || !lsp_find_first_class_header(contents, &class_start, &body_start, &body_end, &body_depth)) {
		if (!Z_ISUNDEF(tokens_zv)) {
			zval_ptr_dtor(&tokens_zv);
		}

		return false;
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
		if (!label || !zend_string_equals(label, member_name)) {
			continue;
		}

		name_offset = (size_t) lsp_token_long(name_token, "offset", 0);
		lsp_definition_location_from_offsets(path, contents, name_offset, name_offset + ZSTR_LEN(label), return_value);
		zval_ptr_dtor(&tokens_zv);

		return true;
	}

	*parent_class = lsp_class_extends_name(contents, class_start, body_start);
	zval_ptr_dtor(&tokens_zv);

	return false;
}

static inline bool lsp_project_method_definition_for_class(lsp_server *server, zend_string *class_name, zend_string *member_name, zval *return_value, uint32_t depth)
{
	zend_string *path, *contents, *parent_class;
	bool found;

	if (depth > 64) {
		return false;
	}

	path = lsp_find_project_symbol_path(server, LSP_SYMBOL_CLASS, class_name);
	if (!path) {
		return false;
	}

	contents = lsp_read_file(path);
	if (contents == zend_empty_string) {
		zend_string_release(path);

		return false;
	}

	parent_class = NULL;
	found = lsp_method_definition_in_contents(path, contents, member_name, return_value, &parent_class);
	zend_string_release(contents);
	zend_string_release(path);
	if (found) {
		if (parent_class) {
			zend_string_release(parent_class);
		}

		return true;
	}

	if (!parent_class) {
		return false;
	}

	found = lsp_project_method_definition_for_class(server, parent_class, member_name, return_value, depth + 1);
	zend_string_release(parent_class);

	return found;
}

static inline lsp_method_visibility lsp_definition_member_visibility(HashTable *tokens, uint32_t index, zend_string *text, zend_long body_depth)
{
	zend_long i;
	zval *token;

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
			return LSP_METHOD_VISIBILITY_PRIVATE;
		}

		if (lsp_token_name_equals(token, "T_PROTECTED")) {
			return LSP_METHOD_VISIBILITY_PROTECTED;
		}

		if (lsp_token_name_equals(token, "T_PUBLIC")) {
			return LSP_METHOD_VISIBILITY_PUBLIC;
		}
	}

	return LSP_METHOD_VISIBILITY_PUBLIC;
}

static inline bool lsp_definition_member_is_static(HashTable *tokens, uint32_t index, zend_string *text, zend_long body_depth)
{
	zend_long i;
	zval *token;

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

		if (lsp_token_name_equals(token, "T_STATIC")) {
			return true;
		}
	}

	return false;
}

static inline bool lsp_definition_member_is_final(HashTable *tokens, uint32_t index, zend_string *text, zend_long body_depth)
{
	zend_long i;
	zval *token;

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

		if (lsp_token_name_equals(token, "T_FINAL")) {
			return true;
		}
	}

	return false;
}

static inline bool lsp_definition_static_visibility_allows(lsp_method_visibility visibility, bool public_only)
{
	if (visibility == LSP_METHOD_VISIBILITY_PRIVATE) {
		return false;
	}

	return !public_only || visibility == LSP_METHOD_VISIBILITY_PUBLIC;
}

static inline bool lsp_definition_label_matches(zend_string *label, zend_string *member_name)
{
	if (zend_string_equals(label, member_name)) {
		return true;
	}

	return ZSTR_LEN(member_name) > 0 &&
		ZSTR_VAL(member_name)[0] == '$' &&
		ZSTR_LEN(label) + 1 == ZSTR_LEN(member_name) &&
		strncasecmp(ZSTR_VAL(label), ZSTR_VAL(member_name) + 1, ZSTR_LEN(label)) == 0
	;
}

static inline bool lsp_static_member_definition_in_contents(zend_string *path, zend_string *contents, zend_string *member_name, bool public_only, zval *return_value, zend_string **parent_class)
{
	zend_long body_depth = 0;
	lsp_method_visibility visibility;
	zend_string *label;
	zval tokens_zv, *token, *name_token;
	HashTable *tokens;
	uint32_t i, count, name_index;
	size_t class_start = 0, body_start = 0, body_end = 0, name_offset;
	bool static_member;

	*parent_class = NULL;

	ZVAL_UNDEF(&tokens_zv);
	lsp_lsparrot_tokens_to_zval(&tokens_zv, contents);

	if (Z_TYPE(tokens_zv) != IS_ARRAY || !lsp_find_first_class_header(contents, &class_start, &body_start, &body_end, &body_depth)) {
		if (!Z_ISUNDEF(tokens_zv)) {
			zval_ptr_dtor(&tokens_zv);
		}

		return false;
	}

	tokens = Z_ARRVAL(tokens_zv);
	count = zend_hash_num_elements(tokens);
	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY || !lsp_token_in_bounds(token, body_start, body_end) || !lsp_token_at_depth(contents, token, body_depth)) {
			continue;
		}

		static_member = lsp_definition_member_is_static(tokens, i, contents, body_depth);
		if (!static_member) {
			continue;
		}

		visibility = lsp_definition_member_visibility(tokens, i, contents, body_depth);
		if (!lsp_definition_static_visibility_allows(visibility, public_only)) {
			continue;
		}

		if (lsp_token_name_equals(token, "T_FUNCTION")) {
			name_token = lsp_next_function_name_token_ex(tokens, i + 1, &name_index);
			label = lsp_token_string(name_token, "text");
			if (!label || !lsp_definition_label_matches(label, member_name)) {
				continue;
			}

			name_offset = (size_t) lsp_token_long(name_token, "offset", 0);
			lsp_definition_location_from_offsets(path, contents, name_offset, name_offset + ZSTR_LEN(label), return_value);
			zval_ptr_dtor(&tokens_zv);

			return true;
		}

		if (!lsp_token_name_equals(token, "T_VARIABLE") || !lsp_token_is_property_declaration(tokens, i, contents, body_depth)) {
			continue;
		}

		label = lsp_token_string(token, "text");
		if (!label || !lsp_definition_label_matches(label, member_name)) {
			continue;
		}

		name_offset = (size_t) lsp_token_long(token, "offset", 0);
		lsp_definition_location_from_offsets(path, contents, name_offset, name_offset + ZSTR_LEN(label), return_value);
		zval_ptr_dtor(&tokens_zv);

		return true;
	}

	*parent_class = lsp_class_extends_name(contents, class_start, body_start);
	zval_ptr_dtor(&tokens_zv);

	return false;
}

static inline bool lsp_project_static_member_definition_for_class(lsp_server *server, zend_string *class_name, zend_string *member_name, bool public_only, zval *return_value, uint32_t depth)
{
	zend_string *path, *contents, *parent_class;
	bool found;

	if (depth > 64) {
		return false;
	}

	path = lsp_find_project_symbol_path(server, LSP_SYMBOL_CLASS, class_name);
	if (!path) {
		return false;
	}

	contents = lsp_read_file(path);
	if (contents == zend_empty_string) {
		zend_string_release(path);

		return false;
	}

	parent_class = NULL;
	found = lsp_static_member_definition_in_contents(path, contents, member_name, public_only, return_value, &parent_class);
	zend_string_release(contents);
	zend_string_release(path);
	if (found) {
		if (parent_class) {
			zend_string_release(parent_class);
		}

		return true;
	}

	if (!parent_class) {
		return false;
	}

	found = lsp_project_static_member_definition_for_class(server, parent_class, member_name, public_only, return_value, depth + 1);
	zend_string_release(parent_class);

	return found;
}

static inline bool lsp_definition_word_bounds(zend_string *text, zend_string *word, size_t offset, size_t *word_start, size_t *word_end)
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
	while (start > 0 && (lsp_doc_is_identifier_char(value[start - 1]) || value[start - 1] == '$' || value[start - 1] == '\\')) {
		start--;
	}

	end = offset;
	while (end < length && (lsp_doc_is_identifier_char(value[end]) || value[end] == '$' || value[end] == '\\')) {
		end++;
	}

	if (end <= start || end - start != ZSTR_LEN(word) || memcmp(value + start, ZSTR_VAL(word), ZSTR_LEN(word)) != 0) {
		return false;
	}

	*word_start = start;
	*word_end = end;

	return true;
}

static inline bool lsp_static_member_receiver_class(lsp_document *document, size_t offset, zend_string *word, zend_string **class_name, bool *public_only)
{
	const char *value;
	zend_string *receiver, *resolved;
	size_t word_start, word_end, i, class_start, class_end, current_class_start, current_body_start, current_body_end;
	zend_long current_body_depth;

	*class_name = NULL;
	*public_only = true;

	if (!lsp_definition_word_bounds(document->text, word, offset, &word_start, &word_end)) {
		return false;
	}

	value = ZSTR_VAL(document->text);
	i = word_start;
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
	if (class_end <= class_start) {
		return false;
	}

	receiver = zend_string_init(value + class_start, class_end - class_start, 0);
	if (zend_string_equals_literal_ci(receiver, "parent")) {
		if (!lsp_find_enclosing_class_header(document->text, offset, &current_class_start, &current_body_start, &current_body_end, &current_body_depth)) {
			zend_string_release(receiver);

			return false;
		}

		*class_name = lsp_class_extends_name(document->text, current_class_start, current_body_start);
		zend_string_release(receiver);
		*public_only = false;

		return *class_name != NULL;
	}

	if (zend_string_equals_literal_ci(receiver, "self") || zend_string_equals_literal_ci(receiver, "static")) {
		if (!lsp_find_enclosing_class_header(document->text, offset, &current_class_start, &current_body_start, &current_body_end, &current_body_depth)) {
			zend_string_release(receiver);

			return false;
		}

		*class_name = lsp_class_declared_name(document->text, current_class_start, current_body_start);
		zend_string_release(receiver);
		*public_only = false;

		return *class_name != NULL;
	}

	resolved = lsp_resolve_class_name(document->text, receiver);
	if (resolved) {
		zend_string_release(receiver);
		*class_name = resolved;
	} else {
		*class_name = receiver;
	}

	return true;
}

static inline bool lsp_project_static_member_definition(lsp_server *server, lsp_document *document, size_t offset, zend_string *word, zval *return_value)
{
	zend_string *class_name;
	bool public_only, found;

	if (!lsp_static_member_receiver_class(document, offset, word, &class_name, &public_only)) {
		return false;
	}

	found = lsp_project_static_member_definition_for_class(server, class_name, word, public_only, return_value, 0);
	zend_string_release(class_name);

	return found;
}

static inline bool lsp_project_class_extends_class(lsp_server *server, zend_string *candidate_class, zend_string *base_class)
{
	zend_string *current, *next;
	zval *entry, *parent;
	HashTable visited;
	uint32_t depth;
	bool found;

	zend_hash_init(&visited, 8, NULL, NULL, 0);
	current = zend_string_copy(candidate_class);
	depth = 0;
	found = false;

	while (current && depth < 64) {
		depth++;
		if (zend_hash_exists(&visited, current)) {
			break;
		}

		zend_hash_add_empty_element(&visited, current);
		entry = lsp_class_member_cache_entry(server, current);
		if (!entry || Z_TYPE_P(entry) != IS_ARRAY) {
			break;
		}

		parent = zend_hash_str_find(Z_ARRVAL_P(entry), "parent", sizeof("parent") - 1);
		next = parent && Z_TYPE_P(parent) == IS_STRING && Z_STRLEN_P(parent) > 0 ? zend_string_copy(Z_STR_P(parent)) : NULL;
		if (!next) {
			break;
		}

		if (lsp_definition_string_equals_ci(next, base_class)) {
			zend_string_release(next);
			found = true;
			break;
		}

		zend_string_release(current);
		current = next;
	}

	if (current) {
		zend_string_release(current);
	}

	zend_hash_destroy(&visited);

	return found;
}

static inline bool lsp_project_declared_method_location(lsp_server *server, zend_string *class_name, zend_string *method_name, zval *location)
{
	lsp_document *document;
	lsp_method_visibility visibility;
	zend_long body_depth;
	zend_string *path, *contents, *label;
	zval tokens_zv, *token, *name_token;
	HashTable *tokens;
	uint32_t i, count, name_index;
	size_t class_start, body_start, body_end, name_start, name_end;
	bool found, owns_contents;

	path = lsp_find_project_symbol_path(server, LSP_SYMBOL_CLASS, class_name);
	if (!path) {
		return false;
	}

	document = lsp_document_for_path(server, path);
	contents = document ? zend_string_copy(document->text) : lsp_read_file(path);
	owns_contents = document || contents != zend_empty_string;
	if (!owns_contents) {
		zend_string_release(path);

		return false;
	}

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
			if (!token || Z_TYPE_P(token) != IS_ARRAY || !lsp_token_in_bounds(token, body_start, body_end)) {
				continue;
			}

			if (!lsp_token_name_equals(token, "T_FUNCTION") || !lsp_token_at_depth(contents, token, body_depth)) {
				continue;
			}

			visibility = lsp_definition_member_visibility(tokens, i, contents, body_depth);
			if (visibility == LSP_METHOD_VISIBILITY_PRIVATE) {
				continue;
			}

			name_token = lsp_next_function_name_token_ex(tokens, i + 1, &name_index);
			label = lsp_token_string(name_token, "text");
			if (!label || !lsp_definition_string_equals_ci(label, method_name)) {
				continue;
			}

			name_start = (size_t) lsp_token_long(name_token, "offset", lsp_token_long(token, "offset", 0));
			name_end = name_start + ZSTR_LEN(label);
			lsp_definition_location_from_offsets(path, contents, name_start, name_end, location);
			found = true;
			break;
		}
	}

	if (!Z_ISUNDEF(tokens_zv)) {
		zval_ptr_dtor(&tokens_zv);
	}

	if (owns_contents) {
		zend_string_release(contents);
	}

	zend_string_release(path);

	return found;
}

static inline uint32_t lsp_add_project_override_locations(lsp_server *server, zval *locations, zend_string *base_class, zend_string *method_name)
{
	lsp_symbol_index_header *header;
	zend_string *candidate;
	zval location;
	uint32_t i, count;
	size_t fqcn_length, path_length;
	char *cursor, *end, kind, *fqcn, *path;

	count = 0;
	lsp_index_join_worker(server);

	if (!server->symbol_index.available || !server->symbol_index.addr) {
		return 0;
	}

	header = (lsp_symbol_index_header *) server->symbol_index.addr;
	if (header->magic != LSP_SYMBOL_INDEX_MAGIC || header->used > header->capacity) {
		return 0;
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
		if (cursor > end || kind != LSP_SYMBOL_CLASS || lsp_path_value_contains_analysis_helper(path, path_length)) {
			continue;
		}

		candidate = zend_string_init(fqcn, fqcn_length, 0);
		if (lsp_definition_string_equals_ci(candidate, base_class)) {
			zend_string_release(candidate);
			continue;
		}

		if (!lsp_project_class_extends_class(server, candidate, base_class)) {
			zend_string_release(candidate);
			continue;
		}

		ZVAL_UNDEF(&location);
		if (lsp_project_declared_method_location(server, candidate, method_name, &location)) {
			add_next_index_zval(locations, &location);
			count++;
		} else if (!Z_ISUNDEF(location)) {
			zval_ptr_dtor(&location);
		}

		zend_string_release(candidate);
	}

	return count;
}

static inline void lsp_add_override_code_lens(lsp_server *server, zval *items, lsp_document *document, zend_string *class_name, zend_string *method_name, size_t name_start, size_t name_end)
{
	zend_string *title;
	zval lens, range, command, arguments, position, locations;
	uint32_t count;

	array_init(&locations);
	count = lsp_add_project_override_locations(server, &locations, class_name, method_name);
	if (count == 0) {
		zval_ptr_dtor(&locations);

		return;
	}

	title = count == 1
		? zend_string_init("1 override", sizeof("1 override") - 1, 0)
		: strpprintf(0, "%u overrides", count)
	;

	array_init(&lens);
	lsp_range_from_offsets(document->text, name_start, name_end, &range);
	add_assoc_zval(&lens, "range", &range);

	array_init(&command);
	add_assoc_str(&command, "title", title);
	add_assoc_string(&command, "command", "lsparrot.showReferences");

	array_init(&arguments);
	add_next_index_str(&arguments, zend_string_copy(document->uri));
	lsp_definition_position_from_offset(document->text, name_start, &position);
	add_next_index_zval(&arguments, &position);
	add_next_index_zval(&arguments, &locations);
	add_assoc_zval(&command, "arguments", &arguments);
	add_assoc_zval(&lens, "command", &command);
	add_next_index_zval(items, &lens);
}

static inline zend_string *lsp_this_member_access_class_name(lsp_document *document, size_t offset, zend_string *prefix)
{
	zend_long body_depth = 0;
	zend_string *receiver, *member_prefix, *class_name;
	size_t class_start = 0, body_start = 0, body_end = 0;

	if (!lsp_member_access_context(document->text, offset, prefix, &receiver, &member_prefix)) {
		return NULL;
	}

	if (!zend_string_equals_literal(receiver, "$this")) {
		zend_string_release(receiver);
		zend_string_release(member_prefix);

		return NULL;
	}

	zend_string_release(receiver);
	zend_string_release(member_prefix);
	if (!lsp_find_enclosing_class_header(document->text, offset, &class_start, &body_start, &body_end, &body_depth)) {
		return NULL;
	}

	class_name = lsp_class_declared_name(document->text, class_start, body_start);

	return class_name;
}

static inline bool lsp_project_member_definition(lsp_server *server, lsp_document *document, size_t offset, zend_string *prefix, zend_string *member_name, zval *return_value)
{
	zend_string *member_prefix, *class_name;
	bool found;

	if (!lsp_member_access_class_context(server, document, offset, prefix, &class_name, &member_prefix)) {
		class_name = lsp_this_member_access_class_name(document, offset, prefix);
		if (!class_name) {
			return false;
		}
	} else {
		zend_string_release(member_prefix);
	}

	found = lsp_project_method_definition_for_class(server, class_name, member_name, return_value, 0);
	zend_string_release(class_name);

	return found;
}

static inline zend_string *lsp_use_statement_class_name_at(zend_string *text, size_t offset)
{
	const char *value = ZSTR_VAL(text), *statement_start, *statement_end, *p, *name_start, *name_end, *alias_keyword;
	size_t start, end, length = ZSTR_LEN(text);

	if (offset > length) {
		offset = length;
	}

	start = offset;
	while (start > 0 && value[start - 1] != '\n' && value[start - 1] != ';' && value[start - 1] != '{' && value[start - 1] != '}') {
		start--;
	}

	end = offset;
	while (end < length && value[end] != '\n' && value[end] != ';' && value[end] != '{' && value[end] != '}') {
		end++;
	}

	statement_start = value + start;
	statement_end = value + end;
	p = statement_start;

	while (p < statement_end && isspace((unsigned char) *p)) {
		p++;
	}

	if ((size_t) (statement_end - p) < sizeof("use") - 1 || memcmp(p, "use", sizeof("use") - 1) != 0) {
		return NULL;
	}

	p += sizeof("use") - 1;
	if (p < statement_end && !isspace((unsigned char) *p)) {
		return NULL;
	}

	while (p < statement_end && isspace((unsigned char) *p)) {
		p++;
	}

	if ((p + sizeof("function") - 1 < statement_end && strncasecmp(p, "function", sizeof("function") - 1) == 0) ||
		(p + sizeof("const") - 1 < statement_end && strncasecmp(p, "const", sizeof("const") - 1) == 0)
	) {
		return NULL;
	}

	if (p < statement_end && *p == '\\') {
		p++;
	}

	name_start = p;
	if (value + offset < name_start || value + offset > statement_end) {
		return NULL;
	}

	alias_keyword = NULL;
	for (p = name_start; p + sizeof(" as ") - 1 <= statement_end; p++) {
		if (strncasecmp(p, " as ", sizeof(" as ") - 1) == 0) {
			alias_keyword = p;
			break;
		}
	}

	name_end = alias_keyword ? alias_keyword : statement_end;
	while (name_end > name_start && isspace((unsigned char) name_end[-1])) {
		name_end--;
	}

	if (name_end <= name_start) {
		return NULL;
	}

	if (memchr(name_start, '{', name_end - name_start) || memchr(name_start, ',', name_end - name_start)) {
		return NULL;
	}

	return zend_string_init(name_start, name_end - name_start, 0);
}

static inline bool lsp_project_class_definition(lsp_server *server, zend_string *class_name, zval *return_value)
{
	const char *class_label;
	zend_string *path, *contents, *label, *uri;
	zval tokens_zv, *token, *name_token, range, start, end_range;
	HashTable *tokens;
	uint32_t i, count, name_index;
	size_t class_label_length, name_offset;
	bool located = false;

	path = lsp_find_project_symbol_path(server, LSP_SYMBOL_CLASS, class_name);
	if (!path) {
		return false;
	}

	contents = lsp_read_file(path);
	if (contents != zend_empty_string) {
		class_label = lsp_basename_from_fqcn(ZSTR_VAL(class_name), ZSTR_LEN(class_name), &class_label_length);
		ZVAL_UNDEF(&tokens_zv);
		lsp_lsparrot_tokens_to_zval(&tokens_zv, contents);

		if (Z_TYPE(tokens_zv) == IS_ARRAY) {
			tokens = Z_ARRVAL(tokens_zv);
			count = zend_hash_num_elements(tokens);
			for (i = 0; i < count; i++) {
				token = zend_hash_index_find(tokens, i);
				if (!token || Z_TYPE_P(token) != IS_ARRAY || !lsp_token_is_class_like(token)) {
					continue;
				}

				name_token = lsp_next_function_name_token_ex(tokens, i + 1, &name_index);
				label = lsp_token_string(name_token, "text");
				if (!label || ZSTR_LEN(label) != class_label_length || strncasecmp(ZSTR_VAL(label), class_label, class_label_length) != 0) {
					continue;
				}

				/* Select the class NAME itself so editor previews (e.g.
				 * cmd-hover in VSCode) show the declaration instead of a
				 * single character at column zero. */
				name_offset = (size_t) lsp_token_long(name_token, "offset", 0);
				lsp_definition_location_from_offsets(path, contents, name_offset, name_offset + ZSTR_LEN(label), return_value);
				located = true;
				break;
			}

			zval_ptr_dtor(&tokens_zv);
		} else if (!Z_ISUNDEF(tokens_zv)) {
			zval_ptr_dtor(&tokens_zv);
		}

		zend_string_release(contents);
	}

	if (!located) {
		uri = lsp_uri_from_path(path);
		array_init(return_value);
		add_assoc_str(return_value, "uri", uri);
		array_init(&start);
		add_assoc_long(&start, "line", 0);
		add_assoc_long(&start, "character", 0);
		array_init(&end_range);
		add_assoc_long(&end_range, "line", 0);
		add_assoc_long(&end_range, "character", 1);
		array_init(&range);
		add_assoc_zval(&range, "start", &start);
		add_assoc_zval(&range, "end", &end_range);
		add_assoc_zval(return_value, "range", &range);
	}

	zend_string_release(path);

	return true;
}
extern bool lsp_find_symbol(zval *lsparrot, zend_string *word, zval **matched_token, zend_string **matched_label, zend_string **matched_detail)
{
	const char *word_value = ZSTR_VAL(word);
	zend_string *label, *detail;
	zval *tokens_zv, *token;
	HashTable *tokens;
	uint32_t i, count;
	size_t word_length = ZSTR_LEN(word);

	if (word_length > 0 && word_value[0] == '$') {
		word_value++;
		word_length--;
	}

	word_value = lsp_skip_global_namespace_prefix(word_value, &word_length);

	tokens_zv = zend_hash_str_find(Z_ARRVAL_P(lsparrot), "tokens", sizeof("tokens") - 1);
	if (!tokens_zv || Z_TYPE_P(tokens_zv) != IS_ARRAY) {
		return false;
	}

	tokens = Z_ARRVAL_P(tokens_zv);
	count = zend_hash_num_elements(tokens);
	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		label = NULL;
		detail = NULL;

		if (!token || Z_TYPE_P(token) != IS_ARRAY) {
			continue;
		}

		if (lsp_token_name_equals(token, "T_FUNCTION")) {
			label = lsp_next_string_token(tokens, i + 1);
			if (label) {
				detail = strpprintf(0, "function %s", ZSTR_VAL(label));
			}
		} else if (lsp_token_is_class_like(token)) {
			label = lsp_next_string_token(tokens, i + 1);
			if (label) {
				detail = strpprintf(0, "%s %s", lsp_token_name_equals(token, "T_INTERFACE") ? "interface" : (lsp_token_name_equals(token, "T_TRAIT") ? "trait" : (lsp_token_name_equals(token, "T_ENUM") ? "enum" : "class")), ZSTR_VAL(label));
			}
		} else if (lsp_token_name_equals(token, "T_VARIABLE")) {
			label = lsp_token_string(token, "text");
			if (label) {
				detail = strpprintf(0, "variable %s", ZSTR_VAL(label));
			}
		}

		if (!label || !detail) {
			if (detail) {
				zend_string_release(detail);
			}

			continue;
		}

		if ((ZSTR_LEN(label) == word_length && strncasecmp(ZSTR_VAL(label), word_value, word_length) == 0) ||
			(ZSTR_LEN(label) > 0 && ZSTR_VAL(label)[0] == '$' && ZSTR_LEN(label) - 1 == word_length && strncasecmp(ZSTR_VAL(label) + 1, word_value, word_length) == 0)
		) {
			if (matched_token) {
				*matched_token = token;
			}

			if (matched_label) {
				*matched_label = label;
			}

			if (matched_detail) {
				*matched_detail = detail;
			} else {
				zend_string_release(detail);
			}

			return true;
		}

		zend_string_release(detail);
	}

	return false;
}

extern void lsp_lsparrot_code_lens(lsp_server *server, zval *return_value, lsp_document *document)
{
	lsp_method_visibility visibility;
	zend_long body_depth;
	zend_string *class_name, *label;
	zval *tokens_zv, *token, *name_token;
	HashTable *tokens;
	uint32_t i, count, name_index;
	size_t class_start, body_start, body_end, search_start, name_start, name_end;

	array_init(return_value);

	if (Z_TYPE(document->lsparrot) != IS_ARRAY) {
		return;
	}

	tokens_zv = zend_hash_str_find(Z_ARRVAL(document->lsparrot), "tokens", sizeof("tokens") - 1);
	if (!tokens_zv || Z_TYPE_P(tokens_zv) != IS_ARRAY) {
		return;
	}

	tokens = Z_ARRVAL_P(tokens_zv);
	count = zend_hash_num_elements(tokens);
	search_start = 0;
	while (lsp_find_class_header_from(document->text, search_start, &class_start, &body_start, &body_end, &body_depth)) {
		class_name = lsp_class_declared_name(document->text, class_start, body_start);
		if (!class_name) {
			search_start = body_end + 1;
			continue;
		}

		for (i = 0; i < count; i++) {
			token = zend_hash_index_find(tokens, i);
			if (!token || Z_TYPE_P(token) != IS_ARRAY || !lsp_token_in_bounds(token, body_start, body_end)) {
				continue;
			}

			if (!lsp_token_name_equals(token, "T_FUNCTION") || !lsp_token_at_depth(document->text, token, body_depth)) {
				continue;
			}

			visibility = lsp_definition_member_visibility(tokens, i, document->text, body_depth);
			if (visibility == LSP_METHOD_VISIBILITY_PRIVATE || lsp_definition_member_is_final(tokens, i, document->text, body_depth)) {
				continue;
			}

			name_token = lsp_next_function_name_token_ex(tokens, i + 1, &name_index);
			label = lsp_token_string(name_token, "text");
			if (!label || zend_string_equals_literal(label, "__construct")) {
				continue;
			}

			name_start = (size_t) lsp_token_long(name_token, "offset", lsp_token_long(token, "offset", 0));
			name_end = name_start + ZSTR_LEN(label);
			lsp_add_override_code_lens(server, return_value, document, class_name, label, name_start, name_end);
		}

		zend_string_release(class_name);
		search_start = body_end + 1;
	}
}

extern void lsp_lsparrot_definition(lsp_server *server, zval *return_value, lsp_document *document, zval *position)
{
	zend_long line, character, target_line;
	zend_string *word, *detail, *prefix, *class_name;
	zval *token, range, start, end;
	size_t offset;

	lsp_position_from_zval(position, &line, &character);
	offset = lsp_offset_at(document->text, line, character);
	word = lsp_word_at(document->text, offset);
	prefix = lsp_prefix_at(document->text, offset);

	if (ZSTR_LEN(word) > 0 && lsp_project_member_definition(server, document, offset, prefix, word, return_value)) {
		zend_string_release(prefix);
		zend_string_release(word);

		return;
	}

	if (ZSTR_LEN(word) > 0 && lsp_project_static_member_definition(server, document, offset, word, return_value)) {
		zend_string_release(prefix);
		zend_string_release(word);

		return;
	}

	zend_string_release(prefix);

	class_name = lsp_use_statement_class_name_at(document->text, offset);
	if (!class_name && ZSTR_LEN(word) > 0) {
		class_name = lsp_resolve_class_name(document->text, word);
	}

	if (class_name) {
		if (lsp_project_class_definition(server, class_name, return_value)) {
			zend_string_release(class_name);
			zend_string_release(word);

			return;
		}

		zend_string_release(class_name);
	}

	if (ZSTR_LEN(word) == 0 || !lsp_find_symbol(&document->lsparrot, word, &token, NULL, &detail)) {
		zend_string_release(word);
		ZVAL_NULL(return_value);

		return;
	}

	target_line = lsp_token_long(token, "line", 1) - 1;
	if (target_line < 0) {
		target_line = 0;
	}

	array_init(return_value);
	add_assoc_str(return_value, "uri", zend_string_copy(document->uri));
	array_init(&start);
	add_assoc_long(&start, "line", target_line);
	add_assoc_long(&start, "character", 0);
	array_init(&end);
	add_assoc_long(&end, "line", target_line);
	add_assoc_long(&end, "character", 1);
	array_init(&range);
	add_assoc_zval(&range, "start", &start);
	add_assoc_zval(&range, "end", &end);
	add_assoc_zval(return_value, "range", &range);
	zend_string_release(detail);
	zend_string_release(word);
}
