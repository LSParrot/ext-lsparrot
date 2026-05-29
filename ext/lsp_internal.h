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

#ifndef LSP_INTERNAL_H
# define LSP_INTERNAL_H

# include "lsp_portability.h"

# include <Zend/zend_compile.h>
# include <Zend/zend_exceptions.h>
# include <Zend/zend_smart_str.h>
# include <Zend/zend_stream.h>
# include <Zend/zend_virtual_cwd.h>

# include <ext/json/php_json.h>

# include "php_lsparrot.h"

# define LSP_SYMBOL_INDEX_MAGIC 0x5a534958u
# define LSP_SYMBOL_INDEX_PAYLOAD_VERSION 1u
# define LSP_SYMBOL_CLASS 'C'
# define LSP_SYMBOL_INTERFACE 'I'
# define LSP_SYMBOL_TRAIT 'T'
# define LSP_SYMBOL_ENUM 'E'
# define LSP_SYMBOL_FUNCTION 'F'
# define LSP_SYMBOL_CONSTANT 'N'
# define LSP_SYMBOL_TOMBSTONE '!'
# define LSP_ANALYZER_PROJECT_PENDING 1
# define LSP_ANALYZER_PROJECT_RUNNING 2
# define LSP_ANALYZER_PROJECT_READY 3
# define LSP_ANALYZER_PROJECT_ERROR 4

typedef struct _php_ver_abstract {
	const char *(*ast_kind_name)(zend_ast_kind kind);
	bool (*ast_is_decl)(zend_ast *ast);
	bool (*ast_is_opaque_node)(zend_ast_kind kind);
	bool (*string_equals_cstr)(zend_string *value, const char *literal, size_t length);
} php_ver_abstract_t;

extern php_ver_abstract_t php_ver_abstract;

/* Hot array/token accessors are header inlines so the strlen() over literal
 * keys constant-folds and no cross-object call is paid on the request paths. */
static inline zval *lsp_array_find(zval *array, const char *key)
{
	if (!array || Z_TYPE_P(array) != IS_ARRAY) {
		return NULL;
	}

	return zend_hash_str_find(Z_ARRVAL_P(array), key, strlen(key));
}

static inline zend_string *lsp_array_string(zval *array, const char *key)
{
	zval *value = lsp_array_find(array, key);

	return value && Z_TYPE_P(value) == IS_STRING ? Z_STR_P(value) : NULL;
}

static inline zend_long lsp_array_long(zval *array, const char *key, zend_long fallback)
{
	zval *value = lsp_array_find(array, key);

	return value && Z_TYPE_P(value) == IS_LONG ? Z_LVAL_P(value) : fallback;
}

static inline zend_string *lsp_token_string(zval *token, const char *key)
{
	return token && Z_TYPE_P(token) == IS_ARRAY ? lsp_array_string(token, key) : NULL;
}

static inline zend_long lsp_token_long(zval *token, const char *key, zend_long fallback)
{
	return token && Z_TYPE_P(token) == IS_ARRAY ? lsp_array_long(token, key, fallback) : fallback;
}

static inline bool lsp_token_name_equals(zval *token, const char *name)
{
	zend_string *token_name = lsp_token_string(token, "name");

	return token_name && php_ver_abstract.string_equals_cstr(token_name, name, strlen(name));
}

static inline bool lsp_token_text_equals(zval *token, const char *text)
{
	zend_string *token_text = lsp_token_string(token, "text");

	return token_text && php_ver_abstract.string_equals_cstr(token_text, text, strlen(text));
}

typedef struct _lsp_document {
	zend_string *uri;
	zend_string *path;
	zend_string *text;
	zend_long version;
	zval lsparrot;
	/* Derived snapshot data computed lazily once per text revision so request
	 * handlers never re-tokenize or re-compile the document per candidate. */
	zend_string *derived_namespace;
	HashTable *derived_imports;
	size_t derived_import_insert_offset;
	bool derived_import_after_use;
	bool derived_valid;
} lsp_document;

typedef struct _lsp_dir {
	php_stream *stream;
	php_stream_dirent entry;
} lsp_dir;

typedef enum _lsp_method_visibility {
	LSP_METHOD_VISIBILITY_PUBLIC = 0,
	LSP_METHOD_VISIBILITY_PROTECTED,
	LSP_METHOD_VISIBILITY_PRIVATE
} lsp_method_visibility;

typedef enum _lsp_psalm_transport {
	LSP_PSALM_TRANSPORT_AUTO = 0,
	LSP_PSALM_TRANSPORT_CLI,
	LSP_PSALM_TRANSPORT_LANGUAGE_SERVER
} lsp_psalm_transport;

typedef struct _lsp_options {
	size_t symbol_index_size;
	zend_long worker_count;
	zend_long phpstan_level;
	zend_long psalm_level;
	double analyzer_diagnostics_timeout;
	bool analyzer_auto;
	bool analyzer_phpstan;
	bool analyzer_psalm;
	bool analyzer_psalm_ls;
	lsp_psalm_transport psalm_transport;
	bool psalm_on_change;
	bool psalm_enable_autocomplete;
	bool psalm_enable_diagnostics;
	bool psalm_enable_hover;
	bool psalm_enable_definition;
	bool psalm_enable_signature_help;
	bool psalm_show_info;
	bool psalm_live_dead_code_diagnostics;
	bool psalm_in_memory;
	zend_long psalm_on_change_debounce_ms;
	zend_long psalm_max_response_wait_ms;
	zend_string *memory_limit;
	zend_string **worker_php_args;
	uint32_t worker_php_arg_count;
} lsp_options;

