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

static inline bool lsp_class_member_cache_entry_is_fresh(zval *entry, zend_string *path, lsp_document *document, bool stat_valid, zend_stat_t *st)
{
	zend_long document_version, mtime, mtime_nsec, size;
	zend_string *cached_path;

	if (!entry || Z_TYPE_P(entry) != IS_ARRAY) {
		return false;
	}

	cached_path = lsp_array_string(entry, "path");
	if (!cached_path || !zend_string_equals(cached_path, path)) {
		return false;
	}

	if (document) {
		document_version = lsp_array_long(entry, "documentVersion", -1);

		return document_version == document->version;
	}

	if (!stat_valid) {
		return false;
	}

	mtime = lsp_array_long(entry, "mtime", -1);
	mtime_nsec = lsp_array_long(entry, "mtimeNsec", -1);
	size = lsp_array_long(entry, "size", -1);

	return mtime == (zend_long) st->st_mtime &&
		mtime_nsec == lsp_stat_mtime_nsec(st) &&
		size == (zend_long) st->st_size
	;
}

static inline lsp_method_visibility lsp_cached_method_visibility(HashTable *tokens, uint32_t index, zend_string *text, zend_long body_depth)
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

static inline bool lsp_cached_method_is_static(HashTable *tokens, uint32_t index, zend_string *text, zend_long body_depth)
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

static inline bool lsp_promoted_property_segment_has_visibility(HashTable *tokens, uint32_t index, zend_string *text, zend_long body_depth)
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

		if (lsp_token_is_char(token, ',') || lsp_token_is_char(token, '(')) {
			break;
		}

		if (lsp_token_is_char(token, ';') || lsp_token_is_char(token, '{') || lsp_token_is_char(token, '}')) {
			return false;
		}

		if (lsp_token_name_equals(token, "T_PUBLIC") ||
			lsp_token_name_equals(token, "T_PROTECTED") ||
			lsp_token_name_equals(token, "T_PRIVATE")
		) {
			return true;
		}
	}

	return false;
}

static inline bool lsp_promoted_property_constructor_param_start(HashTable *tokens, uint32_t index, zend_string *text, zend_long body_depth, size_t *param_start)
{
	zend_long i, j;
	zend_string *name;
	zval *token;
	bool saw_construct;

	*param_start = 0;
	for (i = (zend_long) index - 1; i >= 0; i--) {
		token = zend_hash_index_find(tokens, (zend_ulong) i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY) {
			continue;
		}

		if (!lsp_token_at_depth(text, token, body_depth)) {
			continue;
		}

		if (lsp_token_is_char(token, ';') || lsp_token_is_char(token, '{') || lsp_token_is_char(token, '}')) {
			return false;
		}

		if (!lsp_token_is_char(token, '(')) {
			continue;
		}

		*param_start = (size_t) lsp_token_long(token, "offset", 0);
		saw_construct = false;
		for (j = i - 1; j >= 0; j--) {
			token = zend_hash_index_find(tokens, (zend_ulong) j);
			if (!token || Z_TYPE_P(token) != IS_ARRAY) {
				continue;
			}

			if (!lsp_token_at_depth(text, token, body_depth)) {
				continue;
			}

			if (lsp_token_is_char(token, ';') || lsp_token_is_char(token, '{') || lsp_token_is_char(token, '}')) {
				return false;
			}

			if (!saw_construct && lsp_token_name_equals(token, "T_STRING")) {
				name = lsp_token_string(token, "text");
				if (!name || !zend_string_equals_literal(name, "__construct")) {
					return false;
				}
				saw_construct = true;
				continue;
			}

			if (saw_construct && lsp_token_name_equals(token, "T_FUNCTION")) {
				return true;
			}
		}

		return false;
	}

	return false;
}

static inline bool lsp_token_is_promoted_property_declaration(HashTable *tokens, uint32_t index, zend_string *text, zend_long body_depth, size_t *param_start)
{
	if (!lsp_promoted_property_segment_has_visibility(tokens, index, text, body_depth)) {
		*param_start = 0;

		return false;
	}

	return lsp_promoted_property_constructor_param_start(tokens, index, text, body_depth, param_start);
}

static inline void lsp_cache_class_method(zval *methods, zend_string *label, zend_string *detail, bool is_static, lsp_method_visibility visibility)
{
	zval method;

	array_init(&method);
	add_assoc_str(&method, "label", zend_string_copy(label));
	add_assoc_str(&method, "detail", zend_string_copy(detail));
	add_assoc_bool(&method, "static", is_static);
	add_assoc_long(&method, "visibility", (zend_long) visibility);
	add_next_index_zval(methods, &method);
}

