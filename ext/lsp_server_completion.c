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

static inline bool lsp_static_receiver_is_current_class(zend_string *receiver)
{
	return (ZSTR_LEN(receiver) == sizeof("self") - 1 && strncasecmp(ZSTR_VAL(receiver), "self", ZSTR_LEN(receiver)) == 0) ||
		(ZSTR_LEN(receiver) == sizeof("static") - 1 && strncasecmp(ZSTR_VAL(receiver), "static", ZSTR_LEN(receiver)) == 0)
	;
}

static inline bool lsp_static_receiver_is_parent_class(zend_string *receiver)
{
	return ZSTR_LEN(receiver) == sizeof("parent") - 1 && strncasecmp(ZSTR_VAL(receiver), "parent", ZSTR_LEN(receiver)) == 0;
}

static inline bool lsp_static_member_access_context(lsp_document *document, size_t offset, zend_string *prefix, zend_string **class_name, zend_string **member_prefix, bool *current_class_access, bool *parent_class_access, size_t *current_class_start, size_t *current_body_start, size_t *current_body_end, zend_long *current_body_depth)
{
	const char *value = ZSTR_VAL(document->text);
	zend_string *raw, *resolved, *parent_class;
	size_t i, class_start, class_end;

	*class_name = NULL;
	*member_prefix = NULL;
	*current_class_access = false;
	*parent_class_access = false;
	*current_class_start = 0;
	*current_body_start = 0;
	*current_body_end = 0;
	*current_body_depth = 0;

	if (offset > ZSTR_LEN(document->text)) {
		offset = ZSTR_LEN(document->text);
	}

	i = offset >= ZSTR_LEN(prefix) ? offset - ZSTR_LEN(prefix) : 0;

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
	if (lsp_static_receiver_is_current_class(raw)) {
		if (!lsp_find_enclosing_class_header(document->text, offset, current_class_start, current_body_start, current_body_end, current_body_depth)) {
			zend_string_release(raw);

			return false;
		}

		resolved = lsp_class_declared_name(document->text, *current_class_start, *current_body_start);
		zend_string_release(raw);

		if (!resolved) {
			return false;
		}

		*class_name = resolved;
		*member_prefix = zend_string_copy(prefix);
		*current_class_access = true;

		return true;
	}

	if (lsp_static_receiver_is_parent_class(raw)) {
		if (!lsp_find_enclosing_class_header(document->text, offset, current_class_start, current_body_start, current_body_end, current_body_depth)) {
			zend_string_release(raw);

			return false;
		}

		parent_class = lsp_class_extends_name(document->text, *current_class_start, *current_body_start);
		zend_string_release(raw);
		if (!parent_class) {
			return false;
		}

		*class_name = parent_class;
		*member_prefix = zend_string_copy(prefix);
		*parent_class_access = true;

		return true;
	}

	resolved = lsp_resolve_class_name(document->text, raw);
	if (resolved) {
		zend_string_release(raw);
		*class_name = resolved;
	} else {
		*class_name = raw;
	}

	*member_prefix = zend_string_copy(prefix);

	return true;
}

static inline void lsp_add_class_name_resolution_completion(zval *items, zend_string *member_prefix)
{
	zend_string *label, *detail;

	if (ZSTR_LEN(member_prefix) > 0 && ZSTR_VAL(member_prefix)[0] == '$') {
		return;
	}

	if (!lsp_matches_prefix_literal("class", member_prefix)) {
		return;
	}

	label = zend_string_init("class", sizeof("class") - 1, 0);
	detail = zend_string_init("class name resolution", sizeof("class name resolution") - 1, 0);
	lsp_add_completion_item_ex(items, label, 21, detail, "lsparrot");
	zend_string_release(detail);
	zend_string_release(label);
}

static inline bool lsp_add_static_member_completions(lsp_server *server, zval *items, lsp_document *document, size_t offset, zend_string *prefix)
{
	zend_long current_body_depth;
	zend_string *class_name, *member_prefix, *parent_class;
	size_t current_class_start, current_body_start, current_body_end;
	bool current_class_access, parent_class_access;

	if (!lsp_static_member_access_context(document, offset, prefix, &class_name, &member_prefix, &current_class_access, &parent_class_access, &current_class_start, &current_body_start, &current_body_end, &current_body_depth)) {
		return false;
	}

	lsp_add_class_name_resolution_completion(items, member_prefix);

	if (current_class_access) {
		lsp_add_current_static_member_completions(items, document, current_body_start, current_body_end, current_body_depth, member_prefix);
		lsp_add_current_class_phpdoc_member_completions(server, items, document, current_class_start, member_prefix, true);

		parent_class = lsp_class_extends_name(document->text, current_class_start, current_body_start);
		if (parent_class) {
			lsp_add_static_project_class_member_completions(server, items, parent_class, member_prefix);
			zend_string_release(parent_class);
		}
	}

	if (parent_class_access) {
		lsp_add_inherited_static_project_class_member_completions(server, items, class_name, member_prefix);
	} else {
		lsp_add_static_project_class_member_completions(server, items, class_name, member_prefix);
	}
	zend_string_release(class_name);
	zend_string_release(member_prefix);

	return true;
}

static inline void lsp_phpdoc_add_completions(lsp_server *server, zval *items, lsp_document *document, size_t offset, zend_string *prefix)
{
	lsp_phpdoc_add_annotation_completions(items, document->text, offset);
	lsp_phpdoc_add_variable_type_completions(server, items, document->text, prefix);
	lsp_phpdoc_add_template_completions(server, items, document->text, prefix);
	lsp_phpdoc_add_array_shape_completions(server, items, document, offset);
}

