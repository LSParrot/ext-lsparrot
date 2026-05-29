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

static inline zend_string *lsp_analyzer_config_file_named(zend_string *root, const char **names, uint32_t count)
{
	zend_string *path;
	uint32_t i;

	for (i = 0; i < count; i++) {
		path = lsp_join_path2(root, names[i]);
		if (lsp_is_regular_file(path)) {
			return path;
		}

		zend_string_release(path);
	}

	return NULL;
}

static inline void lsp_append_neon_single_quoted(smart_str *target, zend_string *value)
{
	const char *cursor, *end, *quote;

	cursor = ZSTR_VAL(value);
	end = cursor + ZSTR_LEN(value);
	smart_str_appendc(target, '\'');
	while (cursor < end) {
		quote = memchr(cursor, '\'', end - cursor);
		if (!quote) {
			smart_str_appendl(target, cursor, end - cursor);

			break;
		}

		smart_str_appendl(target, cursor, quote - cursor);
		smart_str_appendl(target, "''", sizeof("''") - 1);
		cursor = quote + 1;
	}

	smart_str_appendc(target, '\'');
}

static inline zend_long lsp_project_vscode_config_long(zend_string *project_root, const char *key, zend_long fallback)
{
	zend_string *path, *contents;
	zval decoded, *value;
	zend_long result;

	result = fallback;
	path = strpprintf(0, "%s/.lsparrot/vscode_config.json", ZSTR_VAL(project_root));
	if (!lsp_is_regular_file(path)) {
		zend_string_release(path);

		return fallback;
	}

	contents = lsp_read_file(path);
	zend_string_release(path);
	if (contents == zend_empty_string) {
		return fallback;
	}

	ZVAL_UNDEF(&decoded);
	php_json_decode_ex(&decoded, ZSTR_VAL(contents), ZSTR_LEN(contents), PHP_JSON_OBJECT_AS_ARRAY, 512);
	zend_string_release(contents);
	if (Z_TYPE(decoded) != IS_ARRAY) {
		if (!Z_ISUNDEF(decoded)) {
			zval_ptr_dtor(&decoded);
		}

		return fallback;
	}

	value = zend_hash_str_find(Z_ARRVAL(decoded), key, strlen(key));
	if (value && Z_TYPE_P(value) == IS_LONG && Z_LVAL_P(value) >= 0) {
		result = Z_LVAL_P(value);
	} else if (value && Z_TYPE_P(value) == IS_DOUBLE && Z_DVAL_P(value) >= 0.0) {
		result = (zend_long) Z_DVAL_P(value);
	}

	zval_ptr_dtor(&decoded);

	return result;
}

static inline const char *lsp_bounded_memstr(const char *haystack, const char *needle, size_t needle_length, const char *end)
{
	const char *cursor, *limit;

	if (needle_length == 0) {
		return haystack;
	}
	if (end < haystack || (size_t) (end - haystack) < needle_length) {
		return NULL;
	}

	limit = end - needle_length + 1;
	for (cursor = haystack; cursor < limit; cursor++) {
		if (*cursor == *needle && memcmp(cursor, needle, needle_length) == 0) {
			return cursor;
		}
	}

	return NULL;
}

static inline const char *lsp_xml_start_tag_end(const char *start, const char *end)
{
	const char *cursor;
	char quote;

	quote = '\0';
	for (cursor = start; cursor < end; cursor++) {
		if (quote != '\0') {
			if (*cursor == quote) {
				quote = '\0';
			}
			continue;
		}

		if (*cursor == '\'' || *cursor == '"') {
			quote = *cursor;
			continue;
		}

		if (*cursor == '>') {
			return cursor;
		}
	}

	return NULL;
}

static inline void lsp_append_xml_escaped(smart_str *target, const char *value, size_t value_length)
{
	size_t i;
	char c;

	for (i = 0; i < value_length; i++) {
		c = value[i];
		if (c == '&') {
			smart_str_appendl(target, "&amp;", sizeof("&amp;") - 1);
		} else if (c == '"') {
			smart_str_appendl(target, "&quot;", sizeof("&quot;") - 1);
		} else if (c == '\'') {
			smart_str_appendl(target, "&apos;", sizeof("&apos;") - 1);
		} else if (c == '<') {
			smart_str_appendl(target, "&lt;", sizeof("&lt;") - 1);
		} else if (c == '>') {
			smart_str_appendl(target, "&gt;", sizeof("&gt;") - 1);
		} else {
			smart_str_appendc(target, c);
		}
	}
}