static inline void lsp_cache_class_property(zval *properties, zend_string *label, zend_string *detail, bool is_static, lsp_method_visibility visibility)
{
	zval property;

	array_init(&property);
	add_assoc_str(&property, "label", zend_string_copy(label));
	add_assoc_str(&property, "detail", zend_string_copy(detail));
	add_assoc_bool(&property, "static", is_static);
	add_assoc_long(&property, "visibility", (zend_long) visibility);
	add_next_index_zval(properties, &property);
}

static inline void lsp_add_cached_class_member_completions_ex(lsp_server *server, zval *items, zval *entry, zend_string *member_prefix, bool public_only)
{
	const char *source = lsp_primary_analyzer_source(server);
	zend_long visibility;
	zend_string *label, *detail;
	zval *methods, *method, *properties, *property, *static_value, *visibility_value;
	bool property_static;

	methods = zend_hash_str_find(Z_ARRVAL_P(entry), "methods", sizeof("methods") - 1);
	if (methods && Z_TYPE_P(methods) == IS_ARRAY) {
		ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(methods), method) {
			if (Z_TYPE_P(method) != IS_ARRAY) {
				continue;
			}

			label = lsp_array_string(method, "label");
			detail = lsp_array_string(method, "detail");
			visibility_value = zend_hash_str_find(Z_ARRVAL_P(method), "visibility", sizeof("visibility") - 1);
			visibility = visibility_value && Z_TYPE_P(visibility_value) == IS_LONG ? Z_LVAL_P(visibility_value) : (zend_long) LSP_METHOD_VISIBILITY_PUBLIC;

			if (!label ||
				!detail ||
				(public_only && visibility != (zend_long) LSP_METHOD_VISIBILITY_PUBLIC) ||
				!lsp_matches_prefix_string(label, member_prefix)
			) {
				continue;
			}

			lsp_add_completion_item_ex(items, label, 2, detail, source);
		} ZEND_HASH_FOREACH_END();
	}

	properties = zend_hash_str_find(Z_ARRVAL_P(entry), "properties", sizeof("properties") - 1);
	if (!properties || Z_TYPE_P(properties) != IS_ARRAY) {
		return;
	}

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(properties), property) {
		if (Z_TYPE_P(property) != IS_ARRAY) {
			continue;
		}

		static_value = zend_hash_str_find(Z_ARRVAL_P(property), "static", sizeof("static") - 1);
		visibility_value = zend_hash_str_find(Z_ARRVAL_P(property), "visibility", sizeof("visibility") - 1);
		property_static = static_value && zend_is_true(static_value);
		visibility = visibility_value && Z_TYPE_P(visibility_value) == IS_LONG ? Z_LVAL_P(visibility_value) : (zend_long) LSP_METHOD_VISIBILITY_PUBLIC;
		if (property_static || (public_only && visibility != (zend_long) LSP_METHOD_VISIBILITY_PUBLIC)) {
			continue;
		}

		label = lsp_array_string(property, "label");
		detail = lsp_array_string(property, "detail");

		if (!label || !detail || !lsp_matches_prefix_string(label, member_prefix)) {
			continue;
		}

		lsp_add_completion_item_ex(items, label, 10, detail, source);
	} ZEND_HASH_FOREACH_END();
}

static inline void lsp_add_cached_class_member_completions(lsp_server *server, zval *items, zval *entry, zend_string *member_prefix)
{
	lsp_add_cached_class_member_completions_ex(server, items, entry, member_prefix, false);
}

static inline void lsp_add_entry_trait_member_completions(lsp_server *server, zval *items, zval *entry, zend_string *member_prefix, HashTable *visited, bool public_only)
{
	zend_string *trait_name;
	zval *traits, *trait_zv, *trait_entry;

	traits = zend_hash_str_find(Z_ARRVAL_P(entry), "traits", sizeof("traits") - 1);
	if (!traits || Z_TYPE_P(traits) != IS_ARRAY) {
		return;
	}

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(traits), trait_zv) {
		if (Z_TYPE_P(trait_zv) != IS_STRING) {
			continue;
		}

		trait_name = Z_STR_P(trait_zv);
		if (zend_hash_exists(visited, trait_name)) {
			continue;
		}

		zend_hash_add_empty_element(visited, trait_name);

		trait_entry = lsp_class_member_cache_entry(server, trait_name);
		if (!trait_entry || Z_TYPE_P(trait_entry) != IS_ARRAY) {
			continue;
		}

		lsp_add_cached_class_member_completions_ex(server, items, trait_entry, member_prefix, public_only);
		lsp_add_entry_trait_member_completions(server, items, trait_entry, member_prefix, visited, public_only);
	} ZEND_HASH_FOREACH_END();
}

