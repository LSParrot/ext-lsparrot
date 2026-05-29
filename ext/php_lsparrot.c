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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <php.h>

#include <string.h>

#include <Zend/zend_compile.h>
#include <Zend/zend_exceptions.h>
#include <Zend/zend_interfaces.h>
#include <Zend/zend_language_parser.h>
#include <Zend/zend_language_scanner.h>
#include <Zend/zend_language_scanner_defs.h>

#include <SAPI.h>

#include <ext/spl/spl_exceptions.h>
#include <ext/standard/info.h>

#include "lsp_internal.h"
#include "lsparrot_arginfo.h"

/* Small LRU of recently tokenized sources keyed by zend_string identity.
 * The cache holds a reference on each key so the pointer cannot be reused
 * while cached, making pointer equality a sound identity test. This makes
 * repeated feature requests against an unchanged document (whose text
 * pointer is stable between didChange notifications) skip re-lexing. */
#define LSP_TOKENS_CACHE_SIZE 8

/* Interned key strings and token-name strings, created once at MINIT so a
 * token record costs zero key/name string allocations. Token construction is
 * the hottest part of tokenization (5 string keys + 1 name per token, tens of
 * thousands of tokens per parse). */
#define LSP_TOKEN_NAME_CACHE_SIZE 512

typedef struct {
	zend_string *text;
	zval tokens;
} lsp_tokens_cache_entry;

static zend_string *lsp_token_key_id, *lsp_token_key_name, *lsp_token_key_text,
	*lsp_token_key_line, *lsp_token_key_offset, *lsp_token_key_length;
static zend_string *lsp_token_name_cache[LSP_TOKEN_NAME_CACHE_SIZE];

static lsp_tokens_cache_entry lsp_tokens_cache[LSP_TOKENS_CACHE_SIZE];
static uint32_t lsp_tokens_cache_next = 0;
static bool lsp_tokens_cache_enabled = false;

static inline const char *lsp_token_name(int token)
{
	switch (token) {
		case T_OPEN_TAG: return "T_OPEN_TAG";
		case T_OPEN_TAG_WITH_ECHO: return "T_OPEN_TAG_WITH_ECHO";
		case T_CLOSE_TAG: return "T_CLOSE_TAG";
		case T_WHITESPACE: return "T_WHITESPACE";
		case T_COMMENT: return "T_COMMENT";
		case T_DOC_COMMENT: return "T_DOC_COMMENT";
		case T_STRING: return "T_STRING";
		case T_VARIABLE: return "T_VARIABLE";
		case T_NAME_FULLY_QUALIFIED: return "T_NAME_FULLY_QUALIFIED";
		case T_NAME_RELATIVE: return "T_NAME_RELATIVE";
		case T_NAME_QUALIFIED: return "T_NAME_QUALIFIED";
		case T_NAMESPACE: return "T_NAMESPACE";
		case T_USE: return "T_USE";
		case T_AS: return "T_AS";
		case T_CLASS: return "T_CLASS";
		case T_INTERFACE: return "T_INTERFACE";
		case T_TRAIT: return "T_TRAIT";
		case T_ENUM: return "T_ENUM";
		case T_FUNCTION: return "T_FUNCTION";
		case T_CONST: return "T_CONST";
		case T_PUBLIC: return "T_PUBLIC";
		case T_PROTECTED: return "T_PROTECTED";
		case T_PRIVATE: return "T_PRIVATE";
		case T_STATIC: return "T_STATIC";
		case T_OBJECT_OPERATOR: return "T_OBJECT_OPERATOR";
		case T_NULLSAFE_OBJECT_OPERATOR: return "T_NULLSAFE_OBJECT_OPERATOR";
		case T_PAAMAYIM_NEKUDOTAYIM: return "T_DOUBLE_COLON";
		case T_NS_SEPARATOR: return "T_NS_SEPARATOR";
		case T_LNUMBER: return "T_LNUMBER";
		case T_DNUMBER: return "T_DNUMBER";
		case T_CONSTANT_ENCAPSED_STRING: return "T_CONSTANT_ENCAPSED_STRING";
		case T_INLINE_HTML: return "T_INLINE_HTML";
		default: return token < 256 ? "CHAR" : "UNKNOWN";
	}
}

