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

#include <Zend/zend_language_parser.h>

/* textDocument/semanticTokens/full (and the range/delta variants below)
 * generated from the lexer token stream. Legend indexes must match
 * lsp_semantic_token_legend() in this file. */

#define LSP_SEM_NAMESPACE 0
#define LSP_SEM_CLASS 1
#define LSP_SEM_INTERFACE 2
#define LSP_SEM_ENUM 3
#define LSP_SEM_FUNCTION 4
#define LSP_SEM_METHOD 5
#define LSP_SEM_PROPERTY 6
#define LSP_SEM_VARIABLE 7
#define LSP_SEM_PARAMETER 8
#define LSP_SEM_KEYWORD 9
#define LSP_SEM_COMMENT 10
#define LSP_SEM_STRING 11
#define LSP_SEM_NUMBER 12
#define LSP_SEM_OPERATOR 13
#define LSP_SEM_ENUM_MEMBER 14
#define LSP_SEM_NONE (-1)

/* Legend order: static, readonly, deprecated, declaration. Bit positions
 * must stay in sync with lsp_semantic_token_legend()'s tokenModifiers. */
#define LSP_SEM_MOD_STATIC (1 << 0)
#define LSP_SEM_MOD_READONLY (1 << 1)
#define LSP_SEM_MOD_DEPRECATED (1 << 2)
#define LSP_SEM_MOD_DECLARATION (1 << 3)

typedef struct _lsp_semantic_emitter {
	zval *data;
	zend_long previous_line;
	zend_long previous_column;
} lsp_semantic_emitter;

typedef struct _lsp_semantic_decl_info {
	bool is_static;
	bool is_readonly;
	bool is_deprecated;
	bool has_modifier;
} lsp_semantic_decl_info;

extern void lsp_semantic_token_legend(zval *legend)
{
	zval token_types, token_modifiers;

	array_init(legend);
	array_init(&token_types);
	add_next_index_string(&token_types, "namespace");
	add_next_index_string(&token_types, "class");
	add_next_index_string(&token_types, "interface");
	add_next_index_string(&token_types, "enum");
	add_next_index_string(&token_types, "function");
	add_next_index_string(&token_types, "method");
	add_next_index_string(&token_types, "property");
	add_next_index_string(&token_types, "variable");
	add_next_index_string(&token_types, "parameter");
	add_next_index_string(&token_types, "keyword");
	add_next_index_string(&token_types, "comment");
	add_next_index_string(&token_types, "string");
	add_next_index_string(&token_types, "number");
	add_next_index_string(&token_types, "operator");
	add_next_index_string(&token_types, "enumMember");
	add_assoc_zval(legend, "tokenTypes", &token_types);
	array_init(&token_modifiers);
	add_next_index_string(&token_modifiers, "static");
	add_next_index_string(&token_modifiers, "readonly");
	add_next_index_string(&token_modifiers, "deprecated");
	add_next_index_string(&token_modifiers, "declaration");
	add_assoc_zval(legend, "tokenModifiers", &token_modifiers);
}

static inline bool lsp_semantic_id_is_keyword(zend_long id)
{
	switch (id) {
		case T_ABSTRACT:
		case T_ARRAY:
		case T_AS:
		case T_BREAK:
		case T_CALLABLE:
		case T_CASE:
		case T_CATCH:
		case T_CLASS:
		case T_CLASS_C:
		case T_CLONE:
		case T_CONST:
		case T_CONTINUE:
		case T_DECLARE:
		case T_DEFAULT:
		case T_DIR:
		case T_DO:
		case T_ECHO:
		case T_ELSE:
		case T_ELSEIF:
		case T_EMPTY:
		case T_ENDDECLARE:
		case T_ENDFOR:
		case T_ENDFOREACH:
		case T_ENDIF:
		case T_ENDSWITCH:
		case T_ENDWHILE:
		case T_ENUM:
		case T_EXIT:
		case T_EXTENDS:
		case T_FILE:
		case T_FINAL:
		case T_FINALLY:
		case T_FN:
		case T_FOR:
		case T_FOREACH:
		case T_FUNC_C:
		case T_FUNCTION:
		case T_GLOBAL:
		case T_GOTO:
		case T_IF:
		case T_IMPLEMENTS:
		case T_INCLUDE:
		case T_INCLUDE_ONCE:
		case T_INSTANCEOF:
		case T_INSTEADOF:
		case T_INTERFACE:
		case T_ISSET:
		case T_LINE:
		case T_LIST:
		case T_MATCH:
		case T_METHOD_C:
		case T_NAMESPACE:
		case T_NEW:
		case T_NS_C:
		case T_PRINT:
		case T_PRIVATE:
		case T_PROTECTED:
		case T_PUBLIC:
		case T_READONLY:
		case T_REQUIRE:
		case T_REQUIRE_ONCE:
		case T_RETURN:
		case T_STATIC:
		case T_SWITCH:
		case T_THROW:
		case T_TRAIT:
		case T_TRY:
		case T_UNSET:
		case T_USE:
		case T_VAR:
		case T_WHILE:
		case T_YIELD:
		case T_YIELD_FROM:
			return true;
	}

	return false;
}