static inline void lsp_append_xml_attribute(smart_str *target, const char *name, size_t name_length, const char *value, size_t value_length)
{
	smart_str_appendc(target, ' ');
	smart_str_appendl(target, name, name_length);
	smart_str_appendl(target, "=\"", sizeof("=\"") - 1);
	lsp_append_xml_escaped(target, value, value_length);
	smart_str_appendc(target, '"');
}

static inline bool lsp_xml_attribute_name_matches(const char *candidate, const char *tag_start, const char *tag_end, const char *name, size_t name_length)
{
	const char *after;

	if (candidate <= tag_start || !isspace((unsigned char) candidate[-1])) {
		return false;
	}

	after = candidate + name_length;
	while (after < tag_end && isspace((unsigned char) *after)) {
		after++;
	}

	return after < tag_end && *after == '=' && memcmp(candidate, name, name_length) == 0;
}

static inline zend_string *lsp_psalm_config_upsert_attribute(zend_string *contents, const char *name, const char *value, size_t value_length)
{
	const char *contents_start, *contents_end, *tag_start, *tag_end, *cursor, *attr, *attr_start,
		*attr_value_start, *attr_value_end, *eq, *insert_at
	;
	smart_str edited = {0};
	size_t name_length;
	bool found;
	char quote;

	found = false;
	contents_start = ZSTR_VAL(contents);
	contents_end = contents_start + ZSTR_LEN(contents);
	name_length = strlen(name);
	tag_start = lsp_bounded_memstr(contents_start, "<psalm", sizeof("<psalm") - 1, contents_end);
	if (!tag_start) {
		return NULL;
	}
	tag_end = lsp_xml_start_tag_end(tag_start, contents_end);
	if (!tag_end) {
		return NULL;
	}

	cursor = tag_start + sizeof("<psalm") - 1;
	attr = NULL;
	while ((attr = lsp_bounded_memstr(cursor, name, name_length, tag_end)) != NULL) {
		if (lsp_xml_attribute_name_matches(attr, tag_start, tag_end, name, name_length)) {
			found = true;
			break;
		}
		cursor = attr + 1;
	}

	if (found) {
		eq = memchr(attr + name_length, '=', tag_end - (attr + name_length));
		if (!eq) {
			return NULL;
		}
		attr_value_start = eq + 1;
		while (attr_value_start < tag_end && isspace((unsigned char) *attr_value_start)) {
			attr_value_start++;
		}
		if (attr_value_start >= tag_end || (*attr_value_start != '\'' && *attr_value_start != '"')) {
			return NULL;
		}
		quote = *attr_value_start;
		attr_value_start++;
		attr_value_end = memchr(attr_value_start, quote, tag_end - attr_value_start);
		if (!attr_value_end) {
			return NULL;
		}
		attr_start = attr;
		while (attr_start > tag_start && isspace((unsigned char) attr_start[-1])) {
			attr_start--;
		}
		smart_str_appendl(&edited, contents_start, attr_start - contents_start);
		lsp_append_xml_attribute(&edited, name, name_length, value, value_length);
		smart_str_appendl(&edited, attr_value_end + 1, contents_end - (attr_value_end + 1));
	} else {
		cursor = tag_end;
		while (cursor > tag_start && isspace((unsigned char) cursor[-1])) {
			cursor--;
		}
		insert_at = cursor > tag_start && cursor[-1] == '/' ? cursor - 1 : tag_end;
		smart_str_appendl(&edited, contents_start, insert_at - contents_start);
		lsp_append_xml_attribute(&edited, name, name_length, value, value_length);
		smart_str_appendl(&edited, insert_at, contents_end - insert_at);
	}

	smart_str_0(&edited);

	return edited.s;
}

static inline bool lsp_directory_exists(zend_string *path)
{
	zend_stat_t st;

	return VCWD_STAT(ZSTR_VAL(path), &st) == 0 && S_ISDIR(st.st_mode);
}

static inline bool lsp_xml_tag_name_is(const char *tag_start, const char *tag_end, const char *name, size_t name_length)
{
	const char *cursor;

	cursor = tag_start;
	if (cursor >= tag_end || *cursor != '<') {
		return false;
	}

	cursor++;
	return (size_t) (tag_end - cursor) >= name_length &&
		memcmp(cursor, name, name_length) == 0 &&
		(cursor + name_length == tag_end || isspace((unsigned char) cursor[name_length]) || cursor[name_length] == '/' || cursor[name_length] == '>')
	;
}

