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

static inline void lsp_psalm_add_parallel_args(lsp_command *command, zend_long workers)
{
	zend_string *threads_arg, *scan_threads_arg;

	if (workers < 2) {
		return;
	}

	threads_arg = strpprintf(0, "--threads=" ZEND_LONG_FMT, workers);
	scan_threads_arg = strpprintf(0, "--scan-threads=" ZEND_LONG_FMT, workers);
	lsp_command_add_zstr(command, threads_arg);
	lsp_command_add_zstr(command, scan_threads_arg);
	zend_string_release(scan_threads_arg);
	zend_string_release(threads_arg);
}

static inline void lsp_psalm_add_configured_level(lsp_command *command, zend_string *project_config, zend_long level)
{
	zend_string *argument;

	if (project_config) {
		return;
	}

	if (level < 1) {
		level = 6;
	}

	argument = strpprintf(0, "--error-level=" ZEND_LONG_FMT, level);
	lsp_command_add_zstr(command, argument);

	zend_string_release(argument);
}

static inline bool lsp_psalm_type_expression_safe(zend_string *expression)
{
	const char *value;
	size_t i;

	if (!expression || ZSTR_LEN(expression) == 0 || ZSTR_VAL(expression)[0] != '$') {
		return false;
	}

	value = ZSTR_VAL(expression);
	for (i = 0; i < ZSTR_LEN(expression); i++) {
		if (value[i] == '\r' || value[i] == '\n' || value[i] == ';') {
			return false;
		}
	}

	return true;
}