static inline bool lsp_previous_identifier_before(zend_string *text, size_t end_offset, size_t *word_start, size_t *word_end)
{
	const char *value = ZSTR_VAL(text);
	size_t i;

	i = end_offset > ZSTR_LEN(text) ? ZSTR_LEN(text) : end_offset;
	while (i > 0 && isspace((unsigned char) value[i - 1])) {
		i--;
	}

	*word_end = i;
	while (i > 0 && lsp_doc_is_identifier_char(value[i - 1])) {
		i--;
	}

	*word_start = i;

	return *word_end > *word_start;
}

static inline bool lsp_identifier_slice_equals_literal(zend_string *text, size_t start, size_t end, const char *literal)
{
	size_t length;

	if (end < start) {
		return false;
	}

	length = strlen(literal);

	return end - start == length && strncasecmp(ZSTR_VAL(text) + start, literal, length) == 0;
}

static inline bool lsp_function_like_keyword_before_open_paren(zend_string *text, size_t open_paren)
{
	const char *value = ZSTR_VAL(text);
	size_t word_start, word_end, cursor;

	if (!lsp_previous_identifier_before(text, open_paren, &word_start, &word_end)) {
		cursor = open_paren > ZSTR_LEN(text) ? ZSTR_LEN(text) : open_paren;
		while (cursor > 0 && isspace((unsigned char) value[cursor - 1])) {
			cursor--;
		}
		if (cursor == 0 || value[cursor - 1] != '&') {
			return false;
		}

		return lsp_previous_identifier_before(text, cursor - 1, &word_start, &word_end) &&
			lsp_identifier_slice_equals_literal(text, word_start, word_end, "function")
		;
	}

	if (lsp_identifier_slice_equals_literal(text, word_start, word_end, "function") ||
		lsp_identifier_slice_equals_literal(text, word_start, word_end, "fn")
	) {
		return true;
	}

	cursor = word_start;
	while (cursor > 0 && isspace((unsigned char) value[cursor - 1])) {
		cursor--;
	}

	if (cursor > 0 && value[cursor - 1] == '&') {
		cursor--;
		while (cursor > 0 && isspace((unsigned char) value[cursor - 1])) {
			cursor--;
		}
	}

	return lsp_previous_identifier_before(text, cursor, &word_start, &word_end) &&
		lsp_identifier_slice_equals_literal(text, word_start, word_end, "function")
	;
}

static inline bool lsp_return_type_scan_reaches_colon(zend_string *text, size_t offset, zend_string *prefix, size_t *colon_offset)
{
	const char *value = ZSTR_VAL(text);
	size_t scan, prefix_start;

	prefix_start = offset >= ZSTR_LEN(prefix) ? offset - ZSTR_LEN(prefix) : 0;
	scan = prefix_start > ZSTR_LEN(text) ? ZSTR_LEN(text) : prefix_start;
	while (scan > 0) {
		while (scan > 0 && isspace((unsigned char) value[scan - 1])) {
			scan--;
		}

		if (scan == 0) {
			break;
		}

		if (value[scan - 1] == ':') {
			*colon_offset = scan - 1;

			return true;
		}

		if (lsp_doc_is_identifier_char(value[scan - 1]) ||
			value[scan - 1] == '\\' ||
			value[scan - 1] == '?' ||
			value[scan - 1] == '|' ||
			value[scan - 1] == '&'
		) {
			scan--;
			continue;
		}

		break;
	}

	*colon_offset = 0;

	return false;
}

static inline bool lsp_is_return_type_completion_context(zend_string *text, size_t offset, zend_string *prefix)
{
	const char *value = ZSTR_VAL(text);
	size_t colon_offset, scan, open_paren;

	if (!lsp_return_type_scan_reaches_colon(text, offset, prefix, &colon_offset)) {
		return false;
	}

	scan = colon_offset;
	while (scan > 0 && isspace((unsigned char) value[scan - 1])) {
		scan--;
	}

	if (scan == 0 || value[scan - 1] != ')') {
		return false;
	}

	return lsp_find_matching_open_paren(text, scan - 1, &open_paren) &&
		lsp_function_like_keyword_before_open_paren(text, open_paren)
	;
}

static inline void lsp_add_return_type_builtin_completions(zval *items, zend_string *prefix)
{
	const char *types[] = {
		"array",
		"bool",
		"callable",
		"false",
		"float",
		"int",
		"iterable",
		"mixed",
		"never",
		"null",
		"object",
		"parent",
		"self",
		"static",
		"string",
		"true",
		"void"
	};
	zend_string *label, *detail;
	uint32_t i;

	for (i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
		label = zend_string_init(types[i], strlen(types[i]), 0);
		if (!lsp_matches_prefix_string(label, prefix)) {
			zend_string_release(label);
			continue;
		}

		detail = strpprintf(0, "type %s", types[i]);
		lsp_add_completion_item_ex(items, label, 25, detail, "lsparrot");
		zend_string_release(detail);
		zend_string_release(label);
	}
}

static inline void lsp_add_current_document_class_like_completions(zval *items, lsp_document *document, HashTable *tokens, zend_string *prefix)
{
	zend_long kind;
	zend_string *label, *detail;
	zval *token;
	uint32_t i, count;

	count = zend_hash_num_elements(tokens);
	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY || !lsp_token_is_class_like(token)) {
			continue;
		}

		label = lsp_next_string_token(tokens, i + 1);
		if (!label || !lsp_matches_prefix_string(label, prefix)) {
			continue;
		}

		kind = lsp_token_name_equals(token, "T_INTERFACE") ? 8 : (lsp_token_name_equals(token, "T_TRAIT") ? 9 : (lsp_token_name_equals(token, "T_ENUM") ? 13 : 7));
		detail = strpprintf(0, "%s %s",
			lsp_token_name_equals(token, "T_INTERFACE") ? "interface" : (lsp_token_name_equals(token, "T_TRAIT") ? "trait" : (lsp_token_name_equals(token, "T_ENUM") ? "enum" : "class")),
			ZSTR_VAL(label)
		);
		lsp_add_completion_item_ex(items, label, kind, detail, "lsparrot");
		zend_string_release(detail);
	}
}

