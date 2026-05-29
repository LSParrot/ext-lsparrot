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

#define LSP_INDEX_CACHE_MAGIC "LSPARROTIDX"
#define LSP_INDEX_CACHE_MAGIC_SIZE (sizeof(LSP_INDEX_CACHE_MAGIC) - 1u)
#define LSP_INDEX_CACHE_VERSION 1u
#define LSP_INDEX_CACHE_FILE "lsparrot-index.bin"
#define LSP_INDEX_SIGNATURE_OFFSET 1469598103934665603ULL
#define LSP_INDEX_SIGNATURE_PRIME 1099511628211ULL

typedef struct _lsp_index_cache_header {
	char magic[LSP_INDEX_CACHE_MAGIC_SIZE];
	uint32_t version;
	uint32_t symbol_index_version;
	uint64_t signature;
	uint64_t file_count;
	uint64_t used;
	uint64_t capacity;
	uint32_t symbol_count;
	uint32_t root_length;
	uint32_t lsp_version_length;
	uint32_t reserved;
} lsp_index_cache_header;

static inline void lsp_index_declared_symbols_in_file(lsp_server *server, zend_string *path);

static inline bool lsp_path_contains_vendor(zend_string *path)
{
	return lsp_path_value_contains_vendor(ZSTR_VAL(path), ZSTR_LEN(path));
}

static inline bool lsp_path_is_under_root(zend_string *path, zend_string *root)
{
	return lsp_path_is_same_or_under(path, root);
}

static inline bool lsp_composer_project_has_tool(zend_string *project_root, const char *name)
{
	zend_string *tool;

	tool = lsp_tool_project_candidate(project_root, name);
	if (!tool) {
		return false;
	}

	zend_string_release(tool);

	return true;
}

static inline bool lsp_workspace_composer_project_has_tool(zend_string *workspace_root, zend_string *dir, const char *name, uint32_t depth)
{
	const char *entry_name;
	lsp_dir *handle;
	zend_string *composer, *child;
	zend_stat_t st;

	if (depth > 8 || (depth > 0 && lsp_path_is_in_workspace_composer_vendor(workspace_root, dir))) {
		return false;
	}

	composer = lsp_join_path2(dir, "composer.json");
	if (lsp_is_regular_file(composer) && lsp_composer_project_has_tool(dir, name)) {
		zend_string_release(composer);

		return true;
	}
	zend_string_release(composer);

	handle = lsp_dir_open(dir);
	if (!handle) {
		return false;
	}

	while ((entry_name = lsp_dir_read(handle)) != NULL) {
		if (lsp_scan_should_skip_dir_name(entry_name)) {
			continue;
		}

		child = lsp_join_path2(dir, entry_name);
		if (VCWD_STAT(ZSTR_VAL(child), &st) != 0) {
			zend_string_release(child);
			continue;
		}

		if (S_ISDIR(st.st_mode) && lsp_workspace_composer_project_has_tool(workspace_root, child, name, depth + 1)) {
			zend_string_release(child);
			lsp_dir_close(handle);

			return true;
		}

		zend_string_release(child);
	}

	lsp_dir_close(handle);

	return false;
}

static inline bool lsp_tool_available_in_workspace(const char *name, zend_string *root)
{
	if (lsp_tool_available(name, root)) {
		return true;
	}

	return lsp_workspace_composer_project_has_tool(root, root, name, 0);
}

static inline uint64_t lsp_index_signature_mix_bytes(uint64_t signature, const char *value, size_t length)
{
	size_t i;

	for (i = 0; i < length; i++) {
		signature ^= (uint64_t) ((unsigned char) value[i]);
		signature *= LSP_INDEX_SIGNATURE_PRIME;
	}

	return signature;
}

static inline uint64_t lsp_index_signature_mix_u64(uint64_t signature, uint64_t value)
{
	uint32_t i;
	unsigned char byte;

	for (i = 0; i < 8; i++) {
		byte = (unsigned char) ((value >> (i * 8)) & 0xffu);
		signature ^= (uint64_t) byte;
		signature *= LSP_INDEX_SIGNATURE_PRIME;
	}

	return signature;
}

static inline bool lsp_string_ends_with_literal(zend_string *value, const char *suffix)
{
	size_t suffix_length = strlen(suffix);

	if (ZSTR_LEN(value) < suffix_length) {
		return false;
	}

	return memcmp(ZSTR_VAL(value) + ZSTR_LEN(value) - suffix_length, suffix, suffix_length) == 0;
}

static inline zend_string *lsp_index_cache_dir(lsp_server *server)
{
	return strpprintf(0, "%s/.lsparrot", ZSTR_VAL(server->root));
}

static inline zend_string *lsp_index_cache_path(lsp_server *server)
{
	return strpprintf(0, "%s/.lsparrot/%s", ZSTR_VAL(server->root), LSP_INDEX_CACHE_FILE);
}

static inline bool lsp_index_cache_disabled(void)
{
	const char *value;

	value = getenv("LSPARROT_NO_INDEX");

	return value && strcmp(value, "1") == 0;
}

static inline bool lsp_index_signature_should_include_file(const char *name, zend_string *path)
{
	return strcmp(name, "composer.json") == 0 ||
		strcmp(name, "composer.lock") == 0 ||
		lsp_string_ends_with_literal(path, ".php")
	;
}

static inline bool lsp_index_signature_should_include_vendor_composer_file(zend_string *path)
{
	return lsp_string_ends_with_literal(path, ".php") ||
		lsp_string_ends_with_literal(path, ".json")
	;
}

static inline void lsp_index_signature_add_file(zend_string *path, zend_stat_t *st, uint64_t *signature, uint64_t *file_count)
{
	*signature = lsp_index_signature_mix_bytes(*signature, ZSTR_VAL(path), ZSTR_LEN(path));
	*signature = lsp_index_signature_mix_u64(*signature, (uint64_t) st->st_size);
	*signature = lsp_index_signature_mix_u64(*signature, (uint64_t) st->st_mtime);
	*signature = lsp_index_signature_mix_u64(*signature, (uint64_t) lsp_stat_mtime_nsec(st));
	(*file_count)++;
}

