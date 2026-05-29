#ifndef LSP_PHP82_ABSTRACTION_H
# define LSP_PHP82_ABSTRACTION_H

# include "lsp_internal.h"

const char *lsp_php82_ast_kind_name(zend_ast_kind kind);
bool lsp_php82_ast_is_decl(zend_ast *ast);
bool lsp_php82_ast_is_opaque_node(zend_ast_kind kind);

#endif