static inline bool lsp_prefix_has_global_namespace(zend_string *prefix)
{
	return ZSTR_LEN(prefix) > 0 && ZSTR_VAL(prefix)[0] == '\\';
}

static inline bool lsp_prefix_is_qualified_name(zend_string *prefix)
{
	return memchr(ZSTR_VAL(prefix), '\\', ZSTR_LEN(prefix)) != NULL;
}

static inline zend_string *lsp_completion_insert_name(zend_string *label, zend_string *qualified_name, zend_string *prefix)
{
	zend_string *name, *global_name;

	name = lsp_prefix_is_qualified_name(prefix) ? zend_string_copy(qualified_name) : zend_string_copy(label);
	if (lsp_prefix_has_global_namespace(prefix)) {
		global_name = strpprintf(0, "\\%s", ZSTR_VAL(name));

		zend_string_release(name);

		return global_name;
	}

	return name;
}

static inline void lsp_add_text_edit_completion_item(zval *items, zend_string *label, zend_string *qualified_name, zend_long kind, zend_string *detail, const char *source, zend_string *text, size_t offset, zend_string *prefix, bool call_snippet)
{
	zend_string *insert_name, *new_text;
	zval item, data, text_edit, range;
	size_t prefix_start;

	prefix_start = offset >= ZSTR_LEN(prefix) ? offset - ZSTR_LEN(prefix) : 0;
	insert_name = lsp_completion_insert_name(label, qualified_name, prefix);
	new_text = call_snippet ? lsp_completion_call_snippet_for_detail(insert_name, detail) : zend_string_copy(insert_name);

	array_init(&item);
	add_assoc_str(&item, "label", zend_string_copy(label));
	add_assoc_long(&item, "kind", kind);
	add_assoc_str(&item, "detail", zend_string_copy(detail));
	add_assoc_str(&item, "filterText", zend_string_copy(insert_name));
	if (call_snippet) {
		add_assoc_long(&item, "insertTextFormat", 2);
	}

	array_init(&text_edit);
	lsp_range_from_offsets(text, prefix_start, offset, &range);
	add_assoc_zval(&text_edit, "range", &range);
	add_assoc_str(&text_edit, "newText", new_text);
	add_assoc_zval(&item, "textEdit", &text_edit);
	array_init(&data);
	add_assoc_string(&data, "source", source);
	add_assoc_str(&data, "qualifiedName", zend_string_copy(qualified_name));
	add_assoc_zval(&item, "data", &data);
	add_next_index_zval(items, &item);
	zend_string_release(insert_name);
}

static inline void lsp_add_current_document_constant_completions(zval *items, lsp_document *document, HashTable *tokens, zend_string *prefix)
{
	zend_string *label, *detail;
	zval *token;
	uint32_t i, count;
	size_t token_offset;

	count = zend_hash_num_elements(tokens);
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
		if (!label || !lsp_matches_prefix_string(label, prefix)) {
			continue;
		}

		detail = strpprintf(0, "constant %s", ZSTR_VAL(label));
		lsp_add_completion_item_ex(items, label, 21, detail, "lsparrot");
		zend_string_release(detail);
	}
}

static inline void lsp_add_builtin_constant_completions(zval *items, zend_string *prefix, zend_string *text, size_t offset)
{
	zend_constant *constant;
	zend_string *label, *detail;
	const char *label_value;
	size_t i, label_length;

	if (!EG(zend_constants) || lsp_effective_name_prefix_length(prefix) == 0) {
		return;
	}

	ZEND_HASH_FOREACH_PTR(EG(zend_constants), constant) {
		if (!constant || !constant->name) {
			continue;
		}

		label_value = ZSTR_VAL(constant->name);
		label_length = ZSTR_LEN(constant->name);
		for (i = ZSTR_LEN(constant->name); i > 0; i--) {
			if (ZSTR_VAL(constant->name)[i - 1] == '\\') {
				label_value = ZSTR_VAL(constant->name) + i;
				label_length = ZSTR_LEN(constant->name) - i;
				break;
			}
		}

		label = zend_string_init(label_value, label_length, 0);
		if (!lsp_matches_prefix_string(label, prefix) && !lsp_matches_prefix_string(constant->name, prefix)) {
			zend_string_release(label);
			continue;
		}

		detail = strpprintf(0, "constant %s", ZSTR_VAL(constant->name));
		lsp_add_text_edit_completion_item(items, label, constant->name, 21, detail, "lsparrot", text, offset, prefix, false);
		zend_string_release(detail);
		zend_string_release(label);
	} ZEND_HASH_FOREACH_END();
}

static inline zend_long lsp_builtin_class_completion_kind(zend_class_entry *ce)
{
	if (ce->ce_flags & ZEND_ACC_INTERFACE) {
		return 8;
	}

	if (ce->ce_flags & ZEND_ACC_TRAIT) {
		return 9;
	}

#ifdef ZEND_ACC_ENUM
	if (ce->ce_flags & ZEND_ACC_ENUM) {
		return 13;
	}
#endif

	return 7;
}

static inline char lsp_builtin_class_symbol_kind(zend_class_entry *ce)
{
	if (ce->ce_flags & ZEND_ACC_INTERFACE) {
		return LSP_SYMBOL_INTERFACE;
	}

	if (ce->ce_flags & ZEND_ACC_TRAIT) {
		return LSP_SYMBOL_TRAIT;
	}

#ifdef ZEND_ACC_ENUM
	if (ce->ce_flags & ZEND_ACC_ENUM) {
		return LSP_SYMBOL_ENUM;
	}
#endif

	return LSP_SYMBOL_CLASS;
}

