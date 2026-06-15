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

extern void lsp_publish_empty_diagnostics(zend_string *uri)
{
	zval params, diagnostics;

	array_init(&params);
	add_assoc_str(&params, "uri", zend_string_copy(uri));
	array_init(&diagnostics);
	add_assoc_zval(&params, "diagnostics", &diagnostics);
	lsp_protocol_notify("textDocument/publishDiagnostics", &params);
	zval_ptr_dtor(&params);
}

extern zend_string *lsp_join_path2(zend_string *base, const char *suffix)
{
	bool slash;

	slash = lsp_path_has_trailing_separator(base);
	return strpprintf(0, "%s%s%s", ZSTR_VAL(base), slash ? "" : "/", suffix);
}

static inline zend_string *lsp_path_join_zstr(zend_string *base, zend_string *suffix)
{
	bool slash;

	slash = lsp_path_has_trailing_separator(base);
	return strpprintf(0, "%s%s%s", ZSTR_VAL(base), slash ? "" : "/", ZSTR_VAL(suffix));
}

static inline zend_string *lsp_resolve_path(zend_string *root, zend_string *path)
{
	if (lsp_path_is_absolute(path)) {
		return zend_string_copy(path);
	}

	return lsp_path_join_zstr(root, path);
}

static inline bool lsp_is_php_script(zend_string *path)
{
	FILE *file = fopen(ZSTR_VAL(path), "rb");
	char buffer[4096];
	size_t n;
	bool result;

	if (!file) {
		return false;
	}

	n = fread(buffer, 1, sizeof(buffer) - 1, file);
	fclose(file);
	buffer[n] = '\0';
	result = strstr(buffer, "<?php") != NULL;

	return result;
}

extern zend_string *lsp_composer_config_string(zend_string *root, const char *key)
{
	zend_string *composer_json, *contents, *result = NULL;
	zval decoded, *config, *value;

	composer_json = lsp_join_path2(root, "composer.json");

	if (!lsp_is_regular_file(composer_json)) {
		zend_string_release(composer_json);

		return NULL;
	}

	contents = lsp_read_file(composer_json);
	zend_string_release(composer_json);

	if (contents == zend_empty_string) {
		return NULL;
	}

	ZVAL_UNDEF(&decoded);
	php_json_decode_ex(&decoded, ZSTR_VAL(contents), ZSTR_LEN(contents), PHP_JSON_OBJECT_AS_ARRAY, 512);
	zend_string_release(contents);
	if (Z_TYPE(decoded) != IS_ARRAY) {
		if (!Z_ISUNDEF(decoded)) {
			zval_ptr_dtor(&decoded);
		}

		return NULL;
	}

	config = zend_hash_str_find(Z_ARRVAL(decoded), "config", sizeof("config") - 1);
	value = config && Z_TYPE_P(config) == IS_ARRAY ? zend_hash_str_find(Z_ARRVAL_P(config), key, strlen(key)) : NULL;
	if (value && Z_TYPE_P(value) == IS_STRING && Z_STRLEN_P(value) > 0) {
		result = zend_string_copy(Z_STR_P(value));
	}

	zval_ptr_dtor(&decoded);

	return result;
}

extern zend_string *lsp_composer_vendor_dir(zend_string *project_root)
{
	zend_string *configured, *resolved;

	configured = lsp_composer_config_string(project_root, "vendor-dir");
	if (!configured) {
		return lsp_join_path2(project_root, "vendor");
	}

	resolved = lsp_resolve_path(project_root, configured);
	zend_string_release(configured);

	return resolved;
}

extern bool lsp_path_is_under_composer_vendor_dir(zend_string *path, zend_string *project_root)
{
	zend_string *vendor_dir;
	bool result;

	vendor_dir = lsp_composer_vendor_dir(project_root);
	result = lsp_path_is_same_or_under(path, vendor_dir);
	zend_string_release(vendor_dir);

	return result;
}