static inline bool lsp_psalm_trace_is_lhs_assignment(zend_string *text, zend_string *expression, size_t offset)
{
	const char *value;
	size_t length, line_start, line_end, word_start, word_end, i;

	if (!text || !expression || ZSTR_LEN(expression) == 0) {
		return false;
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

	word_start = offset;
	while (word_start > line_start && lsp_doc_is_identifier_char(value[word_start - 1])) {
		word_start--;
	}
	if (word_start > line_start && value[word_start - 1] == '$') {
		word_start--;
	}

	word_end = offset;
	while (word_end < line_end && lsp_doc_is_identifier_char(value[word_end])) {
		word_end++;
	}

	if (word_end <= word_start || word_end - word_start != ZSTR_LEN(expression) || memcmp(value + word_start, ZSTR_VAL(expression), ZSTR_LEN(expression)) != 0) {
		return false;
	}

	i = word_end;
	while (i < line_end && isspace((unsigned char) value[i])) {
		i++;
	}

	if (i >= line_end || value[i] != '=') {
		return false;
	}

	if ((i + 1 < line_end && (value[i + 1] == '=' || value[i + 1] == '>')) || (i > line_start && (value[i - 1] == '!' || value[i - 1] == '<' || value[i - 1] == '>'))) {
		return false;
	}

	return true;
}

static inline void lsp_psalm_trace_insert_location(zend_string *text, size_t offset, size_t *line_start, size_t *line_end, zend_long *one_based_line)
{
	const char *value;
	size_t i, limit;

	value = ZSTR_VAL(text);
	limit = offset > ZSTR_LEN(text) ? ZSTR_LEN(text) : offset;
	*line_start = limit;
	while (*line_start > 0 && value[*line_start - 1] != '\n') {
		(*line_start)--;
	}

	*line_end = limit;
	while (*line_end < ZSTR_LEN(text) && value[*line_end] != '\r' && value[*line_end] != '\n') {
		(*line_end)++;
	}

	*one_based_line = 1;
	for (i = 0; i < *line_start; i++) {
		if (value[i] == '\n') {
			(*one_based_line)++;
		}
	}
}

static inline zend_string *lsp_psalm_type_shadow_file(zend_string *project_root, lsp_document *document, zend_string *expression, size_t offset, zend_long *trace_line)
{
	FILE *handle;
	const char *value;
	zend_string *cache_dir, *file;
	smart_str contents = {0};
	size_t line_start, line_end, text_length;
	zend_long one_based_line;
	bool keep_assignment_line;

	value = ZSTR_VAL(document->text);
	text_length = ZSTR_LEN(document->text);
	lsp_psalm_trace_insert_location(document->text, offset, &line_start, &line_end, &one_based_line);
	keep_assignment_line = lsp_psalm_trace_is_lhs_assignment(document->text, expression, offset);

	cache_dir = strpprintf(0, "%s/.lsparrot/shadow/psalm-type", ZSTR_VAL(project_root));
	lsp_mkdir_p(cache_dir);
	file = strpprintf(0, "%s/%ld-" ZEND_LONG_FMT "-%s", ZSTR_VAL(cache_dir), (long) lsp_current_process_id(), document->version, lsp_path_basename(document->path));
	zend_string_release(cache_dir);

	smart_str_appendl(&contents, value, line_start);
	if (keep_assignment_line) {
		smart_str_appendl(&contents, value + line_start, line_end - line_start);
		smart_str_appendc(&contents, '\n');
		*trace_line = one_based_line + 2;
	} else {
		*trace_line = one_based_line + 1;
	}
	smart_str_appendl(&contents, "/** @psalm-trace ", sizeof("/** @psalm-trace ") - 1);
	smart_str_append(&contents, expression);
	smart_str_appendl(&contents, " */\n", sizeof(" */\n") - 1);
	smart_str_append(&contents, expression);
	smart_str_appendl(&contents, ";\n", sizeof(";\n") - 1);
	smart_str_appendl(&contents, value + line_end, text_length - line_end);
	smart_str_0(&contents);

	handle = fopen(ZSTR_VAL(file), "wb");
	if (handle) {
		fwrite(ZSTR_VAL(contents.s), 1, ZSTR_LEN(contents.s), handle);
		fclose(handle);
	}

	smart_str_free(&contents);

	return file;
}

static inline zend_string *lsp_psalm_trace_issue_type(zval *issue_zv, zend_string *expression, zend_long trace_line)
{
	const char *message, *type_start;
	zend_string *type;
	zval *line_zv, *message_zv, *type_zv;
	size_t expression_length, message_length, type_length;

	if (Z_TYPE_P(issue_zv) != IS_ARRAY) {
		return NULL;
	}

	type_zv = zend_hash_str_find(Z_ARRVAL_P(issue_zv), "type", sizeof("type") - 1);
	if (!type_zv || Z_TYPE_P(type_zv) != IS_STRING || !zend_string_equals_literal(Z_STR_P(type_zv), "Trace")) {
		return NULL;
	}

	line_zv = zend_hash_str_find(Z_ARRVAL_P(issue_zv), "line_from", sizeof("line_from") - 1);
	if (line_zv && Z_TYPE_P(line_zv) == IS_LONG && Z_LVAL_P(line_zv) != trace_line) {
		return NULL;
	}

	message_zv = zend_hash_str_find(Z_ARRVAL_P(issue_zv), "message", sizeof("message") - 1);
	if (!message_zv || Z_TYPE_P(message_zv) != IS_STRING) {
		return NULL;
	}

	message = Z_STRVAL_P(message_zv);
	message_length = Z_STRLEN_P(message_zv);
	expression_length = ZSTR_LEN(expression);
	if (message_length <= expression_length + 2 || memcmp(message, ZSTR_VAL(expression), expression_length) != 0 || message[expression_length] != ':' || message[expression_length + 1] != ' ') {
		return NULL;
	}

	type_start = message + expression_length + 2;
	type_length = message_length - expression_length - 2;
	type = zend_string_init(type_start, type_length, 0);
	if (lsp_type_is_unhelpful(type)) {
		zend_string_release(type);

		return NULL;
	}

	return type;
}

static inline zend_string *lsp_psalm_trace_type_from_decoded(zval *decoded, zend_string *expression, zend_long trace_line)
{
	zend_string *type;
	zval *issue_zv;

	if (Z_TYPE_P(decoded) != IS_ARRAY) {
		return NULL;
	}

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(decoded), issue_zv) {
		type = lsp_psalm_trace_issue_type(issue_zv, expression, trace_line);
		if (type) {
			return type;
		}
	} ZEND_HASH_FOREACH_END();

	return NULL;
}