static inline void lsp_add_builtin_class_completions_ex(zval *items, zend_string *prefix, zend_string *text, size_t offset, bool allow_empty_prefix)
{
	const char *label_value;
	zend_string *label, *detail;
	zend_class_entry *ce;
	size_t label_length;

	if (!CG(class_table) || (!allow_empty_prefix && lsp_effective_name_prefix_length(prefix) == 0)) {
		return;
	}

	ZEND_HASH_FOREACH_PTR(CG(class_table), ce) {
		if (!ce || !ce->name) {
			continue;
		}

		label_value = lsp_basename_from_fqcn(ZSTR_VAL(ce->name), ZSTR_LEN(ce->name), &label_length);
		label = zend_string_init(label_value, label_length, 0);
		if (!lsp_matches_prefix_string(label, prefix) && !lsp_matches_prefix_string(ce->name, prefix)) {
			zend_string_release(label);
			continue;
		}

		detail = strpprintf(0, "%s%s", lsp_builtin_class_detail_prefix(ce), ZSTR_VAL(ce->name));
		lsp_add_text_edit_completion_item(items, label, ce->name, lsp_builtin_class_completion_kind(ce), detail, "lsparrot", text, offset, prefix, false);
		zend_string_release(detail);
		zend_string_release(label);
	} ZEND_HASH_FOREACH_END();
}

static inline void lsp_add_builtin_class_completions(zval *items, zend_string *prefix, zend_string *text, size_t offset)
{
	lsp_add_builtin_class_completions_ex(items, prefix, text, offset, false);
}

static inline void lsp_add_return_type_builtin_class_completions(zval *items, lsp_document *document, zend_string *prefix, size_t offset)
{
	const char *label_value;
	zend_string *label;
	zend_class_entry *ce;
	size_t label_length;

	if (!CG(class_table)) {
		return;
	}

	ZEND_HASH_FOREACH_PTR(CG(class_table), ce) {
		if (!ce || !ce->name) {
			continue;
		}

		label_value = lsp_basename_from_fqcn(ZSTR_VAL(ce->name), ZSTR_LEN(ce->name), &label_length);
		label = zend_string_init(label_value, label_length, 0);
		if (!lsp_matches_prefix_string(label, prefix) && !lsp_matches_prefix_string(ce->name, prefix)) {
			zend_string_release(label);
			continue;
		}

		lsp_add_class_like_symbol_completion_item(items, document, offset, prefix, lsp_builtin_class_symbol_kind(ce), ce->name);
		zend_string_release(label);
	} ZEND_HASH_FOREACH_END();
}

static inline void lsp_add_builtin_function_completions(zval *items, zend_string *prefix, zend_string *text, size_t offset)
{
	zend_function *function;
	zend_string *name, *detail;

	if (!CG(function_table) || lsp_effective_name_prefix_length(prefix) == 0) {
		return;
	}

	ZEND_HASH_FOREACH_PTR(CG(function_table), function) {
		if (!function || !function->common.function_name) {
			continue;
		}

		name = function->common.function_name;
		if (!lsp_matches_prefix_string(name, prefix)) {
			continue;
		}

		detail = strpprintf(0, "function %s(...)", ZSTR_VAL(name));
		lsp_add_text_edit_completion_item(items, name, name, 3, detail, "lsparrot", text, offset, prefix, true);
		zend_string_release(detail);
	} ZEND_HASH_FOREACH_END();
}

static inline zend_string *lsp_analyzer_completion_cache_key(const char *analyzer, lsp_document *document, size_t offset, zend_string *prefix)
{
	return strpprintf(0, "%s:%s:" ZEND_LONG_FMT ":%zu:%s", analyzer, ZSTR_VAL(document->uri), document->version, offset, ZSTR_VAL(prefix));
}

static inline lsp_analyzer_job *lsp_analyzer_completion_job(lsp_server *server, const char *analyzer)
{
	return strcmp(analyzer, "phpstan") == 0 ? &server->phpstan_completion_job : &server->psalm_completion_job;
}

static inline bool lsp_analyzer_completion_enabled(lsp_server *server, const char *analyzer)
{
	return strcmp(analyzer, "phpstan") == 0 ? server->phpstan_enabled : server->psalm_enabled;
}

static inline void lsp_completion_item_set_source(zval *item, const char *source)
{
	zval *data, new_data;

	if (Z_TYPE_P(item) != IS_ARRAY) {
		return;
	}

	SEPARATE_ARRAY(item);
	data = zend_hash_str_find(Z_ARRVAL_P(item), "data", sizeof("data") - 1);
	if (!data || Z_TYPE_P(data) != IS_ARRAY) {
		array_init(&new_data);
		add_assoc_string(&new_data, "source", source);
		add_assoc_zval(item, "data", &new_data);

		return;
	}

	SEPARATE_ARRAY(data);
	zend_hash_str_del(Z_ARRVAL_P(data), "source", sizeof("source") - 1);
	add_assoc_string(data, "source", source);
}

static inline void lsp_completion_items_set_source(zval *items, const char *source)
{
	zval *item;

	if (Z_TYPE_P(items) != IS_ARRAY) {
		return;
	}

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(items), item) {
		lsp_completion_item_set_source(item, source);
	} ZEND_HASH_FOREACH_END();
}

static inline bool lsp_phpstan_type_token_is_builtin(zend_string *name)
{
	const char *builtins[] = {
		"array",
		"bool",
		"callable",
		"false",
		"float",
		"int",
		"iterable",
		"mixed",
		"never",
		"non-empty-array",
		"non-empty-list",
		"non-empty-string",
		"null",
		"object",
		"resource",
		"self",
		"static",
		"string",
		"true",
		"void"
	};
	uint32_t i;

	for (i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
		if (ZSTR_LEN(name) == strlen(builtins[i]) && strncasecmp(ZSTR_VAL(name), builtins[i], ZSTR_LEN(name)) == 0) {
			return true;
		}
	}

	return false;
}