static inline void lsp_index_signature_scan_vendor_composer(zend_string *vendor_dir, uint64_t *signature, uint64_t *file_count)
{
	const char *entry_name;
	lsp_dir *handle;
	zend_string *composer_dir, *child;
	zend_stat_t st;

	composer_dir = lsp_join_path2(vendor_dir, "composer");
	handle = lsp_dir_open(composer_dir);
	if (!handle) {
		zend_string_release(composer_dir);

		return;
	}

	while ((entry_name = lsp_dir_read(handle)) != NULL) {
		if (strcmp(entry_name, ".") == 0 || strcmp(entry_name, "..") == 0) {
			continue;
		}

		child = lsp_join_path2(composer_dir, entry_name);
		if (VCWD_STAT(ZSTR_VAL(child), &st) != 0) {
			zend_string_release(child);
			continue;
		}

		if (S_ISREG(st.st_mode) && lsp_index_signature_should_include_vendor_composer_file(child)) {
			lsp_index_signature_add_file(child, &st, signature, file_count);
		}

		zend_string_release(child);
	}

	lsp_dir_close(handle);

	zend_string_release(composer_dir);
}

static inline void lsp_index_signature_scan_dir(zend_string *dir, uint32_t depth, uint64_t *signature, uint64_t *file_count)
{
	const char *entry_name;
	lsp_dir *handle;
	zend_string *child;
	zend_stat_t st;
	bool is_vendor;

	if (depth > 64) {
		return;
	}

	handle = lsp_dir_open(dir);
	if (!handle) {
		return;
	}

	while ((entry_name = lsp_dir_read(handle)) != NULL) {
		if (strcmp(entry_name, ".") == 0 || strcmp(entry_name, "..") == 0) {
			continue;
		}

		is_vendor = strcmp(entry_name, "vendor") == 0;
		if (!is_vendor && (lsp_scan_should_skip_dir_name(entry_name) || strcmp(entry_name, ".lsparrot") == 0)) {
			continue;
		}

		child = lsp_join_path2(dir, entry_name);
		if (VCWD_STAT(ZSTR_VAL(child), &st) != 0) {
			zend_string_release(child);
			continue;
		}

		if (S_ISDIR(st.st_mode)) {
			if (is_vendor) {
				lsp_index_signature_scan_vendor_composer(child, signature, file_count);
			} else {
				lsp_index_signature_scan_dir(child, depth + 1, signature, file_count);
			}
		} else if (S_ISREG(st.st_mode) && lsp_index_signature_should_include_file(entry_name, child)) {
			lsp_index_signature_add_file(child, &st, signature, file_count);
		}

		zend_string_release(child);
	}

	lsp_dir_close(handle);
}

static inline void lsp_index_signature(lsp_server *server, uint64_t *signature, uint64_t *file_count)
{
	*signature = LSP_INDEX_SIGNATURE_OFFSET;
	*file_count = 0;
	*signature = lsp_index_signature_mix_bytes(*signature, ZSTR_VAL(server->root), ZSTR_LEN(server->root));
	*signature = lsp_index_signature_mix_bytes(*signature, PHP_LSPARROT_VERSION, strlen(PHP_LSPARROT_VERSION));
	*signature = lsp_index_signature_mix_u64(*signature, LSP_SYMBOL_INDEX_PAYLOAD_VERSION);

	lsp_index_signature_scan_dir(server->root, 0, signature, file_count);
}

static inline bool lsp_index_cache_write_all(FILE *fp, const void *data, size_t length)
{
	return length == 0 || fwrite(data, 1, length, fp) == length;
}

static inline bool lsp_index_cache_read_all(FILE *fp, void *data, size_t length)
{
	return length == 0 || fread(data, 1, length, fp) == length;
}

static inline bool lsp_index_cache_save(lsp_server *server)
{
	lsp_symbol_index_header *symbol_index_header;
	lsp_index_cache_header cache_header;
	zend_string *dir, *path, *tmp_path;
	uint64_t signature, file_count;
	bool result = false;
	FILE *fp;

	if (lsp_index_cache_disabled()) {
		return false;
	}

	if (!server->symbol_index.available || !server->symbol_index.addr) {
		return false;
	}

	symbol_index_header = (lsp_symbol_index_header *) server->symbol_index.addr;
	if (symbol_index_header->magic != LSP_SYMBOL_INDEX_MAGIC || symbol_index_header->used > symbol_index_header->capacity) {
		return false;
	}

	lsp_index_signature(server, &signature, &file_count);

	dir = lsp_index_cache_dir(server);
	path = lsp_index_cache_path(server);
	tmp_path = strpprintf(0, "%s.tmp.%ld", ZSTR_VAL(path), (long) lsp_current_process_id());
	lsp_mkdir_p(dir);

	memset(&cache_header, 0, sizeof(cache_header));
	memcpy(cache_header.magic, LSP_INDEX_CACHE_MAGIC, LSP_INDEX_CACHE_MAGIC_SIZE);
	cache_header.version = LSP_INDEX_CACHE_VERSION;
	cache_header.symbol_index_version = LSP_SYMBOL_INDEX_PAYLOAD_VERSION;
	cache_header.signature = signature;
	cache_header.file_count = file_count;
	cache_header.used = (uint64_t) symbol_index_header->used;
	cache_header.capacity = (uint64_t) symbol_index_header->capacity;
	cache_header.symbol_count = symbol_index_header->symbol_count;
	cache_header.root_length = (uint32_t) ZSTR_LEN(server->root);
	cache_header.lsp_version_length = (uint32_t) strlen(PHP_LSPARROT_VERSION);

	fp = fopen(ZSTR_VAL(tmp_path), "wb");
	if (fp) {
		result = lsp_index_cache_write_all(fp, &cache_header, sizeof(cache_header)) &&
			lsp_index_cache_write_all(fp, ZSTR_VAL(server->root), ZSTR_LEN(server->root)) &&
			lsp_index_cache_write_all(fp, PHP_LSPARROT_VERSION, strlen(PHP_LSPARROT_VERSION)) &&
			lsp_index_cache_write_all(fp, server->symbol_index.addr, symbol_index_header->used)
		;
		result = fclose(fp) == 0 && result;

		if (result) {
			result = VCWD_RENAME(ZSTR_VAL(tmp_path), ZSTR_VAL(path)) == 0;
		}

		if (!result) {
			VCWD_UNLINK(ZSTR_VAL(tmp_path));
		}
	}

	zend_string_release(tmp_path);
	zend_string_release(path);
	zend_string_release(dir);

	return result;
}