typedef struct _lsp_symbol_index_header {
	uint32_t magic;
	uint32_t reserved;
	uint64_t generation;
	size_t capacity;
	size_t used;
	uint32_t symbol_count;
	uint32_t flags;
} lsp_symbol_index_header;

# define LSP_SYMBOL_ENTRY_VENDOR (1u << 0)
# define LSP_SYMBOL_ENTRY_DELETED (1u << 1)

typedef struct _lsp_symbol_entry {
	uint32_t record_offset;
	uint32_t fqcn_length;
	uint32_t path_length;
	uint8_t kind;
	uint8_t flags;
} lsp_symbol_entry;

typedef struct _lsp_symbol_segment {
	uint32_t entry_index;
	uint32_t segment_offset;
} lsp_symbol_segment;

typedef struct _lsp_symbol_index {
	size_t size;
	void *addr;
	bool available;
	bool keys_initialized;
	HashTable symbol_keys;
	/* Derived lookup tables rebuilt lazily after the arena changes: a packed
	 * entry table (no strlen on the hot path) and a sorted FQCN-segment array
	 * enabling binary-search prefix completion instead of linear scans. */
	lsp_symbol_entry *entries;
	lsp_symbol_segment *segments;
	uint32_t entry_count;
	uint32_t segment_count;
	bool table_valid;
} lsp_symbol_index;

typedef struct _lsp_analyzer_job {
	lsp_process_id pid;
	zend_string *uri;
	zend_string *cache_key;
	zend_string *cache_file;
	zend_string *project_root;
	zend_long version;
	bool running;
} lsp_analyzer_job;

typedef struct _lsp_perf_counter {
	uint64_t count;
	double total_seconds;
	double max_seconds;
} lsp_perf_counter;

typedef struct _lsp_server {
	HashTable documents;
	HashTable member_cache;
	HashTable completion_cache;
	HashTable type_cache;
	HashTable phpstan_projects;
	HashTable psalm_projects;
	HashTable psalm_ls_project_states;
	HashTable psalm_ls_projects;
	HashTable perf_stats;
	HashTable runner_sessions;
	zend_string *root;
	lsp_options options;
	lsp_symbol_index symbol_index;
	lsp_analyzer_job phpstan_job;
	lsp_analyzer_job psalm_job;
	lsp_analyzer_job phpstan_completion_job;
	lsp_analyzer_job psalm_completion_job;
	lsp_process_id index_worker_pid;
	bool index_worker_running;
	bool phpstan_enabled;
	bool psalm_enabled;
	bool psalm_ls_enabled;
	bool shutdown;
	bool saw_shutdown;
	bool symbol_index_dirty;
} lsp_server;

typedef struct _lsp_command {
	char **argv;
	uint32_t count;
	uint32_t capacity;
} lsp_command;

typedef struct _lsp_process_pipes {
	lsp_process_id process;
	lsp_pipe_handle input;
	lsp_pipe_handle output;
	lsp_pipe_handle error;
} lsp_process_pipes;

static inline void lsp_command_init(lsp_command *command)
{
	memset(command, 0, sizeof(*command));
}

static inline void lsp_command_add(lsp_command *command, const char *value)
{
	if (command->count + 1 >= command->capacity) {
		command->capacity = command->capacity == 0 ? 8 : command->capacity * 2;
		command->argv = erealloc(command->argv, sizeof(char *) * command->capacity);
	}

	command->argv[command->count++] = estrdup(value);
	command->argv[command->count] = NULL;
}

static inline void lsp_command_add_zstr(lsp_command *command, zend_string *value)
{
	if (command->count + 1 >= command->capacity) {
		command->capacity = command->capacity == 0 ? 8 : command->capacity * 2;
		command->argv = erealloc(command->argv, sizeof(char *) * command->capacity);
	}

	command->argv[command->count++] = estrndup(ZSTR_VAL(value), ZSTR_LEN(value));
	command->argv[command->count] = NULL;
}

static inline void lsp_command_destroy(lsp_command *command)
{
	uint32_t i;

	for (i = 0; i < command->count; i++) {
		efree(command->argv[i]);
	}

	if (command->argv) {
		efree(command->argv);
	}

	memset(command, 0, sizeof(*command));
}

static inline zend_string *lsp_type_cache_key(const char *analyzer, lsp_document *document, zend_string *expression, size_t offset)
{
	return strpprintf(0, "%s:type:%s:" ZEND_LONG_FMT ":%zu:%s", analyzer, ZSTR_VAL(document->uri), document->version, offset, ZSTR_VAL(expression));
}

static inline bool lsp_type_is_unhelpful(zend_string *type)
{
	if (!type || ZSTR_LEN(type) == 0) {
		return true;
	}

	if (zend_string_equals_literal_ci(type, "mixed") || zend_string_equals_literal(type, "*ERROR*")) {
		return true;
	}

	return false;
}

static inline const char *lsp_skip_global_namespace_prefix(const char *value, size_t *length)
{
	if (*length > 0 && value[0] == '\\') {
		(*length)--;

		return value + 1;
	}

	return value;
}

static inline size_t lsp_effective_name_prefix_length(zend_string *prefix)
{
	const char *value;
	size_t length;

	value = ZSTR_VAL(prefix);
	length = ZSTR_LEN(prefix);

	value = lsp_skip_global_namespace_prefix(value, &length);
	if (length > 0 && value[0] == '$') {
		length--;
	}

	return length;
}

