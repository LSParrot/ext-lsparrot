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

/* textDocument/semanticTokens/full generated from the lexer token stream.
 * Legend indexes must match lsp_semantic_token_legend() in this file. */

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

typedef struct _lsp_semantic_emitter {
	zval *data;
	zend_long previous_line;
	zend_long previous_column;
} lsp_semantic_emitter;

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

static inline void lsp_semantic_emit(lsp_semantic_emitter *emitter, zend_long line, zend_long column, zend_long length, zend_long type)
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
	add_next_index_long(emitter->data, 0);
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

	return (zend_long) (offset - line_start);
}

extern void lsp_lsparrot_semantic_tokens(lsp_server *server, zval *return_value, lsp_document *document)
{
	lsp_semantic_emitter emitter;
	zend_long id, type, line, column, offset, length, depth, class_depth, previous_id, context_id;
	zend_string *token_text;
	zval data, *tokens_zv, *token;
	HashTable *tokens;
	uint32_t i, count;
	bool pending_class_body;

	(void) server;
	array_init(return_value);
	array_init(&data);
	emitter.data = &data;
	emitter.previous_line = 0;
	emitter.previous_column = 0;

	tokens_zv = Z_TYPE(document->lsparrot) == IS_ARRAY
		? zend_hash_str_find(Z_ARRVAL(document->lsparrot), "tokens", sizeof("tokens") - 1)
		: NULL
	;
	if (!tokens_zv || Z_TYPE_P(tokens_zv) != IS_ARRAY) {
		add_assoc_zval(return_value, "data", &data);

		return;
	}

	tokens = Z_ARRVAL_P(tokens_zv);
	count = zend_hash_num_elements(tokens);
	depth = 0;
	class_depth = -1;
	previous_id = 0;
	context_id = 0;
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
			type = token_text && zend_string_equals_literal(token_text, "$this")
				? LSP_SEM_KEYWORD
				: (previous_id == T_PAAMAYIM_NEKUDOTAYIM ? LSP_SEM_PROPERTY : LSP_SEM_VARIABLE)
			;
		} else if (lsp_semantic_id_is_keyword(id)) {
			type = LSP_SEM_KEYWORD;
			if (id == T_CLASS || id == T_INTERFACE || id == T_TRAIT || id == T_ENUM) {
				context_id = id;
			} else if (id == T_FUNCTION || id == T_CONST || id == T_NAMESPACE || id == T_USE ||
				id == T_NEW || id == T_EXTENDS || id == T_IMPLEMENTS || id == T_INSTANCEOF
			) {
				context_id = id;
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
				}
			} else if (context_id == T_NEW || context_id == T_EXTENDS || context_id == T_IMPLEMENTS || context_id == T_INSTANCEOF) {
				type = LSP_SEM_CLASS;
			} else if (context_id == T_FUNCTION) {
				type = class_depth >= 0 ? LSP_SEM_METHOD : LSP_SEM_FUNCTION;
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

		if (type != LSP_SEM_NONE) {
			lsp_semantic_emit(&emitter, line, column, length, type);
		}

		if (lsp_semantic_id_is_significant(id)) {
			previous_id = id;
		}
	}

	add_assoc_zval(return_value, "data", &data);
}