static inline bool lsp_index_cache_load_ex(lsp_server *server, bool verify_signature)
{
	lsp_index_cache_header cache_header;
	lsp_symbol_index_header *payload_header, *symbol_index_header;
	zend_string *path;
	uint64_t signature, file_count;
	bool result = false;
	void *payload;
	char *root_buffer, *version_buffer;
	FILE *fp;

	if (lsp_index_cache_disabled()) {
		return false;
	}

	if (!server->symbol_index.available || !server->symbol_index.addr) {
		return false;
	}

	path = lsp_index_cache_path(server);
	fp = fopen(ZSTR_VAL(path), "rb");
	if (!fp) {
		zend_string_release(path);

		return false;
	}

	root_buffer = NULL;
	version_buffer = NULL;
	payload = NULL;

	if (!lsp_index_cache_read_all(fp, &cache_header, sizeof(cache_header)) ||
		memcmp(cache_header.magic, LSP_INDEX_CACHE_MAGIC, LSP_INDEX_CACHE_MAGIC_SIZE) != 0 ||
		cache_header.version != LSP_INDEX_CACHE_VERSION ||
		cache_header.symbol_index_version != LSP_SYMBOL_INDEX_PAYLOAD_VERSION ||
		cache_header.root_length != ZSTR_LEN(server->root) ||
		cache_header.lsp_version_length != strlen(PHP_LSPARROT_VERSION) ||
		cache_header.root_length > 1024u * 1024u ||
		cache_header.lsp_version_length > 1024u ||
		cache_header.used < sizeof(lsp_symbol_index_header) ||
		cache_header.used > (uint64_t) server->symbol_index.size
	) {
		goto done;
	}

	root_buffer = emalloc((size_t) cache_header.root_length + 1);
	version_buffer = emalloc((size_t) cache_header.lsp_version_length + 1);
	if (!lsp_index_cache_read_all(fp, root_buffer, (size_t) cache_header.root_length) ||
		!lsp_index_cache_read_all(fp, version_buffer, (size_t) cache_header.lsp_version_length)
	) {
		goto done;
	}

	root_buffer[cache_header.root_length] = '\0';
	version_buffer[cache_header.lsp_version_length] = '\0';
	if (memcmp(root_buffer, ZSTR_VAL(server->root), ZSTR_LEN(server->root)) != 0 ||
		memcmp(version_buffer, PHP_LSPARROT_VERSION, strlen(PHP_LSPARROT_VERSION)) != 0
	) {
		goto done;
	}

	if (verify_signature) {
		lsp_index_signature(server, &signature, &file_count);
		if (signature != cache_header.signature || file_count != cache_header.file_count) {
			goto done;
		}
	}

	payload = emalloc((size_t) cache_header.used);
	if (!lsp_index_cache_read_all(fp, payload, (size_t) cache_header.used)) {
		goto done;
	}

	payload_header = (lsp_symbol_index_header *) payload;
	if (payload_header->magic != LSP_SYMBOL_INDEX_MAGIC ||
		payload_header->used != (size_t) cache_header.used ||
		payload_header->used > payload_header->capacity ||
		payload_header->symbol_count != cache_header.symbol_count
	) {
		goto done;
	}

	memcpy(server->symbol_index.addr, payload, (size_t) cache_header.used);
	symbol_index_header = (lsp_symbol_index_header *) server->symbol_index.addr;
	symbol_index_header->capacity = server->symbol_index.size;
	symbol_index_header->generation++;
	lsp_symbol_index_rebuild_keys(&server->symbol_index);
	result = true;

done:
	if (payload) {
		efree(payload);
	}

	if (version_buffer) {
		efree(version_buffer);
	}

	if (root_buffer) {
		efree(root_buffer);
	}

	fclose(fp);
	zend_string_release(path);

	return result;
}

static inline char lsp_ast_class_like_symbol_kind(zend_ast_decl *decl)
{
	if ((decl->flags & ZEND_ACC_INTERFACE) != 0) {
		return LSP_SYMBOL_INTERFACE;
	}

	if ((decl->flags & ZEND_ACC_TRAIT) != 0) {
		return LSP_SYMBOL_TRAIT;
	}

	if ((decl->flags & ZEND_ACC_ENUM) != 0) {
		return LSP_SYMBOL_ENUM;
	}

	return LSP_SYMBOL_CLASS;
}

static inline char lsp_class_like_symbol_kind_from_token(zval *token)
{
	if (lsp_token_name_equals(token, "T_INTERFACE")) {
		return LSP_SYMBOL_INTERFACE;
	}

	if (lsp_token_name_equals(token, "T_TRAIT")) {
		return LSP_SYMBOL_TRAIT;
	}

	if (lsp_token_name_equals(token, "T_ENUM")) {
		return LSP_SYMBOL_ENUM;
	}

	return LSP_SYMBOL_CLASS;
}

static inline bool lsp_index_ast_name_matches(zend_string *name, const char *label_value, size_t label_length)
{
	return name &&
		ZSTR_LEN(name) == label_length &&
		strncasecmp(ZSTR_VAL(name), label_value, label_length) == 0
	;
}