extern zend_string *lsp_psalm_type_for_expression(lsp_server *server, lsp_document *document, zend_string *expression, size_t offset)
{
	lsp_command command;
	zend_long trace_line, level;
	zend_string *project_root, *analysis_file, *config, *generated_config, *output, *json, *type, *cache_key, *root_arg, *config_arg;
	zval decoded, *cached, cache_value;

	if (!server->psalm_enabled || !lsp_psalm_type_expression_safe(expression)) {
		return NULL;
	}

	cache_key = lsp_type_cache_key("psalm", document, expression, offset);
	cached = zend_hash_find(&server->type_cache, cache_key);
	if (cached) {
		if (Z_TYPE_P(cached) == IS_STRING) {
			type = zend_string_copy(Z_STR_P(cached));
			zend_string_release(cache_key);

			return type;
		}

		zend_string_release(cache_key);

		return NULL;
	}

	project_root = lsp_document_project_root(server, document);
	if (!lsp_document_is_in_analyzer_scope(document, project_root)) {
		zend_string_release(project_root);
		ZVAL_FALSE(&cache_value);
		zend_hash_update(&server->type_cache, cache_key, &cache_value);
		zend_string_release(cache_key);

		return NULL;
	}

	config = lsp_psalm_config_file(project_root);
	level = lsp_project_psalm_level(server, project_root);
	generated_config = lsp_psalm_type_config_file(project_root, config, level, server->options.psalm_live_dead_code_diagnostics);
	if (!generated_config && !config) {
		if (config) {
			zend_string_release(config);
		}
		zend_string_release(project_root);
		ZVAL_FALSE(&cache_value);
		zend_hash_update(&server->type_cache, cache_key, &cache_value);
		zend_string_release(cache_key);

		return NULL;
	}

	lsp_command_init(&command);
	if (!lsp_tool_command(server, project_root, "psalm", &command)) {
		lsp_command_destroy(&command);
		if (generated_config) {
			zend_string_release(generated_config);
		}
		if (config) {
			zend_string_release(config);
		}
		zend_string_release(project_root);
		ZVAL_FALSE(&cache_value);
		zend_hash_update(&server->type_cache, cache_key, &cache_value);
		zend_string_release(cache_key);

		return NULL;
	}

	analysis_file = lsp_psalm_type_shadow_file(project_root, document, expression, offset, &trace_line);
	root_arg = strpprintf(0, "--root=%s", ZSTR_VAL(project_root));
	config_arg = strpprintf(0, "--config=%s", ZSTR_VAL(generated_config ? generated_config : config));
	lsp_command_add(&command, "--output-format=json");
	lsp_command_add(&command, "--no-progress");
	lsp_command_add(&command, "--show-info=true");
	lsp_analyzer_add_memory_limit(&command, server);
	lsp_psalm_add_parallel_args(&command, lsp_analyzer_parallel_workers(server));
	lsp_psalm_add_configured_level(&command, config, level);
	lsp_command_add_zstr(&command, root_arg);
	lsp_command_add_zstr(&command, config_arg);
	lsp_command_add_zstr(&command, analysis_file);
	zend_string_release(root_arg);
	zend_string_release(config_arg);
	if (generated_config) {
		zend_string_release(generated_config);
	}
	if (config) {
		zend_string_release(config);
	}

	output = lsp_runner_run_capture(server, "psalm", project_root, &command, project_root, server->options.analyzer_diagnostics_timeout);
	lsp_command_destroy(&command);
	json = lsp_json_slice_from(output, '[');
	ZVAL_UNDEF(&decoded);
	php_json_decode_ex(&decoded, ZSTR_VAL(json), ZSTR_LEN(json), PHP_JSON_OBJECT_AS_ARRAY, 512);
	zend_string_release(json);

	type = NULL;
	if (Z_TYPE(decoded) == IS_ARRAY) {
		type = lsp_psalm_trace_type_from_decoded(&decoded, expression, trace_line);
	}

	if (!Z_ISUNDEF(decoded)) {
		zval_ptr_dtor(&decoded);
	}

	if (output != zend_empty_string) {
		zend_string_release(output);
	}

	zend_string_release(analysis_file);
	zend_string_release(project_root);
	if (lsp_type_is_unhelpful(type)) {
		if (type) {
			zend_string_release(type);
		}
		type = NULL;
	}

	if (type) {
		ZVAL_STR_COPY(&cache_value, type);
	} else {
		ZVAL_FALSE(&cache_value);
	}
	zend_hash_update(&server->type_cache, cache_key, &cache_value);
	zend_string_release(cache_key);

	return type;
}