extern void lsp_token_strings_init(void)
{
	const char *name;
	uint32_t i;

	lsp_token_key_id = zend_string_init_interned("id", sizeof("id") - 1, 1);
	lsp_token_key_name = zend_string_init_interned("name", sizeof("name") - 1, 1);
	lsp_token_key_text = zend_string_init_interned("text", sizeof("text") - 1, 1);
	lsp_token_key_line = zend_string_init_interned("line", sizeof("line") - 1, 1);
	lsp_token_key_offset = zend_string_init_interned("offset", sizeof("offset") - 1, 1);
	lsp_token_key_length = zend_string_init_interned("length", sizeof("length") - 1, 1);

	for (i = 0; i < LSP_TOKEN_NAME_CACHE_SIZE; i++) {
		name = lsp_token_name((int) i);
		lsp_token_name_cache[i] = zend_string_init_interned(name, strlen(name), 1);
	}
}

static inline void lsp_add_token(HashTable *tokens, int token_type, const char *text, size_t length, uint32_t line, size_t offset)
{
	HashTable *fields;
	zval token, value;

	if (token_type == ';' && length > 1) {
		token_type = T_CLOSE_TAG;
	} else if (token_type == T_ECHO && length == sizeof("<?=") - 1) {
		token_type = T_OPEN_TAG_WITH_ECHO;
	}

	array_init(&token);
	fields = Z_ARRVAL(token);

	ZVAL_LONG(&value, token_type);
	zend_hash_add_new(fields, lsp_token_key_id, &value);

	if (token_type >= 0 && token_type < LSP_TOKEN_NAME_CACHE_SIZE && lsp_token_name_cache[token_type]) {
		ZVAL_INTERNED_STR(&value, lsp_token_name_cache[token_type]);
	} else {
		ZVAL_STRING(&value, lsp_token_name(token_type));
	}
	zend_hash_add_new(fields, lsp_token_key_name, &value);

	ZVAL_STRINGL(&value, text, length);
	zend_hash_add_new(fields, lsp_token_key_text, &value);

	ZVAL_LONG(&value, line);
	zend_hash_add_new(fields, lsp_token_key_line, &value);

	ZVAL_LONG(&value, (zend_long) offset);
	zend_hash_add_new(fields, lsp_token_key_offset, &value);

	ZVAL_LONG(&value, (zend_long) length);
	zend_hash_add_new(fields, lsp_token_key_length, &value);

	zend_hash_next_index_insert_new(tokens, &token);
}

static inline void lsp_lsparrot_tokenize_uncached(zval *return_value, zend_string *source)
{
	zend_lex_state original_lex_state;
	zval source_zval, token;
	uint32_t token_line = 1;
	size_t offset = 0;
	int token_type;

	ZVAL_STR_COPY(&source_zval, source);
	zend_save_lexical_state(&original_lex_state);
	zend_prepare_string_for_scanning(&source_zval, ZSTR_EMPTY_ALLOC());
	LANG_SCNG(yy_state) = yycINITIAL;

	array_init(return_value);

	while ((token_type = lex_scan(&token, NULL))) {
		lsp_add_token(Z_ARRVAL_P(return_value), token_type,
			(const char *) LANG_SCNG(yy_text), LANG_SCNG(yy_leng), token_line, offset);
		offset += LANG_SCNG(yy_leng);

		if (Z_TYPE(token) != IS_UNDEF) {
			zval_ptr_dtor_nogc(&token);
			ZVAL_UNDEF(&token);
		}

		if (CG(increment_lineno)) {
			CG(zend_lineno)++;
			CG(increment_lineno) = 0;
		}

		token_line = CG(zend_lineno);
	}

	zval_ptr_dtor_str(&source_zval);
	zend_restore_lexical_state(&original_lex_state);
	zend_clear_exception();
}

static inline void lsp_tokens_cache_clear(void)
{
	uint32_t i;

	for (i = 0; i < LSP_TOKENS_CACHE_SIZE; i++) {
		if (lsp_tokens_cache[i].text) {
			zend_string_release(lsp_tokens_cache[i].text);
			zval_ptr_dtor(&lsp_tokens_cache[i].tokens);
			lsp_tokens_cache[i].text = NULL;
			ZVAL_UNDEF(&lsp_tokens_cache[i].tokens);
		}
	}

	lsp_tokens_cache_next = 0;
}

extern void lsp_tokens_cache_set_enabled(bool enabled)
{
	lsp_tokens_cache_clear();
	lsp_tokens_cache_enabled = enabled;
}