static inline bool lsp_semantic_id_is_string(zend_long id)
{
	return id == T_CONSTANT_ENCAPSED_STRING ||
		id == T_ENCAPSED_AND_WHITESPACE ||
		id == T_START_HEREDOC ||
		id == T_END_HEREDOC
	;
}

static inline bool lsp_semantic_text_is_keyword_literal(zend_string *text)
{
	return zend_string_equals_literal_ci(text, "true") ||
		zend_string_equals_literal_ci(text, "false") ||
		zend_string_equals_literal_ci(text, "null") ||
		zend_string_equals_literal_ci(text, "self") ||
		zend_string_equals_literal_ci(text, "parent")
	;
}

static inline bool lsp_semantic_id_is_significant(zend_long id)
{
	return id != T_WHITESPACE && id != T_COMMENT && id != T_DOC_COMMENT &&
		id != T_OPEN_TAG && id != T_CLOSE_TAG && id != T_OPEN_TAG_WITH_ECHO
	;
}

static inline zend_long lsp_semantic_next_significant_id(HashTable *tokens, uint32_t start, uint32_t count)
{
	zend_long id;
	zval *token;
	uint32_t i;

	for (i = start; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY) {
			continue;
		}

		id = lsp_token_long(token, "id", 0);
		if (lsp_semantic_id_is_significant(id)) {
			return id;
		}
	}

	return 0;
}

static inline bool lsp_semantic_next_is_open_paren(HashTable *tokens, uint32_t start, uint32_t count)
{
	return lsp_semantic_next_significant_id(tokens, start, count) == (zend_long) '(';
}

static inline zend_long lsp_semantic_class_like_type(zend_long keyword_id)
{
	if (keyword_id == T_INTERFACE) {
		return LSP_SEM_INTERFACE;
	}

	if (keyword_id == T_ENUM) {
		return LSP_SEM_ENUM;
	}

	return LSP_SEM_CLASS;
}

static inline bool lsp_semantic_doc_comment_is_deprecated(zend_string *text)
{
	if (!text) {
		return false;
	}

	return zend_memnstr(ZSTR_VAL(text), "@deprecated", sizeof("@deprecated") - 1, ZSTR_VAL(text) + ZSTR_LEN(text)) != NULL;
}

/* Walks backward from (but excluding) start_index collecting the declaration
 * modifiers (static/readonly/visibility/var) and the nearest preceding doc
 * comment, stopping at the previous statement boundary. In param_context
 * (promoted constructor properties) '(' and ',' also terminate the scan so
 * sibling parameters/the function's own modifiers are never crossed. */
static void lsp_semantic_scan_declaration_modifiers(HashTable *tokens, uint32_t start_index, bool param_context, lsp_semantic_decl_info *info)
{
	zend_long i, id;
	zval *token;
	zend_string *text;

	memset(info, 0, sizeof(*info));

	for (i = (zend_long) start_index - 1; i >= 0; i--) {
		token = zend_hash_index_find(tokens, (zend_ulong) i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY) {
			continue;
		}

		id = lsp_token_long(token, "id", 0);

		if (param_context && (id == (zend_long) '(' || id == (zend_long) ',')) {
			break;
		}

		if (id == (zend_long) ';' || id == (zend_long) '{' || id == (zend_long) '}') {
			break;
		}

		if (id == T_STATIC) {
			info->is_static = true;
			info->has_modifier = true;
		} else if (id == T_READONLY) {
			info->is_readonly = true;
			info->has_modifier = true;
		} else if (id == T_PUBLIC || id == T_PROTECTED || id == T_PRIVATE || id == T_VAR) {
			info->has_modifier = true;
		} else if (id == T_DOC_COMMENT) {
			text = lsp_token_string(token, "text");
			if (lsp_semantic_doc_comment_is_deprecated(text)) {
				info->is_deprecated = true;
			}
		}
	}
}