static inline bool lsp_completion_detail_has_empty_parameters(zend_string *detail)
{
	const char *value;
	size_t i, j;

	if (!detail) {
		return false;
	}

	value = ZSTR_VAL(detail);
	for (i = 0; i < ZSTR_LEN(detail); i++) {
		if (value[i] != '(') {
			continue;
		}

		for (j = i + 1; j < ZSTR_LEN(detail); j++) {
			if (value[j] == ' ' || value[j] == '\t' || value[j] == '\r' || value[j] == '\n') {
				continue;
			}

			return value[j] == ')';
		}

		return false;
	}

	return false;
}

static inline zend_string *lsp_completion_call_snippet_ex(zend_string *insert_text, bool cursor_after_call)
{
	smart_str snippet = {0};
	size_t i;
	char c;

	for (i = 0; i < ZSTR_LEN(insert_text); i++) {
		c = ZSTR_VAL(insert_text)[i];
		if (c == '\\' || c == '$' || c == '}') {
			smart_str_appendc(&snippet, '\\');
		}
		smart_str_appendc(&snippet, c);
	}

	if (cursor_after_call) {
		smart_str_appendl(&snippet, "()$0", sizeof("()$0") - 1);
	} else {
		smart_str_appendl(&snippet, "($0)", sizeof("($0)") - 1);
	}
	smart_str_0(&snippet);

	return snippet.s;
}

static inline zend_string *lsp_completion_call_snippet(zend_string *insert_text)
{
	return lsp_completion_call_snippet_ex(insert_text, false);
}

static inline zend_string *lsp_completion_call_snippet_for_detail(zend_string *insert_text, zend_string *detail)
{
	return lsp_completion_call_snippet_ex(insert_text, lsp_completion_detail_has_empty_parameters(detail));
}

static inline lsp_dir *lsp_dir_open(zend_string *path)
{
	lsp_dir *dir;
	php_stream *stream;

	stream = php_stream_opendir(ZSTR_VAL(path), 0, NULL);
	if (!stream) {
		return NULL;
	}

	dir = emalloc(sizeof(*dir));
	dir->stream = stream;
	memset(&dir->entry, 0, sizeof(dir->entry));

	return dir;
}

static inline const char *lsp_dir_read(lsp_dir *dir)
{
	php_stream_dirent *entry;

	entry = php_stream_readdir(dir->stream, &dir->entry);
	if (!entry) {
		return NULL;
	}

	return entry->d_name;
}

static inline void lsp_dir_close(lsp_dir *dir)
{
	if (!dir) {
		return;
	}

	php_stream_closedir(dir->stream);
	efree(dir);
}

static inline bool lsp_zval_string_equals_literal(zval *value, const char *literal)
{
	return value &&
		Z_TYPE_P(value) == IS_STRING &&
		php_ver_abstract.string_equals_cstr(Z_STR_P(value), literal, strlen(literal))
	;
}

static inline zend_ast *lsp_compile_string_to_ast_silent(zend_string *code, zend_string *uri, zend_arena **ast_arena)
{
	zend_ast *ast;
	bool orig_record_errors;

	*ast_arena = NULL;
	orig_record_errors = EG(record_errors);
	if (!orig_record_errors) {
		zend_begin_record_errors();
	}

	ast = zend_compile_string_to_ast(code, ast_arena, uri ? uri : ZSTR_EMPTY_ALLOC());
	if (EG(exception)) {
		zend_clear_exception();
	}

	if (!orig_record_errors) {
		EG(record_errors) = false;
		zend_free_recorded_errors();
	}

	return ast;
}

static inline void lsp_compiled_ast_destroy(zend_ast *ast, zend_arena *ast_arena)
{
	if (ast) {
		zend_ast_destroy(ast);
		if (ast_arena) {
			zend_arena_destroy(ast_arena);
		}
	}
}

static inline zend_string *lsp_ast_string_value(zend_ast *ast)
{
	zval *value;

	if (!ast) {
		return NULL;
	}

	if (ast->kind == ZEND_AST_ZVAL) {
		value = zend_ast_get_zval(ast);
		if (Z_TYPE_P(value) == IS_STRING) {
			return Z_STR_P(value);
		}

		return NULL;
	}

	if (ast->kind == ZEND_AST_CONSTANT) {
		return zend_ast_get_constant_name(ast);
	}

	return NULL;
}

void lsp_lsparrot_tokens_to_zval(zval *return_value, zend_string *source);
bool lsp_doc_is_identifier_char(char c);
zend_string *lsp_resolve_class_name(zend_string *text, zend_string *type);
bool lsp_token_in_bounds(zval *token, size_t start, size_t end);
bool lsp_token_at_depth(zend_string *text, zval *token, zend_long depth);
bool lsp_find_first_class_bounds(zend_string *text, size_t *body_start, size_t *body_end, zend_long *body_depth);

static inline bool lsp_keyword_at_slice(const char *value, size_t start, size_t end, const char *keyword, size_t *keyword_end)
{
	size_t length;

	length = strlen(keyword);
	if (start + length > end || strncasecmp(value + start, keyword, length) != 0) {
		return false;
	}

	if (start > 0 && lsp_doc_is_identifier_char(value[start - 1])) {
		return false;
	}

	if (start + length < end && lsp_doc_is_identifier_char(value[start + length])) {
		return false;
	}

	*keyword_end = start + length;

	return true;
}

static inline void lsp_add_resolved_trait_name(zval *traits, zend_string *text, const char *name_start, const char *name_end)
{
	zend_string *raw, *resolved;

	if (name_end <= name_start) {
		return;
	}

	raw = zend_string_init(name_start, name_end - name_start, 0);
	resolved = lsp_resolve_class_name(text, raw);
	zend_string_release(raw);
	if (resolved) {
		add_next_index_str(traits, resolved);
	}
}