extern void lsp_lsparrot_tokens_to_zval(zval *return_value, zend_string *source)
{
	lsp_tokens_cache_entry *slot;
	uint32_t i;

	if (lsp_tokens_cache_enabled) {
		for (i = 0; i < LSP_TOKENS_CACHE_SIZE; i++) {
			if (lsp_tokens_cache[i].text == source) {
				ZVAL_COPY(return_value, &lsp_tokens_cache[i].tokens);

				return;
			}
		}
	}

	lsp_lsparrot_tokenize_uncached(return_value, source);

	if (lsp_tokens_cache_enabled && Z_TYPE_P(return_value) == IS_ARRAY) {
		slot = &lsp_tokens_cache[lsp_tokens_cache_next];
		lsp_tokens_cache_next = (lsp_tokens_cache_next + 1) % LSP_TOKENS_CACHE_SIZE;

		if (slot->text) {
			zend_string_release(slot->text);
			zval_ptr_dtor(&slot->tokens);
		}

		slot->text = zend_string_copy(source);
		ZVAL_COPY(&slot->tokens, return_value);
	}
}

static inline void lsp_line_map_to_zval(zval *return_value, zend_string *source)
{
	const char *code = ZSTR_VAL(source);
	size_t i, len = ZSTR_LEN(source);

	array_init(return_value);
	add_next_index_long(return_value, 0);
	for (i = 0; i < len; i++) {
		if (code[i] == '\n') {
			add_next_index_long(return_value, (zend_long) i + 1);
		}
	}
}

static inline void lsp_ast_to_zval(zval *return_value, zend_ast *ast, uint32_t depth)
{
	zend_ast_decl *decl;
	zend_ast_list *list;
	zval children, child, value, *zv;
	uint32_t children_count, i;

	if (!ast) {
		ZVAL_NULL(return_value);
		return;
	}

	if (depth > 512) {
		array_init(return_value);
		add_assoc_long(return_value, "kind", ast->kind);
		add_assoc_string(return_value, "kindName", "DEPTH_LIMIT");
		return;
	}

	array_init(return_value);
	add_assoc_long(return_value, "kind", ast->kind);
	add_assoc_string(return_value, "kindName", php_ver_abstract.ast_kind_name(ast->kind));
	add_assoc_long(return_value, "attr", ast->attr);
	add_assoc_long(return_value, "line", zend_ast_get_lineno(ast));

	if (zend_ast_is_list(ast)) {
		list = zend_ast_get_list(ast);

		array_init_size(&children, list->children);
		for (i = 0; i < list->children; i++) {
			lsp_ast_to_zval(&child, list->child[i], depth + 1);
			add_next_index_zval(&children, &child);
		}
		add_assoc_zval(return_value, "children", &children);
		add_assoc_long(return_value, "childCount", list->children);
		return;
	}

	if (php_ver_abstract.ast_is_decl(ast)) {
		decl = (zend_ast_decl *) ast;

		add_assoc_long(return_value, "startLine", decl->start_lineno);
		add_assoc_long(return_value, "endLine", decl->end_lineno);
		add_assoc_long(return_value, "flags", decl->flags);
		if (decl->name) {
			add_assoc_str(return_value, "name", zend_string_copy(decl->name));
		}
		if (decl->doc_comment) {
			add_assoc_str(return_value, "docComment", zend_string_copy(decl->doc_comment));
		}

		array_init_size(&children, 5);
		for (i = 0; i < 5; i++) {
			lsp_ast_to_zval(&child, decl->child[i], depth + 1);
			add_next_index_zval(&children, &child);
		}
		add_assoc_zval(return_value, "children", &children);
		add_assoc_long(return_value, "childCount", 5);
		return;
	}

	if (ast->kind == ZEND_AST_ZVAL || ast->kind == ZEND_AST_CONSTANT) {
		zv = zend_ast_get_zval(ast);
		ZVAL_COPY(&value, zv);
		add_assoc_zval(return_value, "value", &value);
		return;
	}

	if (php_ver_abstract.ast_is_opaque_node(ast->kind)) {
		return;
	}

	children_count = zend_ast_get_num_children(ast);
	array_init_size(&children, children_count);
	for (i = 0; i < children_count; i++) {
		lsp_ast_to_zval(&child, ast->child[i], depth + 1);
		add_next_index_zval(&children, &child);
	}
	add_assoc_zval(return_value, "children", &children);
	add_assoc_long(return_value, "childCount", children_count);
}

static inline void lsp_add_diagnostic(zval *diagnostics, const char *source, const char *message, uint32_t line, int severity)
{
	zval diagnostic, range, start, end;
	uint32_t lsp_line = line > 0 ? line - 1 : 0;

	array_init(&diagnostic);
	add_assoc_string(&diagnostic, "source", source);
	add_assoc_string(&diagnostic, "message", message);
	add_assoc_long(&diagnostic, "severity", severity);

	array_init(&start);
	add_assoc_long(&start, "line", lsp_line);
	add_assoc_long(&start, "character", 0);
	array_init(&end);
	add_assoc_long(&end, "line", lsp_line);
	add_assoc_long(&end, "character", 1);
	array_init(&range);
	add_assoc_zval(&range, "start", &start);
	add_assoc_zval(&range, "end", &end);
	add_assoc_zval(&diagnostic, "range", &range);

	add_next_index_zval(diagnostics, &diagnostic);
}