static inline bool lsp_index_find_class_like_kind_in_ast(zend_ast *ast, const char *label_value, size_t label_length, char *kind)
{
	zend_ast_list *list;
	zend_ast_decl *decl;
	uint32_t i, count;

	if (!ast) {
		return false;
	}

	if (zend_ast_is_list(ast)) {
		list = zend_ast_get_list(ast);
		for (i = 0; i < list->children; i++) {
			if (lsp_index_find_class_like_kind_in_ast(list->child[i], label_value, label_length, kind)) {
				return true;
			}
		}

		return false;
	}

	if (ast->kind == ZEND_AST_CLASS) {
		decl = (zend_ast_decl *) ast;
		if ((decl->flags & ZEND_ACC_ANON_CLASS) == 0 && lsp_index_ast_name_matches(decl->name, label_value, label_length)) {
			*kind = lsp_ast_class_like_symbol_kind(decl);

			return true;
		}

		return false;
	}

	if (zend_ast_is_special(ast) || php_ver_abstract.ast_is_opaque_node(ast->kind)) {
		return false;
	}

	count = zend_ast_get_num_children(ast);
	for (i = 0; i < count; i++) {
		if (lsp_index_find_class_like_kind_in_ast(ast->child[i], label_value, label_length, kind)) {
			return true;
		}
	}

	return false;
}

static inline char lsp_class_like_symbol_kind_for_file_tokens(zend_string *contents, zend_string *fqcn)
{
	const char *label_value;
	zend_string *label;
	zval tokens_zv, *token;
	HashTable *tokens;
	uint32_t i, count;
	size_t label_length;
	char kind;

	kind = LSP_SYMBOL_CLASS;
	ZVAL_UNDEF(&tokens_zv);
	lsp_lsparrot_tokens_to_zval(&tokens_zv, contents);
	if (Z_TYPE(tokens_zv) != IS_ARRAY) {
		if (!Z_ISUNDEF(tokens_zv)) {
			zval_ptr_dtor(&tokens_zv);
		}

		return kind;
	}

	label_value = lsp_basename_from_fqcn(ZSTR_VAL(fqcn), ZSTR_LEN(fqcn), &label_length);
	tokens = Z_ARRVAL(tokens_zv);
	count = zend_hash_num_elements(tokens);
	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY || !lsp_token_is_class_like(token)) {
			continue;
		}

		label = lsp_next_string_token(tokens, i + 1);
		if (!label) {
			continue;
		}

		if (ZSTR_LEN(label) == label_length && strncasecmp(ZSTR_VAL(label), label_value, label_length) == 0) {
			kind = lsp_class_like_symbol_kind_from_token(token);
			break;
		}
	}

	zval_ptr_dtor(&tokens_zv);

	return kind;
}

static inline char lsp_class_like_symbol_kind_for_file(zend_string *path, zend_string *fqcn)
{
	const char *label_value;
	zend_arena *ast_arena;
	zend_ast *ast;
	zend_string *contents;
	size_t label_length;
	char kind;

	kind = LSP_SYMBOL_CLASS;
	contents = lsp_read_file(path);
	if (contents == zend_empty_string) {
		return kind;
	}

	ast = lsp_compile_string_to_ast_silent(contents, path, &ast_arena);
	if (!ast) {
		kind = lsp_class_like_symbol_kind_for_file_tokens(contents, fqcn);
		zend_string_release(contents);

		return kind;
	}
	zend_string_release(contents);

	label_value = lsp_basename_from_fqcn(ZSTR_VAL(fqcn), ZSTR_LEN(fqcn), &label_length);
	lsp_index_find_class_like_kind_in_ast(ast, label_value, label_length, &kind);
	lsp_compiled_ast_destroy(ast, ast_arena);

	return kind;
}

static inline void lsp_scan_psr4_dir(lsp_server *server, zend_string *dir, uint32_t depth)
{
	const char *entry_name;
	lsp_dir *handle;
	zend_string *child;
	zend_stat_t st;

	if (depth > 32 || !lsp_path_is_under_root(dir, server->root) || lsp_path_contains_vendor(dir)) {
		return;
	}

	handle = lsp_dir_open(dir);
	if (!handle) {
		return;
	}

	while ((entry_name = lsp_dir_read(handle)) != NULL) {
		if (strcmp(entry_name, ".") == 0 || strcmp(entry_name, "..") == 0) {
			continue;
		}

		child = lsp_join_path2(dir, entry_name);
		if (VCWD_STAT(ZSTR_VAL(child), &st) != 0) {
			zend_string_release(child);
			continue;
		}

		if (S_ISDIR(st.st_mode)) {
			lsp_scan_psr4_dir(server, child, depth + 1);
		} else if (S_ISREG(st.st_mode) && lsp_string_ends_with_literal(child, ".php")) {
			lsp_index_declared_symbols_in_file(server, child);
		}

		zend_string_release(child);
	}

	lsp_dir_close(handle);
}

static inline void lsp_scan_psr0_dir(lsp_server *server, zend_string *dir, uint32_t depth)
{
	const char *entry_name;
	lsp_dir *handle;
	zend_string *child;
	zend_stat_t st;

	if (depth > 32 || !lsp_path_is_under_root(dir, server->root) || lsp_path_contains_vendor(dir)) {
		return;
	}

	handle = lsp_dir_open(dir);
	if (!handle) {
		return;
	}

	while ((entry_name = lsp_dir_read(handle)) != NULL) {
		if (strcmp(entry_name, ".") == 0 || strcmp(entry_name, "..") == 0) {
			continue;
		}

		child = lsp_join_path2(dir, entry_name);
		if (VCWD_STAT(ZSTR_VAL(child), &st) != 0) {
			zend_string_release(child);
			continue;
		}

		if (S_ISDIR(st.st_mode)) {
			lsp_scan_psr0_dir(server, child, depth + 1);
		} else if (S_ISREG(st.st_mode) && lsp_string_ends_with_literal(child, ".php")) {
			lsp_index_declared_symbols_in_file(server, child);
		}

		zend_string_release(child);
	}

	lsp_dir_close(handle);
}

static inline void lsp_index_classmap(lsp_server *server, zend_string *composer_dir)
{
	zend_string *classmap_path = lsp_join_path2(composer_dir, "autoload_classmap.php"), *class_name;
	zval *path_zv, classmap;

	if (!lsp_include_array_file(classmap_path, &classmap)) {
		zend_string_release(classmap_path);
		return;
	}

	ZEND_HASH_FOREACH_STR_KEY_VAL(Z_ARRVAL(classmap), class_name, path_zv) {
		if (!class_name || Z_TYPE_P(path_zv) != IS_STRING) {
			continue;
		}
		lsp_symbol_index_add_symbol_kind(&server->symbol_index, lsp_class_like_symbol_kind_for_file(Z_STR_P(path_zv), class_name), class_name, Z_STR_P(path_zv));
	} ZEND_HASH_FOREACH_END();

	zval_ptr_dtor(&classmap);
	zend_string_release(classmap_path);
}