static inline void lsp_collect_trait_names_from_use_slice(zval *traits, zend_string *text, const char *slice_start, const char *slice_end)
{
	const char *p, *name_start, *name_end;

	p = slice_start;
	while (p < slice_end) {
		while (p < slice_end && (isspace((unsigned char) *p) || *p == ',')) {
			p++;
		}

		name_start = p;
		while (p < slice_end && (lsp_doc_is_identifier_char(*p) || *p == '\\')) {
			p++;
		}
		name_end = p;

		lsp_add_resolved_trait_name(traits, text, name_start, name_end);

		while (p < slice_end && *p != ',') {
			p++;
		}
	}
}

static inline void lsp_collect_class_trait_names(zend_string *text, zval *traits)
{
	const char *value, *slice_start, *slice_end;
	zend_long body_depth, offset;
	zval tokens_zv, *token;
	HashTable *tokens;
	uint32_t i, count;
	size_t body_start, body_end, text_length;

	array_init(traits);
	value = ZSTR_VAL(text);
	text_length = ZSTR_LEN(text);
	body_start = 0;
	body_end = 0;
	body_depth = 0;
	ZVAL_UNDEF(&tokens_zv);
	lsp_lsparrot_tokens_to_zval(&tokens_zv, text);
	if (Z_TYPE(tokens_zv) != IS_ARRAY || !lsp_find_first_class_bounds(text, &body_start, &body_end, &body_depth)) {
		if (!Z_ISUNDEF(tokens_zv)) {
			zval_ptr_dtor(&tokens_zv);
		}

		return;
	}

	tokens = Z_ARRVAL(tokens_zv);
	count = zend_hash_num_elements(tokens);
	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token ||
			Z_TYPE_P(token) != IS_ARRAY ||
			!lsp_token_name_equals(token, "T_USE") ||
			!lsp_token_in_bounds(token, body_start, body_end) ||
			!lsp_token_at_depth(text, token, body_depth)
		) {
			continue;
		}

		offset = lsp_token_long(token, "offset", 0);
		if (offset < 0 || (size_t) offset + sizeof("use") - 1 >= text_length) {
			continue;
		}

		slice_start = value + (size_t) offset + sizeof("use") - 1;
		slice_end = slice_start;
		while (slice_end < value + text_length && *slice_end != ';' && *slice_end != '{') {
			slice_end++;
		}
		lsp_collect_trait_names_from_use_slice(traits, text, slice_start, slice_end);
	}

	zval_ptr_dtor(&tokens_zv);
}

void lsp_lsparrot_parse_to_zval(zval *return_value, zend_string *code, zend_string *uri);
void lsp_lsparrot_tokens_to_zval(zval *return_value, zend_string *source);
void lsp_tokens_cache_set_enabled(bool enabled);
void lsp_server_run(zval *options);
void lsp_member_cache_invalidate_path(lsp_server *server, zend_string *path);

void lsp_document_destroy(zval *value);
void lsp_analyzer_job_clear(lsp_analyzer_job *job);
void lsp_analyzer_job_destroy(lsp_analyzer_job *job);
bool lsp_analyzer_jobs_running_for_document(lsp_server *server, lsp_document *document);
void lsp_reap_analyzer_completion_jobs(void);
void lsp_psalm_ls_project_destroy(zval *value);
void lsp_psalm_ls_shutdown_all(lsp_server *server);
void lsp_psalm_ls_pump(lsp_server *server, double timeout);
bool lsp_psalm_ls_enabled(lsp_server *server);
bool lsp_psalm_ls_schedule_project(lsp_server *server, zend_string *project_root);
void lsp_psalm_ls_document_open(lsp_server *server, lsp_document *document);
void lsp_psalm_ls_document_change(lsp_server *server, lsp_document *document);
void lsp_psalm_ls_document_save(lsp_server *server, lsp_document *document);
void lsp_psalm_ls_document_close(lsp_server *server, zend_string *uri);
void lsp_psalm_ls_append_diagnostics(lsp_server *server, lsp_document *document, zval *diagnostics);
bool lsp_psalm_ls_completion_cache_or_schedule(lsp_server *server, zval *items, lsp_document *document, zval *position, size_t offset, zend_string *prefix);
zend_string *lsp_psalm_ls_type_for_position(lsp_server *server, lsp_document *document, zval *position, size_t offset, zend_string *word);
zend_string *lsp_psalm_ls_type_for_position_async(lsp_server *server, lsp_document *document, zval *position, size_t offset, zend_string *word);
uint32_t lsp_active_process_count(lsp_server *server);
zend_string *lsp_uri_to_path(zend_string *uri);
zend_string *lsp_uri_from_path(zend_string *path);
zend_string *lsp_read_file(zend_string *path);
void lsp_options_from_zval(lsp_options *options, zval *value);
void lsp_options_destroy(lsp_options *options);
void lsp_position_from_zval(zval *position, zend_long *line, zend_long *character);
size_t lsp_offset_at(zend_string *text, zend_long line, zend_long character);
zend_string *lsp_prefix_at(zend_string *text, size_t offset);
zend_string *lsp_word_at(zend_string *text, size_t offset);
bool lsp_matches_prefix_string(zend_string *label, zend_string *prefix);
bool lsp_matches_prefix_literal(const char *label, zend_string *prefix);
bool lsp_is_member_access_completion(zend_string *text, size_t offset, zend_string *prefix);