static inline const char *lsp_xml_next_file_filter_tag(const char *cursor, const char *end)
{
	const char *directory_tag, *file_tag;

	directory_tag = lsp_bounded_memstr(cursor, "<directory", sizeof("<directory") - 1, end);
	file_tag = lsp_bounded_memstr(cursor, "<file", sizeof("<file") - 1, end);
	if (!directory_tag) {
		return file_tag;
	}
	if (!file_tag) {
		return directory_tag;
	}

	return directory_tag < file_tag ? directory_tag : file_tag;
}

static inline bool lsp_xml_name_attribute_bounds(const char *tag_start, const char *tag_end, const char **value_start, const char **value_end)
{
	const char *attr, *eq, *cursor;
	char quote;

	attr = lsp_bounded_memstr(tag_start, "name", sizeof("name") - 1, tag_end);
	while (attr) {
		if (lsp_xml_attribute_name_matches(attr, tag_start, tag_end, "name", sizeof("name") - 1)) {
			break;
		}
		attr = lsp_bounded_memstr(attr + 1, "name", sizeof("name") - 1, tag_end);
	}
	if (!attr) {
		return false;
	}

	eq = memchr(attr + sizeof("name") - 1, '=', tag_end - (attr + sizeof("name") - 1));
	if (!eq) {
		return false;
	}

	cursor = eq + 1;
	while (cursor < tag_end && isspace((unsigned char) *cursor)) {
		cursor++;
	}
	if (cursor >= tag_end || (*cursor != '\'' && *cursor != '"')) {
		return false;
	}

	quote = *cursor;
	*value_start = cursor + 1;
	*value_end = memchr(*value_start, quote, tag_end - *value_start);

	return *value_end != NULL;
}

static inline bool lsp_psalm_path_value_is_lsparrot_root(zend_string *project_root, const char *value_start, const char *value_end)
{
	const char *start, *end;
	size_t root_length, value_length;
	bool root_has_slash;

	start = value_start;
	end = value_end;
	while (start < end && isspace((unsigned char) *start)) {
		start++;
	}
	while (end > start && (isspace((unsigned char) end[-1]) || lsp_is_path_separator(end[-1]))) {
		end--;
	}

	value_length = end > start ? (size_t) (end - start) : 0;
	if ((value_length == sizeof(".lsparrot") - 1 && memcmp(start, ".lsparrot", sizeof(".lsparrot") - 1) == 0) ||
		(value_length == sizeof("./.lsparrot") - 1 && memcmp(start, "./.lsparrot", sizeof("./.lsparrot") - 1) == 0) ||
		(value_length == sizeof(".\\.lsparrot") - 1 && memcmp(start, ".\\.lsparrot", sizeof(".\\.lsparrot") - 1) == 0)
	) {
		return true;
	}

	root_length = ZSTR_LEN(project_root);
	root_has_slash = root_length > 0 && lsp_is_path_separator(ZSTR_VAL(project_root)[root_length - 1]);
	if (value_length == root_length + (root_has_slash ? 0 : 1) + sizeof(".lsparrot") - 1 &&
		lsp_path_compare(start, ZSTR_VAL(project_root), root_length) == 0
	) {
		if (root_has_slash) {
			return memcmp(start + root_length, ".lsparrot", sizeof(".lsparrot") - 1) == 0;
		}

		return lsp_is_path_separator(start[root_length]) && memcmp(start + root_length + 1, ".lsparrot", sizeof(".lsparrot") - 1) == 0;
	}

	return false;
}

static inline zend_string *lsp_psalm_config_without_lsparrot_root_ignore(zend_string *contents, zend_string *project_root)
{
	const char *contents_start, *contents_end, *copy_start, *tag_start, *tag_end, *value_start, *value_end, *remove_start, *remove_end;
	smart_str edited = {0};

	contents_start = ZSTR_VAL(contents);
	contents_end = contents_start + ZSTR_LEN(contents);
	copy_start = contents_start;
	tag_start = contents_start;
	while ((tag_start = lsp_xml_next_file_filter_tag(tag_start, contents_end)) != NULL) {
		tag_end = lsp_xml_start_tag_end(tag_start, contents_end);
		if (!tag_end) {
			break;
		}

		if ((lsp_xml_tag_name_is(tag_start, tag_end, "directory", sizeof("directory") - 1) || lsp_xml_tag_name_is(tag_start, tag_end, "file", sizeof("file") - 1)) &&
			lsp_xml_name_attribute_bounds(tag_start, tag_end, &value_start, &value_end) &&
			lsp_psalm_path_value_is_lsparrot_root(project_root, value_start, value_end)
		) {
			remove_start = tag_start;
			while (remove_start > contents_start && (remove_start[-1] == ' ' || remove_start[-1] == '\t')) {
				remove_start--;
			}
			remove_end = tag_end + 1;
			if (remove_end < contents_end && *remove_end == '\r') {
				remove_end++;
			}
			if (remove_end < contents_end && *remove_end == '\n') {
				remove_end++;
			}
			smart_str_appendl(&edited, copy_start, remove_start - copy_start);
			copy_start = remove_end;
			tag_start = remove_end;
			continue;
		}

		tag_start = tag_end + 1;
	}

	smart_str_appendl(&edited, copy_start, contents_end - copy_start);
	smart_str_0(&edited);

	return edited.s;
}