extern bool lsp_path_is_in_workspace_composer_vendor(zend_string *workspace_root, zend_string *path)
{
	zend_string *dir, *composer;
	size_t workspace_root_length;
	char *buffer, *slash;
	bool result = false;

	if (!lsp_path_is_same_or_under(path, workspace_root)) {
		return false;
	}

	workspace_root_length = ZSTR_LEN(workspace_root);
	buffer = estrndup(ZSTR_VAL(path), ZSTR_LEN(path));
	while (strlen(buffer) >= workspace_root_length) {
		dir = zend_string_init(buffer, strlen(buffer), 0);
		composer = lsp_join_path2(dir, "composer.json");

		if (!zend_string_equals(dir, path) &&
			lsp_is_regular_file(composer) &&
			lsp_path_is_under_composer_vendor_dir(path, dir)
		) {
			result = true;
			zend_string_release(composer);
			zend_string_release(dir);
			break;
		}

		zend_string_release(composer);
		zend_string_release(dir);

		if (strlen(buffer) == workspace_root_length && lsp_path_compare(buffer, ZSTR_VAL(workspace_root), workspace_root_length) == 0) {
			break;
		}

		slash = lsp_last_path_separator(buffer);
		if (!slash || slash == buffer) {
			break;
		}

		*slash = '\0';
	}
	efree(buffer);

	return result;
}

extern zend_string *lsp_tool_project_candidate(zend_string *root, const char *name)
{
	zend_string *bin_dir, *vendor_dir = NULL, *vendor_bin, *name_string, *candidate, *resolved;

	bin_dir = lsp_composer_config_string(root, "bin-dir");
	name_string = zend_string_init(name, strlen(name), 0);

	if (bin_dir) {
		resolved = lsp_resolve_path(root, bin_dir);
		candidate = lsp_path_join_zstr(resolved, name_string);
		zend_string_release(resolved);
		zend_string_release(bin_dir);
		zend_string_release(name_string);

		if (lsp_is_regular_file(candidate)) {
			return candidate;
		}

		zend_string_release(candidate);

		return NULL;
	}

	vendor_dir = lsp_composer_vendor_dir(root);
	vendor_bin = lsp_join_path2(vendor_dir, "bin");
	zend_string_release(vendor_dir);
	candidate = lsp_path_join_zstr(vendor_bin, name_string);
	zend_string_release(vendor_bin);
	zend_string_release(name_string);

	if (lsp_is_regular_file(candidate)) {
		return candidate;
	}

	zend_string_release(candidate);

	return NULL;
}

static inline bool lsp_tool_path_candidate_is_usable(zend_string *candidate)
{
	return !lsp_path_is_windows_batch_file(candidate) && lsp_path_is_executable_file(candidate);
}

static inline zend_string *lsp_tool_path_candidate_in_dir(const char *dir, size_t dir_length, const char *name)
{
	zend_string *candidate;
	const char *extension;
	size_t name_length, extension_length;
	uint32_t i;

	name_length = strlen(name);
	for (i = 0; i < lsp_executable_extension_count(); i++) {
		extension = lsp_executable_extension(i);
		extension_length = strlen(extension);
		candidate = zend_string_alloc(dir_length + 1 + name_length + extension_length, 0);
		memcpy(ZSTR_VAL(candidate), dir, dir_length);
		ZSTR_VAL(candidate)[dir_length] = '/';
		memcpy(ZSTR_VAL(candidate) + dir_length + 1, name, name_length);
		memcpy(ZSTR_VAL(candidate) + dir_length + 1 + name_length, extension, extension_length);
		ZSTR_VAL(candidate)[ZSTR_LEN(candidate)] = '\0';

		if (lsp_tool_path_candidate_is_usable(candidate)) {
			return candidate;
		}

		zend_string_release(candidate);
	}

	return NULL;
}