static inline void lsp_index_psr4(lsp_server *server, zend_string *composer_dir)
{
	zend_string *psr4_path = lsp_join_path2(composer_dir, "autoload_psr4.php"), *prefix;
	zval *dirs_zv, psr4, *dir_zv;

	if (!lsp_include_array_file(psr4_path, &psr4)) {
		zend_string_release(psr4_path);
		return;
	}

	ZEND_HASH_FOREACH_STR_KEY_VAL(Z_ARRVAL(psr4), prefix, dirs_zv) {
		if (!prefix || Z_TYPE_P(dirs_zv) != IS_ARRAY) {
			continue;
		}

		ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(dirs_zv), dir_zv) {
			if (Z_TYPE_P(dir_zv) != IS_STRING) {
				continue;
			}
			lsp_scan_psr4_dir(server, Z_STR_P(dir_zv), 0);
		} ZEND_HASH_FOREACH_END();
	} ZEND_HASH_FOREACH_END();

	zval_ptr_dtor(&psr4);
	zend_string_release(psr4_path);
}

static inline void lsp_index_psr0(lsp_server *server, zend_string *composer_dir)
{
	zend_string *psr0_path = lsp_join_path2(composer_dir, "autoload_namespaces.php"), *prefix;
	zval *dirs_zv, *dir_zv, psr0;

	if (!lsp_include_array_file(psr0_path, &psr0)) {
		zend_string_release(psr0_path);

		return;
	}

	ZEND_HASH_FOREACH_STR_KEY_VAL(Z_ARRVAL(psr0), prefix, dirs_zv) {
		if (!prefix) {
			continue;
		}

		if (Z_TYPE_P(dirs_zv) == IS_STRING) {
			lsp_scan_psr0_dir(server, Z_STR_P(dirs_zv), 0);
			continue;
		}

		if (Z_TYPE_P(dirs_zv) != IS_ARRAY) {
			continue;
		}

		ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(dirs_zv), dir_zv) {
			if (Z_TYPE_P(dir_zv) != IS_STRING) {
				continue;
			}
			lsp_scan_psr0_dir(server, Z_STR_P(dir_zv), 0);
		} ZEND_HASH_FOREACH_END();
	} ZEND_HASH_FOREACH_END();

	zval_ptr_dtor(&psr0);
	zend_string_release(psr0_path);
}

static inline void lsp_index_function_symbol(lsp_server *server, zend_string *namespace_name, zend_string *name, zend_string *path)
{
	zend_string *fqfn;

	if (namespace_name != zend_empty_string && ZSTR_LEN(namespace_name) > 0) {
		fqfn = strpprintf(0, "%s\\%s", ZSTR_VAL(namespace_name), ZSTR_VAL(name));
	} else {
		fqfn = zend_string_copy(name);
	}

	lsp_symbol_index_add_symbol_kind(&server->symbol_index, LSP_SYMBOL_FUNCTION, fqfn, path);

	zend_string_release(fqfn);
}

static inline void lsp_index_named_symbol(lsp_server *server, char kind, zend_string *namespace_name, zend_string *name, zend_string *path)
{
	zend_string *fqcn;

	if (namespace_name != zend_empty_string && ZSTR_LEN(namespace_name) > 0) {
		fqcn = strpprintf(0, "%s\\%s", ZSTR_VAL(namespace_name), ZSTR_VAL(name));
	} else {
		fqcn = zend_string_copy(name);
	}

	lsp_symbol_index_add_symbol_kind(&server->symbol_index, kind, fqcn, path);

	zend_string_release(fqcn);
}

static inline bool lsp_index_token_is_namespace_name(zval *token)
{
	return lsp_token_name_equals(token, "T_STRING") ||
		lsp_token_name_equals(token, "T_NAME_QUALIFIED") ||
		lsp_token_name_equals(token, "T_NAME_FULLY_QUALIFIED") ||
		lsp_token_name_equals(token, "T_NAME_RELATIVE") ||
		lsp_token_name_equals(token, "T_NS_SEPARATOR")
	;
}

static inline zend_string *lsp_index_namespace_from_tokens(HashTable *tokens, uint32_t start)
{
	zend_string *text;
	zval *token;
	smart_str namespace_name = {0};
	uint32_t i, count;

	count = zend_hash_num_elements(tokens);
	for (i = start; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY) {
			continue;
		}

		if (lsp_token_name_equals(token, "T_WHITESPACE")) {
			continue;
		}

		if (lsp_token_is_char(token, ';') || lsp_token_is_char(token, '{')) {
			break;
		}

		if (!lsp_index_token_is_namespace_name(token)) {
			continue;
		}

		text = lsp_token_string(token, "text");
		if (!text) {
			continue;
		}

		if (ZSTR_LEN(text) > 0 && ZSTR_VAL(text)[0] == '\\') {
			smart_str_appendl(&namespace_name, ZSTR_VAL(text) + 1, ZSTR_LEN(text) - 1);
		} else {
			smart_str_append(&namespace_name, text);
		}
	}

	if (!namespace_name.s) {
		return zend_empty_string;
	}

	smart_str_0(&namespace_name);

	return namespace_name.s;
}