static inline void lsp_add_cached_static_class_member_completions_ex(lsp_server *server, zval *items, zval *entry, zend_string *member_prefix, bool public_only)
{
	const char *source = lsp_primary_analyzer_source(server);
	zend_long visibility;
	zend_string *label, *detail, *static_label;
	zval *methods, *method, *static_value, *properties, *property;
	bool property_static;

	methods = zend_hash_str_find(Z_ARRVAL_P(entry), "methods", sizeof("methods") - 1);
	if (methods && Z_TYPE_P(methods) == IS_ARRAY) {
		ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(methods), method) {
			if (Z_TYPE_P(method) != IS_ARRAY) {
				continue;
			}

			static_value = zend_hash_str_find(Z_ARRVAL_P(method), "static", sizeof("static") - 1);
			visibility = lsp_array_long(method, "visibility", (zend_long) LSP_METHOD_VISIBILITY_PUBLIC);
			if (!static_value ||
				!zend_is_true(static_value) ||
				visibility == (zend_long) LSP_METHOD_VISIBILITY_PRIVATE ||
				(public_only && visibility != (zend_long) LSP_METHOD_VISIBILITY_PUBLIC)
			) {
				continue;
			}

			label = lsp_array_string(method, "label");
			detail = lsp_array_string(method, "detail");
			if (!label || !detail || !lsp_matches_prefix_string(label, member_prefix)) {
				continue;
			}

			lsp_add_completion_item_ex(items, label, 2, detail, source);
		} ZEND_HASH_FOREACH_END();
	}

	properties = zend_hash_str_find(Z_ARRVAL_P(entry), "properties", sizeof("properties") - 1);
	if (!properties || Z_TYPE_P(properties) != IS_ARRAY) {
		return;
	}

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(properties), property) {
		if (Z_TYPE_P(property) != IS_ARRAY) {
			continue;
		}

		static_value = zend_hash_str_find(Z_ARRVAL_P(property), "static", sizeof("static") - 1);
		visibility = lsp_array_long(property, "visibility", (zend_long) LSP_METHOD_VISIBILITY_PUBLIC);
		property_static = static_value && zend_is_true(static_value);
		if (!property_static ||
			visibility == (zend_long) LSP_METHOD_VISIBILITY_PRIVATE ||
			(public_only && visibility != (zend_long) LSP_METHOD_VISIBILITY_PUBLIC)
		) {
			continue;
		}

		label = lsp_array_string(property, "label");
		detail = lsp_array_string(property, "detail");
		if (!label || !detail) {
			continue;
		}

		static_label = strpprintf(0, "$%s", ZSTR_VAL(label));
		if (lsp_matches_prefix_string(static_label, member_prefix)) {
			lsp_add_completion_item_ex(items, static_label, 10, detail, source);
		}

		zend_string_release(static_label);
	} ZEND_HASH_FOREACH_END();
}

static inline void lsp_add_cached_static_class_member_completions(lsp_server *server, zval *items, zval *entry, zend_string *member_prefix)
{
	lsp_add_cached_static_class_member_completions_ex(server, items, entry, member_prefix, true);
}

static inline void lsp_add_entry_trait_static_member_completions(lsp_server *server, zval *items, zval *entry, zend_string *member_prefix, HashTable *visited, bool public_only)
{
	zend_string *trait_name;
	zval *traits, *trait_zv, *trait_entry;

	traits = zend_hash_str_find(Z_ARRVAL_P(entry), "traits", sizeof("traits") - 1);
	if (!traits || Z_TYPE_P(traits) != IS_ARRAY) {
		return;
	}

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(traits), trait_zv) {
		if (Z_TYPE_P(trait_zv) != IS_STRING) {
			continue;
		}

		trait_name = Z_STR_P(trait_zv);
		if (zend_hash_exists(visited, trait_name)) {
			continue;
		}

		zend_hash_add_empty_element(visited, trait_name);

		trait_entry = lsp_class_member_cache_entry(server, trait_name);
		if (!trait_entry || Z_TYPE_P(trait_entry) != IS_ARRAY) {
			continue;
		}

		lsp_add_cached_static_class_member_completions_ex(server, items, trait_entry, member_prefix, public_only);
		lsp_add_entry_trait_static_member_completions(server, items, trait_entry, member_prefix, visited, public_only);
	} ZEND_HASH_FOREACH_END();
}