static inline void lsp_collect_exception_diagnostic(zval *diagnostics)
{
	zend_class_entry *base_ce;
	zend_string *message;
	zval rv_message, rv_line, *message_zv, *line_zv;
	uint32_t line;

	if (!EG(exception)) {
		return;
	}

	base_ce = instanceof_function(EG(exception)->ce, zend_ce_exception) ? zend_ce_exception : zend_ce_error;
	message_zv = zend_read_property_ex(base_ce, EG(exception), ZSTR_KNOWN(ZEND_STR_MESSAGE), false, &rv_message);
	line_zv = zend_read_property_ex(base_ce, EG(exception), ZSTR_KNOWN(ZEND_STR_LINE), false, &rv_line);
	message = zval_get_string(message_zv);
	line = Z_TYPE_P(line_zv) == IS_LONG ? (uint32_t) Z_LVAL_P(line_zv) : 1;

	lsp_add_diagnostic(diagnostics, "php", ZSTR_VAL(message), line, 1);

	zend_string_release(message);
	zend_clear_exception();
}

static inline void lsp_collect_recorded_diagnostics(zval *diagnostics)
{
	zend_error_info *info;
	uint32_t i;

	for (i = 0; i < EG(num_errors); i++) {
		info = EG(errors)[i];
		lsp_add_diagnostic(diagnostics, "php", ZSTR_VAL(info->message), info->lineno,
			info->type == E_DEPRECATED || info->type == E_WARNING || info->type == E_NOTICE ? 2 : 1);
	}
}

static inline bool lsp_is_valid_analyzer_string(zend_string *value, bool list_context)
{
	if (!list_context && (zend_string_equals_literal(value, "auto") || zend_string_equals_literal(value, "lsparrot"))) {
		return true;
	}

	return zend_string_equals_literal(value, "phpstan") ||
		zend_string_equals_literal(value, "psalm") ||
		zend_string_equals_literal(value, "psalm-ls")
	;
}

static inline bool lsp_validate_analyzer_option(zval *value)
{
	zval *entry;

	if (Z_TYPE_P(value) == IS_STRING) {
		return lsp_is_valid_analyzer_string(Z_STR_P(value), false);
	}

	if (Z_TYPE_P(value) == IS_ARRAY) {
		ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(value), entry) {
			if (Z_TYPE_P(entry) != IS_STRING || !lsp_is_valid_analyzer_string(Z_STR_P(entry), true)) {
				return false;
			}
		} ZEND_HASH_FOREACH_END();
		return true;
	}

	return false;
}

extern void lsp_lsparrot_parse_to_zval(zval *return_value, zend_string *code, zend_string *uri)
{
	zend_arena *ast_arena = NULL;
	zend_ast *ast = NULL;
	zval diagnostics, tokens, line_map, ast_zv, *ok_zv;
	bool orig_record_errors;

	array_init(return_value);
	add_assoc_bool(return_value, "ok", false);
	add_assoc_long(return_value, "phpVersionId", PHP_VERSION_ID);
	if (uri) {
		add_assoc_str(return_value, "uri", zend_string_copy(uri));
	} else {
		add_assoc_null(return_value, "uri");
	}

	lsp_lsparrot_tokens_to_zval(&tokens, code);
	add_assoc_zval(return_value, "tokens", &tokens);
	lsp_line_map_to_zval(&line_map, code);
	add_assoc_zval(return_value, "lineMap", &line_map);

	array_init(&diagnostics);

	orig_record_errors = EG(record_errors);
	if (!orig_record_errors) {
		zend_begin_record_errors();
	}

	ast = zend_compile_string_to_ast(code, &ast_arena, uri ? uri : ZSTR_EMPTY_ALLOC());
	lsp_collect_exception_diagnostic(&diagnostics);
	lsp_collect_recorded_diagnostics(&diagnostics);

	if (!orig_record_errors) {
		EG(record_errors) = false;
		zend_free_recorded_errors();
	}

	if (ast) {
		lsp_ast_to_zval(&ast_zv, ast, 0);
		add_assoc_zval(return_value, "ast", &ast_zv);
		ok_zv = zend_hash_str_find(Z_ARRVAL_P(return_value), "ok", sizeof("ok") - 1);
		if (ok_zv) {
			ZVAL_TRUE(ok_zv);
		}
		zend_ast_destroy(ast);
		if (ast_arena) {
			zend_arena_destroy(ast_arena);
		}
	} else {
		add_assoc_null(return_value, "ast");
	}

	add_assoc_zval(return_value, "diagnostics", &diagnostics);
}

