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

static inline zend_string *lsp_composer_autoload_file(lsp_server *server, const char *name)
{
	zend_string *vendor_dir, *composer_dir, *path;

	vendor_dir = lsp_composer_vendor_dir(server->root);
	composer_dir = lsp_join_path2(vendor_dir, "composer");
	path = lsp_join_path2(composer_dir, name);
	zend_string_release(composer_dir);
	zend_string_release(vendor_dir);

	return path;
}

static inline zend_string *lsp_fqcn_relative_path(const char *value, size_t length, bool psr0)
{
	zend_string *relative;
	size_t i;

	relative = zend_string_alloc(length + sizeof(".php") - 1, 0);

	for (i = 0; i < length; i++) {
		ZSTR_VAL(relative)[i] = (value[i] == '\\' || (psr0 && value[i] == '_')) ? '/' : value[i];
	}

	memcpy(ZSTR_VAL(relative) + length, ".php", sizeof(".php") - 1);
	ZSTR_VAL(relative)[ZSTR_LEN(relative)] = '\0';

	return relative;
}

static inline zend_string *lsp_autoload_candidate_from_dir(zend_string *dir, zend_string *relative)
{
	zend_string *candidate;
	bool slash;

	slash = lsp_path_has_trailing_separator(dir);
	candidate = strpprintf(0, "%s%s%s", ZSTR_VAL(dir), slash ? "" : "/", ZSTR_VAL(relative));
	if (lsp_is_regular_file(candidate)) {
		return candidate;
	}

	zend_string_release(candidate);

	return NULL;
}

static inline zend_string *lsp_autoload_candidate_from_dirs(zval *dirs_zv, zend_string *relative)
{
	zend_string *candidate;
	zval *dir_zv;

	if (Z_TYPE_P(dirs_zv) == IS_STRING) {
		return lsp_autoload_candidate_from_dir(Z_STR_P(dirs_zv), relative);
	}

	if (Z_TYPE_P(dirs_zv) != IS_ARRAY) {
		return NULL;
	}

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(dirs_zv), dir_zv) {
		if (Z_TYPE_P(dir_zv) != IS_STRING) {
			continue;
		}

		candidate = lsp_autoload_candidate_from_dir(Z_STR_P(dir_zv), relative);
		if (candidate) {
			return candidate;
		}
	} ZEND_HASH_FOREACH_END();

	return NULL;
}

static inline zend_string *lsp_find_autoload_classmap_path(lsp_server *server, zend_string *fqcn)
{
	zend_string *path, *classmap_file;
	zval classmap, *path_zv;

	classmap_file = lsp_composer_autoload_file(server, "autoload_classmap.php");
	if (!lsp_include_array_file(classmap_file, &classmap)) {
		zend_string_release(classmap_file);

		return NULL;
	}

	zend_string_release(classmap_file);

	path = NULL;
	path_zv = zend_hash_find(Z_ARRVAL(classmap), fqcn);
	if (path_zv && Z_TYPE_P(path_zv) == IS_STRING && lsp_is_regular_file(Z_STR_P(path_zv))) {
		path = zend_string_copy(Z_STR_P(path_zv));
	}

	zval_ptr_dtor(&classmap);

	return path;
}

static inline zend_string *lsp_find_autoload_psr4_path(lsp_server *server, zend_string *fqcn)
{
	zend_string *psr4_file, *prefix, *relative, *candidate;
	zval psr4, *dirs_zv;
	size_t prefix_length;

	psr4_file = lsp_composer_autoload_file(server, "autoload_psr4.php");
	if (!lsp_include_array_file(psr4_file, &psr4)) {
		zend_string_release(psr4_file);

		return NULL;
	}
	zend_string_release(psr4_file);

	candidate = NULL;
	ZEND_HASH_FOREACH_STR_KEY_VAL(Z_ARRVAL(psr4), prefix, dirs_zv) {
		if (!prefix || ZSTR_LEN(prefix) > ZSTR_LEN(fqcn) || strncasecmp(ZSTR_VAL(fqcn), ZSTR_VAL(prefix), ZSTR_LEN(prefix)) != 0) {
			continue;
		}

		prefix_length = ZSTR_LEN(prefix);
		relative = lsp_fqcn_relative_path(ZSTR_VAL(fqcn) + prefix_length, ZSTR_LEN(fqcn) - prefix_length, false);
		candidate = lsp_autoload_candidate_from_dirs(dirs_zv, relative);

		zend_string_release(relative);

		if (candidate) {
			break;
		}
	} ZEND_HASH_FOREACH_END();

	zval_ptr_dtor(&psr4);

	return candidate;
}

static inline zend_string *lsp_find_autoload_psr0_path(lsp_server *server, zend_string *fqcn)
{
	zend_string *psr0_file, *prefix, *relative, *candidate;
	zval psr0, *dirs_zv;
	size_t prefix_length;

	psr0_file = lsp_composer_autoload_file(server, "autoload_namespaces.php");
	if (!lsp_include_array_file(psr0_file, &psr0)) {
		zend_string_release(psr0_file);

		return NULL;
	}
	zend_string_release(psr0_file);

	candidate = NULL;
	ZEND_HASH_FOREACH_STR_KEY_VAL(Z_ARRVAL(psr0), prefix, dirs_zv) {
		if (!prefix || ZSTR_LEN(prefix) > ZSTR_LEN(fqcn) || strncasecmp(ZSTR_VAL(fqcn), ZSTR_VAL(prefix), ZSTR_LEN(prefix)) != 0) {
			continue;
		}

		prefix_length = ZSTR_LEN(prefix);
		relative = lsp_fqcn_relative_path(ZSTR_VAL(fqcn) + prefix_length, ZSTR_LEN(fqcn) - prefix_length, true);
		candidate = lsp_autoload_candidate_from_dirs(dirs_zv, relative);
		zend_string_release(relative);
		if (candidate) {
			break;
		}
	} ZEND_HASH_FOREACH_END();

	zval_ptr_dtor(&psr0);

	return candidate;
}

static inline zend_string *lsp_find_autoload_class_like_path(lsp_server *server, zend_string *fqcn)
{
	zend_string *path;

	path = lsp_find_autoload_classmap_path(server, fqcn);
	if (path) {
		return path;
	}

	path = lsp_find_autoload_psr4_path(server, fqcn);
	if (path) {
		return path;
	}

	return lsp_find_autoload_psr0_path(server, fqcn);
}

extern zend_string *lsp_find_project_symbol_path(lsp_server *server, char expected_kind, zend_string *fqcn)
{
	const char *path;
	size_t path_length = 0;
	char found_kind = '\0';

	lsp_index_join_worker(server);

	/* O(1) hash probe over the symbol-key map instead of the historical
	 * linear arena scan. */
	path = lsp_symbol_index_lookup(&server->symbol_index, expected_kind, fqcn, &found_kind, NULL, &path_length);
	if (path && path_length > 0) {
		return zend_string_init(path, path_length, 0);
	}

	if (expected_kind == LSP_SYMBOL_CLASS) {
		return lsp_find_autoload_class_like_path(server, fqcn);
	}

	return NULL;
}
