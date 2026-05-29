/* This is a generated file, edit the .stub.php file instead.
 * Stub hash: 6f109692d641012bbef0958cbff694a89f107028 */

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_LSParrot_start_lsp, 0, 0, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, options, IS_ARRAY, 0, "[]")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_LSParrot_lsparrot_parse, 0, 1, IS_ARRAY, 0)
	ZEND_ARG_TYPE_INFO(0, code, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, uri, IS_STRING, 1, "null")
ZEND_END_ARG_INFO()

#define arginfo_LSParrot_lsparrot_tokens arginfo_LSParrot_lsparrot_parse

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_LSParrot_lsparrot_version, 0, 0, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_FUNCTION(LSParrot_start_lsp);
ZEND_FUNCTION(LSParrot_lsparrot_parse);
ZEND_FUNCTION(LSParrot_lsparrot_tokens);
ZEND_FUNCTION(LSParrot_lsparrot_version);

static const zend_function_entry ext_functions[] = {
#if (PHP_VERSION_ID >= 80400)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("LSParrot", "start_lsp"), zif_LSParrot_start_lsp, arginfo_LSParrot_start_lsp, 0, NULL, NULL)
#else
	ZEND_RAW_FENTRY(ZEND_NS_NAME("LSParrot", "start_lsp"), zif_LSParrot_start_lsp, arginfo_LSParrot_start_lsp, 0)
#endif
#if (PHP_VERSION_ID >= 80400)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("LSParrot", "lsparrot_parse"), zif_LSParrot_lsparrot_parse, arginfo_LSParrot_lsparrot_parse, 0, NULL, NULL)
#else
	ZEND_RAW_FENTRY(ZEND_NS_NAME("LSParrot", "lsparrot_parse"), zif_LSParrot_lsparrot_parse, arginfo_LSParrot_lsparrot_parse, 0)
#endif
#if (PHP_VERSION_ID >= 80400)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("LSParrot", "lsparrot_tokens"), zif_LSParrot_lsparrot_tokens, arginfo_LSParrot_lsparrot_tokens, 0, NULL, NULL)
#else
	ZEND_RAW_FENTRY(ZEND_NS_NAME("LSParrot", "lsparrot_tokens"), zif_LSParrot_lsparrot_tokens, arginfo_LSParrot_lsparrot_tokens, 0)
#endif
#if (PHP_VERSION_ID >= 80400)
	ZEND_RAW_FENTRY(ZEND_NS_NAME("LSParrot", "lsparrot_version"), zif_LSParrot_lsparrot_version, arginfo_LSParrot_lsparrot_version, 0, NULL, NULL)
#else
	ZEND_RAW_FENTRY(ZEND_NS_NAME("LSParrot", "lsparrot_version"), zif_LSParrot_lsparrot_version, arginfo_LSParrot_lsparrot_version, 0)
#endif
	ZEND_FE_END
};