void lsp_symbol_index_init(lsp_symbol_index *region, lsp_options *options);
void lsp_symbol_index_destroy(lsp_symbol_index *region);
void lsp_symbol_index_reset(lsp_symbol_index *region);
bool lsp_symbol_index_add_symbol_kind(lsp_symbol_index *region, char kind, zend_string *fqcn, zend_string *path);
const char *lsp_basename_from_fqcn(const char *fqcn, size_t length, size_t *label_length);
bool lsp_token_is_class_like(zval *token);
zend_string *lsp_next_string_token(HashTable *tokens, uint32_t start);
zend_string *lsp_next_function_name_token(HashTable *tokens, uint32_t start);
zval *lsp_next_function_name_token_ex(HashTable *tokens, uint32_t start, uint32_t *index);
void lsp_add_completion_item_ex(zval *items, zend_string *label, zend_long kind, zend_string *detail, const char *source);
void lsp_add_variable_completion_item_ex(zval *items, zend_string *label, zend_string *detail, const char *source, zend_string *text, size_t start_offset, size_t end_offset);
void lsp_add_completion_item(zval *items, zend_string *label, zend_long kind, zend_string *detail);
void lsp_add_keyword_completion(zval *items, const char *keyword, zend_string *prefix);
void lsp_deduplicate_completion_items(zval *items);
bool lsp_completion_attach_items(zval *return_value, zval *items);
zend_long lsp_symbol_workspace_kind(char kind);
void lsp_range_from_offsets(zend_string *text, size_t start_offset, size_t end_offset, zval *range);
bool lsp_path_value_contains_vendor(const char *path, size_t path_length);
zend_string *lsp_document_namespace(zend_string *text);
bool lsp_symbol_in_current_namespace(zend_string *current_namespace, const char *fqcn, size_t fqcn_length);
bool lsp_document_has_import(zend_string *text, char kind, const char *fqcn);
size_t lsp_import_insert_offset(zend_string *text, bool *after_existing_use);
zend_string *lsp_symbol_import_text(char kind, const char *fqcn, bool compact);
void lsp_add_class_like_symbol_completion_item(zval *items, lsp_document *document, size_t offset, zend_string *prefix, char kind, zend_string *fqcn);
void lsp_add_project_symbol_completions(lsp_server *server, zval *items, lsp_document *document, size_t offset, zend_string *prefix);
void lsp_add_project_class_like_completions(lsp_server *server, zval *items, lsp_document *document, size_t offset, zend_string *prefix);
void lsp_add_project_symbol_kind_completions(lsp_server *server, zval *items, lsp_document *document, size_t offset, zend_string *prefix, char filter_kind);

const char *lsp_primary_analyzer_source(lsp_server *server);
bool lsp_doc_is_identifier_start(char c);
bool lsp_doc_is_identifier_char(char c);
char lsp_type_constraint_completion_kind(zend_string *text, size_t offset, zend_string *prefix);
void lsp_phpdoc_add_variable_type_completions(lsp_server *server, zval *items, zend_string *text, zend_string *prefix);
void lsp_phpdoc_add_variable_type_completion_edits(lsp_server *server, zval *items, zend_string *text, zend_string *prefix, size_t prefix_start, size_t offset);
void lsp_phpdoc_add_annotation_completions(zval *items, zend_string *text, size_t offset);
zend_string *lsp_phpdoc_type_for_word(zend_string *text, zend_string *word);
zend_string *lsp_phpdoc_type_for_word_raw(zend_string *text, zend_string *word);
zend_string *lsp_phpdoc_property_type_for_word(zend_string *text, zend_string *word, size_t offset);
zend_string *lsp_phpdoc_property_type_for_word_raw(zend_string *text, zend_string *word, size_t offset);
void lsp_phpdoc_add_template_completions(lsp_server *server, zval *items, zend_string *text, zend_string *prefix);
zend_string *lsp_phpdoc_return_type_from_comment(zend_string *comment);
zend_string *lsp_phpdoc_return_type_from_comment_raw(zend_string *comment);
bool lsp_phpdoc_type_has_array_shape(zend_string *type);
zend_string *lsp_phpdoc_array_shape_key_type(zend_string *type, zend_string *key);
bool lsp_phpdoc_add_array_shape_completions(lsp_server *server, zval *items, lsp_document *document, size_t offset);
bool lsp_phpdoc_add_object_shape_member_completions(lsp_server *server, zval *items, lsp_document *document, size_t offset, zend_string *prefix);
zend_string *lsp_phpdoc_attribute_shape_type_before(zend_string *text, size_t offset, bool normalize);
void lsp_phpdoc_cache_class_methods(zval *methods, zend_string *contents, size_t class_start);
void lsp_phpdoc_cache_class_properties(zval *properties, zend_string *contents, size_t class_start);
zend_string *lsp_phpdoc_template_type_at(zend_string *text, uint32_t target_index);
zend_string *lsp_phpdoc_trait_use_generic_argument(zend_string *text, zend_string *trait_name, uint32_t target_index);
bool lsp_member_access_arrow(zend_string *text, size_t offset, zend_string *prefix, size_t *arrow_start, size_t *receiver_end);
bool lsp_member_access_context(zend_string *text, size_t offset, zend_string *prefix, zend_string **receiver, zend_string **member_prefix);
bool lsp_find_matching_open_paren(zend_string *text, size_t close_offset, size_t *open_offset);
bool lsp_find_matching_open_bracket(zend_string *text, size_t close_offset, size_t *open_offset);
bool lsp_parse_method_call_before_offset(zend_string *text, size_t offset, zend_string **method_name, size_t *receiver_end);
bool lsp_parse_this_method_call_ending_at(zend_string *text, size_t offset, zend_string **method_name);
bool lsp_this_method_call_member_access_context(zend_string *text, size_t offset, zend_string *prefix, zend_string **method_name, zend_string **member_prefix);
zend_string *lsp_infer_new_assignment_class(zend_string *text, zend_string *receiver, size_t offset);
size_t lsp_current_statement_scan_limit(zend_string *text, size_t offset);
zend_string *lsp_type_generic_argument(zend_string *type, uint32_t target_index);
bool lsp_type_equals(zend_string *left, zend_string *right);
zend_string *lsp_type_array_element_type(zend_string *type);
zend_string *lsp_resolve_class_name(zend_string *text, zend_string *type);