static inline zend_string *lsp_tool_path_candidate(const char *name)
{
	const char *path = getenv("PATH"), *cursor, *end, *sep;
	zend_string *candidate;
	size_t dir_length;
	char path_separator;

	if (!path || *path == '\0') {
		return NULL;
	}

	path_separator = lsp_path_list_separator();

	cursor = path;
	while (*cursor) {
		sep = strchr(cursor, path_separator);
		if (sep) {
			dir_length = sep - cursor;
			end = sep;
		} else {
			dir_length = strlen(cursor);
			end = cursor + dir_length;
		}

		if (dir_length > 0) {
			candidate = lsp_tool_path_candidate_in_dir(cursor, dir_length, name);
			if (candidate) {
				return candidate;
			}
		}

		cursor = *end == path_separator ? end + 1 : end;
	}

	return NULL;
}

static inline zend_string *lsp_find_tool(const char *name, zend_string *root, bool *run_with_php)
{
	zend_string *path = lsp_tool_project_candidate(root, name);

	if (!path) {
		path = lsp_tool_path_candidate(name);
	}

	if (!path) {
		return NULL;
	}

	*run_with_php = lsp_is_php_script(path);

	return path;
}

extern bool lsp_tool_available(const char *name, zend_string *root)
{
	bool run_with_php = false;
	zend_string *path = lsp_find_tool(name, root, &run_with_php);

	if (!path) {
		return false;
	}

	zend_string_release(path);

	return true;
}

extern bool lsp_tool_command(lsp_server *server, zend_string *root, const char *name, lsp_command *command)
{
	bool run_with_php = false;
	zend_string *tool = lsp_find_tool(name, root, &run_with_php);
	uint32_t i;

	if (!tool) {
		return false;
	}

	if (run_with_php) {
		lsp_command_add(command, lsp_php_binary());

		for (i = 0; i < server->options.worker_php_arg_count; i++) {
			lsp_command_add_zstr(command, server->options.worker_php_args[i]);
		}

		lsp_command_add_zstr(command, tool);
	} else {
		lsp_command_add_zstr(command, tool);
	}

	zend_string_release(tool);

	return true;
}

extern zend_string *lsp_run_command_capture(lsp_command *command, zend_string *cwd, double timeout)
{
	return lsp_process_run_capture(command, cwd, timeout);
}

extern bool lsp_scan_should_skip_dir_name(const char *name)
{
	return strcmp(name, ".") == 0 ||
		strcmp(name, "..") == 0 ||
		strcmp(name, ".git") == 0 ||
		strcmp(name, ".cache") == 0 ||
		strcmp(name, ".lsparrot") == 0 ||
		strcmp(name, "vendor") == 0 ||
		strcmp(name, "node_modules") == 0 ||
		strcmp(name, "var") == 0 ||
		strcmp(name, "tmp") == 0 ||
		strcmp(name, "build") == 0 ||
		strcmp(name, "dist") == 0
	;
}

static inline bool lsp_path_is_under_workspace_root(zend_string *path, zend_string *root)
{
	return lsp_path_is_same_or_under(path, root);
}

static inline zend_string *lsp_vendor_owner_project_root(lsp_server *server, zend_string *candidate)
{
	zend_string *dir, *composer, *owner = NULL;
	size_t root_length;
	char *buffer, *slash;

	if (!lsp_path_is_under_workspace_root(candidate, server->root)) {
		return NULL;
	}

	root_length = ZSTR_LEN(server->root);
	buffer = estrndup(ZSTR_VAL(candidate), ZSTR_LEN(candidate));

	slash = lsp_last_path_separator(buffer);
	if (!slash) {
		efree(buffer);
		return NULL;
	}

	if (slash == buffer) {
		buffer[1] = '\0';
	} else {
		*slash = '\0';
	}

	while (strlen(buffer) >= root_length) {
		dir = zend_string_init(buffer, strlen(buffer), 0);
		composer = lsp_join_path2(dir, "composer.json");

		if (!zend_string_equals(dir, candidate) &&
			lsp_is_regular_file(composer) &&
			lsp_path_is_under_composer_vendor_dir(candidate, dir)
		) {
			owner = dir;
			zend_string_release(composer);
			break;
		}

		zend_string_release(composer);
		zend_string_release(dir);

		if (strlen(buffer) == root_length && lsp_path_compare(buffer, ZSTR_VAL(server->root), root_length) == 0) {
			break;
		}

		slash = lsp_last_path_separator(buffer);
		if (!slash || slash == buffer) {
			break;
		}

		*slash = '\0';
	}

	efree(buffer);

	return owner;
}