static inline zend_string *lsp_phpstan_type_first_name(zend_string *type)
{
	const char *p, *end, *start;

	p = ZSTR_VAL(type);
	end = p + ZSTR_LEN(type);
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
	while (p < end && (lsp_doc_is_identifier_char(*p) || *p == '\\' || *p == '-')) {
		p++;
	}

	if (p == start) {
		return NULL;
	}

	return zend_string_init(start, p - start, 0);
}

static inline zend_string *lsp_phpstan_completion_class_from_type(lsp_document *document, zend_string *type)
{
	const char *slash;
	zend_string *raw, *resolved;

	raw = lsp_phpstan_type_first_name(type);
	if (!raw) {
		return NULL;
	}

	if (lsp_phpstan_type_token_is_builtin(raw)) {
		zend_string_release(raw);

		return NULL;
	}

	slash = memchr(ZSTR_VAL(raw), '\\', ZSTR_LEN(raw));
	if (slash) {
		return raw;
	}

	resolved = lsp_resolve_class_name(document->text, raw);
	zend_string_release(raw);

	return resolved;
}

static inline zend_string *lsp_phpstan_member_receiver_expression(zend_string *text, size_t offset, zend_string *prefix, zend_string **member_prefix, size_t *receiver_start_offset)
{
	const char *value;
	size_t receiver_end, start, i, depth;

	*member_prefix = NULL;
	if (receiver_start_offset) {
		*receiver_start_offset = 0;
	}
	if (!lsp_member_access_arrow(text, offset, prefix, NULL, &receiver_end)) {
		return NULL;
	}

	value = ZSTR_VAL(text);
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

	if (receiver_end <= start) {
		return NULL;
	}

	*member_prefix = zend_string_copy(prefix);
	if (receiver_start_offset) {
		*receiver_start_offset = start;
	}

	return zend_string_init(value + start, receiver_end - start, 0);
}

static inline void lsp_add_phpstan_member_completions(lsp_server *server, zval *items, lsp_document *document, size_t offset, zend_string *prefix)
{
	zend_string *receiver, *member_prefix, *type, *class_name;

	receiver = lsp_phpstan_member_receiver_expression(document->text, offset, prefix, &member_prefix, NULL);
	if (!receiver) {
		return;
	}

	type = lsp_phpstan_type_for_expression(server, document, receiver, offset);
	if (!type) {
		zend_string_release(receiver);
		zend_string_release(member_prefix);

		return;
	}

	class_name = lsp_phpstan_completion_class_from_type(document, type);
	if (class_name) {
		lsp_add_inherited_public_project_class_member_completions(server, items, class_name, member_prefix);
		zend_string_release(class_name);
	}

	zend_string_release(type);
	zend_string_release(receiver);
	zend_string_release(member_prefix);
}

static inline void lsp_add_psalm_member_completions(lsp_server *server, zval *items, lsp_document *document, size_t offset, zend_string *prefix)
{
	zend_string *receiver, *member_prefix, *type, *class_name;

	receiver = lsp_phpstan_member_receiver_expression(document->text, offset, prefix, &member_prefix, NULL);
	if (!receiver) {
		return;
	}

	type = lsp_psalm_type_for_expression(server, document, receiver, offset);
	if (!type) {
		zend_string_release(receiver);
		zend_string_release(member_prefix);

		return;
	}

	class_name = lsp_phpstan_completion_class_from_type(document, type);
	if (class_name) {
		lsp_add_inherited_public_project_class_member_completions(server, items, class_name, member_prefix);
		zend_string_release(class_name);
	}

	zend_string_release(type);
	zend_string_release(receiver);
	zend_string_release(member_prefix);
}

static inline void lsp_position_zval_at_offset(zval *position, zend_string *text, size_t offset)
{
	const char *value;
	size_t i, length, line_start;
	zend_long line, character;

	value = ZSTR_VAL(text);
	length = ZSTR_LEN(text);
	if (offset > length) {
		offset = length;
	}

	line = 0;
	line_start = 0;
	for (i = 0; i < offset; i++) {
		if (value[i] == '\n') {
			line++;
			line_start = i + 1;
		}
	}

	character = (zend_long) (offset - line_start);
	array_init(position);
	add_assoc_long(position, "line", line);
	add_assoc_long(position, "character", character);
}

static inline size_t lsp_psalm_ls_rhs_hover_probe_offset(zend_string *text, size_t rhs_start, size_t rhs_end)
{
	const char *value;
	size_t i, best_arrow, best_static, first_identifier, p, length;
	int32_t paren_depth, bracket_depth, brace_depth;
	bool found_arrow, found_static, found_identifier;

	value = ZSTR_VAL(text);
	length = ZSTR_LEN(text);
	if (rhs_start >= length) {
		return length;
	}
	if (rhs_end > length) {
		rhs_end = length;
	}
	if (rhs_end <= rhs_start) {
		return rhs_start;
	}

	best_arrow = rhs_start;
	best_static = rhs_start;
	first_identifier = rhs_start;
	paren_depth = 0;
	bracket_depth = 0;
	brace_depth = 0;
	found_arrow = false;
	found_static = false;
	found_identifier = false;

	for (i = rhs_start; i < rhs_end; i++) {
		if (!found_identifier && (lsp_doc_is_identifier_char(value[i]) || value[i] == '$' || value[i] == '\\')) {
			first_identifier = value[i] == '$' && i + 1 < rhs_end ? i + 1 : i;
			found_identifier = true;
		}

		if (value[i] == '(') {
			paren_depth++;
			continue;
		}
		if (value[i] == ')' && paren_depth > 0) {
			paren_depth--;
			continue;
		}
		if (value[i] == '[') {
			bracket_depth++;
			continue;
		}
		if (value[i] == ']' && bracket_depth > 0) {
			bracket_depth--;
			continue;
		}
		if (value[i] == '{') {
			brace_depth++;
			continue;
		}
		if (value[i] == '}' && brace_depth > 0) {
			brace_depth--;
			continue;
		}

		if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 0) {
			if (i + 1 < rhs_end && value[i] == '-' && value[i + 1] == '>') {
				best_arrow = i + 1;
				found_arrow = true;
			} else if (i + 2 < rhs_end && value[i] == '?' && value[i + 1] == '-' && value[i + 2] == '>') {
				best_arrow = i + 2;
				found_arrow = true;
			} else if (i + 1 < rhs_end && value[i] == ':' && value[i + 1] == ':') {
				best_static = i + 1;
				found_static = true;
			}
		}
	}

	if (found_arrow) {
		return best_arrow;
	}
	if (found_static) {
		return best_static;
	}

	p = rhs_start;
	while (p < rhs_end && isspace((unsigned char) value[p])) {
		p++;
	}
	if (p + sizeof("new") - 1 < rhs_end &&
		strncasecmp(value + p, "new", sizeof("new") - 1) == 0 &&
		!lsp_doc_is_identifier_char(value[p + sizeof("new") - 1])
	) {
		p += sizeof("new") - 1;
		while (p < rhs_end && isspace((unsigned char) value[p])) {
			p++;
		}
		if (p < rhs_end && value[p] == '\\') {
			p++;
		}
		if (p < rhs_end) {
			return p;
		}
	}

	return found_identifier ? first_identifier : rhs_start;
}