bool lsp_text_is_word_boundary(zend_string *text, size_t offset);
zend_long lsp_brace_depth_at(zend_string *text, size_t offset);
bool lsp_find_matching_brace(zend_string *text, size_t open_offset, size_t *close_offset);
bool lsp_token_is_char(zval *token, char value);
bool lsp_token_in_bounds(zval *token, size_t start, size_t end);
bool lsp_token_at_depth(zend_string *text, zval *token, zend_long depth);
bool lsp_token_is_property_declaration(HashTable *tokens, uint32_t index, zend_string *text, zend_long body_depth);
zend_string *lsp_infer_variable_type(lsp_server *server, lsp_document *document, zend_string *variable, size_t offset);
zend_string *lsp_infer_variable_phpdoc_type(lsp_document *document, zend_string *variable, size_t offset);
zend_string *lsp_infer_variable_declared_type(lsp_server *server, lsp_document *document, zend_string *variable, size_t offset);
bool lsp_find_first_class_bounds(zend_string *text, size_t *body_start, size_t *body_end, zend_long *body_depth);
bool lsp_method_is_public(HashTable *tokens, uint32_t index, zend_string *text, zend_long body_depth);
bool lsp_path_value_contains_analysis_helper(const char *path, size_t path_length);
zend_string *lsp_find_project_symbol_path(lsp_server *server, char expected_kind, zend_string *fqcn);
bool lsp_member_access_class_context(lsp_server *server, lsp_document *document, size_t offset, zend_string *prefix, zend_string **class_name, zend_string **member_prefix);

size_t lsp_method_signature_end(HashTable *tokens, uint32_t name_index, zend_string *text, zend_long body_depth);
zend_string *lsp_function_signature_detail(zend_string *text, zval *name_token, HashTable *tokens, uint32_t name_index, zend_long body_depth, const char *prefix);
bool lsp_find_enclosing_class_header(zend_string *text, size_t offset, size_t *class_start, size_t *body_start, size_t *body_end, zend_long *body_depth);
bool lsp_find_class_header_from(zend_string *text, size_t search_start, size_t *class_start, size_t *body_start, size_t *body_end, zend_long *body_depth);
bool lsp_find_first_class_header(zend_string *text, size_t *class_start, size_t *body_start, size_t *body_end, zend_long *body_depth);
bool lsp_word_at_slice_equals(const char *start, const char *end, const char *word);
zend_string *lsp_class_extends_name(zend_string *text, size_t class_start, size_t body_start);
zend_string *lsp_class_declared_name(zend_string *text, size_t class_start, size_t body_start);
zend_string *lsp_property_completion_detail(zend_string *text, zval *variable_token, bool is_static);
zend_string *lsp_promoted_property_completion_detail(zend_string *text, zval *variable_token, size_t param_start);
void lsp_add_scope_variable_completions(lsp_server *server, zval *items, lsp_document *document, HashTable *tokens, size_t offset, zend_string *prefix, size_t prefix_start);
bool lsp_find_class_header_for_name(zend_string *text, zend_string *class_name, size_t *class_start, size_t *body_start, size_t *body_end, zend_long *body_depth);
lsp_document *lsp_document_for_path(lsp_server *server, zend_string *path);
zval *lsp_class_member_cache_entry(lsp_server *server, zend_string *class_name);
void lsp_add_inherited_project_class_member_completions(lsp_server *server, zval *items, zend_string *class_name, zend_string *member_prefix);
void lsp_add_inherited_public_project_class_member_completions(lsp_server *server, zval *items, zend_string *class_name, zend_string *member_prefix);
void lsp_add_static_project_class_member_completions(lsp_server *server, zval *items, zend_string *class_name, zend_string *member_prefix);
void lsp_add_inherited_static_project_class_member_completions(lsp_server *server, zval *items, zend_string *class_name, zend_string *member_prefix);
void lsp_add_current_class_phpdoc_member_completions(lsp_server *server, zval *items, lsp_document *document, size_t class_start, zend_string *member_prefix, bool static_access);
void lsp_add_current_static_member_completions(zval *items, lsp_document *document, size_t body_start, size_t body_end, zend_long body_depth, zend_string *member_prefix);
void lsp_add_inferred_member_completions(lsp_server *server, zval *items, lsp_document *document, size_t offset, zend_string *prefix);
void lsp_add_this_member_completions(lsp_server *server, zval *items, lsp_document *document, size_t offset, zend_string *prefix);
bool lsp_add_override_method_completions(lsp_server *server, zval *items, lsp_document *document, HashTable *tokens, size_t offset, zend_string *prefix);
bool lsp_find_symbol(zval *lsparrot, zend_string *word, zval **matched_token, zend_string **matched_label, zend_string **matched_detail);
const char *lsp_builtin_class_detail_prefix(zend_class_entry *ce);
bool lsp_offset_is_inside_class_body(zend_string *text, size_t offset);