static inline void lsp_add_reflection_method_completions(lsp_server *server, zval *items, zend_string *text, size_t offset, zend_string *prefix)
{
	const char *source = lsp_primary_analyzer_source(server);
	zend_string *receiver, *member_prefix, *class_name, *method_name, *detail;
	zend_class_entry *ce;
	zend_function *function;

	if (!server->phpstan_enabled && !server->psalm_enabled && !server->psalm_ls_enabled) {
		return;
	}

	if (!lsp_member_access_context(text, offset, prefix, &receiver, &member_prefix)) {
		return;
	}

	class_name = lsp_infer_new_assignment_class(text, receiver, offset);
	if (!class_name) {
		zend_string_release(receiver);
		zend_string_release(member_prefix);

		return;
	}

	ce = zend_lookup_class(class_name);
	if (ce) {
		ZEND_HASH_FOREACH_STR_KEY_PTR(&ce->function_table, method_name, function) {
			if (!method_name || (function->common.fn_flags & ZEND_ACC_PUBLIC) == 0) {
				continue;
			}

			if (!lsp_matches_prefix_string(method_name, member_prefix)) {
				continue;
			}

			detail = strpprintf(0, "%s(...)", ZSTR_VAL(method_name));
			lsp_add_completion_item_ex(items, method_name, 2, detail, source);
			zend_string_release(detail);
		} ZEND_HASH_FOREACH_END();
	}

	zend_string_release(class_name);
	zend_string_release(receiver);
	zend_string_release(member_prefix);
}

static inline zend_class_entry *lsp_lookup_runtime_class(zend_string *class_name)
{
	zend_class_entry *ce;
	zend_string *lookup_name;

	if (ZSTR_LEN(class_name) == 0) {
		return NULL;
	}

	if (ZSTR_VAL(class_name)[0] == '\\') {
		if (ZSTR_LEN(class_name) == 1) {
			return NULL;
		}
		lookup_name = zend_string_init(ZSTR_VAL(class_name) + 1, ZSTR_LEN(class_name) - 1, 0);
	} else {
		lookup_name = zend_string_copy(class_name);
	}

	ce = zend_lookup_class(lookup_name);
	zend_string_release(lookup_name);

	return ce;
}

static inline void lsp_add_reflection_class_method_completions(lsp_server *server, zval *items, zend_class_entry *ce, zend_string *member_prefix, bool public_only, bool static_only, bool include_constructor)
{
	const char *source = lsp_primary_analyzer_source(server);
	zend_function *function;
	zend_string *method_name, *label, *detail;
	uint32_t flags;

	ZEND_HASH_FOREACH_STR_KEY_PTR(&ce->function_table, method_name, function) {
		if (!function) {
			continue;
		}

		flags = function->common.fn_flags;
		if ((flags & ZEND_ACC_PRIVATE) != 0 ||
			(public_only && (flags & ZEND_ACC_PUBLIC) == 0) ||
			(static_only && (flags & ZEND_ACC_STATIC) == 0)
		) {
			continue;
		}

		label = function->common.function_name ? function->common.function_name : method_name;
		if (!label) {
			continue;
		}

		if (!include_constructor && zend_string_equals_literal(label, "__construct")) {
			continue;
		}

		if (!lsp_matches_prefix_string(label, member_prefix)) {
			continue;
		}

		detail = strpprintf(0, "%s(...)", ZSTR_VAL(label));
		lsp_add_completion_item_ex(items, label, 2, detail, source);
		zend_string_release(detail);
	} ZEND_HASH_FOREACH_END();
}

extern void lsp_add_parent_reflection_class_member_completions(lsp_server *server, zval *items, zend_string *class_name, zend_string *member_prefix)
{
	zend_class_entry *ce;

	ce = lsp_lookup_runtime_class(class_name);
	if (!ce || ce->type != ZEND_INTERNAL_CLASS) {
		return;
	}

	lsp_add_reflection_class_method_completions(server, items, ce, member_prefix, false, false, true);
}