static inline bool lsp_previous_variable_assignment_probe_offset(zend_string *text, zend_string *variable, size_t before_offset, size_t *probe_offset)
{
	const char *value;
	size_t length, variable_length, i, candidate, after, rhs_start, rhs_end;
	int32_t paren_depth, bracket_depth, brace_depth;

	if (!text || !variable || ZSTR_LEN(variable) == 0) {
		return false;
	}

	value = ZSTR_VAL(text);
	length = ZSTR_LEN(text);
	variable_length = ZSTR_LEN(variable);
	if (before_offset > length) {
		before_offset = length;
	}
	if (before_offset < variable_length) {
		return false;
	}

	i = before_offset;
	while (i >= variable_length) {
		candidate = i - variable_length;
		if (memcmp(value + candidate, ZSTR_VAL(variable), variable_length) == 0 &&
			(candidate == 0 || !lsp_doc_is_identifier_char(value[candidate - 1])) &&
			(candidate + variable_length >= length || !lsp_doc_is_identifier_char(value[candidate + variable_length]))
		) {
			after = candidate + variable_length;
			while (after < length && isspace((unsigned char) value[after])) {
				after++;
			}
			if (after < length && value[after] == '=' &&
				(after + 1 >= length || (value[after + 1] != '=' && value[after + 1] != '>')) &&
				(after == 0 || (value[after - 1] != '!' && value[after - 1] != '<' && value[after - 1] != '>'))
			) {
				rhs_start = after + 1;
				while (rhs_start < length && isspace((unsigned char) value[rhs_start])) {
					rhs_start++;
				}

				rhs_end = rhs_start;
				paren_depth = 0;
				bracket_depth = 0;
				brace_depth = 0;
				while (rhs_end < length) {
					if (value[rhs_end] == '(') {
						paren_depth++;
					} else if (value[rhs_end] == ')' && paren_depth > 0) {
						paren_depth--;
					} else if (value[rhs_end] == '[') {
						bracket_depth++;
					} else if (value[rhs_end] == ']' && bracket_depth > 0) {
						bracket_depth--;
					} else if (value[rhs_end] == '{') {
						brace_depth++;
					} else if (value[rhs_end] == '}' && brace_depth > 0) {
						brace_depth--;
					} else if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 0 && (value[rhs_end] == ';' || value[rhs_end] == '\n' || value[rhs_end] == '\r')) {
						break;
					}
					rhs_end++;
				}

				while (rhs_end > rhs_start && isspace((unsigned char) value[rhs_end - 1])) {
					rhs_end--;
				}

				*probe_offset = rhs_end > rhs_start ? lsp_psalm_ls_rhs_hover_probe_offset(text, rhs_start, rhs_end) : candidate + 1;

				return true;
			}
		}

		if (i == variable_length) {
			break;
		}
		i--;
	}

	return false;
}

static inline void lsp_add_psalm_ls_hover_member_completions(lsp_server *server, zval *items, lsp_document *document, size_t offset, zend_string *prefix)
{
	zend_string *receiver, *member_prefix, *type, *class_name;
	zval receiver_position;
	size_t receiver_start, receiver_probe_offset, assignment_probe_offset;

	receiver = lsp_phpstan_member_receiver_expression(document->text, offset, prefix, &member_prefix, &receiver_start);
	if (!receiver) {
		return;
	}

	receiver_probe_offset = receiver_start;
	if (ZSTR_LEN(receiver) > 1 && ZSTR_VAL(receiver)[0] == '$') {
		if (lsp_previous_variable_assignment_probe_offset(document->text, receiver, receiver_start, &assignment_probe_offset)) {
			receiver_probe_offset = assignment_probe_offset;
		} else {
			receiver_probe_offset++;
		}
	}
	lsp_position_zval_at_offset(&receiver_position, document->text, receiver_probe_offset);
	type = lsp_psalm_ls_type_for_position_async(server, document, &receiver_position, receiver_probe_offset, receiver);
	zval_ptr_dtor(&receiver_position);
	if (!type) {
		zend_string_release(receiver);
		zend_string_release(member_prefix);

		return;
	}

	class_name = lsp_phpstan_completion_class_from_type(document, type);
	if (class_name) {
		lsp_add_inherited_public_project_class_member_completions(server, items, class_name, member_prefix);
		zend_string_release(class_name);
	}

	zend_string_release(type);
	zend_string_release(receiver);
	zend_string_release(member_prefix);
}