static inline zend_string *lsp_analyzer_config_resolve_path(zend_string *root, zend_string *path)
{
	bool slash;

	if (ZSTR_LEN(path) == 0) {
		return zend_string_copy(root);
	}

	if (lsp_path_is_absolute(path)) {
		return zend_string_copy(path);
	}

	slash = lsp_path_has_trailing_separator(root);

	return strpprintf(0, "%s%s%s", ZSTR_VAL(root), slash ? "" : "/", ZSTR_VAL(path));
}

static inline bool lsp_analysis_path_exists(zend_string *path)
{
	return lsp_is_regular_file(path) || lsp_directory_exists(path);
}

static inline bool lsp_analysis_paths_contains(zval *paths, zend_string *candidate)
{
	zval *path_zv;

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(paths), path_zv) {
		if (Z_TYPE_P(path_zv) == IS_STRING && zend_string_equals(Z_STR_P(path_zv), candidate)) {
			return true;
		}
	} ZEND_HASH_FOREACH_END();

	return false;
}

static inline void lsp_analysis_paths_add(zval *paths, zend_string *path)
{
	if (!lsp_analysis_path_exists(path) || lsp_analysis_paths_contains(paths, path)) {
		return;
	}

	add_next_index_str(paths, zend_string_copy(path));
}

static inline void lsp_composer_collect_path_value(zend_string *project_root, zval *paths, zval *value)
{
	zend_string *resolved;
	zval *entry;

	if (!value) {
		return;
	}

	if (Z_TYPE_P(value) == IS_STRING) {
		resolved = lsp_analyzer_config_resolve_path(project_root, Z_STR_P(value));
		lsp_analysis_paths_add(paths, resolved);
		zend_string_release(resolved);

		return;
	}

	if (Z_TYPE_P(value) != IS_ARRAY) {
		return;
	}

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(value), entry) {
		lsp_composer_collect_path_value(project_root, paths, entry);
	} ZEND_HASH_FOREACH_END();
}

static inline void lsp_composer_collect_psr_map(zend_string *project_root, zval *paths, zval *map)
{
	zval *value;

	if (!map || Z_TYPE_P(map) != IS_ARRAY) {
		return;
	}

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(map), value) {
		lsp_composer_collect_path_value(project_root, paths, value);
	} ZEND_HASH_FOREACH_END();
}

static inline void lsp_composer_collect_autoload_section(zend_string *project_root, zval *paths, zval *section)
{
	zval *psr4, *psr0, *classmap, *files;

	if (!section || Z_TYPE_P(section) != IS_ARRAY) {
		return;
	}

	psr4 = zend_hash_str_find(Z_ARRVAL_P(section), "psr-4", sizeof("psr-4") - 1);
	psr0 = zend_hash_str_find(Z_ARRVAL_P(section), "psr-0", sizeof("psr-0") - 1);
	classmap = zend_hash_str_find(Z_ARRVAL_P(section), "classmap", sizeof("classmap") - 1);
	files = zend_hash_str_find(Z_ARRVAL_P(section), "files", sizeof("files") - 1);

	lsp_composer_collect_psr_map(project_root, paths, psr4);
	lsp_composer_collect_psr_map(project_root, paths, psr0);
	lsp_composer_collect_path_value(project_root, paths, classmap);
	lsp_composer_collect_path_value(project_root, paths, files);
}

static inline void lsp_append_phpstan_analysis_paths(smart_str *contents, zval *paths)
{
	zval *path_zv;

	if (zend_hash_num_elements(Z_ARRVAL_P(paths)) == 0) {
		return;
	}

	smart_str_appendl(contents, "    paths:\n", sizeof("    paths:\n") - 1);
	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(paths), path_zv) {
		if (Z_TYPE_P(path_zv) != IS_STRING) {
			continue;
		}

		smart_str_appendl(contents, "        - ", sizeof("        - ") - 1);
		lsp_append_neon_single_quoted(contents, Z_STR_P(path_zv));
		smart_str_appendc(contents, '\n');
	} ZEND_HASH_FOREACH_END();
}