void lsp_lsparrot_completion(lsp_server *server, zval *return_value, lsp_document *document, zval *position);
void lsp_lsparrot_hover(lsp_server *server, zval *return_value, lsp_document *document, zval *position);
void lsp_lsparrot_definition(lsp_server *server, zval *return_value, lsp_document *document, zval *position);
void lsp_lsparrot_code_lens(lsp_server *server, zval *return_value, lsp_document *document);
void lsp_lsparrot_signature_help(lsp_server *server, zval *return_value, lsp_document *document, zval *position);
void lsp_lsparrot_references(lsp_server *server, zval *return_value, lsp_document *document, zval *params);
void lsp_lsparrot_document_highlight(lsp_server *server, zval *return_value, lsp_document *document, zval *position);
void lsp_lsparrot_implementation(lsp_server *server, zval *return_value, lsp_document *document, zval *position);
void lsp_lsparrot_code_action(lsp_server *server, zval *return_value, lsp_document *document, zval *params);
void lsp_add_analyzer_quick_fixes(zval *actions, lsp_document *document, zval *params);
void lsp_lsparrot_prepare_rename(lsp_server *server, zval *return_value, lsp_document *document, zval *position);
void lsp_lsparrot_rename(lsp_server *server, zval *return_value, lsp_document *document, zval *params);
void lsp_lsparrot_formatting(lsp_server *server, zval *return_value, lsp_document *document);
void lsp_lsparrot_range_formatting(lsp_server *server, zval *return_value, lsp_document *document, zval *params);
void lsp_lsparrot_inlay_hint(lsp_server *server, zval *return_value, lsp_document *document, zval *params);
void lsp_lsparrot_semantic_tokens(lsp_server *server, zval *return_value, lsp_document *document);
void lsp_semantic_token_legend(zval *legend);

lsp_document *lsp_document_open_or_change(lsp_server *server, zend_string *uri, zend_long version, zend_string *text);
lsp_document *lsp_document_from_uri(lsp_server *server, zend_string *uri);
void lsp_document_analyze(lsp_document *document);
void lsp_document_derived_invalidate(lsp_document *document);
void lsp_document_derived_ensure(lsp_document *document);
zend_string *lsp_document_namespace_cached(lsp_document *document);
bool lsp_document_has_import_cached(lsp_document *document, char kind, const char *fqcn, size_t fqcn_length);
size_t lsp_document_import_insert_offset_cached(lsp_document *document, bool *after_existing_use);
void lsp_document_collect_imports(zend_string *text, HashTable *imports);
void lsp_perf_stats_record(lsp_server *server, zend_string *method, double elapsed_seconds);
void lsp_symbol_index_table_ensure(lsp_symbol_index *region);
void lsp_symbol_index_table_invalidate(lsp_symbol_index *region);
const char *lsp_symbol_entry_fqcn(lsp_symbol_index *region, const lsp_symbol_entry *entry);
const char *lsp_symbol_entry_path(lsp_symbol_index *region, const lsp_symbol_entry *entry);
const char *lsp_symbol_index_lookup(lsp_symbol_index *region, char expected_kind, zend_string *fqcn, char *found_kind, const char **stored_fqcn, size_t *path_length);
uint32_t lsp_symbol_index_remove_path(lsp_symbol_index *region, zend_string *path);
void lsp_symbol_index_rebuild_keys(lsp_symbol_index *region);
void lsp_index_refresh_file(lsp_server *server, zend_string *path);
void lsp_project_index_persist(lsp_server *server);
void lsp_index_join_worker(lsp_server *server);
void lsp_index_poll_worker(lsp_server *server);
void lsp_index_stop_worker(lsp_server *server);
void lsp_protocol_respond(zval *id, zval *result);
void lsp_protocol_error(zval *id, int code, const char *message);
void lsp_protocol_notify(const char *method, zval *params);
void lsp_analyzer_status(const char *analyzer, const char *state, const char *message);
void lsp_analyzer_project_status(const char *analyzer, const char *state, const char *message, zend_string *project_root);
void lsp_analyzer_project_state(lsp_server *server, const char *analyzer, zend_string *project_root, zend_long state);
void lsp_reap_analyzer_jobs(lsp_server *server);
void lsp_analyzer_unavailable(const char *analyzer, const char *label);
void lsp_driver_status(lsp_server *server);
void lsp_server_status(lsp_server *server, zval *return_value);
bool lsp_protocol_next_message(lsp_server *server, zval *message);
bool lsp_protocol_request_is_cancelled(zval *id);
bool lsp_protocol_queue_supersedes_change(zend_string *uri);
void lsp_transport_shutdown(void);

double lsp_now_seconds(void);
zend_string *lsp_process_run_capture(lsp_command *command, zend_string *cwd, double timeout);
lsp_process_id lsp_process_spawn_to_file(lsp_command *command, zend_string *cwd, zend_string *output_file);
bool lsp_process_spawn_piped(lsp_command *command, zend_string *cwd, lsp_process_pipes *pipes);
bool lsp_process_wait_nonblocking(lsp_process_id process, int *status);
bool lsp_process_wait_timeout(lsp_process_id process, int *status, double timeout);
void lsp_process_wait(lsp_process_id process, int *status);
void lsp_process_terminate(lsp_process_id process);
void lsp_process_terminate_force(lsp_process_id process);
void lsp_process_close(lsp_process_id process);
void lsp_pipe_close(lsp_pipe_handle *pipe);
bool lsp_pipe_write_all_timeout(lsp_pipe_handle pipe, const char *data, size_t length, double timeout);
bool lsp_pipe_read_available(lsp_pipe_handle pipe, smart_str *output, bool *closed);