extern void lsp_add_inherited_public_project_class_member_completions(lsp_server *server, zval *items, zend_string *class_name, zend_string *member_prefix)
{
	zend_string *current, *next;
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

		lsp_add_cached_class_member_completions_ex(server, items, entry, member_prefix, true);
		parent = zend_hash_str_find(Z_ARRVAL_P(entry), "parent", sizeof("parent") - 1);
		next = parent && Z_TYPE_P(parent) == IS_STRING && Z_STRLEN_P(parent) > 0 ? zend_string_copy(Z_STR_P(parent)) : NULL;
		lsp_add_entry_trait_member_completions(server, items, entry, member_prefix, &visited, true);
		zend_string_release(current);
		current = next;
	}

	if (current) {
		zend_string_release(current);
	}

	zend_hash_destroy(&visited);
}

extern void lsp_member_cache_invalidate_path(lsp_server *server, zend_string *path)
{
	zend_string *class_name, *entry_path, **doomed;
	zval *entry;
	uint32_t doomed_count = 0, doomed_capacity = 8, i;

	if (!path || zend_hash_num_elements(&server->member_cache) == 0) {
		return;
	}

	doomed = safe_emalloc(doomed_capacity, sizeof(zend_string *), 0);

	ZEND_HASH_FOREACH_STR_KEY_VAL(&server->member_cache, class_name, entry) {
		if (!class_name || Z_TYPE_P(entry) != IS_ARRAY) {
			continue;
		}

		entry_path = lsp_array_string(entry, "path");
		if (!entry_path || !zend_string_equals(entry_path, path)) {
			continue;
		}

		if (doomed_count >= doomed_capacity) {
			doomed_capacity *= 2;
			doomed = safe_erealloc(doomed, doomed_capacity, sizeof(zend_string *), 0);
		}

		doomed[doomed_count++] = zend_string_copy(class_name);
	} ZEND_HASH_FOREACH_END();

	for (i = 0; i < doomed_count; i++) {
		zend_hash_del(&server->member_cache, doomed[i]);
		zend_string_release(doomed[i]);
	}

	efree(doomed);
}