static inline void lsp_append_psalm_analysis_paths(smart_str *contents, zval *paths)
{
	zend_string *tag;
	zval *path_zv;

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(paths), path_zv) {
		if (Z_TYPE_P(path_zv) != IS_STRING) {
			continue;
		}

		tag = lsp_directory_exists(Z_STR_P(path_zv)) ?
			zend_string_init("directory", sizeof("directory") - 1, 0) :
			zend_string_init("file", sizeof("file") - 1, 0)
		;
		smart_str_appendl(contents, "        <", sizeof("        <") - 1);
		smart_str_append(contents, tag);
		lsp_append_xml_attribute(contents, "name", sizeof("name") - 1, ZSTR_VAL(Z_STR_P(path_zv)), ZSTR_LEN(Z_STR_P(path_zv)));
		smart_str_appendl(contents, " />\n", sizeof(" />\n") - 1);
		zend_string_release(tag);
	} ZEND_HASH_FOREACH_END();
}

static inline void lsp_psalm_ignore_paths_add_existing(zval *paths, zend_string *path)
{
	if (lsp_analysis_path_exists(path) && !lsp_analysis_paths_contains(paths, path)) {
		add_next_index_str(paths, zend_string_copy(path));
	}
}

static inline void lsp_psalm_collect_shadow_ignore_children(zval *paths, zend_string *shadow_dir)
{
	const char *entry_name;
	lsp_dir *handle;
	zend_string *child;

	handle = lsp_dir_open(shadow_dir);
	if (!handle) {
		return;
	}

	while ((entry_name = lsp_dir_read(handle)) != NULL) {
		if (strcmp(entry_name, ".") == 0 || strcmp(entry_name, "..") == 0 || strcmp(entry_name, "psalm-type") == 0) {
			continue;
		}

		child = lsp_join_path2(shadow_dir, entry_name);
		lsp_psalm_ignore_paths_add_existing(paths, child);
		zend_string_release(child);
	}

	lsp_dir_close(handle);
}

static inline void lsp_psalm_collect_type_shadow_ignore_paths(zend_string *project_root, zval *paths)
{
	const char *entry_name;
	lsp_dir *handle;
	zend_string *lsparrot_dir, *child;

	array_init(paths);
	lsparrot_dir = lsp_join_path2(project_root, ".lsparrot");
	handle = lsp_dir_open(lsparrot_dir);
	if (!handle) {
		zend_string_release(lsparrot_dir);

		return;
	}

	while ((entry_name = lsp_dir_read(handle)) != NULL) {
		if (strcmp(entry_name, ".") == 0 || strcmp(entry_name, "..") == 0) {
			continue;
		}

		child = lsp_join_path2(lsparrot_dir, entry_name);
		if (strcmp(entry_name, "shadow") == 0) {
			lsp_psalm_collect_shadow_ignore_children(paths, child);
		} else {
			lsp_psalm_ignore_paths_add_existing(paths, child);
		}
		zend_string_release(child);
	}

	lsp_dir_close(handle);
	zend_string_release(lsparrot_dir);
}

static inline void lsp_psalm_append_ignore_path_entry(smart_str *contents, zend_string *path)
{
	const char *tag_name;
	size_t tag_name_length;

	tag_name = lsp_directory_exists(path) ? "directory" : "file";
	tag_name_length = lsp_directory_exists(path) ? sizeof("directory") - 1 : sizeof("file") - 1;
	smart_str_appendl(contents, "            <", sizeof("            <") - 1);
	smart_str_appendl(contents, tag_name, tag_name_length);
	lsp_append_xml_attribute(contents, "name", sizeof("name") - 1, ZSTR_VAL(path), ZSTR_LEN(path));
	smart_str_appendl(contents, " />\n", sizeof(" />\n") - 1);
}

static inline void lsp_psalm_append_ignore_path_entries(smart_str *contents, zval *paths)
{
	zval *path_zv;

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(paths), path_zv) {
		if (Z_TYPE_P(path_zv) != IS_STRING) {
			continue;
		}

		lsp_psalm_append_ignore_path_entry(contents, Z_STR_P(path_zv));
	} ZEND_HASH_FOREACH_END();
}