static inline bool lsp_build_analyzer_completion_items(lsp_server *server, zval *items, lsp_document *document, size_t offset, zend_string *prefix, const char *analyzer)
{
	array_init(items);
	if (lsp_is_member_access_completion(document->text, offset, prefix)) {
		if (strcmp(analyzer, "phpstan") == 0) {
			lsp_add_phpstan_member_completions(server, items, document, offset, prefix);
		} else if (strcmp(analyzer, "psalm") == 0) {
			lsp_add_psalm_member_completions(server, items, document, offset, prefix);
		}
		lsp_add_this_member_completions(server, items, document, offset, prefix);
		lsp_phpdoc_add_object_shape_member_completions(server, items, document, offset, prefix);
		lsp_add_inferred_member_completions(server, items, document, offset, prefix);
	} else if (!lsp_add_static_member_completions(server, items, document, offset, prefix)) {
		return false;
	}

	if (zend_hash_num_elements(Z_ARRVAL_P(items)) == 0) {
		return false;
	}

	lsp_completion_items_set_source(items, analyzer);
	lsp_deduplicate_completion_items(items);

	return zend_hash_num_elements(Z_ARRVAL_P(items)) > 0;
}

static inline void lsp_append_cached_analyzer_completion_items(lsp_server *server, zval *items, zend_string *key)
{
	zval *cached, *item, copy;

	cached = zend_hash_find(&server->completion_cache, key);
	if (!cached || Z_TYPE_P(cached) != IS_ARRAY) {
		return;
	}

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(cached), item) {
		ZVAL_COPY(&copy, item);
		add_next_index_zval(items, &copy);
	} ZEND_HASH_FOREACH_END();
}

static inline bool lsp_analyzer_completion_cache_or_schedule(lsp_server *server, zval *items, lsp_document *document, size_t offset, zend_string *prefix, const char *analyzer)
{
	zend_string *key;
	zval built_items;
	bool built;

	if (!lsp_analyzer_completion_enabled(server, analyzer)) {
		return false;
	}

	key = lsp_analyzer_completion_cache_key(analyzer, document, offset, prefix);
	if (zend_hash_exists(&server->completion_cache, key)) {
		lsp_append_cached_analyzer_completion_items(server, items, key);
		zend_string_release(key);

		return false;
	}

	built = false;
	if (lsp_build_analyzer_completion_items(server, &built_items, document, offset, prefix, analyzer)) {
		zend_hash_update(&server->completion_cache, key, &built_items);
		lsp_append_cached_analyzer_completion_items(server, items, key);
		built = true;
	}

	zend_string_release(key);

	return built;
}

static inline bool lsp_add_analyzer_completion_items(lsp_server *server, zval *items, lsp_document *document, zval *position, size_t offset, zend_string *prefix)
{
	bool pending = false;

	pending = lsp_analyzer_completion_cache_or_schedule(server, items, document, offset, prefix, "phpstan") || pending;
	pending = lsp_analyzer_completion_cache_or_schedule(server, items, document, offset, prefix, "psalm") || pending;
	if (lsp_psalm_ls_enabled(server)) {
		if (lsp_is_member_access_completion(document->text, offset, prefix)) {
			lsp_add_psalm_ls_hover_member_completions(server, items, document, offset, prefix);
		}
		pending = lsp_psalm_ls_completion_cache_or_schedule(server, items, document, position, offset, prefix) || pending;
	}

	return pending;
}

extern bool lsp_offset_is_inside_class_body(zend_string *text, size_t offset)
{
	zend_long body_depth;
	size_t search_start, class_start, body_start, body_end;

	search_start = 0;
	while (lsp_find_class_header_from(text, search_start, &class_start, &body_start, &body_end, &body_depth)) {
		if (offset >= body_start && offset < body_end) {
			return true;
		}

		search_start = body_end + 1;
	}

	return false;
}

extern const char *lsp_builtin_class_detail_prefix(zend_class_entry *ce)
{
	if (ce->ce_flags & ZEND_ACC_INTERFACE) {
		return "interface ";
	}

	if (ce->ce_flags & ZEND_ACC_TRAIT) {
		return "trait ";
	}

#ifdef ZEND_ACC_ENUM
	if (ce->ce_flags & ZEND_ACC_ENUM) {
		return "enum ";
	}
#endif

	return "class ";
}

extern void lsp_reap_analyzer_completion_jobs(void)
{
}

