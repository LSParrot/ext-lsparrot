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

static inline void lsp_phpstan_add_configured_level(lsp_command *command, zend_string *project_config, zend_long level)
{
	zend_string *argument;

	if (project_config) {
		return;
	}

	if (level < 0) {
		level = 6;
	}

	argument = strpprintf(0, "--level=" ZEND_LONG_FMT, level);
	lsp_command_add_zstr(command, argument);

	zend_string_release(argument);
}

static inline bool lsp_start_phpstan_project_analyzer(lsp_server *server, zend_string *project_root)
{
	lsp_command command;
	zend_string *config, *generated_config, *output_file;
	bool started;

	if (!lsp_analyzer_project_has_scope(project_root)) {
		return false;
	}

	lsp_command_init(&command);
	if (!lsp_tool_command(server, project_root, "phpstan", &command)) {
		lsp_command_destroy(&command);

		return false;
	}

	output_file = lsp_analyzer_project_output_file(project_root, "phpstan");
	lsp_command_add(&command, "analyse");
	config = lsp_phpstan_config_file(project_root);
	generated_config = lsp_phpstan_lsp_config_file(project_root, config, lsp_analyzer_parallel_workers(server));

	if (!config) {
		lsp_command_add_composer_analysis_paths(&command, project_root);
	}

	lsp_command_add(&command, "--error-format=json");
	lsp_command_add(&command, "--no-progress");
	lsp_analyzer_add_memory_limit(&command, server);
	lsp_phpstan_add_configured_level(&command, config, lsp_project_phpstan_level(server, project_root));

	if (generated_config) {
		lsp_command_add(&command, "-c");
		lsp_command_add_zstr(&command, generated_config);
		zend_string_release(generated_config);
	} else if (config) {
		lsp_command_add(&command, "-c");
		lsp_command_add_zstr(&command, config);
	}

	if (config) {
		zend_string_release(config);
	}

	started = lsp_start_analyzer_project_job(server, "phpstan", project_root, &command, output_file);
	if (started) {
		lsp_analyzer_project_status("phpstan", "running", "Prewarming PHPStan project diagnostics.", project_root);
	}

	zend_string_release(output_file);
	lsp_command_destroy(&command);

	return started;
}

extern void lsp_start_pending_phpstan_project_analyzer(lsp_server *server)
{
	zend_string *project_root = NULL, *candidate = NULL;
	zval *state_zv;
	bool started = false;

	if (server->phpstan_job.running) {
		return;
	}

	ZEND_HASH_FOREACH_STR_KEY_VAL(&server->phpstan_projects, project_root, state_zv) {
		if (project_root && Z_TYPE_P(state_zv) == IS_LONG && Z_LVAL_P(state_zv) == LSP_ANALYZER_PROJECT_PENDING) {
			candidate = zend_string_copy(project_root);
			break;
		}
	} ZEND_HASH_FOREACH_END();

	if (!candidate) {
		return;
	}

	started = lsp_start_phpstan_project_analyzer(server, candidate);
	if (!started) {
		zend_hash_del(&server->phpstan_projects, candidate);
	}

	zend_string_release(candidate);
}

extern void lsp_schedule_phpstan_project_analyzer(lsp_server *server, zend_string *project_root)
{
	if (!server->phpstan_enabled || lsp_analyzer_project_has_state(server, "phpstan", project_root)) {
		return;
	}

	if (!lsp_analyzer_project_has_scope(project_root)) {
		return;
	}

	if (!lsp_tool_available("phpstan", project_root)) {
		return;
	}

	lsp_analyzer_project_state(server, "phpstan", project_root, LSP_ANALYZER_PROJECT_PENDING);
	lsp_start_pending_phpstan_project_analyzer(server);
}