static inline void lsp_semantic_emit(lsp_semantic_emitter *emitter, zend_long line, zend_long column, zend_long length, zend_long type, zend_long modifiers)
{
	zend_long delta_line, delta_column;

	if (length <= 0 || type == LSP_SEM_NONE) {
		return;
	}

	delta_line = line - emitter->previous_line;
	delta_column = delta_line == 0 ? column - emitter->previous_column : column;
	if (delta_line < 0 || delta_column < 0) {
		return;
	}

	add_next_index_long(emitter->data, delta_line);
	add_next_index_long(emitter->data, delta_column);
	add_next_index_long(emitter->data, length);
	add_next_index_long(emitter->data, type);
	add_next_index_long(emitter->data, modifiers);
	emitter->previous_line = line;
	emitter->previous_column = column;
}

static inline zend_long lsp_semantic_token_column(zend_string *text, size_t offset)
{
	const char *value = ZSTR_VAL(text);
	size_t line_start = offset;

	if (offset > ZSTR_LEN(text)) {
		return 0;
	}

	while (line_start > 0 && value[line_start - 1] != '\n') {
		line_start--;
	}

	return lsp_byte_offset_to_utf16_units(value, line_start, offset);
}

/* Shared core for full/range responses: classifies every token in the
 * document, but only emits (and therefore only affects the running delta
 * base) tokens whose line falls within [line_start, line_end] when
 * has_bounds is set. The first emitted token's deltaLine always stays
 * relative to absolute line 0, matching the LSP delta-encoding rules for
 * both full and ranged responses. */