static inline zend_string *lsp_psalm_config_append_type_shadow_ignores(zend_string *contents, zend_string *project_root)
{
	const char *contents_start, *contents_end, *ignore_close, *project_close;
	smart_str edited = {0};
	zval paths;

	lsp_psalm_collect_type_shadow_ignore_paths(project_root, &paths);
	if (zend_hash_num_elements(Z_ARRVAL(paths)) == 0) {
		zval_ptr_dtor(&paths);

		return zend_string_copy(contents);
	}

	contents_start = ZSTR_VAL(contents);
	contents_end = contents_start + ZSTR_LEN(contents);
	ignore_close = lsp_bounded_memstr(contents_start, "</ignoreFiles>", sizeof("</ignoreFiles>") - 1, contents_end);
	if (ignore_close) {
		smart_str_appendl(&edited, contents_start, ignore_close - contents_start);
		lsp_psalm_append_ignore_path_entries(&edited, &paths);
		smart_str_appendl(&edited, ignore_close, contents_end - ignore_close);
		smart_str_0(&edited);
		zval_ptr_dtor(&paths);

		return edited.s;
	}

	project_close = lsp_bounded_memstr(contents_start, "</projectFiles>", sizeof("</projectFiles>") - 1, contents_end);
	if (!project_close) {
		zval_ptr_dtor(&paths);

		return zend_string_copy(contents);
	}

	smart_str_appendl(&edited, contents_start, project_close - contents_start);
	smart_str_appendl(&edited, "        <ignoreFiles>\n", sizeof("        <ignoreFiles>\n") - 1);
	lsp_psalm_append_ignore_path_entries(&edited, &paths);
	smart_str_appendl(&edited, "        </ignoreFiles>\n", sizeof("        </ignoreFiles>\n") - 1);
	smart_str_appendl(&edited, project_close, contents_end - project_close);
	smart_str_0(&edited);
	zval_ptr_dtor(&paths);

	return edited.s;
}

extern void lsp_composer_analysis_paths(zend_string *project_root, zval *paths)
{
	zend_string *composer_json, *contents;
	zval decoded, *autoload, *autoload_dev;

	array_init(paths);
	composer_json = lsp_join_path2(project_root, "composer.json");
	if (!lsp_is_regular_file(composer_json)) {
		zend_string_release(composer_json);

		return;
	}

	contents = lsp_read_file(composer_json);
	zend_string_release(composer_json);
	if (contents == zend_empty_string) {
		return;
	}

	ZVAL_UNDEF(&decoded);
	php_json_decode_ex(&decoded, ZSTR_VAL(contents), ZSTR_LEN(contents), PHP_JSON_OBJECT_AS_ARRAY, 512);
	zend_string_release(contents);
	if (Z_TYPE(decoded) != IS_ARRAY) {
		if (!Z_ISUNDEF(decoded)) {
			zval_ptr_dtor(&decoded);
		}

		return;
	}

	autoload = zend_hash_str_find(Z_ARRVAL(decoded), "autoload", sizeof("autoload") - 1);
	autoload_dev = zend_hash_str_find(Z_ARRVAL(decoded), "autoload-dev", sizeof("autoload-dev") - 1);
	lsp_composer_collect_autoload_section(project_root, paths, autoload);
	lsp_composer_collect_autoload_section(project_root, paths, autoload_dev);

	zval_ptr_dtor(&decoded);
}

extern bool lsp_composer_project_has_analysis_paths(zend_string *project_root)
{
	zval paths;
	bool result;

	lsp_composer_analysis_paths(project_root, &paths);
	result = zend_hash_num_elements(Z_ARRVAL(paths)) > 0;
	zval_ptr_dtor(&paths);

	return result;
}

extern bool lsp_path_is_in_composer_analysis_paths(zend_string *path, zend_string *project_root)
{
	zval paths, *path_zv;
	bool result;

	result = false;
	lsp_composer_analysis_paths(project_root, &paths);
	ZEND_HASH_FOREACH_VAL(Z_ARRVAL(paths), path_zv) {
		if (Z_TYPE_P(path_zv) == IS_STRING && lsp_path_is_same_or_under(path, Z_STR_P(path_zv))) {
			result = true;
			break;
		}
	} ZEND_HASH_FOREACH_END();
	zval_ptr_dtor(&paths);

	return result;
}