static inline bool lsp_phpstan_type_expression_safe(zend_string *expression)
{
	const char *value;
	size_t i;

	if (!expression || ZSTR_LEN(expression) == 0) {
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

static inline void lsp_phpstan_dump_insert_location(zend_string *text, size_t offset, size_t *line_start, size_t *line_end, zend_long *one_based_line)
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

static inline zend_string *lsp_phpstan_type_shadow_file(zend_string *project_root, lsp_document *document, zend_string *expression, size_t offset, zend_long *dump_line)
{
	FILE *handle;
	const char *value;
	zend_string *cache_dir, *file;
	smart_str contents = {0};
	size_t line_start, line_end, text_length;

	value = ZSTR_VAL(document->text);
	text_length = ZSTR_LEN(document->text);
	lsp_phpstan_dump_insert_location(document->text, offset, &line_start, &line_end, dump_line);

	cache_dir = strpprintf(0, "%s/.lsparrot/shadow/phpstan-type", ZSTR_VAL(project_root));
	lsp_mkdir_p(cache_dir);
	file = strpprintf(0, "%s/%ld-" ZEND_LONG_FMT "-%s", ZSTR_VAL(cache_dir), (long) lsp_current_process_id(), document->version, lsp_path_basename(document->path));
	zend_string_release(cache_dir);

	smart_str_appendl(&contents, value, line_start);
	smart_str_appendl(&contents, "\\PHPStan\\dumpType(", sizeof("\\PHPStan\\dumpType(") - 1);
	smart_str_append(&contents, expression);
	smart_str_appendl(&contents, ");\n", sizeof(");\n") - 1);
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

static inline zend_string *lsp_phpstan_dump_message_type(zval *message_zv, zend_long dump_line)
{
	const char *prefix, *message;
	zend_string *type;
	zval *line_zv, *message_text_zv, *identifier_zv;
	size_t prefix_length;

	prefix = "Dumped type: ";
	prefix_length = sizeof("Dumped type: ") - 1;

	if (Z_TYPE_P(message_zv) != IS_ARRAY) {
		return NULL;
	}

	identifier_zv = zend_hash_str_find(Z_ARRVAL_P(message_zv), "identifier", sizeof("identifier") - 1);
	if (identifier_zv && Z_TYPE_P(identifier_zv) == IS_STRING && !zend_string_equals_literal(Z_STR_P(identifier_zv), "phpstan.dumpType")) {
		return NULL;
	}

	message_text_zv = zend_hash_str_find(Z_ARRVAL_P(message_zv), "message", sizeof("message") - 1);
	if (!message_text_zv || Z_TYPE_P(message_text_zv) != IS_STRING || Z_STRLEN_P(message_text_zv) <= prefix_length) {
		return NULL;
	}

	message = Z_STRVAL_P(message_text_zv);
	if (memcmp(message, prefix, prefix_length) != 0) {
		return NULL;
	}

	line_zv = zend_hash_str_find(Z_ARRVAL_P(message_zv), "line", sizeof("line") - 1);
	if (line_zv && Z_TYPE_P(line_zv) == IS_LONG && Z_LVAL_P(line_zv) != dump_line) {
		return NULL;
	}

	type = zend_string_init(message + prefix_length, Z_STRLEN_P(message_text_zv) - prefix_length, 0);
	if (lsp_type_is_unhelpful(type)) {
		zend_string_release(type);

		return NULL;
	}

	return type;
}

static inline zend_string *lsp_phpstan_dump_type_from_messages(zval *messages, zend_long dump_line)
{
	zend_string *fallback, *type;
	zval *message_zv, *line_zv;

	if (!messages || Z_TYPE_P(messages) != IS_ARRAY) {
		return NULL;
	}

	fallback = NULL;
	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(messages), message_zv) {
		type = lsp_phpstan_dump_message_type(message_zv, dump_line);
		if (!type) {
			continue;
		}

		line_zv = zend_hash_str_find(Z_ARRVAL_P(message_zv), "line", sizeof("line") - 1);
		if (!line_zv || Z_TYPE_P(line_zv) != IS_LONG || Z_LVAL_P(line_zv) == dump_line) {
			if (fallback) {
				zend_string_release(fallback);
			}

			return type;
		}

		if (!fallback) {
			fallback = type;
		} else {
			zend_string_release(type);
		}
	} ZEND_HASH_FOREACH_END();

	return fallback;
}

static inline zend_string *lsp_phpstan_dump_type_from_decoded(zval *decoded, zend_string *analysis_file, zend_long dump_line)
{
	zend_string *type;
	zval *files, *file_result, *messages, *other_file_result;

	files = zend_hash_str_find(Z_ARRVAL_P(decoded), "files", sizeof("files") - 1);
	if (!files || Z_TYPE_P(files) != IS_ARRAY) {
		return NULL;
	}

	file_result = zend_hash_find(Z_ARRVAL_P(files), analysis_file);
	messages = file_result && Z_TYPE_P(file_result) == IS_ARRAY ? zend_hash_str_find(Z_ARRVAL_P(file_result), "messages", sizeof("messages") - 1) : NULL;
	type = lsp_phpstan_dump_type_from_messages(messages, dump_line);
	if (type) {
		return type;
	}

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(files), other_file_result) {
		if (Z_TYPE_P(other_file_result) != IS_ARRAY) {
			continue;
		}

		messages = zend_hash_str_find(Z_ARRVAL_P(other_file_result), "messages", sizeof("messages") - 1);
		type = lsp_phpstan_dump_type_from_messages(messages, dump_line);
		if (type) {
			return type;
		}
	} ZEND_HASH_FOREACH_END();

	return NULL;
}

extern zend_string *lsp_phpstan_type_for_expression(lsp_server *server, lsp_document *document, zend_string *expression, size_t offset)
{
	lsp_command command;
	zend_long dump_line;
	zend_string *project_root, *analysis_file, *config, *generated_config, *output, *json, *type, *cache_key;
	zval decoded, *cached, cache_value;

	if (!server->phpstan_enabled || !lsp_phpstan_type_expression_safe(expression)) {
		return NULL;
	}

	cache_key = lsp_type_cache_key("phpstan", document, expression, offset);
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

	lsp_command_init(&command);
	if (!lsp_tool_command(server, project_root, "phpstan", &command)) {
		lsp_command_destroy(&command);
		zend_string_release(project_root);
		ZVAL_FALSE(&cache_value);
		zend_hash_update(&server->type_cache, cache_key, &cache_value);
		zend_string_release(cache_key);

		return NULL;
	}

	analysis_file = lsp_phpstan_type_shadow_file(project_root, document, expression, offset, &dump_line);
	lsp_command_add(&command, "analyse");
	lsp_command_add_zstr(&command, analysis_file);
	lsp_command_add(&command, "--error-format=json");
	lsp_command_add(&command, "--no-progress");
	config = lsp_phpstan_config_file(project_root);
	generated_config = lsp_phpstan_lsp_config_file(project_root, config, lsp_analyzer_parallel_workers(server));
	lsp_analyzer_add_memory_limit(&command, server);
	lsp_phpstan_add_configured_level(&command, config, lsp_project_phpstan_level(server, project_root));

	if (generated_config) {
		lsp_command_add(&command, "-c");
		lsp_command_add_zstr(&command, generated_config);
		zend_string_release(generated_config);
	} else if (config) {
		lsp_command_add(&command, "-c");
		lsp_command_add_zstr(&command, config);
	}

	if (config) {
		zend_string_release(config);
	}

	output = lsp_runner_run_capture(server, "phpstan", project_root, &command, project_root, server->options.analyzer_diagnostics_timeout);
	lsp_command_destroy(&command);
	json = lsp_json_slice_from(output, '{');
	ZVAL_UNDEF(&decoded);
	php_json_decode_ex(&decoded, ZSTR_VAL(json), ZSTR_LEN(json), PHP_JSON_OBJECT_AS_ARRAY, 512);
	zend_string_release(json);

	type = NULL;
	if (Z_TYPE(decoded) == IS_ARRAY) {
		type = lsp_phpstan_dump_type_from_decoded(&decoded, analysis_file, dump_line);
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

extern void lsp_append_phpstan_cached_diagnostics(lsp_server *server, lsp_document *document, zval *diagnostics)
{
	zend_long line;
	zend_string *project_root, *output_file, *output, *json, *failure_message;
	zval decoded, *files, *file_result, *messages, *message_zv,
		*line_zv, *message_text_zv, *identifier_zv, range
	;

	if (!server->phpstan_enabled) {
		return;
	}

	project_root = lsp_document_project_root(server, document);
	if (!lsp_document_is_in_analyzer_scope(document, project_root)) {
		zend_string_release(project_root);

		return;
	}

	output_file = lsp_analyzer_project_output_file(project_root, "phpstan");
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

	json = lsp_json_slice_from(output, '{');

	ZVAL_UNDEF(&decoded);
	php_json_decode_ex(&decoded, ZSTR_VAL(json), ZSTR_LEN(json), PHP_JSON_OBJECT_AS_ARRAY, 512);
	zend_string_release(json);
	if (Z_TYPE(decoded) != IS_ARRAY) {
		if (!Z_ISUNDEF(decoded)) {
			zval_ptr_dtor(&decoded);
		}

		failure_message = lsp_analyzer_failure_message("phpstan", output);
		lsp_line_range(&range, document->text, 1);
		lsp_add_analyzer_diagnostic(diagnostics, "phpstan", failure_message, NULL, &range, 1);
		zend_string_release(failure_message);
		zend_string_release(output);

		return;
	}
	zend_string_release(output);

	files = zend_hash_str_find(Z_ARRVAL(decoded), "files", sizeof("files") - 1);
	file_result = files && Z_TYPE_P(files) == IS_ARRAY ? zend_hash_find(Z_ARRVAL_P(files), document->path) : NULL;

	messages = file_result && Z_TYPE_P(file_result) == IS_ARRAY ? zend_hash_str_find(Z_ARRVAL_P(file_result), "messages", sizeof("messages") - 1) : NULL;
	if (messages && Z_TYPE_P(messages) == IS_ARRAY) {
		ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(messages), message_zv) {
			if (Z_TYPE_P(message_zv) != IS_ARRAY) {
				continue;
			}

			line_zv = zend_hash_str_find(Z_ARRVAL_P(message_zv), "line", sizeof("line") - 1);
			message_text_zv = zend_hash_str_find(Z_ARRVAL_P(message_zv), "message", sizeof("message") - 1);
			identifier_zv = zend_hash_str_find(Z_ARRVAL_P(message_zv), "identifier", sizeof("identifier") - 1);

			if (!message_text_zv || Z_TYPE_P(message_text_zv) != IS_STRING) {
				continue;
			}

			line = line_zv && Z_TYPE_P(line_zv) == IS_LONG ? Z_LVAL_P(line_zv) : 1;
			lsp_line_range(&range, document->text, line);
			lsp_add_analyzer_diagnostic(diagnostics, "phpstan", Z_STR_P(message_text_zv),
				identifier_zv && Z_TYPE_P(identifier_zv) == IS_STRING ? Z_STR_P(identifier_zv) : NULL, &range, 2
			);
		} ZEND_HASH_FOREACH_END();
	}

	zval_ptr_dtor(&decoded);
}

extern void lsp_reschedule_phpstan_project_analyzer(lsp_server *server, zend_string *project_root)
{
	zend_long state;

	if (!server->phpstan_enabled || !lsp_tool_available("phpstan", project_root)) {
		return;
	}

	if (!lsp_analyzer_project_has_scope(project_root)) {
		return;
	}

	state = lsp_analyzer_project_state_value(server, "phpstan", project_root);
	if (state == LSP_ANALYZER_PROJECT_RUNNING) {
		lsp_analyzer_project_state(server, "phpstan", project_root, LSP_ANALYZER_PROJECT_PENDING);

		return;
	}

	zend_hash_del(&server->phpstan_projects, project_root);
	lsp_schedule_phpstan_project_analyzer(server, project_root);
}
