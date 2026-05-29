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

#include "lsp_internal.h"

#if PHP_VERSION_ID >= 80500
static inline const char *lsp_latest_ast_kind_name(zend_ast_kind kind)
{
	switch (kind) {
		case ZEND_AST_ZVAL: return "ZVAL";
		case ZEND_AST_CONSTANT: return "CONSTANT";
		case ZEND_AST_OP_ARRAY: return "OP_ARRAY";
		case ZEND_AST_ZNODE: return "ZNODE";
		case ZEND_AST_FUNC_DECL: return "FUNC_DECL";
		case ZEND_AST_CLOSURE: return "CLOSURE";
		case ZEND_AST_METHOD: return "METHOD";
		case ZEND_AST_CLASS: return "CLASS";
		case ZEND_AST_ARROW_FUNC: return "ARROW_FUNC";
		case ZEND_AST_PROPERTY_HOOK: return "PROPERTY_HOOK";
		case ZEND_AST_ARG_LIST: return "ARG_LIST";
		case ZEND_AST_ARRAY: return "ARRAY";
		case ZEND_AST_ENCAPS_LIST: return "ENCAPS_LIST";
		case ZEND_AST_EXPR_LIST: return "EXPR_LIST";
		case ZEND_AST_STMT_LIST: return "STMT_LIST";
		case ZEND_AST_IF: return "IF";
		case ZEND_AST_SWITCH_LIST: return "SWITCH_LIST";
		case ZEND_AST_CATCH_LIST: return "CATCH_LIST";
		case ZEND_AST_PARAM_LIST: return "PARAM_LIST";
		case ZEND_AST_CLOSURE_USES: return "CLOSURE_USES";
		case ZEND_AST_PROP_DECL: return "PROP_DECL";
		case ZEND_AST_CONST_DECL: return "CONST_DECL";
		case ZEND_AST_CLASS_CONST_DECL: return "CLASS_CONST_DECL";
		case ZEND_AST_NAME_LIST: return "NAME_LIST";
		case ZEND_AST_TRAIT_ADAPTATIONS: return "TRAIT_ADAPTATIONS";
		case ZEND_AST_USE: return "USE";
		case ZEND_AST_TYPE_UNION: return "TYPE_UNION";
		case ZEND_AST_TYPE_INTERSECTION: return "TYPE_INTERSECTION";
		case ZEND_AST_ATTRIBUTE_LIST: return "ATTRIBUTE_LIST";
		case ZEND_AST_ATTRIBUTE_GROUP: return "ATTRIBUTE_GROUP";
		case ZEND_AST_MATCH_ARM_LIST: return "MATCH_ARM_LIST";
		case ZEND_AST_MODIFIER_LIST: return "MODIFIER_LIST";
		case ZEND_AST_MAGIC_CONST: return "MAGIC_CONST";
		case ZEND_AST_TYPE: return "TYPE";
		case ZEND_AST_CONSTANT_CLASS: return "CONSTANT_CLASS";
		case ZEND_AST_CALLABLE_CONVERT: return "CALLABLE_CONVERT";
		case ZEND_AST_VAR: return "VAR";
		case ZEND_AST_CONST: return "CONST";
		case ZEND_AST_UNPACK: return "UNPACK";
		case ZEND_AST_UNARY_PLUS: return "UNARY_PLUS";
		case ZEND_AST_UNARY_MINUS: return "UNARY_MINUS";
		case ZEND_AST_CAST: return "CAST";
		case ZEND_AST_CAST_VOID: return "CAST_VOID";
		case ZEND_AST_EMPTY: return "EMPTY";
		case ZEND_AST_ISSET: return "ISSET";
		case ZEND_AST_SILENCE: return "SILENCE";
		case ZEND_AST_SHELL_EXEC: return "SHELL_EXEC";
		case ZEND_AST_PRINT: return "PRINT";
		case ZEND_AST_INCLUDE_OR_EVAL: return "INCLUDE_OR_EVAL";
		case ZEND_AST_UNARY_OP: return "UNARY_OP";
		case ZEND_AST_PRE_INC: return "PRE_INC";
		case ZEND_AST_PRE_DEC: return "PRE_DEC";
		case ZEND_AST_POST_INC: return "POST_INC";
		case ZEND_AST_POST_DEC: return "POST_DEC";
		case ZEND_AST_YIELD_FROM: return "YIELD_FROM";
		case ZEND_AST_CLASS_NAME: return "CLASS_NAME";
		case ZEND_AST_GLOBAL: return "GLOBAL";
		case ZEND_AST_UNSET: return "UNSET";
		case ZEND_AST_RETURN: return "RETURN";
		case ZEND_AST_LABEL: return "LABEL";
		case ZEND_AST_REF: return "REF";
		case ZEND_AST_HALT_COMPILER: return "HALT_COMPILER";
		case ZEND_AST_ECHO: return "ECHO";
		case ZEND_AST_THROW: return "THROW";
		case ZEND_AST_GOTO: return "GOTO";
		case ZEND_AST_BREAK: return "BREAK";
		case ZEND_AST_CONTINUE: return "CONTINUE";
		case ZEND_AST_PROPERTY_HOOK_SHORT_BODY: return "PROPERTY_HOOK_SHORT_BODY";
		case ZEND_AST_DIM: return "DIM";
		case ZEND_AST_PROP: return "PROP";
		case ZEND_AST_NULLSAFE_PROP: return "NULLSAFE_PROP";
		case ZEND_AST_STATIC_PROP: return "STATIC_PROP";
		case ZEND_AST_CALL: return "CALL";
		case ZEND_AST_CLASS_CONST: return "CLASS_CONST";
		case ZEND_AST_ASSIGN: return "ASSIGN";
		case ZEND_AST_ASSIGN_REF: return "ASSIGN_REF";
		case ZEND_AST_ASSIGN_OP: return "ASSIGN_OP";
		case ZEND_AST_BINARY_OP: return "BINARY_OP";
		case ZEND_AST_GREATER: return "GREATER";
		case ZEND_AST_GREATER_EQUAL: return "GREATER_EQUAL";
		case ZEND_AST_AND: return "AND";
		case ZEND_AST_OR: return "OR";
		case ZEND_AST_ARRAY_ELEM: return "ARRAY_ELEM";
		case ZEND_AST_NEW: return "NEW";
		case ZEND_AST_INSTANCEOF: return "INSTANCEOF";
		case ZEND_AST_YIELD: return "YIELD";
		case ZEND_AST_COALESCE: return "COALESCE";
		case ZEND_AST_ASSIGN_COALESCE: return "ASSIGN_COALESCE";
		case ZEND_AST_STATIC: return "STATIC";
		case ZEND_AST_WHILE: return "WHILE";
		case ZEND_AST_DO_WHILE: return "DO_WHILE";
		case ZEND_AST_IF_ELEM: return "IF_ELEM";
		case ZEND_AST_SWITCH: return "SWITCH";
		case ZEND_AST_SWITCH_CASE: return "SWITCH_CASE";
		case ZEND_AST_DECLARE: return "DECLARE";
		case ZEND_AST_USE_TRAIT: return "USE_TRAIT";
		case ZEND_AST_TRAIT_PRECEDENCE: return "TRAIT_PRECEDENCE";
		case ZEND_AST_METHOD_REFERENCE: return "METHOD_REFERENCE";
		case ZEND_AST_NAMESPACE: return "NAMESPACE";
		case ZEND_AST_USE_ELEM: return "USE_ELEM";
		case ZEND_AST_TRAIT_ALIAS: return "TRAIT_ALIAS";
		case ZEND_AST_GROUP_USE: return "GROUP_USE";
		case ZEND_AST_ATTRIBUTE: return "ATTRIBUTE";
		case ZEND_AST_MATCH: return "MATCH";
		case ZEND_AST_MATCH_ARM: return "MATCH_ARM";
		case ZEND_AST_NAMED_ARG: return "NAMED_ARG";
		case ZEND_AST_PARENT_PROPERTY_HOOK_CALL: return "PARENT_PROPERTY_HOOK_CALL";
		case ZEND_AST_PIPE: return "PIPE";
		case ZEND_AST_METHOD_CALL: return "METHOD_CALL";
		case ZEND_AST_NULLSAFE_METHOD_CALL: return "NULLSAFE_METHOD_CALL";
		case ZEND_AST_STATIC_CALL: return "STATIC_CALL";
		case ZEND_AST_CONDITIONAL: return "CONDITIONAL";
		case ZEND_AST_TRY: return "TRY";
		case ZEND_AST_CATCH: return "CATCH";
		case ZEND_AST_PROP_GROUP: return "PROP_GROUP";
		case ZEND_AST_CONST_ELEM: return "CONST_ELEM";
		case ZEND_AST_CLASS_CONST_GROUP: return "CLASS_CONST_GROUP";
		case ZEND_AST_FOR: return "FOR";
		case ZEND_AST_FOREACH: return "FOREACH";
		case ZEND_AST_ENUM_CASE: return "ENUM_CASE";
		case ZEND_AST_PROP_ELEM: return "PROP_ELEM";
		case ZEND_AST_CONST_ENUM_INIT: return "CONST_ENUM_INIT";
		case ZEND_AST_PARAM: return "PARAM";
		default: return "UNKNOWN";
	}
}