static inline void lsp_index_declared_symbols_from_tokens(lsp_server *server, zend_string *contents, zend_string *path)
{
	zend_long depth, class_depth;
	zend_string *namespace_name, *name;
	zval tokens_zv, *token;
	HashTable *tokens;
	uint32_t i, count;
	bool pending_class_like;

	depth = 0;
	class_depth = -1;
	pending_class_like = false;
	ZVAL_UNDEF(&tokens_zv);
	lsp_lsparrot_tokens_to_zval(&tokens_zv, contents);
	if (Z_TYPE(tokens_zv) != IS_ARRAY) {
		if (!Z_ISUNDEF(tokens_zv)) {
			zval_ptr_dtor(&tokens_zv);
		}
		return;
	}

	tokens = Z_ARRVAL(tokens_zv);
	count = zend_hash_num_elements(tokens);
	namespace_name = zend_empty_string;
	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY) {
			continue;
		}

		if (lsp_token_name_equals(token, "T_NAMESPACE")) {
			if (namespace_name != zend_empty_string) {
				zend_string_release(namespace_name);
			}
			namespace_name = lsp_index_namespace_from_tokens(tokens, i + 1);

			continue;
		}

		if (lsp_token_is_class_like(token)) {
			name = lsp_next_string_token(tokens, i + 1);
			if (name && class_depth < 0) {
				lsp_index_named_symbol(server, lsp_class_like_symbol_kind_from_token(token), namespace_name, name, path);
			}

			pending_class_like = true;

			continue;
		}

		if (lsp_token_is_char(token, '{')) {
			depth++;

			if (pending_class_like) {
				class_depth = depth;
				pending_class_like = false;
			}

			continue;
		}

		if (lsp_token_is_char(token, '}')) {
			if (class_depth >= 0 && depth == class_depth) {
				class_depth = -1;
			}

			if (depth > 0) {
				depth--;
			}

			continue;
		}

		if (lsp_token_name_equals(token, "T_FUNCTION") && class_depth < 0) {
			name = lsp_next_function_name_token(tokens, i + 1);
			if (name) {
				lsp_index_function_symbol(server, namespace_name, name, path);
			}

			continue;
		}

		if (lsp_token_name_equals(token, "T_CONST") && class_depth < 0) {
			name = lsp_next_string_token(tokens, i + 1);
			if (name) {
				lsp_index_named_symbol(server, LSP_SYMBOL_CONSTANT, namespace_name, name, path);
			}
		}
	}

	if (namespace_name != zend_empty_string) {
		zend_string_release(namespace_name);
	}

	zval_ptr_dtor(&tokens_zv);
}

static inline zend_string *lsp_index_ast_name_copy(zend_ast *ast)
{
	zend_string *name;

	name = lsp_ast_string_value(ast);
	if (!name || ZSTR_LEN(name) == 0) {
		return zend_empty_string;
	}

	if (ZSTR_VAL(name)[0] == '\\') {
		if (ZSTR_LEN(name) == 1) {
			return zend_empty_string;
		}

		return zend_string_init(ZSTR_VAL(name) + 1, ZSTR_LEN(name) - 1, 0);
	}

	return zend_string_copy(name);
}

static inline void lsp_index_const_decl_symbols(lsp_server *server, zend_ast *ast, zend_string *namespace_name, zend_string *path)
{
	zend_ast_list *list;
	zend_ast *elem;
	zend_string *name;
	uint32_t i;

	if (!ast || !zend_ast_is_list(ast)) {
		return;
	}

	list = zend_ast_get_list(ast);
	for (i = 0; i < list->children; i++) {
		elem = list->child[i];
		if (!elem || elem->kind != ZEND_AST_CONST_ELEM) {
			continue;
		}

		name = lsp_ast_string_value(elem->child[0]);
		if (name) {
			lsp_index_named_symbol(server, LSP_SYMBOL_CONSTANT, namespace_name, name, path);
		}
	}
}

static inline void lsp_index_declared_symbols_from_ast(lsp_server *server, zend_ast *ast, zend_string **namespace_name, zend_string *path, bool in_class)
{
	zend_ast_list *list;
	zend_ast_decl *decl;
	zend_string *node_namespace;
	uint32_t i, count;

	if (!ast) {
		return;
	}

	if (ast->kind == ZEND_AST_CONST_DECL) {
		if (!in_class) {
			lsp_index_const_decl_symbols(server, ast, *namespace_name, path);
		}

		return;
	}

	if (zend_ast_is_list(ast)) {
		list = zend_ast_get_list(ast);
		for (i = 0; i < list->children; i++) {
			lsp_index_declared_symbols_from_ast(server, list->child[i], namespace_name, path, in_class);
		}

		return;
	}

	if (ast->kind == ZEND_AST_NAMESPACE) {
		node_namespace = lsp_index_ast_name_copy(ast->child[0]);
		if (ast->child[1]) {
			lsp_index_declared_symbols_from_ast(server, ast->child[1], &node_namespace, path, false);
			if (node_namespace != zend_empty_string) {
				zend_string_release(node_namespace);
			}
		} else {
			if (*namespace_name != zend_empty_string) {
				zend_string_release(*namespace_name);
			}
			*namespace_name = node_namespace;
		}

		return;
	}

	if (ast->kind == ZEND_AST_CLASS) {
		decl = (zend_ast_decl *) ast;
		if (!in_class && (decl->flags & ZEND_ACC_ANON_CLASS) == 0 && decl->name) {
			lsp_index_named_symbol(server, lsp_ast_class_like_symbol_kind(decl), *namespace_name, decl->name, path);
		}

		return;
	}

	if (ast->kind == ZEND_AST_FUNC_DECL) {
		decl = (zend_ast_decl *) ast;
		if (!in_class && decl->name) {
			lsp_index_function_symbol(server, *namespace_name, decl->name, path);
		}
		if (!in_class) {
			lsp_index_declared_symbols_from_ast(server, decl->child[2], namespace_name, path, false);
		}

		return;
	}

	if (zend_ast_is_special(ast) || php_ver_abstract.ast_is_opaque_node(ast->kind)) {
		return;
	}

	count = zend_ast_get_num_children(ast);
	for (i = 0; i < count; i++) {
		lsp_index_declared_symbols_from_ast(server, ast->child[i], namespace_name, path, in_class);
	}
}