extern zval *lsp_class_member_cache_entry(lsp_server *server, zend_string *class_name)
{
	lsp_document *document;
	lsp_method_visibility visibility;
	zend_long body_depth = 0;
	zend_string *path, *contents, *label, *detail, *parent_class = NULL, *variable;
	zend_stat_t st;
	zval entry, methods, properties, traits, tokens_zv, *cached, *token, *name_token;
	HashTable *tokens;
	uint32_t i, count, name_index;
	size_t class_start = 0, body_start = 0, body_end = 0, promoted_param_start = 0;
	bool stat_valid, owns_contents, method_static, property_static, promoted_property;

	path = lsp_find_project_symbol_path(server, LSP_SYMBOL_CLASS, class_name);
	if (!path) {
		return NULL;
	}

	document = lsp_document_for_path(server, path);
	stat_valid = document ? false : VCWD_STAT(ZSTR_VAL(path), &st) == 0;

	cached = zend_hash_find(&server->member_cache, class_name);
	if (lsp_class_member_cache_entry_is_fresh(cached, path, document, stat_valid, &st)) {
		zend_string_release(path);

		return cached;
	}

	if (cached) {
		zend_hash_del(&server->member_cache, class_name);
	}

	array_init(&entry);
	array_init(&methods);
	array_init(&properties);
	array_init(&traits);
	ZVAL_UNDEF(&tokens_zv);

	contents = document ? zend_string_copy(document->text) : lsp_read_file(path);
	owns_contents = document || contents != zend_empty_string;
	if (owns_contents) {
		lsp_lsparrot_tokens_to_zval(&tokens_zv, contents);

		if (Z_TYPE(tokens_zv) == IS_ARRAY && lsp_find_class_header_for_name(contents, class_name, &class_start, &body_start, &body_end, &body_depth)) {
			lsp_phpdoc_cache_class_methods(&methods, contents, class_start);
			lsp_phpdoc_cache_class_properties(&properties, contents, class_start);
			tokens = Z_ARRVAL(tokens_zv);
			count = zend_hash_num_elements(tokens);

			for (i = 0; i < count; i++) {
				token = zend_hash_index_find(tokens, i);
				if (!token || Z_TYPE_P(token) != IS_ARRAY || !lsp_token_in_bounds(token, body_start, body_end)) {
					continue;
				}

				if (!lsp_token_name_equals(token, "T_FUNCTION") || !lsp_token_at_depth(contents, token, body_depth)) {
					promoted_property = false;
					promoted_param_start = 0;
					if (lsp_token_name_equals(token, "T_VARIABLE") && lsp_token_at_depth(contents, token, body_depth)) {
						if (!lsp_token_is_property_declaration(tokens, i, contents, body_depth)) {
							promoted_property = lsp_token_is_promoted_property_declaration(tokens, i, contents, body_depth, &promoted_param_start);
							if (!promoted_property) {
								continue;
							}
						}

						visibility = lsp_cached_method_visibility(tokens, i, contents, body_depth);
						if (visibility == LSP_METHOD_VISIBILITY_PRIVATE) {
							continue;
						}
						property_static = promoted_property ? false : lsp_cached_method_is_static(tokens, i, contents, body_depth);
						variable = lsp_token_string(token, "text");
						if (!variable || ZSTR_LEN(variable) <= 1) {
							continue;
						}

						label = zend_string_init(ZSTR_VAL(variable) + 1, ZSTR_LEN(variable) - 1, 0);
						detail = promoted_property
							? lsp_promoted_property_completion_detail(contents, token, promoted_param_start)
							: lsp_property_completion_detail(contents, token, property_static)
						;
						if (!detail) {
							continue;
						}
						lsp_cache_class_property(&properties, label, detail, property_static, visibility);
						zend_string_release(detail);
						zend_string_release(label);
					}
					continue;
				}

				visibility = lsp_cached_method_visibility(tokens, i, contents, body_depth);
				if (visibility == LSP_METHOD_VISIBILITY_PRIVATE) {
					continue;
				}

				method_static = lsp_cached_method_is_static(tokens, i, contents, body_depth);
				name_token = lsp_next_function_name_token_ex(tokens, i + 1, &name_index);

				label = lsp_token_string(name_token, "text");
				if (!label || zend_string_equals_literal(label, "__construct")) {
					continue;
				}

				detail = lsp_function_signature_detail(contents, name_token, tokens, name_index, body_depth, NULL);
				if (!detail) {
					detail = strpprintf(0, "%s(...)", ZSTR_VAL(label));
				}

				lsp_cache_class_method(&methods, label, detail, method_static, visibility);
				zend_string_release(detail);
			}

			parent_class = lsp_class_extends_name(contents, class_start, body_start);

			zval_ptr_dtor(&traits);
			lsp_collect_class_trait_names(contents, &traits);
		}

		if (!Z_ISUNDEF(tokens_zv)) {
			zval_ptr_dtor(&tokens_zv);
		}

		if (contents != zend_empty_string || document) {
			zend_string_release(contents);
		}
	}

	add_assoc_str(&entry, "path", zend_string_copy(path));
	add_assoc_long(&entry, "documentVersion", document ? document->version : -1);
	add_assoc_long(&entry, "mtime", stat_valid ? (zend_long) st.st_mtime : -1);
	add_assoc_long(&entry, "mtimeNsec", stat_valid ? lsp_stat_mtime_nsec(&st) : -1);
	add_assoc_long(&entry, "size", stat_valid ? (zend_long) st.st_size : -1);
	add_assoc_zval(&entry, "methods", &methods);
	add_assoc_zval(&entry, "properties", &properties);
	add_assoc_zval(&entry, "traits", &traits);

	if (parent_class) {
		add_assoc_str(&entry, "parent", parent_class);
	} else {
		add_assoc_null(&entry, "parent");
	}

	cached = zend_hash_update(&server->member_cache, class_name, &entry);
	zend_string_release(path);

	return cached;
}

extern void lsp_add_inherited_project_class_member_completions(lsp_server *server, zval *items, zend_string *class_name, zend_string *member_prefix)
{
	zend_string *current, *next;
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

		lsp_add_cached_class_member_completions(server, items, entry, member_prefix);
		parent = zend_hash_str_find(Z_ARRVAL_P(entry), "parent", sizeof("parent") - 1);
		next = parent && Z_TYPE_P(parent) == IS_STRING && Z_STRLEN_P(parent) > 0 ? zend_string_copy(Z_STR_P(parent)) : NULL;
		lsp_add_entry_trait_member_completions(server, items, entry, member_prefix, &visited, false);
		zend_string_release(current);
		current = next;
	}

	if (current) {
		zend_string_release(current);
	}

	zend_hash_destroy(&visited);
}

static inline void lsp_add_static_project_class_member_completions_ex(lsp_server *server, zval *items, zend_string *class_name, zend_string *member_prefix, bool public_only)
{
	zend_string *current, *next;
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

		lsp_add_cached_static_class_member_completions_ex(server, items, entry, member_prefix, public_only);
		parent = zend_hash_str_find(Z_ARRVAL_P(entry), "parent", sizeof("parent") - 1);
		next = parent && Z_TYPE_P(parent) == IS_STRING && Z_STRLEN_P(parent) > 0 ? zend_string_copy(Z_STR_P(parent)) : NULL;
		lsp_add_entry_trait_static_member_completions(server, items, entry, member_prefix, &visited, public_only);
		zend_string_release(current);
		current = next;
	}

	if (current) {
		zend_string_release(current);
	}

	zend_hash_destroy(&visited);
}