static void lsp_semantic_tokens_collect(lsp_document *document, zval *data, bool has_bounds, zend_long line_start, zend_long line_end)
{
	lsp_semantic_emitter emitter;
	zend_long id, type, line, column, offset, length, depth, class_depth, previous_id, context_id, context_keyword_index, paren_depth, modifiers;
	zend_string *token_text;
	zval *tokens_zv, *token;
	HashTable *tokens;
	uint32_t i, count;
	bool pending_class_body;

	emitter.data = data;
	emitter.previous_line = 0;
	emitter.previous_column = 0;

	tokens_zv = Z_TYPE(document->lsparrot) == IS_ARRAY
		? zend_hash_str_find(Z_ARRVAL(document->lsparrot), "tokens", sizeof("tokens") - 1)
		: NULL
	;
	if (!tokens_zv || Z_TYPE_P(tokens_zv) != IS_ARRAY) {
		return;
	}

	tokens = Z_ARRVAL_P(tokens_zv);
	count = zend_hash_num_elements(tokens);
	depth = 0;
	class_depth = -1;
	previous_id = 0;
	context_id = 0;
	context_keyword_index = -1;
	paren_depth = 0;
	pending_class_body = false;

	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY) {
			continue;
		}

		id = lsp_token_long(token, "id", 0);
		offset = lsp_token_long(token, "offset", 0);
		length = lsp_token_long(token, "length", 0);
		line = lsp_token_long(token, "line", 1) - 1;
		column = lsp_semantic_token_column(document->text, (size_t) offset);
		token_text = lsp_token_string(token, "text");
		type = LSP_SEM_NONE;
		modifiers = 0;

		/* Comments and string literals are deliberately NOT emitted: the
		 * client-side grammar colors them with richer sub-decorations (the
		 * VSCode extension injects a PHPDoc grammar into comment scopes,
		 * and string scopes carry escape-sequence highlighting). Semantic
		 * tokens take precedence over the grammar in VSCode, so emitting
		 * a flat comment/string token here would erase those decorations. */
		if (id == T_COMMENT || id == T_DOC_COMMENT) {
			continue;
		}

		if (lsp_semantic_id_is_string(id)) {
			previous_id = id;

			continue;
		}

		if (id == T_WHITESPACE || id == T_OPEN_TAG || id == T_CLOSE_TAG || id == T_INLINE_HTML) {
			continue;
		}

		if (id == (zend_long) '(') {
			paren_depth++;
		} else if (id == (zend_long) ')') {
			if (paren_depth > 0) {
				paren_depth--;
			}
		}

		if (id == (zend_long) '{') {
			depth++;
			if (pending_class_body) {
				class_depth = depth;
				pending_class_body = false;
			}
		} else if (id == (zend_long) '}') {
			if (class_depth >= 0 && depth == class_depth) {
				class_depth = -1;
			}
			if (depth > 0) {
				depth--;
			}
		}

		if (id == T_LNUMBER || id == T_DNUMBER) {
			type = LSP_SEM_NUMBER;
		} else if (id == T_VARIABLE) {
			bool is_this = token_text && zend_string_equals_literal(token_text, "$this");

			/* $this is a variable, not a keyword: tagging it keyword paints
			 * it like `public`/`return` in clients that trust semantic
			 * tokens over the grammar. */
			type = previous_id == T_PAAMAYIM_NEKUDOTAYIM ? LSP_SEM_PROPERTY : LSP_SEM_VARIABLE;

			/* Property declarations (and promoted constructor properties)
			 * live directly at the class body depth: plain field
			 * declarations sit at class_depth itself, and constructor
			 * parameter lists never bump the brace depth, so promoted
			 * parameters are also observed at class_depth while
			 * paren_depth > 0 distinguishes them from a field statement. */
			if (!is_this && class_depth >= 0 && depth == class_depth) {
				lsp_semantic_decl_info decl_info;

				lsp_semantic_scan_declaration_modifiers(tokens, i, paren_depth > 0, &decl_info);
				if (decl_info.has_modifier) {
					modifiers |= LSP_SEM_MOD_DECLARATION;
					if (decl_info.is_static) {
						modifiers |= LSP_SEM_MOD_STATIC;
					}
					if (decl_info.is_readonly) {
						modifiers |= LSP_SEM_MOD_READONLY;
					}
					if (decl_info.is_deprecated) {
						modifiers |= LSP_SEM_MOD_DEPRECATED;
					}
				}
			}
		} else if (lsp_semantic_id_is_keyword(id)) {
			type = LSP_SEM_KEYWORD;
			if (id == T_CLASS || id == T_INTERFACE || id == T_TRAIT || id == T_ENUM) {
				context_id = id;
				context_keyword_index = (zend_long) i;
			} else if (id == T_FUNCTION || id == T_CONST || id == T_NAMESPACE || id == T_USE ||
				id == T_NEW || id == T_EXTENDS || id == T_IMPLEMENTS || id == T_INSTANCEOF
			) {
				context_id = id;
				if (id == T_FUNCTION) {
					context_keyword_index = (zend_long) i;
				}
			}
		} else if (id == T_STRING || id == T_NAME_QUALIFIED || id == T_NAME_FULLY_QUALIFIED || id == T_NAME_RELATIVE) {
			if (token_text && lsp_semantic_text_is_keyword_literal(token_text) &&
				previous_id != T_OBJECT_OPERATOR && previous_id != T_NULLSAFE_OBJECT_OPERATOR &&
				lsp_semantic_next_significant_id(tokens, i + 1, count) != T_PAAMAYIM_NEKUDOTAYIM
			) {
				type = LSP_SEM_KEYWORD;
			} else if (context_id == T_NAMESPACE || context_id == T_USE) {
				type = context_id == T_NAMESPACE ? LSP_SEM_NAMESPACE : LSP_SEM_CLASS;
				if (id == T_STRING && context_id == T_USE && previous_id == T_AS) {
					type = LSP_SEM_CLASS;
				}
			} else if (context_id == T_CLASS || context_id == T_INTERFACE || context_id == T_TRAIT || context_id == T_ENUM) {
				type = lsp_semantic_class_like_type(context_id);
				if (previous_id != T_EXTENDS && previous_id != T_IMPLEMENTS && previous_id != (zend_long) ',') {
					pending_class_body = true;
					modifiers |= LSP_SEM_MOD_DECLARATION;
					if (context_keyword_index >= 0) {
						lsp_semantic_decl_info decl_info;

						lsp_semantic_scan_declaration_modifiers(tokens, (uint32_t) context_keyword_index, false, &decl_info);
						if (decl_info.is_deprecated) {
							modifiers |= LSP_SEM_MOD_DEPRECATED;
						}
					}
				}
			} else if (context_id == T_NEW || context_id == T_EXTENDS || context_id == T_IMPLEMENTS || context_id == T_INSTANCEOF) {
				type = LSP_SEM_CLASS;
			} else if (context_id == T_FUNCTION) {
				type = class_depth >= 0 ? LSP_SEM_METHOD : LSP_SEM_FUNCTION;
				modifiers |= LSP_SEM_MOD_DECLARATION;
				if (context_keyword_index >= 0) {
					lsp_semantic_decl_info decl_info;

					lsp_semantic_scan_declaration_modifiers(tokens, (uint32_t) context_keyword_index, false, &decl_info);
					if (decl_info.is_static) {
						modifiers |= LSP_SEM_MOD_STATIC;
					}
					if (decl_info.is_deprecated) {
						modifiers |= LSP_SEM_MOD_DEPRECATED;
					}
				}
			} else if (context_id == T_CONST) {
				type = LSP_SEM_ENUM_MEMBER;
			} else if (previous_id == T_OBJECT_OPERATOR || previous_id == T_NULLSAFE_OBJECT_OPERATOR) {
				type = lsp_semantic_next_is_open_paren(tokens, i + 1, count) ? LSP_SEM_METHOD : LSP_SEM_PROPERTY;
			} else if (previous_id == T_PAAMAYIM_NEKUDOTAYIM) {
				if (token_text && zend_string_equals_literal_ci(token_text, "class")) {
					type = LSP_SEM_KEYWORD;
				} else {
					type = lsp_semantic_next_is_open_paren(tokens, i + 1, count) ? LSP_SEM_METHOD : LSP_SEM_ENUM_MEMBER;
				}
			} else if (lsp_semantic_next_significant_id(tokens, i + 1, count) == T_PAAMAYIM_NEKUDOTAYIM) {
				type = LSP_SEM_CLASS;
			} else if (lsp_semantic_next_is_open_paren(tokens, i + 1, count)) {
				type = LSP_SEM_FUNCTION;
			} else if (id != T_STRING) {
				type = LSP_SEM_CLASS;
			} else if (lsp_semantic_next_significant_id(tokens, i + 1, count) == T_VARIABLE) {
				/* Bare identifier directly before a variable: a type hint. */
				type = LSP_SEM_CLASS;
			}
		}

		/* Single-statement contexts end at statement delimiters. */
		if (id == (zend_long) ';' || id == (zend_long) '{' || id == (zend_long) '(' ||
			id == (zend_long) ')' || id == (zend_long) '='
		) {
			context_id = 0;
		}

		if (type != LSP_SEM_NONE && (!has_bounds || (line >= line_start && line <= line_end))) {
			/* Token lengths arrive in bytes; the wire format counts UTF-16
			 * code units. */
			if ((size_t) (offset + length) <= ZSTR_LEN(document->text)) {
				length = lsp_byte_offset_to_utf16_units(ZSTR_VAL(document->text), (size_t) offset, (size_t) (offset + length));
			}
			lsp_semantic_emit(&emitter, line, column, length, type, modifiers);
		}

		if (lsp_semantic_id_is_significant(id)) {
			previous_id = id;
		}
	}
}