static inline void lsp_index_declared_symbols_in_contents(lsp_server *server, zend_string *contents, zend_string *path)
{
	zend_arena *ast_arena;
	zend_ast *ast;
	zend_string *namespace_name;

	if (contents == zend_empty_string) {
		return;
	}

	ast = lsp_compile_string_to_ast_silent(contents, path, &ast_arena);
	if (!ast) {
		lsp_index_declared_symbols_from_tokens(server, contents, path);

		return;
	}

	namespace_name = zend_empty_string;
	lsp_index_declared_symbols_from_ast(server, ast, &namespace_name, path, false);

	if (namespace_name != zend_empty_string) {
		zend_string_release(namespace_name);
	}

	lsp_compiled_ast_destroy(ast, ast_arena);
}

static inline void lsp_index_declared_symbols_in_file(lsp_server *server, zend_string *path)
{
	zend_string *contents;

	contents = lsp_read_file(path);
	if (contents == zend_empty_string) {
		return;
	}

	lsp_index_declared_symbols_in_contents(server, contents, path);
	zend_string_release(contents);
}

static inline void lsp_index_declared_symbols_in_dir(lsp_server *server, zend_string *dir, uint32_t depth)
{
	const char *entry_name;
	lsp_dir *handle;
	zend_string *child;
	zend_stat_t st;

	if (depth > 32 || lsp_path_contains_vendor(dir) || (depth > 0 && lsp_path_is_in_workspace_composer_vendor(server->root, dir))) {
		return;
	}

	handle = lsp_dir_open(dir);
	if (!handle) {
		return;
	}

	while ((entry_name = lsp_dir_read(handle)) != NULL) {
		if (lsp_scan_should_skip_dir_name(entry_name)) {
			continue;
		}

		child = lsp_join_path2(dir, entry_name);
		if (VCWD_STAT(ZSTR_VAL(child), &st) != 0) {
			zend_string_release(child);

			continue;
		}

		if (S_ISDIR(st.st_mode)) {
			lsp_index_declared_symbols_in_dir(server, child, depth + 1);
		} else if (S_ISREG(st.st_mode) && lsp_string_ends_with_literal(child, ".php")) {
			lsp_index_declared_symbols_in_file(server, child);
		}

		zend_string_release(child);
	}

	lsp_dir_close(handle);
}

static inline void lsp_index_declared_symbols_in_path(lsp_server *server, zend_string *path)
{
	zend_stat_t st;

	if (!lsp_path_is_under_root(path, server->root) || VCWD_STAT(ZSTR_VAL(path), &st) != 0) {
		return;
	}

	if (S_ISDIR(st.st_mode)) {
		lsp_index_declared_symbols_in_dir(server, path, 0);
		return;
	}

	if (S_ISREG(st.st_mode) && lsp_string_ends_with_literal(path, ".php")) {
		lsp_index_declared_symbols_in_file(server, path);
	}
}

static inline void lsp_index_composer_project(lsp_server *server, zend_string *project_root)
{
	zend_string *vendor_dir, *composer_dir;
	zval paths, *path_zv;

	vendor_dir = lsp_composer_vendor_dir(project_root);
	composer_dir = lsp_join_path2(vendor_dir, "composer");

	lsp_index_classmap(server, composer_dir);
	lsp_index_psr4(server, composer_dir);
	lsp_index_psr0(server, composer_dir);

	lsp_composer_analysis_paths(project_root, &paths);
	if (zend_hash_num_elements(Z_ARRVAL(paths)) > 0) {
		ZEND_HASH_FOREACH_VAL(Z_ARRVAL(paths), path_zv) {
			if (Z_TYPE_P(path_zv) == IS_STRING) {
				lsp_index_declared_symbols_in_path(server, Z_STR_P(path_zv));
			}
		} ZEND_HASH_FOREACH_END();
	}
	zval_ptr_dtor(&paths);

	zend_string_release(composer_dir);
	zend_string_release(vendor_dir);
}

static inline void lsp_index_workspace_composer_projects(lsp_server *server, zend_string *dir, uint32_t depth)
{
	const char *entry_name;
	lsp_dir *handle;
	zend_string *composer, *child;
	zend_stat_t st;

	if (depth > 8 || lsp_path_contains_vendor(dir) || (depth > 0 && lsp_path_is_in_workspace_composer_vendor(server->root, dir))) {
		return;
	}

	composer = lsp_join_path2(dir, "composer.json");
	if (depth > 0 && lsp_is_regular_file(composer)) {
		lsp_index_composer_project(server, dir);
	}
	zend_string_release(composer);

	handle = lsp_dir_open(dir);
	if (!handle) {
		return;
	}

	while ((entry_name = lsp_dir_read(handle)) != NULL) {
		if (lsp_scan_should_skip_dir_name(entry_name)) {
			continue;
		}

		child = lsp_join_path2(dir, entry_name);
		if (VCWD_STAT(ZSTR_VAL(child), &st) != 0) {
			zend_string_release(child);

			continue;
		}

		if (S_ISDIR(st.st_mode)) {
			lsp_index_workspace_composer_projects(server, child, depth + 1);
		}

		zend_string_release(child);
	}

	lsp_dir_close(handle);
}

static inline void lsp_build_project_index_sync(lsp_server *server)
{
	if (!server->symbol_index.available) {
		return;
	}

	lsp_symbol_index_reset(&server->symbol_index);
	lsp_index_composer_project(server, server->root);
	lsp_index_workspace_composer_projects(server, server->root, 0);
	lsp_index_cache_save(server);
}

extern bool lsp_include_array_file(zend_string *path, zval *return_value)
{
	zend_file_handle file_handle;

	if (VCWD_ACCESS(ZSTR_VAL(path), F_OK) != 0) {
		return false;
	}

	ZVAL_UNDEF(return_value);
	zend_stream_init_filename_ex(&file_handle, path);
	if (zend_execute_scripts(ZEND_REQUIRE, return_value, 1, &file_handle) != SUCCESS || EG(exception)) {
		if (EG(exception)) {
			zend_clear_exception();
		}

		if (!Z_ISUNDEF_P(return_value)) {
			zval_ptr_dtor(return_value);
			ZVAL_UNDEF(return_value);
		}

		return false;
	}

	if (Z_TYPE_P(return_value) != IS_ARRAY) {
		if (!Z_ISUNDEF_P(return_value)) {
			zval_ptr_dtor(return_value);
			ZVAL_UNDEF(return_value);
		}

		return false;
	}

	return true;
}