extern void lsp_add_static_project_class_member_completions(lsp_server *server, zval *items, zend_string *class_name, zend_string *member_prefix)
{
	lsp_add_static_project_class_member_completions_ex(server, items, class_name, member_prefix, true);
}

extern void lsp_add_inherited_static_project_class_member_completions(lsp_server *server, zval *items, zend_string *class_name, zend_string *member_prefix)
{
	lsp_add_static_project_class_member_completions_ex(server, items, class_name, member_prefix, false);
}

extern void lsp_add_current_class_phpdoc_member_completions(lsp_server *server, zval *items, lsp_document *document, size_t class_start, zend_string *member_prefix, bool static_access)
{
	zval entry, methods, properties;

	array_init(&entry);
	array_init(&methods);
	array_init(&properties);
	lsp_phpdoc_cache_class_methods(&methods, document->text, class_start);
	lsp_phpdoc_cache_class_properties(&properties, document->text, class_start);
	add_assoc_zval(&entry, "methods", &methods);
	add_assoc_zval(&entry, "properties", &properties);

	if (static_access) {
		lsp_add_cached_static_class_member_completions(server, items, &entry, member_prefix);
	} else {
		lsp_add_cached_class_member_completions(server, items, &entry, member_prefix);
	}

	zval_ptr_dtor(&entry);
}

extern void lsp_add_current_static_member_completions(zval *items, lsp_document *document, size_t body_start, size_t body_end, zend_long body_depth, zend_string *member_prefix)
{
	zend_string *label, *variable, *detail;
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
		label = NULL;
		detail = NULL;

		if (!token || Z_TYPE_P(token) != IS_ARRAY || !lsp_token_in_bounds(token, body_start, body_end)) {
			continue;
		}

		if (lsp_token_name_equals(token, "T_FUNCTION") &&
			lsp_token_at_depth(document->text, token, body_depth) &&
			lsp_cached_method_is_static(tokens, i, document->text, body_depth)
		) {
			name_token = lsp_next_function_name_token_ex(tokens, i + 1, &name_index);
			label = lsp_token_string(name_token, "text");
			if (!label || zend_string_equals_literal(label, "__construct") || !lsp_matches_prefix_string(label, member_prefix)) {
				continue;
			}

			detail = lsp_function_signature_detail(document->text, name_token, tokens, name_index, body_depth, NULL);
			if (!detail) {
				detail = strpprintf(0, "%s(...)", ZSTR_VAL(label));
			}

			lsp_add_completion_item_ex(items, label, 2, detail, "lsparrot");
			zend_string_release(detail);

			continue;
		}

		if (lsp_token_name_equals(token, "T_VARIABLE") &&
			lsp_token_at_depth(document->text, token, body_depth) &&
			lsp_token_is_property_declaration(tokens, i, document->text, body_depth) &&
			lsp_cached_method_is_static(tokens, i, document->text, body_depth)
		) {
			variable = lsp_token_string(token, "text");
			if (!variable || ZSTR_LEN(variable) <= 1 || !lsp_matches_prefix_string(variable, member_prefix)) {
				continue;
			}

			detail = lsp_property_completion_detail(document->text, token, true);
			if (!detail) {
				continue;
			}
			lsp_add_completion_item_ex(items, variable, 10, detail, "lsparrot");
			zend_string_release(detail);
		}
	}
}

extern void lsp_add_inferred_member_completions(lsp_server *server, zval *items, lsp_document *document, size_t offset, zend_string *prefix)
{
	const char *source = lsp_primary_analyzer_source(server);
	zend_class_entry *ce;
	zend_function *function;
	zend_string *member_prefix, *class_name, *method_name, *detail;

	if (!lsp_member_access_class_context(server, document, offset, prefix, &class_name, &member_prefix)) {
		return;
	}

	ce = zend_lookup_class(class_name);
	if (ce) {
		ZEND_HASH_FOREACH_STR_KEY_PTR(&ce->function_table, method_name, function) {
			if (!method_name || (function->common.fn_flags & ZEND_ACC_PUBLIC) == 0) {
				continue;
			}

			if (zend_string_equals_literal(method_name, "__construct")) {
				continue;
			}

			if (!lsp_matches_prefix_string(method_name, member_prefix)) {
				continue;
			}

			detail = strpprintf(0, "%s(...)", ZSTR_VAL(method_name));

			lsp_add_completion_item_ex(items, method_name, 2, detail, source);

			zend_string_release(detail);
		} ZEND_HASH_FOREACH_END();
	}

	lsp_add_inherited_public_project_class_member_completions(server, items, class_name, member_prefix);

	zend_string_release(class_name);
	zend_string_release(member_prefix);
}