static inline zend_string *lsp_psalm_default_config_contents(zend_string *project_root, zend_long level)
{
	smart_str contents = {0};
	zend_string *vendor_dir;
	zval paths;
	char level_buffer[32];
	int level_length;

	if (level < 1) {
		level = 6;
	}

	level_length = snprintf(level_buffer, sizeof(level_buffer), ZEND_LONG_FMT, level);
	vendor_dir = lsp_composer_vendor_dir(project_root);
	lsp_composer_analysis_paths(project_root, &paths);

	smart_str_appendl(&contents, "<?xml version=\"1.0\"?>\n<psalm", sizeof("<?xml version=\"1.0\"?>\n<psalm") - 1);
	if (level_length > 0) {
		lsp_append_xml_attribute(&contents, "errorLevel", sizeof("errorLevel") - 1, level_buffer, (size_t) level_length);
	}

	smart_str_appendl(&contents, ">\n    <projectFiles>\n", sizeof(">\n    <projectFiles>\n") - 1);
	lsp_append_psalm_analysis_paths(&contents, &paths);
	smart_str_appendl(&contents, "        <ignoreFiles>\n            <directory name=\".lsparrot\" />\n", sizeof("        <ignoreFiles>\n            <directory name=\".lsparrot\" />\n") - 1);
	if (lsp_directory_exists(vendor_dir)) {
		smart_str_appendl(&contents, "            <directory", sizeof("            <directory") - 1);
		lsp_append_xml_attribute(&contents, "name", sizeof("name") - 1, ZSTR_VAL(vendor_dir), ZSTR_LEN(vendor_dir));
		smart_str_appendl(&contents, " />\n", sizeof(" />\n") - 1);
	}

	smart_str_appendl(&contents, "        </ignoreFiles>\n    </projectFiles>\n</psalm>\n", sizeof("        </ignoreFiles>\n    </projectFiles>\n</psalm>\n") - 1);
	smart_str_0(&contents);

	zend_string_release(vendor_dir);
	zval_ptr_dtor(&paths);

	return contents.s;
}

extern zend_string *lsp_phpstan_config_file(zend_string *root)
{
	const char *names[] = {"phpstan.neon", "phpstan.neon.dist", "phpstan.dist.neon", ".phpstan.neon", ".phpstan.neon.dist"};

	return lsp_analyzer_config_file_named(root, names, sizeof(names) / sizeof(names[0]));
}

extern zend_string *lsp_phpstan_lsp_config_file(zend_string *project_root, zend_string *project_config, zend_long parallel_workers)
{
	smart_str contents = {0};
	zend_string *dir, *cache_dir, *path;
	zval paths;
	char parallel_buffer[32];
	int parallel_length;
	bool ok;

	ok = false;
	dir = strpprintf(0, "%s/.lsparrot/phpstan", ZSTR_VAL(project_root));
	cache_dir = strpprintf(0, "%s/cache", ZSTR_VAL(dir));
	path = strpprintf(0, "%s/lsp.neon", ZSTR_VAL(dir));
	lsp_mkdir_p(cache_dir);

	if (project_config) {
		smart_str_appendl(&contents, "includes:\n    - ", sizeof("includes:\n    - ") - 1);
		lsp_append_neon_single_quoted(&contents, project_config);
		smart_str_appendl(&contents, "\n\n", sizeof("\n\n") - 1);
	}

	smart_str_appendl(&contents, "parameters:\n    tmpDir: ", sizeof("parameters:\n    tmpDir: ") - 1);
	lsp_append_neon_single_quoted(&contents, cache_dir);
	smart_str_appendc(&contents, '\n');

	if (!project_config) {
		lsp_composer_analysis_paths(project_root, &paths);
		lsp_append_phpstan_analysis_paths(&contents, &paths);
		zval_ptr_dtor(&paths);
	}

	if (!project_config && parallel_workers > 1) {
		parallel_length = snprintf(parallel_buffer, sizeof(parallel_buffer), ZEND_LONG_FMT, parallel_workers);
		if (parallel_length > 0) {
			smart_str_appendl(&contents, "    parallel:\n        maximumNumberOfProcesses: ", sizeof("    parallel:\n        maximumNumberOfProcesses: ") - 1);
			smart_str_appendl(&contents, parallel_buffer, (size_t) parallel_length);
			smart_str_appendl(&contents, "\n        processTimeout: 600.0\n        jobSize: 20\n", sizeof("\n        processTimeout: 600.0\n        jobSize: 20\n") - 1);
		}
	}
	smart_str_0(&contents);

	if (contents.s) {
		ok = lsp_write_string_file(path, contents.s);
		zend_string_release(contents.s);
	}

	zend_string_release(cache_dir);
	zend_string_release(dir);

	if (!ok) {
		zend_string_release(path);

		return NULL;
	}

	return path;
}

extern zend_long lsp_project_phpstan_level(lsp_server *server, zend_string *project_root)
{
	return lsp_project_vscode_config_long(project_root, "phpstanLevel", server->options.phpstan_level);
}

