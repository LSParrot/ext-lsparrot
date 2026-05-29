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

static inline void lsp_perf_counter_dtor(zval *value)
{
	efree(Z_PTR_P(value));
}

extern void lsp_server_run(zval *options)
{
	lsp_server server;

	memset(&server, 0, sizeof(server));
	lsp_set_lsp_stdio_binary();
	lsp_options_from_zval(&server.options, options);
	lsp_symbol_index_init(&server.symbol_index, &server.options);
	zend_hash_init(&server.documents, 8, NULL, lsp_document_destroy, 0);
	zend_hash_init(&server.member_cache, 8, NULL, ZVAL_PTR_DTOR, 0);
	zend_hash_init(&server.completion_cache, 8, NULL, ZVAL_PTR_DTOR, 0);
	zend_hash_init(&server.type_cache, 64, NULL, ZVAL_PTR_DTOR, 0);
	zend_hash_init(&server.phpstan_projects, 8, NULL, ZVAL_PTR_DTOR, 0);
	zend_hash_init(&server.psalm_projects, 8, NULL, ZVAL_PTR_DTOR, 0);
	zend_hash_init(&server.psalm_ls_project_states, 8, NULL, ZVAL_PTR_DTOR, 0);
	zend_hash_init(&server.psalm_ls_projects, 8, NULL, lsp_psalm_ls_project_destroy, 0);
	zend_hash_init(&server.perf_stats, 8, NULL, lsp_perf_counter_dtor, 0);
	zend_hash_init(&server.runner_sessions, 8, NULL, lsp_runner_session_destroy, 0);
	server.root = zend_string_init(".", sizeof(".") - 1, 0);
	server.phpstan_enabled = false;
	server.psalm_enabled = false;
	server.psalm_ls_enabled = false;
	server.shutdown = false;
	server.saw_shutdown = false;

	lsp_tokens_cache_set_enabled(true);
	lsp_server_loop(&server);
	lsp_tokens_cache_set_enabled(false);
	lsp_transport_shutdown();
	lsp_index_stop_worker(&server);
	lsp_project_index_persist(&server);

	lsp_runner_shutdown_all(&server);
	zend_hash_destroy(&server.runner_sessions);
	lsp_psalm_ls_shutdown_all(&server);
	lsp_analyzer_job_destroy(&server.phpstan_job);
	lsp_analyzer_job_destroy(&server.psalm_job);
	lsp_analyzer_job_destroy(&server.phpstan_completion_job);
	lsp_analyzer_job_destroy(&server.psalm_completion_job);
	zend_hash_destroy(&server.psalm_ls_projects);
	zend_hash_destroy(&server.psalm_ls_project_states);
	zend_hash_destroy(&server.psalm_projects);
	zend_hash_destroy(&server.phpstan_projects);
	zend_hash_destroy(&server.type_cache);
	zend_hash_destroy(&server.completion_cache);
	zend_hash_destroy(&server.member_cache);
	zend_hash_destroy(&server.documents);
	zend_hash_destroy(&server.perf_stats);
	zend_string_release(server.root);
	lsp_symbol_index_destroy(&server.symbol_index);
	lsp_options_destroy(&server.options);

	EG(exit_status) = server.saw_shutdown ? 0 : 1;
}