extern void lsp_lsparrot_completion(lsp_server *server, zval *return_value, lsp_document *document, zval *position)
{
	zend_long line, character, kind;
	zend_string *prefix, *label, *detail;
	zval items, *tokens_zv, *token, *name_token;
	HashTable *tokens;
	uint32_t i, count, name_index;
	size_t offset, prefix_start;
	char constraint_kind;
	bool incomplete;

	lsp_position_from_zval(position, &line, &character);
	offset = lsp_offset_at(document->text, line, character);
	prefix = lsp_prefix_at(document->text, offset);
	lsp_reap_analyzer_completion_jobs();

	array_init(return_value);
	incomplete = lsp_analyzer_jobs_running_for_document(server, document);
	add_assoc_bool(return_value, "isIncomplete", incomplete);
	array_init(&items);

	constraint_kind = lsp_type_constraint_completion_kind(document->text, offset, prefix);
	if (constraint_kind != '\0') {
		lsp_add_project_symbol_kind_completions(server, &items, document, offset, prefix, constraint_kind);
		lsp_deduplicate_completion_items(&items);
		lsp_completion_attach_items(return_value, &items);
		zend_string_release(prefix);

		return;
	}

	if (lsp_is_member_access_completion(document->text, offset, prefix)) {
		lsp_add_this_member_completions(server, &items, document, offset, prefix);
		lsp_phpdoc_add_object_shape_member_completions(server, &items, document, offset, prefix);
		lsp_add_inferred_member_completions(server, &items, document, offset, prefix);
		incomplete = lsp_add_analyzer_completion_items(server, &items, document, position, offset, prefix) || incomplete;
		lsp_deduplicate_completion_items(&items);
		add_assoc_bool(return_value, "isIncomplete", incomplete);
		lsp_completion_attach_items(return_value, &items);
		zend_string_release(prefix);

		return;
	}

	if (lsp_add_static_member_completions(server, &items, document, offset, prefix)) {
		incomplete = lsp_add_analyzer_completion_items(server, &items, document, position, offset, prefix) || incomplete;
		lsp_deduplicate_completion_items(&items);
		add_assoc_bool(return_value, "isIncomplete", incomplete);
		lsp_completion_attach_items(return_value, &items);
		zend_string_release(prefix);

		return;
	}

	if (lsp_phpdoc_add_array_shape_completions(server, &items, document, offset)) {
		lsp_deduplicate_completion_items(&items);
		lsp_completion_attach_items(return_value, &items);
		zend_string_release(prefix);

		return;
	}

	tokens_zv = zend_hash_str_find(Z_ARRVAL(document->lsparrot), "tokens", sizeof("tokens") - 1);
	if (!tokens_zv || Z_TYPE_P(tokens_zv) != IS_ARRAY) {
		lsp_completion_attach_items(return_value, &items);
		zend_string_release(prefix);

		return;
	}

	tokens = Z_ARRVAL_P(tokens_zv);
	count = zend_hash_num_elements(tokens);
	prefix_start = offset >= ZSTR_LEN(prefix) ? offset - ZSTR_LEN(prefix) : 0;
	if (ZSTR_LEN(prefix) > 0 && ZSTR_VAL(prefix)[0] == '$') {
		lsp_add_scope_variable_completions(server, &items, document, tokens, offset, prefix, prefix_start);
		lsp_phpdoc_add_variable_type_completion_edits(server, &items, document->text, prefix, prefix_start, offset);
		lsp_deduplicate_completion_items(&items);
		lsp_completion_attach_items(return_value, &items);
		zend_string_release(prefix);

		return;
	}

	if (lsp_add_override_method_completions(server, &items, document, tokens, offset, prefix)) {
		lsp_deduplicate_completion_items(&items);
		lsp_completion_attach_items(return_value, &items);
		zend_string_release(prefix);

		return;
	}

	if (lsp_is_return_type_completion_context(document->text, offset, prefix)) {
		lsp_add_return_type_builtin_completions(&items, prefix);
		lsp_add_current_document_class_like_completions(&items, document, tokens, prefix);
		lsp_add_project_class_like_completions(server, &items, document, offset, prefix);
		lsp_add_return_type_builtin_class_completions(&items, document, prefix, offset);
		lsp_deduplicate_completion_items(&items);
		lsp_completion_attach_items(return_value, &items);
		zend_string_release(prefix);

		return;
	}

	lsp_add_current_document_constant_completions(&items, document, tokens, prefix);

	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		label = NULL;
		detail = NULL;
		kind = 6;

		if (!token || Z_TYPE_P(token) != IS_ARRAY) {
			continue;
		}

		if (lsp_token_name_equals(token, "T_FUNCTION")) {
			name_token = lsp_next_function_name_token_ex(tokens, i + 1, &name_index);
			label = lsp_token_string(name_token, "text");
			kind = 3;
			if (label) {
				detail = lsp_function_signature_detail(document->text, name_token, tokens, name_index, lsp_brace_depth_at(document->text, (size_t) lsp_token_long(token, "offset", 0)), "function");
				if (!detail) {
					detail = strpprintf(0, "function %s", ZSTR_VAL(label));
				}
			}
		} else if (lsp_token_is_class_like(token)) {
			label = lsp_next_string_token(tokens, i + 1);
			kind = lsp_token_name_equals(token, "T_INTERFACE") ? 11 : (lsp_token_name_equals(token, "T_ENUM") ? 10 : 7);
			if (label) {
				detail = strpprintf(0, "%s %s", lsp_token_name_equals(token, "T_INTERFACE") ? "interface" : (lsp_token_name_equals(token, "T_TRAIT") ? "trait" : (lsp_token_name_equals(token, "T_ENUM") ? "enum" : "class")), ZSTR_VAL(label));
			}
		} else {
			continue;
		}

		if (!label || !detail) {
			continue;
		}

		if (!lsp_matches_prefix_string(label, prefix)) {
			zend_string_release(detail);

			continue;
		}

		lsp_add_completion_item(&items, label, kind, detail);
		zend_string_release(detail);
	}

	lsp_add_project_symbol_completions(server, &items, document, offset, prefix);
	lsp_add_builtin_constant_completions(&items, prefix, document->text, offset);
	lsp_add_builtin_class_completions(&items, prefix, document->text, offset);
	lsp_add_builtin_function_completions(&items, prefix, document->text, offset);
	lsp_phpdoc_add_completions(server, &items, document, offset, prefix);

	lsp_add_keyword_completion(&items, "class", prefix);
	lsp_add_keyword_completion(&items, "function", prefix);
	lsp_add_keyword_completion(&items, "interface", prefix);
	lsp_add_keyword_completion(&items, "trait", prefix);
	lsp_add_keyword_completion(&items, "enum", prefix);
	lsp_add_keyword_completion(&items, "public", prefix);
	lsp_add_keyword_completion(&items, "private", prefix);
	lsp_add_keyword_completion(&items, "protected", prefix);
	lsp_add_keyword_completion(&items, "static", prefix);
	lsp_add_keyword_completion(&items, "readonly", prefix);

	lsp_deduplicate_completion_items(&items);
	lsp_completion_attach_items(return_value, &items);
	zend_string_release(prefix);
}