extern zend_string *lsp_psalm_config_file(zend_string *root)
{
	const char *names[] = {"psalm.xml", "psalm.xml.dist"};

	return lsp_analyzer_config_file_named(root, names, sizeof(names) / sizeof(names[0]));
}

extern zend_long lsp_project_psalm_level(lsp_server *server, zend_string *project_root)
{
	return lsp_project_vscode_config_long(project_root, "psalmLevel", server->options.psalm_level);
}

extern zend_string *lsp_psalm_ls_config_file(zend_string *project_root, zend_string *project_config, zend_long level, bool live_dead_code_diagnostics)
{
	zend_string *dir, *cache_dir, *path, *contents, *cache_updated, *resolve_updated, *unused_updated, *baseline_updated;
	bool ok;

	ok = false;
	dir = strpprintf(0, "%s/.lsparrot/psalm", ZSTR_VAL(project_root));
	cache_dir = strpprintf(0, "%s/cache", ZSTR_VAL(dir));
	path = strpprintf(0, "%s/lsp.xml", ZSTR_VAL(dir));
	lsp_mkdir_p(cache_dir);

	contents = project_config ? lsp_read_file(project_config) : lsp_psalm_default_config_contents(project_root, level);
	if (contents == zend_empty_string) {
		zend_string_release(cache_dir);
		zend_string_release(dir);
		zend_string_release(path);

		return NULL;
	}

	cache_updated = lsp_psalm_config_upsert_attribute(contents, "cacheDirectory", ZSTR_VAL(cache_dir), ZSTR_LEN(cache_dir));
	zend_string_release(contents);
	if (!cache_updated) {
		zend_string_release(cache_dir);
		zend_string_release(dir);
		zend_string_release(path);

		return NULL;
	}

	resolve_updated = lsp_psalm_config_upsert_attribute(cache_updated, "resolveFromConfigFile", "false", sizeof("false") - 1);
	zend_string_release(cache_updated);
	if (!resolve_updated) {
		zend_string_release(cache_dir);
		zend_string_release(dir);
		zend_string_release(path);

		return NULL;
	}

	if (!live_dead_code_diagnostics) {
		unused_updated = lsp_psalm_config_upsert_attribute(resolve_updated, "findUnusedCode", "false", sizeof("false") - 1);
		zend_string_release(resolve_updated);
		if (!unused_updated) {
			zend_string_release(cache_dir);
			zend_string_release(dir);
			zend_string_release(path);

			return NULL;
		}

		baseline_updated = lsp_psalm_config_upsert_attribute(unused_updated, "findUnusedBaselineEntry", "false", sizeof("false") - 1);
		zend_string_release(unused_updated);
		if (!baseline_updated) {
			zend_string_release(cache_dir);
			zend_string_release(dir);
			zend_string_release(path);

			return NULL;
		}

		resolve_updated = baseline_updated;
	}

	if (resolve_updated) {
		ok = lsp_write_string_file(path, resolve_updated);

		zend_string_release(resolve_updated);
	}

	zend_string_release(cache_dir);
	zend_string_release(dir);

	if (!ok) {
		zend_string_release(path);

		return NULL;
	}

	return path;
}

extern zend_string *lsp_psalm_type_config_file(zend_string *project_root, zend_string *project_config, zend_long level, bool live_dead_code_diagnostics)
{
	zend_string *base_path, *base_contents, *without_lsparrot_ignore, *type_contents, *dir, *path;
	bool ok;

	ok = false;
	base_path = lsp_psalm_ls_config_file(project_root, project_config, level, live_dead_code_diagnostics);
	if (!base_path) {
		return NULL;
	}

	base_contents = lsp_read_file(base_path);
	zend_string_release(base_path);
	if (base_contents == zend_empty_string) {
		return NULL;
	}

	without_lsparrot_ignore = lsp_psalm_config_without_lsparrot_root_ignore(base_contents, project_root);
	zend_string_release(base_contents);
	type_contents = lsp_psalm_config_append_type_shadow_ignores(without_lsparrot_ignore, project_root);
	zend_string_release(without_lsparrot_ignore);

	dir = strpprintf(0, "%s/.lsparrot/psalm", ZSTR_VAL(project_root));
	path = strpprintf(0, "%s/type.xml", ZSTR_VAL(dir));
	lsp_mkdir_p(dir);
	ok = lsp_write_string_file(path, type_contents);
	zend_string_release(type_contents);
	zend_string_release(dir);

	if (!ok) {
		zend_string_release(path);

		return NULL;
	}

	return path;
}