extern zend_string *lsp_document_project_root(lsp_server *server, lsp_document *document)
{
	zend_string *dir, *composer, *owner;
	size_t root_length = ZSTR_LEN(server->root);
	char *buffer, *slash;

	if (!lsp_path_is_under_workspace_root(document->path, server->root)) {
		return zend_string_copy(server->root);
	}

	buffer = estrndup(ZSTR_VAL(document->path), ZSTR_LEN(document->path));
	slash = lsp_last_path_separator(buffer);
	if (!slash) {
		efree(buffer);
		return zend_string_copy(server->root);
	}

	if (slash == buffer) {
		buffer[1] = '\0';
	} else {
		*slash = '\0';
	}

	while (strlen(buffer) >= root_length) {
		dir = zend_string_init(buffer, strlen(buffer), 0);
		composer = lsp_join_path2(dir, "composer.json");

		if (lsp_is_regular_file(composer)) {
			owner = lsp_vendor_owner_project_root(server, dir);
			zend_string_release(composer);
			efree(buffer);

			if (owner) {
				zend_string_release(dir);
				return owner;
			}

			return dir;
		}

		zend_string_release(composer);
		zend_string_release(dir);

		if (strlen(buffer) == root_length && lsp_path_compare(buffer, ZSTR_VAL(server->root), root_length) == 0) {
			break;
		}

		slash = lsp_last_path_separator(buffer);
		if (!slash || slash == buffer) {
			break;
		}

		*slash = '\0';
	}

	efree(buffer);

	return zend_string_copy(server->root);
}

extern void lsp_line_range(zval *range, zend_string *text, zend_long one_based_line)
{
	const char *next;
	zend_long line = one_based_line > 0 ? one_based_line - 1 : 0, current;
	zval start, end;
	size_t offset = 0, start_offset, end_offset, length = ZSTR_LEN(text);

	for (current = 0; current < line; current++) {
		next = memchr(ZSTR_VAL(text) + offset, '\n', length - offset);
		if (!next) {
			offset = length;
			break;
		}

		offset = (size_t) (next - ZSTR_VAL(text)) + 1;
	}

	start_offset = offset;
	while (offset < length && ZSTR_VAL(text)[offset] != '\n' && ZSTR_VAL(text)[offset] != '\r') {
		offset++;
	}
	end_offset = offset;

	array_init(range);
	array_init(&start);
	add_assoc_long(&start, "line", line);
	add_assoc_long(&start, "character", 0);
	array_init(&end);
	add_assoc_long(&end, "line", line);
	add_assoc_long(&end, "character", (zend_long) (end_offset > start_offset ? end_offset - start_offset : 1));
	add_assoc_zval(range, "start", &start);
	add_assoc_zval(range, "end", &end);
}

extern void lsp_add_analyzer_diagnostic(zval *diagnostics, const char *source, zend_string *message, zend_string *code, zval *range, zend_long severity)
{
	zval diagnostic;

	array_init(&diagnostic);
	add_assoc_string(&diagnostic, "source", source);
	add_assoc_str(&diagnostic, "message", zend_string_copy(message));

	if (code && ZSTR_LEN(code) > 0) {
		add_assoc_str(&diagnostic, "code", zend_string_copy(code));
	}

	add_assoc_long(&diagnostic, "severity", severity);
	add_assoc_zval(&diagnostic, "range", range);
	add_next_index_zval(diagnostics, &diagnostic);
}