static inline bool lsp_start_psalm_project_analyzer(lsp_server *server, zend_string *project_root)
{
	lsp_command command;
	zend_string *config, *generated_config, *output_file, *root_arg, *config_arg;
	zend_long level;
	bool started;

	config = lsp_psalm_config_file(project_root);
	level = lsp_project_psalm_level(server, project_root);

	if (!lsp_analyzer_project_has_scope(project_root)) {
		if (config) {
			zend_string_release(config);
		}

		return false;
	}

	lsp_command_init(&command);
	if (!lsp_tool_command(server, project_root, "psalm", &command)) {
		lsp_command_destroy(&command);
		if (config) {
			zend_string_release(config);
		}

		return false;
	}

	output_file = lsp_analyzer_project_output_file(project_root, "psalm");
	root_arg = strpprintf(0, "--root=%s", ZSTR_VAL(project_root));

	generated_config = lsp_psalm_ls_config_file(project_root, config, level, server->options.psalm_live_dead_code_diagnostics);
	if (!generated_config && !config) {
		zend_string_release(root_arg);
		zend_string_release(output_file);
		lsp_command_destroy(&command);

		return false;
	}

	config_arg = strpprintf(0, "--config=%s", ZSTR_VAL(generated_config ? generated_config : config));
	lsp_command_add(&command, "--output-format=json");
	lsp_command_add(&command, "--no-progress");
	lsp_command_add(&command, server->options.psalm_show_info ? "--show-info=true" : "--show-info=false");
	lsp_analyzer_add_memory_limit(&command, server);
	lsp_psalm_add_parallel_args(&command, lsp_analyzer_parallel_workers(server));
	lsp_psalm_add_configured_level(&command, config, level);
	lsp_command_add_zstr(&command, root_arg);
	lsp_command_add_zstr(&command, config_arg);
	zend_string_release(root_arg);
	zend_string_release(config_arg);

	if (generated_config) {
		zend_string_release(generated_config);
	}

	if (config) {
		zend_string_release(config);
	}

	started = lsp_start_analyzer_project_job(server, "psalm", project_root, &command, output_file);
	if (started) {
		lsp_analyzer_project_status("psalm", "running", "Prewarming Psalm project diagnostics.", project_root);
	}

	zend_string_release(output_file);
	lsp_command_destroy(&command);

	return started;
}

extern void lsp_start_pending_psalm_project_analyzer(lsp_server *server)
{
	zend_string *project_root = NULL, *candidate = NULL;
	zval *state_zv;
	bool started = false;

	if (server->psalm_job.running) {
		return;
	}

	ZEND_HASH_FOREACH_STR_KEY_VAL(&server->psalm_projects, project_root, state_zv) {
		if (project_root && Z_TYPE_P(state_zv) == IS_LONG && Z_LVAL_P(state_zv) == LSP_ANALYZER_PROJECT_PENDING) {
			candidate = zend_string_copy(project_root);
			break;
		}
	} ZEND_HASH_FOREACH_END();

	if (!candidate) {
		return;
	}

	started = lsp_start_psalm_project_analyzer(server, candidate);
	if (!started) {
		zend_hash_del(&server->psalm_projects, candidate);
	}

	zend_string_release(candidate);
}

extern void lsp_schedule_psalm_project_analyzer(lsp_server *server, zend_string *project_root)
{
	if (!server->psalm_enabled || lsp_analyzer_project_has_state(server, "psalm", project_root)) {
		return;
	}

	if (!lsp_analyzer_project_has_scope(project_root)) {
		return;
	}

	if (!lsp_tool_available("psalm", project_root)) {
		return;
	}

	lsp_analyzer_project_state(server, "psalm", project_root, LSP_ANALYZER_PROJECT_PENDING);
	lsp_start_pending_psalm_project_analyzer(server);
}