#if LSP_HAVE_POSIX_PROCESS
/* Fork a child that performs the full project scan and persists the result
 * to the .lsparrot cache file (atomic tmp+rename). The parent keeps serving
 * requests; the first consumer that needs project symbols joins the worker
 * and loads the freshly written cache. fork(2) inherits the already booted
 * Zend engine copy-on-write, so the child starts with zero warm-up cost. */
static inline bool lsp_index_worker_start(lsp_server *server)
{
	pid_t pid;

	if (lsp_index_cache_disabled()) {
		return false;
	}

	fflush(stdout);
	fflush(stderr);
	pid = fork();
	if (pid < 0) {
		return false;
	}

	if (pid == 0) {
		/* Child: detach from the LSP transport so no stray frames reach the
		 * client, build + persist the index, and exit without running any
		 * inherited shutdown handlers. */
		if (!freopen("/dev/null", "r", stdin)) {
			_exit(1);
		}
		if (!freopen("/dev/null", "w", stdout)) {
			_exit(1);
		}

		lsp_build_project_index_sync(server);
		lsp_coverage_flush();
		_exit(0);
	}

	server->index_worker_pid = pid;
	server->index_worker_running = true;

	return true;
}
#endif

static inline void lsp_index_worker_finished(lsp_server *server)
{
	server->index_worker_running = false;

	if (lsp_index_cache_load_ex(server, false)) {
		lsp_analyzer_status("index", "idle", "Project index ready.");

		return;
	}

	/* The worker failed to produce a loadable cache; fall back to an inline
	 * synchronous build so project symbols stay available. */
	lsp_build_project_index_sync(server);
	lsp_analyzer_status("index", "idle", "Project index ready.");
}

extern void lsp_index_join_worker(lsp_server *server)
{
	int status = 0;

	if (!server->index_worker_running) {
		return;
	}

	lsp_process_wait(server->index_worker_pid, &status);
	lsp_index_worker_finished(server);
}

extern void lsp_index_poll_worker(lsp_server *server)
{
	int status = 0;

	if (!server->index_worker_running) {
		return;
	}

	if (lsp_process_wait_nonblocking(server->index_worker_pid, &status)) {
		lsp_index_worker_finished(server);
	}
}

extern void lsp_index_stop_worker(lsp_server *server)
{
	int status = 0;

	if (!server->index_worker_running) {
		return;
	}

	/* The cache write is atomic, so terminating mid-build is safe; the next
	 * start simply rebuilds. */
	lsp_process_terminate(server->index_worker_pid);
	lsp_process_wait(server->index_worker_pid, &status);
	server->index_worker_running = false;
}

extern void lsp_build_project_index(lsp_server *server)
{
	zend_hash_clean(&server->member_cache);

	if (lsp_index_cache_load_ex(server, true)) {
		lsp_analyzer_status("index", "idle", "Project index loaded from .lsparrot cache.");
		return;
	}

	lsp_analyzer_status("index", "running", "Indexing PHP project.");

#if LSP_HAVE_POSIX_PROCESS
	if (lsp_index_worker_start(server)) {
		return;
	}
#endif

	lsp_build_project_index_sync(server);
	lsp_analyzer_status("index", "idle", "Project index ready.");
}

/* Incrementally refresh the symbols declared by one file: drop the file's
 * stale records and re-scan its current contents (preferring the open
 * document text over the on-disk copy). Keeps newly added classes available
 * to completion/definition without a server restart. */
extern void lsp_index_refresh_file(lsp_server *server, zend_string *path)
{
	lsp_document *document;
	zend_stat_t st;

	if (!server->symbol_index.available ||
		!path ||
		!lsp_string_ends_with_literal(path, ".php") ||
		lsp_path_value_contains_analysis_helper(ZSTR_VAL(path), ZSTR_LEN(path))
	) {
		return;
	}

	lsp_index_join_worker(server);
	lsp_symbol_index_remove_path(&server->symbol_index, path);

	document = lsp_document_for_path(server, path);
	if (document) {
		lsp_index_declared_symbols_in_contents(server, document->text, path);
	} else if (VCWD_STAT(ZSTR_VAL(path), &st) == 0 && S_ISREG(st.st_mode)) {
		lsp_index_declared_symbols_in_file(server, path);
	}

	server->symbol_index_dirty = true;
}

extern void lsp_project_index_persist(lsp_server *server)
{
	if (!server->symbol_index_dirty) {
		return;
	}

	if (lsp_index_cache_save(server)) {
		server->symbol_index_dirty = false;
	}
}

extern void lsp_resolve_analyzers(lsp_server *server)
{
	bool phpstan_available, psalm_available, psalm_ls_available;

	phpstan_available = lsp_tool_available_in_workspace("phpstan", server->root);
	psalm_available = lsp_tool_available_in_workspace("psalm", server->root);
	psalm_ls_available = lsp_composer_project_has_tool(server->root, "psalm-language-server") ||
		lsp_workspace_composer_project_has_tool(server->root, server->root, "psalm-language-server", 0)
	;

	server->phpstan_enabled = false;
	server->psalm_enabled = false;
	server->psalm_ls_enabled = false;

	if (server->options.analyzer_auto) {
		server->phpstan_enabled = phpstan_available;
		server->psalm_enabled = psalm_available;
		server->psalm_ls_enabled = psalm_ls_available;
	} else {
		if (server->options.analyzer_phpstan) {
			if (phpstan_available) {
				server->phpstan_enabled = true;
			} else {
				lsp_analyzer_unavailable("phpstan", "PHPStan");
			}
		}

		if (server->options.analyzer_psalm) {
			if (psalm_available) {
				server->psalm_enabled = true;
			} else {
				lsp_analyzer_unavailable("psalm", "Psalm");
			}
		}

		if (server->options.analyzer_psalm_ls) {
			if (psalm_ls_available || lsp_tool_available_in_workspace("psalm", server->root)) {
				server->psalm_ls_enabled = true;
			} else {
				lsp_analyzer_unavailable("psalm-ls", "Psalm LS");
			}
		}
	}

	lsp_driver_status(server);
}