zend_string *lsp_runner_run_capture(lsp_server *server, const char *analyzer, zend_string *project_root, lsp_command *command, zend_string *cwd, double timeout);
void lsp_runner_shutdown_all(lsp_server *server);
void lsp_runner_status(lsp_server *server, zval *target);
void lsp_runner_session_destroy(zval *value);

void lsp_publish_empty_diagnostics(zend_string *uri);
zend_string *lsp_join_path2(zend_string *base, const char *suffix);
zend_string *lsp_tool_project_candidate(zend_string *root, const char *name);
bool lsp_tool_available(const char *name, zend_string *root);
bool lsp_tool_command(lsp_server *server, zend_string *root, const char *name, lsp_command *command);
bool lsp_include_array_file(zend_string *path, zval *return_value);
zend_string *lsp_run_command_capture(lsp_command *command, zend_string *cwd, double timeout);
void lsp_add_analyzer_diagnostic(zval *diagnostics, const char *source, zend_string *message, zend_string *code, zval *range, zend_long severity);
zend_string *lsp_json_slice_from(zend_string *output, char open_char);
zend_string *lsp_analyzer_failure_message(const char *analyzer, zend_string *output);
zend_long lsp_analyzer_parallel_workers(lsp_server *server);
void lsp_analyzer_add_memory_limit(lsp_command *command, lsp_server *server);
zend_string *lsp_analyzer_project_output_file(zend_string *project_root, const char *analyzer);
zend_long lsp_analyzer_project_state_value(lsp_server *server, const char *analyzer, zend_string *project_root);
bool lsp_analyzer_project_has_state(lsp_server *server, const char *analyzer, zend_string *project_root);
bool lsp_analyzer_project_has_scope(zend_string *project_root);
bool lsp_document_is_in_analyzer_scope(lsp_document *document, zend_string *project_root);
void lsp_command_add_composer_analysis_paths(lsp_command *command, zend_string *project_root);
bool lsp_start_analyzer_project_job(lsp_server *server, const char *analyzer, zend_string *project_root, lsp_command *command, zend_string *output_file);
void lsp_analyzer_project_finished(lsp_server *server, const char *analyzer, zend_string *project_root, zend_string *output_file);
void lsp_schedule_workspace_analyzers(lsp_server *server);
void lsp_schedule_project_analyzers(lsp_server *server, lsp_document *document);
void lsp_reschedule_project_analyzers(lsp_server *server, lsp_document *document);
void lsp_start_pending_phpstan_project_analyzer(lsp_server *server);
void lsp_schedule_phpstan_project_analyzer(lsp_server *server, zend_string *project_root);
void lsp_append_phpstan_cached_diagnostics(lsp_server *server, lsp_document *document, zval *diagnostics);
void lsp_reschedule_phpstan_project_analyzer(lsp_server *server, zend_string *project_root);
void lsp_start_pending_psalm_project_analyzer(lsp_server *server);
void lsp_schedule_psalm_project_analyzer(lsp_server *server, zend_string *project_root);
void lsp_append_psalm_cached_diagnostics(lsp_server *server, lsp_document *document, zval *diagnostics);
void lsp_reschedule_psalm_project_analyzer(lsp_server *server, zend_string *project_root);
bool lsp_scan_should_skip_dir_name(const char *name);
zend_string *lsp_composer_config_string(zend_string *root, const char *key);
zend_string *lsp_composer_vendor_dir(zend_string *project_root);
bool lsp_path_is_under_composer_vendor_dir(zend_string *path, zend_string *project_root);
bool lsp_path_is_in_workspace_composer_vendor(zend_string *workspace_root, zend_string *path);
void lsp_line_range(zval *range, zend_string *text, zend_long one_based_line);
void lsp_publish_document_diagnostics(lsp_server *server, lsp_document *document);
zend_string *lsp_document_project_root(lsp_server *server, lsp_document *document);
zend_string *lsp_phpstan_type_for_expression(lsp_server *server, lsp_document *document, zend_string *expression, size_t offset);
zend_string *lsp_psalm_type_for_expression(lsp_server *server, lsp_document *document, zend_string *expression, size_t offset);
zend_string *lsp_phpstan_config_file(zend_string *root);
zend_string *lsp_phpstan_lsp_config_file(zend_string *project_root, zend_string *project_config, zend_long parallel_workers);
zend_long lsp_project_phpstan_level(lsp_server *server, zend_string *project_root);
zend_string *lsp_psalm_config_file(zend_string *root);
zend_long lsp_project_psalm_level(lsp_server *server, zend_string *project_root);
zend_string *lsp_psalm_ls_config_file(zend_string *project_root, zend_string *project_config, zend_long level, bool live_dead_code_diagnostics);
zend_string *lsp_psalm_type_config_file(zend_string *project_root, zend_string *project_config, zend_long level, bool live_dead_code_diagnostics);
void lsp_composer_analysis_paths(zend_string *project_root, zval *paths);
bool lsp_composer_project_has_analysis_paths(zend_string *project_root);
bool lsp_path_is_in_composer_analysis_paths(zend_string *path, zend_string *project_root);

void lsp_build_project_index(lsp_server *server);
void lsp_resolve_analyzers(lsp_server *server);

void lsp_server_loop(lsp_server *server);

#endif /* LSP_INTERNAL_H */