extern zend_string *lsp_json_slice_from(zend_string *output, char open_char)
{
	const char *value, *start, *p, *end;
	uint32_t curly_depth, square_depth;
	bool in_string, escaped;

	value = ZSTR_VAL(output);
	end = value + ZSTR_LEN(output);
	start = memchr(value, open_char, ZSTR_LEN(output));
	if (!start) {
		return zend_string_copy(output);
	}

	curly_depth = 0;
	square_depth = 0;
	in_string = false;
	escaped = false;
	for (p = start; p < end; p++) {
		if (in_string) {
			if (escaped) {
				escaped = false;
			} else if (*p == '\\') {
				escaped = true;
			} else if (*p == '"') {
				in_string = false;
			}
			continue;
		}

		if (*p == '"') {
			in_string = true;
			continue;
		}

		if (*p == '{') {
			curly_depth++;
		} else if (*p == '}' && curly_depth > 0) {
			curly_depth--;
		} else if (*p == '[') {
			square_depth++;
		} else if (*p == ']' && square_depth > 0) {
			square_depth--;
		}

		if (p > start && curly_depth == 0 && square_depth == 0) {
			return zend_string_init(start, p - start + 1, 0);
		}
	}

	return zend_string_init(start, end - start, 0);
}

static inline const char *lsp_analyzer_label(const char *analyzer)
{
	if (strcmp(analyzer, "phpstan") == 0) {
		return "PHPStan";
	}

	return "Psalm";
}

static inline char lsp_analyzer_json_open_char(const char *analyzer)
{
	return strcmp(analyzer, "phpstan") == 0 ? '{' : '[';
}

static inline bool lsp_analyzer_output_is_json(const char *analyzer, zend_string *output)
{
	zend_string *json;
	zval decoded;
	bool ok;

	json = lsp_json_slice_from(output, lsp_analyzer_json_open_char(analyzer));
	ZVAL_UNDEF(&decoded);
	php_json_decode_ex(&decoded, ZSTR_VAL(json), ZSTR_LEN(json), PHP_JSON_OBJECT_AS_ARRAY, 512);
	zend_string_release(json);
	ok = Z_TYPE(decoded) == IS_ARRAY;
	if (!Z_ISUNDEF(decoded)) {
		zval_ptr_dtor(&decoded);
	}

	return ok;
}

extern zend_string *lsp_analyzer_failure_message(const char *analyzer, zend_string *output)
{
	const char *value, *end, *line_start, *line_end, *marker, *label;

	value = ZSTR_VAL(output);
	end = value + ZSTR_LEN(output);
	label = lsp_analyzer_label(analyzer);
	marker = strstr(value, "Error:");

	if (marker && marker < end) {
		line_start = marker;
	} else {
		line_start = value;
		while (line_start < end && isspace((unsigned char) *line_start)) {
			line_start++;
		}
	}

	line_end = line_start;
	while (line_end < end && *line_end != '\r' && *line_end != '\n') {
		line_end++;
	}

	while (line_end > line_start && isspace((unsigned char) line_end[-1])) {
		line_end--;
	}

	if (line_end <= line_start) {
		return strpprintf(0, "%s diagnostics failed.", label);
	}

	return strpprintf(0, "%s diagnostics failed: %.*s", label, (int) (line_end - line_start), line_start);
}


extern zend_long lsp_analyzer_parallel_workers(lsp_server *server)
{
	long detected = 1;
	zend_long configured;

	configured = server->options.worker_count;
	if (configured > 0) {
		return configured;
	}

#ifdef _SC_NPROCESSORS_ONLN
	detected = sysconf(_SC_NPROCESSORS_ONLN);
#endif
	if (detected < 1) {
		detected = 1;
	}

	return (zend_long) detected;
}

