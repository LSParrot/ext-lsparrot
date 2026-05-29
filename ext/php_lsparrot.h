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

#ifndef PHP_LSPARROT_H
# define PHP_LSPARROT_H

extern zend_module_entry lsparrot_module_entry;
# define phpext_lsparrot_ptr &lsparrot_module_entry

# define PHP_LSPARROT_VERSION "9.9.9"

# if defined(ZTS) && defined(COMPILE_DL_LSPARROT)
ZEND_TSRMLS_CACHE_EXTERN()
# endif

#endif	/* PHP_LSPARROT_H */