/* Self-contained resultId -> {resultId, data} cache keyed by document uri,
 * used to serve textDocument/semanticTokens/full/delta. Entries are
 * overwritten (never merged) on every full/delta computation, so the
 * hashtable never grows past one entry per distinct document uri seen. */
static HashTable lsp_semantic_tokens_cache;
static bool lsp_semantic_tokens_cache_ready = false;

static inline HashTable *lsp_semantic_tokens_cache_ht(void)
{
	if (!lsp_semantic_tokens_cache_ready) {
		zend_hash_init(&lsp_semantic_tokens_cache, 8, NULL, ZVAL_PTR_DTOR, 0);
		lsp_semantic_tokens_cache_ready = true;
	}

	return &lsp_semantic_tokens_cache;
}

extern void lsp_semantic_tokens_cache_clear(void)
{
	if (lsp_semantic_tokens_cache_ready) {
		zend_hash_destroy(&lsp_semantic_tokens_cache);
		lsp_semantic_tokens_cache_ready = false;
	}
}

static zend_string *lsp_semantic_tokens_next_result_id(void)
{
	static zend_ulong lsp_semantic_tokens_result_counter = 0;
	char buf[32];
	int len;

	lsp_semantic_tokens_result_counter++;
	len = snprintf(buf, sizeof(buf), "%lu", (unsigned long) lsp_semantic_tokens_result_counter);

	return zend_string_init(buf, (size_t) (len > 0 ? len : 0), 0);
}