extern void lsp_analyzer_add_memory_limit(lsp_command *command, lsp_server *server)
{
	zend_string *argument;

	if (!server->options.memory_limit || ZSTR_LEN(server->options.memory_limit) == 0) {
		return;
	}

	argument = strpprintf(0, "--memory-limit=%s", ZSTR_VAL(server->options.memory_limit));
	lsp_command_add_zstr(command, argument);

	zend_string_release(argument);
}


static inline HashTable *lsp_analyzer_project_table(lsp_server *server, const char *analyzer)
{
	if (strcmp(analyzer, "phpstan") == 0) {
		return &server->phpstan_projects;
	}

	if (strcmp(analyzer, "psalm-ls") == 0) {
		return &server->psalm_ls_project_states;
	}

	return &server->psalm_projects;
}

static inline lsp_analyzer_job *lsp_analyzer_project_job(lsp_server *server, const char *analyzer)
{
	return strcmp(analyzer, "phpstan") == 0 ? &server->phpstan_job : &server->psalm_job;
}

extern zend_string *lsp_analyzer_project_output_file(zend_string *project_root, const char *analyzer)
{
	zend_string *dir, *path;

	dir = strpprintf(0, "%s/.lsparrot/%s", ZSTR_VAL(project_root), analyzer);
	lsp_mkdir_p(dir);
	path = strpprintf(0, "%s/project-diagnostics.json", ZSTR_VAL(dir));
	zend_string_release(dir);

	return path;
}

extern void lsp_analyzer_project_state(lsp_server *server, const char *analyzer, zend_string *project_root, zend_long state)
{
	zval state_zv;

	ZVAL_LONG(&state_zv, state);
	zend_hash_update(lsp_analyzer_project_table(server, analyzer), project_root, &state_zv);
}

extern zend_long lsp_analyzer_project_state_value(lsp_server *server, const char *analyzer, zend_string *project_root)
{
	zval *state_zv;

	state_zv = zend_hash_find(lsp_analyzer_project_table(server, analyzer), project_root);

	return state_zv &&
		Z_TYPE_P(state_zv) == IS_LONG ? Z_LVAL_P(state_zv) : 0
	;
}

extern bool lsp_analyzer_project_has_state(lsp_server *server, const char *analyzer, zend_string *project_root)
{
	return lsp_analyzer_project_state_value(server, analyzer, project_root) > 0;
}

static inline void lsp_analyzer_project_config_error(lsp_server *server, const char *analyzer, const char *message, zend_string *project_root)
{
	lsp_analyzer_project_state(server, analyzer, project_root, LSP_ANALYZER_PROJECT_ERROR);
	lsp_analyzer_project_status(analyzer, "error", message, project_root);
}

extern bool lsp_analyzer_project_has_scope(zend_string *project_root)
{
	return lsp_composer_project_has_analysis_paths(project_root);
}

extern bool lsp_document_is_in_analyzer_scope(lsp_document *document, zend_string *project_root)
{
	return lsp_path_is_in_composer_analysis_paths(document->path, project_root);
}

extern void lsp_command_add_composer_analysis_paths(lsp_command *command, zend_string *project_root)
{
	zval paths, *path_zv;

	lsp_composer_analysis_paths(project_root, &paths);
	ZEND_HASH_FOREACH_VAL(Z_ARRVAL(paths), path_zv) {
		if (Z_TYPE_P(path_zv) == IS_STRING) {
			lsp_command_add_zstr(command, Z_STR_P(path_zv));
		}
	} ZEND_HASH_FOREACH_END();
	zval_ptr_dtor(&paths);
}

static inline lsp_process_id lsp_start_command_to_file(lsp_command *command, zend_string *cwd, zend_string *output_file)
{
	return lsp_process_spawn_to_file(command, cwd, output_file);
}