PHP_FUNCTION(LSParrot_lsparrot_parse)
{
	zend_string *code, *uri = NULL;

	ZEND_PARSE_PARAMETERS_START(1, 2)
		Z_PARAM_STR(code)
		Z_PARAM_OPTIONAL
		Z_PARAM_STR_OR_NULL(uri)
	ZEND_PARSE_PARAMETERS_END();

	lsp_lsparrot_parse_to_zval(return_value, code, uri);
}

PHP_FUNCTION(LSParrot_lsparrot_tokens)
{
	zend_string *code, *uri = NULL;

	ZEND_PARSE_PARAMETERS_START(1, 2)
		Z_PARAM_STR(code)
		Z_PARAM_OPTIONAL
		Z_PARAM_STR_OR_NULL(uri)
	ZEND_PARSE_PARAMETERS_END();

	lsp_lsparrot_tokens_to_zval(return_value, code);
}

PHP_FUNCTION(LSParrot_lsparrot_version)
{
	array_init(return_value);
	add_assoc_long(return_value, "phpVersionId", PHP_VERSION_ID);
	add_assoc_string(return_value, "phpVersion", PHP_VERSION);
	add_assoc_string(return_value, "extensionVersion", PHP_LSPARROT_VERSION);
	add_assoc_bool(return_value, "cli", strcmp(sapi_module.name, "cli") == 0);
	add_assoc_bool(return_value, "processControl", true);
}

PHP_FUNCTION(LSParrot_start_lsp)
{
	zval default_options, *options = NULL, *analyser, *analyzer;
	bool owns_options = false;

	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ARRAY(options)
	ZEND_PARSE_PARAMETERS_END();

	if (strcmp(sapi_module.name, "cli") != 0) {
		zend_throw_exception(spl_ce_RuntimeException, "LSParrot\\start_lsp() is only supported by the PHP CLI SAPI", 0);
		RETURN_THROWS();
	}

	if (!options) {
		array_init(&default_options);
		add_assoc_string(&default_options, "analyzer", "auto");
		options = &default_options;
		owns_options = true;
	}

	analyser = zend_hash_str_find(Z_ARRVAL_P(options), "analyser", sizeof("analyser") - 1);
	if (analyser) {
		zend_throw_exception(spl_ce_InvalidArgumentException, "Unsupported option \"analyser\"; use \"analyzer\" instead.", 0);
		if (owns_options) {
			zval_ptr_dtor(options);
		}
		RETURN_THROWS();
	}

	analyzer = zend_hash_str_find(Z_ARRVAL_P(options), "analyzer", sizeof("analyzer") - 1);
	if (analyzer && !lsp_validate_analyzer_option(analyzer)) {
		zend_throw_exception(spl_ce_InvalidArgumentException, "Option \"analyzer\" must be \"auto\", \"lsparrot\", \"phpstan\", \"psalm\", \"psalm-ls\", or a list containing \"phpstan\", \"psalm\" and/or \"psalm-ls\".", 0);
		if (owns_options) {
			zval_ptr_dtor(options);
		}
		RETURN_THROWS();
	}

	lsp_server_run(options);

	if (owns_options) {
		zval_ptr_dtor(options);
	}

	RETURN_NULL();
}

PHP_MINIT_FUNCTION(lsparrot)
{
	lsp_token_strings_init();

	return SUCCESS;
}

PHP_RINIT_FUNCTION(lsparrot)
{
#if defined(ZTS) && defined(COMPILE_DL_LSPARROT)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif

	return SUCCESS;
}

PHP_MINFO_FUNCTION(lsparrot)
{
	php_info_print_table_start();
	php_info_print_table_row(2, "lsparrot support", "enabled");
	php_info_print_table_end();
}

zend_module_entry lsparrot_module_entry = {
	STANDARD_MODULE_HEADER,
	"lsparrot",
	ext_functions,
	PHP_MINIT(lsparrot),
	NULL, /* MSHUTDOWN */
	PHP_RINIT(lsparrot),
	NULL, /* RSHUTDOWN */
	PHP_MINFO(lsparrot),
	PHP_LSPARROT_VERSION,
	STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_LSPARROT
# ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
# endif
ZEND_GET_MODULE(lsparrot)
#endif