static bool lsp_semantic_tokens_group_equal(HashTable *a, uint32_t group_a, HashTable *b, uint32_t group_b)
{
	uint32_t k;
	zval *va, *vb;

	for (k = 0; k < 5; k++) {
		va = zend_hash_index_find(a, (zend_ulong) (group_a * 5 + k));
		vb = zend_hash_index_find(b, (zend_ulong) (group_b * 5 + k));
		if (!va || !vb || Z_TYPE_P(va) != IS_LONG || Z_TYPE_P(vb) != IS_LONG || Z_LVAL_P(va) != Z_LVAL_P(vb)) {
			return false;
		}
	}

	return true;
}

/* Computes a minimal single-splice SemanticTokensEdit between the previously
 * served flat data array and the freshly computed one, comparing in
 * 5-element (line, col, length, type, modifiers) groups so a splice never
 * lands in the middle of a token. */
static void lsp_semantic_tokens_build_edits(zval *return_value, HashTable *old_ht, HashTable *new_ht)
{
	zval edits, edit, edit_data;
	uint32_t old_len, new_len, old_groups, new_groups, prefix, suffix, max_common, i, k;
	zval *v;

	old_len = zend_hash_num_elements(old_ht);
	new_len = zend_hash_num_elements(new_ht);
	old_groups = old_len / 5;
	new_groups = new_len / 5;

	prefix = 0;
	max_common = old_groups < new_groups ? old_groups : new_groups;
	while (prefix < max_common && lsp_semantic_tokens_group_equal(old_ht, prefix, new_ht, prefix)) {
		prefix++;
	}

	suffix = 0;
	max_common -= prefix;
	while (suffix < max_common &&
		lsp_semantic_tokens_group_equal(old_ht, old_groups - 1 - suffix, new_ht, new_groups - 1 - suffix)
	) {
		suffix++;
	}

	array_init(&edits);
	array_init(&edit);
	add_assoc_long(&edit, "start", (zend_long) (prefix * 5));
	add_assoc_long(&edit, "deleteCount", (zend_long) ((old_groups - prefix - suffix) * 5));

	array_init(&edit_data);
	for (i = prefix; i < new_groups - suffix; i++) {
		for (k = 0; k < 5; k++) {
			v = zend_hash_index_find(new_ht, (zend_ulong) (i * 5 + k));
			add_next_index_long(&edit_data, v && Z_TYPE_P(v) == IS_LONG ? Z_LVAL_P(v) : 0);
		}
	}
	add_assoc_zval(&edit, "data", &edit_data);

	add_next_index_zval(&edits, &edit);
	add_assoc_zval(return_value, "edits", &edits);
}

extern void lsp_lsparrot_semantic_tokens(lsp_server *server, zval *return_value, lsp_document *document)
{
	zval data, cache_data, entry;
	zend_string *result_id;

	(void) server;
	array_init(return_value);
	array_init(&data);
	lsp_semantic_tokens_collect(document, &data, false, 0, 0);

	result_id = lsp_semantic_tokens_next_result_id();

	ZVAL_COPY(&cache_data, &data);
	array_init(&entry);
	add_assoc_str(&entry, "resultId", zend_string_copy(result_id));
	add_assoc_zval(&entry, "data", &cache_data);
	zend_hash_update(lsp_semantic_tokens_cache_ht(), document->uri, &entry);

	add_assoc_str(return_value, "resultId", result_id);
	add_assoc_zval(return_value, "data", &data);
}