extern bool lsp_start_analyzer_project_job(lsp_server *server, const char *analyzer, zend_string *project_root, lsp_command *command, zend_string *output_file)
{
	lsp_analyzer_job *job;
	zend_string *temporary_file;
	lsp_process_id pid;

	job = lsp_analyzer_project_job(server, analyzer);
	if (job->running) {
		return false;
	}

	temporary_file = strpprintf(0, "%s.tmp.%ld", ZSTR_VAL(output_file), (long) lsp_current_process_id());

	pid = lsp_start_command_to_file(command, project_root, temporary_file);
	if (!lsp_process_id_valid(pid)) {
		zend_string_release(temporary_file);

		return false;
	}

	lsp_analyzer_job_clear(job);
	job->pid = pid;
	job->cache_file = temporary_file;
	job->project_root = zend_string_copy(project_root);
	job->running = true;
	lsp_analyzer_project_state(server, analyzer, project_root, LSP_ANALYZER_PROJECT_RUNNING);

	return true;
}



static inline bool lsp_analyzer_output_has_content(zend_string *path)
{
	zend_stat_t st;

	return VCWD_STAT(ZSTR_VAL(path), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

static inline void lsp_commit_analyzer_project_output(const char *analyzer, zend_string *project_root, zend_string *output_file)
{
	zend_string *final_file;

	if (!output_file) {
		return;
	}

	if (!lsp_analyzer_output_has_content(output_file)) {
		VCWD_UNLINK(ZSTR_VAL(output_file));
		return;
	}

	final_file = lsp_analyzer_project_output_file(project_root, analyzer);
	VCWD_RENAME(ZSTR_VAL(output_file), ZSTR_VAL(final_file));
	zend_string_release(final_file);
}

static inline void lsp_publish_project_document_diagnostics(lsp_server *server, zend_string *project_root)
{
	lsp_document *document;
	zend_string *document_root;
	zval *document_zv;

	ZEND_HASH_FOREACH_VAL(&server->documents, document_zv) {
		document = (lsp_document *) Z_PTR_P(document_zv);
		document_root = lsp_document_project_root(server, document);
		if (zend_string_equals(document_root, project_root)) {
			lsp_publish_document_diagnostics(server, document);
		}
		zend_string_release(document_root);
	} ZEND_HASH_FOREACH_END();
}

static inline void lsp_schedule_workspace_analyzer_projects(lsp_server *server, zend_string *dir, uint32_t depth)
{
	const char *entry_name;
	lsp_dir *handle;
	zend_string *composer, *child;
	zend_stat_t st;

	if (depth > 8 || (depth > 0 && lsp_path_is_in_workspace_composer_vendor(server->root, dir))) {
		return;
	}

	composer = lsp_join_path2(dir, "composer.json");
	if (lsp_is_regular_file(composer) && lsp_analyzer_project_has_scope(dir)) {
		lsp_schedule_phpstan_project_analyzer(server, dir);
		lsp_schedule_psalm_project_analyzer(server, dir);
		lsp_psalm_ls_schedule_project(server, dir);
	}

	zend_string_release(composer);

	handle = lsp_dir_open(dir);
	if (!handle) {
		return;
	}

	while ((entry_name = lsp_dir_read(handle)) != NULL) {
		if (lsp_scan_should_skip_dir_name(entry_name)) {
			continue;
		}

		child = lsp_join_path2(dir, entry_name);
		if (VCWD_STAT(ZSTR_VAL(child), &st) != 0) {
			zend_string_release(child);
			continue;
		}

		if (S_ISDIR(st.st_mode)) {
			lsp_schedule_workspace_analyzer_projects(server, child, depth + 1);
		}

		zend_string_release(child);
	}

	lsp_dir_close(handle);
}



extern void lsp_analyzer_project_finished(lsp_server *server, const char *analyzer, zend_string *project_root, zend_string *output_file)
{
	zend_long state;
	zend_string *output, *message;
	bool output_is_json;

	state = lsp_analyzer_project_state_value(server, analyzer, project_root);
	if (state == LSP_ANALYZER_PROJECT_PENDING) {
		if (output_file) {
			VCWD_UNLINK(ZSTR_VAL(output_file));
		}
	} else {
		output = output_file && lsp_is_regular_file(output_file) ? lsp_read_file(output_file) : zend_empty_string;
		output_is_json = output != zend_empty_string && lsp_analyzer_output_is_json(analyzer, output);
		lsp_commit_analyzer_project_output(analyzer, project_root, output_file);

		if (output_is_json) {
			lsp_analyzer_project_state(server, analyzer, project_root, LSP_ANALYZER_PROJECT_READY);
		} else {
			lsp_analyzer_project_state(server, analyzer, project_root, LSP_ANALYZER_PROJECT_ERROR);
			message = lsp_analyzer_failure_message(analyzer, output);
			lsp_analyzer_project_status(analyzer, "error", ZSTR_VAL(message), project_root);
			zend_string_release(message);
		}

		if (output != zend_empty_string) {
			zend_string_release(output);
		}

		lsp_publish_project_document_diagnostics(server, project_root);
	}

	if (strcmp(analyzer, "phpstan") == 0) {
		lsp_start_pending_phpstan_project_analyzer(server);
	} else {
		lsp_start_pending_psalm_project_analyzer(server);
	}
}

extern void lsp_schedule_workspace_analyzers(lsp_server *server)
{
	lsp_schedule_workspace_analyzer_projects(server, server->root, 0);
}

extern void lsp_schedule_project_analyzers(lsp_server *server, lsp_document *document)
{
	zend_string *project_root;

	project_root = lsp_document_project_root(server, document);
	if (lsp_document_is_in_analyzer_scope(document, project_root)) {
		lsp_schedule_phpstan_project_analyzer(server, project_root);
		lsp_schedule_psalm_project_analyzer(server, project_root);
		lsp_psalm_ls_schedule_project(server, project_root);
	}
	zend_string_release(project_root);
}

extern void lsp_reschedule_project_analyzers(lsp_server *server, lsp_document *document)
{
	zend_string *project_root;

	project_root = lsp_document_project_root(server, document);
	if (lsp_document_is_in_analyzer_scope(document, project_root)) {
		lsp_reschedule_phpstan_project_analyzer(server, project_root);
		lsp_reschedule_psalm_project_analyzer(server, project_root);
	}
	zend_string_release(project_root);
}

extern void lsp_publish_document_diagnostics(lsp_server *server, lsp_document *document)
{
	zval params, diagnostics, item_copy, *lsparrot_diagnostics, *lsparrot_item;

	lsp_schedule_project_analyzers(server, document);
	lsparrot_diagnostics = zend_hash_str_find(Z_ARRVAL(document->lsparrot), "diagnostics", sizeof("diagnostics") - 1);

	array_init(&params);
	add_assoc_str(&params, "uri", zend_string_copy(document->uri));
	array_init(&diagnostics);

	if (lsparrot_diagnostics && Z_TYPE_P(lsparrot_diagnostics) == IS_ARRAY) {
		ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(lsparrot_diagnostics), lsparrot_item) {
			ZVAL_COPY(&item_copy, lsparrot_item);
			add_next_index_zval(&diagnostics, &item_copy);
		} ZEND_HASH_FOREACH_END();
	}

	if (!lsparrot_diagnostics || Z_TYPE_P(lsparrot_diagnostics) != IS_ARRAY || zend_hash_num_elements(Z_ARRVAL_P(lsparrot_diagnostics)) == 0) {
		lsp_append_phpstan_cached_diagnostics(server, document, &diagnostics);
		lsp_append_psalm_cached_diagnostics(server, document, &diagnostics);
		lsp_psalm_ls_append_diagnostics(server, document, &diagnostics);
	}

	add_assoc_zval(&params, "diagnostics", &diagnostics);
	add_assoc_long(&params, "version", document->version);
	lsp_protocol_notify("textDocument/publishDiagnostics", &params);
	zval_ptr_dtor(&params);
}