static inline bool lsp_latest_ast_is_decl(zend_ast *ast)
{
	return zend_ast_is_decl(ast);
}

static inline bool lsp_latest_ast_is_opaque_node(zend_ast_kind kind)
{
	return kind == ZEND_AST_OP_ARRAY || kind == ZEND_AST_ZNODE;
}

php_ver_abstract_t php_ver_abstract = {
	lsp_latest_ast_kind_name,
	lsp_latest_ast_is_decl,
	lsp_latest_ast_is_opaque_node
};
#elif PHP_VERSION_ID >= 80400 && PHP_VERSION_ID < 80500
# include "lsp_php84_abstraction.h"

php_ver_abstract_t php_ver_abstract = {
	lsp_php84_ast_kind_name,
	lsp_php84_ast_is_decl,
	lsp_php84_ast_is_opaque_node
};
#elif PHP_VERSION_ID >= 80300 && PHP_VERSION_ID < 80400
# include "lsp_php83_abstraction.h"

php_ver_abstract_t php_ver_abstract = {
	lsp_php83_ast_kind_name,
	lsp_php83_ast_is_decl,
	lsp_php83_ast_is_opaque_node
};
#elif PHP_VERSION_ID >= 80200 && PHP_VERSION_ID < 80300
# include "lsp_php82_abstraction.h"

php_ver_abstract_t php_ver_abstract = {
	lsp_php82_ast_kind_name,
	lsp_php82_ast_is_decl,
	lsp_php82_ast_is_opaque_node
};
#elif PHP_VERSION_ID < 80200
# error "LSParrot requires PHP 8.2 or later"
#endif