extern void lsp_append_psalm_cached_diagnostics(lsp_server *server, lsp_document *document, zval *diagnostics)
{
	zend_long line, severity;
	zend_string *project_root, *output_file, *output, *json, *failure_message;
	zval decoded, *file_zv, *issue_zv, *line_zv, *message_text_zv, *type_zv, *severity_zv, range;

	if (!server->psalm_enabled) {
		return;
	}

	project_root = lsp_document_project_root(server, document);
	if (!lsp_document_is_in_analyzer_scope(document, project_root)) {
		zend_string_release(project_root);

		return;
	}

	output_file = lsp_analyzer_project_output_file(project_root, "psalm");
	if (!lsp_is_regular_file(output_file)) {
		zend_string_release(output_file);
		zend_string_release(project_root);

		return;
	}

	output = lsp_read_file(output_file);
	zend_string_release(output_file);
	zend_string_release(project_root);
	if (output == zend_empty_string) {
		return;
	}

	json = lsp_json_slice_from(output, '[');

	ZVAL_UNDEF(&decoded);
	php_json_decode_ex(&decoded, ZSTR_VAL(json), ZSTR_LEN(json), PHP_JSON_OBJECT_AS_ARRAY, 512);
	zend_string_release(json);
	if (Z_TYPE(decoded) != IS_ARRAY) {
		if (!Z_ISUNDEF(decoded)) {
			zval_ptr_dtor(&decoded);
		}

		failure_message = lsp_analyzer_failure_message("psalm", output);
		lsp_line_range(&range, document->text, 1);
		lsp_add_analyzer_diagnostic(diagnostics, "psalm", failure_message, NULL, &range, 1);
		zend_string_release(failure_message);
		zend_string_release(output);

		return;
	}
	zend_string_release(output);

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL(decoded), issue_zv) {
		severity = 2;
		if (Z_TYPE_P(issue_zv) != IS_ARRAY) {
			continue;
		}

		file_zv = zend_hash_str_find(Z_ARRVAL_P(issue_zv), "file_path", sizeof("file_path") - 1);
		if (!file_zv || Z_TYPE_P(file_zv) != IS_STRING || !zend_string_equals(Z_STR_P(file_zv), document->path)) {
			continue;
		}

		message_text_zv = zend_hash_str_find(Z_ARRVAL_P(issue_zv), "message", sizeof("message") - 1);
		if (!message_text_zv || Z_TYPE_P(message_text_zv) != IS_STRING) {
			continue;
		}

		line_zv = zend_hash_str_find(Z_ARRVAL_P(issue_zv), "line_from", sizeof("line_from") - 1);
		type_zv = zend_hash_str_find(Z_ARRVAL_P(issue_zv), "type", sizeof("type") - 1);

		severity_zv = zend_hash_str_find(Z_ARRVAL_P(issue_zv), "severity", sizeof("severity") - 1);
		if (severity_zv && Z_TYPE_P(severity_zv) == IS_STRING && zend_string_equals_literal(Z_STR_P(severity_zv), "info")) {
			severity = 3;
		}

		line = line_zv && Z_TYPE_P(line_zv) == IS_LONG ? Z_LVAL_P(line_zv) : 1;
		lsp_line_range(&range, document->text, line);
		lsp_add_analyzer_diagnostic(diagnostics, "psalm", Z_STR_P(message_text_zv),
			type_zv && Z_TYPE_P(type_zv) == IS_STRING ? Z_STR_P(type_zv) : NULL, &range, severity
		);
	} ZEND_HASH_FOREACH_END();

	zval_ptr_dtor(&decoded);
}

extern void lsp_reschedule_psalm_project_analyzer(lsp_server *server, zend_string *project_root)
{
	zend_long state;

	if (!server->psalm_enabled) {
		return;
	}

	if (!lsp_analyzer_project_has_scope(project_root)) {
		return;
	}

	if (!lsp_tool_available("psalm", project_root)) {
		return;
	}

	state = lsp_analyzer_project_state_value(server, "psalm", project_root);
	if (state == LSP_ANALYZER_PROJECT_RUNNING) {
		lsp_analyzer_project_state(server, "psalm", project_root, LSP_ANALYZER_PROJECT_PENDING);
		return;
	}

	zend_hash_del(&server->psalm_projects, project_root);
	lsp_schedule_psalm_project_analyzer(server, project_root);
}