/* textDocument/semanticTokens/range: identical classification pass, but only
 * tokens whose (0-based) line falls within params["range"] are emitted. */
extern void lsp_lsparrot_semantic_tokens_range(lsp_server *server, zval *return_value, lsp_document *document, zval *params)
{
	zval data, *range_zv, *start_zv, *end_zv;
	zend_long line_start = 0, line_end = ZEND_LONG_MAX;

	(void) server;
	array_init(return_value);
	array_init(&data);

	range_zv = lsp_array_find(params, "range");
	if (range_zv) {
		start_zv = lsp_array_find(range_zv, "start");
		end_zv = lsp_array_find(range_zv, "end");
		if (start_zv) {
			line_start = lsp_array_long(start_zv, "line", 0);
		}
		if (end_zv) {
			line_end = lsp_array_long(end_zv, "line", ZEND_LONG_MAX);
		}
	}

	lsp_semantic_tokens_collect(document, &data, true, line_start, line_end);
	add_assoc_zval(return_value, "data", &data);
}

/* textDocument/semanticTokens/full/delta: serves a SemanticTokensDelta when
 * params["previousResultId"] matches what is cached for this document uri,
 * otherwise falls back to a full SemanticTokens response (both are valid
 * per the LSP spec). The cache is always refreshed with the newly computed
 * snapshot so the next delta request has a baseline to diff against. */
extern void lsp_lsparrot_semantic_tokens_full_delta(lsp_server *server, zval *return_value, lsp_document *document, zval *params)
{
	zend_string *previous_result_id, *new_result_id;
	zval *cache_entry, *cached_result_id_zv, *cached_data_zv;
	zval new_data, old_data_snapshot, cache_data, entry;
	HashTable *cache;
	bool matched, have_snapshot;

	(void) server;

	previous_result_id = lsp_array_string(params, "previousResultId");
	cache = lsp_semantic_tokens_cache_ht();
	cache_entry = zend_hash_find(cache, document->uri);
	matched = false;
	have_snapshot = false;
	ZVAL_UNDEF(&old_data_snapshot);

	if (previous_result_id && cache_entry && Z_TYPE_P(cache_entry) == IS_ARRAY) {
		cached_result_id_zv = lsp_array_find(cache_entry, "resultId");
		if (cached_result_id_zv && Z_TYPE_P(cached_result_id_zv) == IS_STRING &&
			zend_string_equals(Z_STR_P(cached_result_id_zv), previous_result_id)
		) {
			cached_data_zv = lsp_array_find(cache_entry, "data");
			if (cached_data_zv && Z_TYPE_P(cached_data_zv) == IS_ARRAY) {
				ZVAL_COPY(&old_data_snapshot, cached_data_zv);
				have_snapshot = true;
				matched = true;
			}
		}
	}

	array_init(&new_data);
	lsp_semantic_tokens_collect(document, &new_data, false, 0, 0);
	new_result_id = lsp_semantic_tokens_next_result_id();

	/* Refresh the cache before consuming new_data into the response so the
	 * cached snapshot and the response each hold an independent reference. */
	ZVAL_COPY(&cache_data, &new_data);
	array_init(&entry);
	add_assoc_str(&entry, "resultId", zend_string_copy(new_result_id));
	add_assoc_zval(&entry, "data", &cache_data);
	zend_hash_update(cache, document->uri, &entry);

	array_init(return_value);

	if (matched && have_snapshot) {
		lsp_semantic_tokens_build_edits(return_value, Z_ARRVAL(old_data_snapshot), Z_ARRVAL(new_data));
		add_assoc_str(return_value, "resultId", zend_string_copy(new_result_id));
		zval_ptr_dtor(&new_data);
	} else {
		add_assoc_str(return_value, "resultId", zend_string_copy(new_result_id));
		add_assoc_zval(return_value, "data", &new_data);
	}

	if (have_snapshot) {
		zval_ptr_dtor(&old_data_snapshot);
	}

	zend_string_release(new_result_id);
}