extern void lsp_add_this_member_completions(lsp_server *server, zval *items, lsp_document *document, size_t offset, zend_string *prefix)
{
	zend_long body_depth = 0;
	zend_string *receiver, *member_prefix, *label, *variable, *detail, *parent_class;
	zval *tokens_zv, *token, *name_token;
	HashTable *tokens;
	uint32_t i, count, name_index;
	size_t class_start = 0, body_start = 0, body_end = 0, promoted_param_start = 0;
	bool promoted_property;

	if (!lsp_member_access_context(document->text, offset, prefix, &receiver, &member_prefix)) {
		return;
	}

	if (!zend_string_equals_literal(receiver, "$this")) {
		zend_string_release(receiver);
		zend_string_release(member_prefix);

		return;
	}

	zend_string_release(receiver);

	if (!lsp_find_enclosing_class_header(document->text, offset, &class_start, &body_start, &body_end, &body_depth)) {
		zend_string_release(member_prefix);

		return;
	}

	tokens_zv = zend_hash_str_find(Z_ARRVAL(document->lsparrot), "tokens", sizeof("tokens") - 1);
	if (!tokens_zv || Z_TYPE_P(tokens_zv) != IS_ARRAY) {
		zend_string_release(member_prefix);

		return;
	}

	tokens = Z_ARRVAL_P(tokens_zv);
	count = zend_hash_num_elements(tokens);
	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		label = NULL;
		detail = NULL;

		if (!token || Z_TYPE_P(token) != IS_ARRAY || !lsp_token_in_bounds(token, body_start, body_end)) {
			continue;
		}

		if (lsp_token_name_equals(token, "T_FUNCTION") && lsp_token_at_depth(document->text, token, body_depth)) {
			name_token = lsp_next_function_name_token_ex(tokens, i + 1, &name_index);
			label = lsp_token_string(name_token, "text");
			if (!label || !lsp_matches_prefix_string(label, member_prefix)) {
				continue;
			}

			detail = lsp_function_signature_detail(document->text, name_token, tokens, name_index, body_depth, NULL);
			if (!detail) {
				detail = strpprintf(0, "%s(...)", ZSTR_VAL(label));
			}

			lsp_add_completion_item_ex(items, label, 2, detail, "lsparrot");
			zend_string_release(detail);
			continue;
		}

		promoted_property = false;
		promoted_param_start = 0;
		if (lsp_token_name_equals(token, "T_VARIABLE") && lsp_token_at_depth(document->text, token, body_depth)) {
			if (!lsp_token_is_property_declaration(tokens, i, document->text, body_depth)) {
				promoted_property = lsp_token_is_promoted_property_declaration(tokens, i, document->text, body_depth, &promoted_param_start);
				if (!promoted_property) {
					continue;
				}
			}

			variable = lsp_token_string(token, "text");
			if (!variable || ZSTR_LEN(variable) <= 1) {
				continue;
			}

			label = zend_string_init(ZSTR_VAL(variable) + 1, ZSTR_LEN(variable) - 1, 0);
			if (!lsp_matches_prefix_string(label, member_prefix)) {
				zend_string_release(label);
				continue;
			}

			detail = promoted_property
				? lsp_promoted_property_completion_detail(document->text, token, promoted_param_start)
				: lsp_property_completion_detail(document->text, token, false)
			;
			if (!detail) {
				zend_string_release(label);
				continue;
			}
			lsp_add_completion_item_ex(items, label, 10, detail, "lsparrot");
			zend_string_release(detail);
			zend_string_release(label);
		}
	}

	lsp_add_current_class_phpdoc_member_completions(server, items, document, class_start, member_prefix, false);
	parent_class = lsp_class_extends_name(document->text, class_start, body_start);
	if (parent_class) {
		lsp_add_inherited_project_class_member_completions(server, items, parent_class, member_prefix);
		zend_string_release(parent_class);
	}

	zend_string_release(member_prefix);
}
