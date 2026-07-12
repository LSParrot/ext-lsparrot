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

/* Multi-root workspace bookkeeping and workspace/willRenameFiles.
 *
 * Roots: the first workspace folder stays server->root (compatibility anchor
 * for the index cache location); every folder lives in server->workspace_roots
 * and the indexer/analyzer scheduling iterate all of them.
 *
 * willRenameFiles: renaming Foo.php to Bar.php renames the class Foo declared
 * in it project-wide (through the ordinary rename machinery, so the family
 * scoping rules apply), and moving a file across PSR-4 directories rewrites
 * its namespace declaration to the mapping of the destination. Edits target
 * the OLD uris, per the LSP contract (they apply before the file moves). */

extern void lsp_workspace_roots_add(lsp_server *server, zend_string *root)
{
	if (!root || ZSTR_LEN(root) == 0) {
		return;
	}

	zend_hash_add_empty_element(&server->workspace_roots, root);
}

extern void lsp_workspace_roots_remove(lsp_server *server, zend_string *root)
{
	if (root) {
		zend_hash_del(&server->workspace_roots, root);
	}
}

extern bool lsp_path_is_under_any_root(lsp_server *server, zend_string *path)
{
	zend_string *root;

	if (lsp_path_is_same_or_under(path, server->root)) {
		return true;
	}

	ZEND_HASH_FOREACH_STR_KEY(&server->workspace_roots, root) {
		if (root && lsp_path_is_same_or_under(path, root)) {
			return true;
		}
	} ZEND_HASH_FOREACH_END();

	return false;
}

/* Iterate every configured root exactly once (server->root included even
 * when the client sent no workspaceFolders). */
extern void lsp_workspace_roots_each(lsp_server *server, void (*callback)(lsp_server *server, zend_string *root, void *context), void *context)
{
	zend_string *root;
	bool saw_primary = false;

	ZEND_HASH_FOREACH_STR_KEY(&server->workspace_roots, root) {
		if (!root) {
			continue;
		}

		if (zend_string_equals(root, server->root)) {
			saw_primary = true;
		}

		callback(server, root, context);
	} ZEND_HASH_FOREACH_END();

	if (!saw_primary) {
		callback(server, server->root, context);
	}
}

extern void lsp_workspace_parse_folders(lsp_server *server, zval *params)
{
	zval *folders, *folder;
	zend_string *uri, *path;
	bool first = true;

	folders = lsp_array_find(params, "workspaceFolders");
	if (!folders || Z_TYPE_P(folders) != IS_ARRAY) {
		return;
	}

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(folders), folder) {
		uri = lsp_array_string(folder, "uri");
		if (!uri) {
			continue;
		}

		path = lsp_uri_to_path(uri);
		if (first) {
			/* rootUri (handled by the caller) and the first folder are the
			 * same in practice; keep server->root pointing at it. */
			zend_string_release(server->root);
			server->root = zend_string_copy(path);
			first = false;
		}

		lsp_workspace_roots_add(server, path);
		zend_string_release(path);
	} ZEND_HASH_FOREACH_END();
}

extern void lsp_workspace_did_change_folders(lsp_server *server, zval *params)
{
	zval *event, *added, *removed, *folder;
	zend_string *uri, *path;
	bool changed = false;

	event = lsp_array_find(params, "event");
	added = event ? lsp_array_find(event, "added") : NULL;
	removed = event ? lsp_array_find(event, "removed") : NULL;

	if (removed && Z_TYPE_P(removed) == IS_ARRAY) {
		ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(removed), folder) {
			uri = lsp_array_string(folder, "uri");
			if (!uri) {
				continue;
			}

			path = lsp_uri_to_path(uri);
			lsp_workspace_roots_remove(server, path);
			zend_string_release(path);
			changed = true;
		} ZEND_HASH_FOREACH_END();
	}

	if (added && Z_TYPE_P(added) == IS_ARRAY) {
		ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(added), folder) {
			uri = lsp_array_string(folder, "uri");
			if (!uri) {
				continue;
			}

			path = lsp_uri_to_path(uri);
			lsp_workspace_roots_add(server, path);
			zend_string_release(path);
			changed = true;
		} ZEND_HASH_FOREACH_END();
	}

	if (changed) {
		/* server->root is treated as always in scope; when the client removed
		 * the primary folder, promote the first remaining one so the stale
		 * root stops being indexed and analyzed. */
		if (zend_hash_num_elements(&server->workspace_roots) > 0 &&
			!zend_hash_exists(&server->workspace_roots, server->root)
		) {
			zend_string *replacement = NULL, *candidate;

			ZEND_HASH_FOREACH_STR_KEY(&server->workspace_roots, candidate) {
				if (candidate) {
					replacement = candidate;
					break;
				}
			} ZEND_HASH_FOREACH_END();

			if (replacement) {
				zend_string_release(server->root);
				server->root = zend_string_copy(replacement);
			}
		}

		/* The symbol index and analyzers are root-derived; rebuild both. */
		lsp_index_stop_worker(server);
		lsp_build_project_index(server);
		lsp_schedule_workspace_analyzers(server);
	}
}

/* ----------------------------------------------------------------------
 * workspace/willRenameFiles
 * ---------------------------------------------------------------------- */

static inline zend_string *lsp_workspace_php_basename(zend_string *path)
{
	const char *value = ZSTR_VAL(path), *slash, *end;

	slash = strrchr(value, '/');
	slash = slash ? slash + 1 : value;
	end = value + ZSTR_LEN(path);
	if ((size_t) (end - slash) > sizeof(".php") - 1 && strcasecmp(end - (sizeof(".php") - 1), ".php") == 0) {
		end -= sizeof(".php") - 1;
	}

	if (end <= slash) {
		return NULL;
	}

	return zend_string_init(slash, (size_t) (end - slash), 0);
}

static inline zend_string *lsp_workspace_dirname(zend_string *path)
{
	const char *value = ZSTR_VAL(path), *slash = strrchr(value, '/');

	if (!slash || slash == value) {
		return zend_string_init("/", 1, 0);
	}

	return zend_string_init(value, (size_t) (slash - value), 0);
}

/* Locate the class-like whose DECLARED short name (the token right after the
 * class/interface/trait/enum keyword -- never extends/implements clauses)
 * matches the file's basename, returning the byte offset of that name.
 * Anchoring on the keyword prevents `class Bar extends Baz` in Baz.php from
 * hijacking a rename of Baz.php into renaming Baz project-wide. */
static inline bool lsp_workspace_declared_name_offset(zend_string *contents, zend_string *short_name, size_t *name_offset)
{
	static const char *keywords[4] = { "class", "interface", "trait", "enum" };
	zend_long body_depth = 0;
	const char *value;
	size_t search_start = 0, class_start, body_start, body_end, header_end, p, keyword_end, name_start, i;

	value = ZSTR_VAL(contents);
	while (lsp_find_class_header_from(contents, search_start, &class_start, &body_start, &body_end, &body_depth)) {
		header_end = body_start > 0 ? body_start - 1 : body_start;

		for (p = class_start; p < header_end; p++) {
			for (i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
				if (!lsp_keyword_at_slice(value, p, header_end, keywords[i], &keyword_end)) {
					continue;
				}

				name_start = keyword_end;
				while (name_start < header_end && isspace((unsigned char) value[name_start])) {
					name_start++;
				}

				if (name_start + ZSTR_LEN(short_name) <= header_end &&
					strncasecmp(value + name_start, ZSTR_VAL(short_name), ZSTR_LEN(short_name)) == 0 &&
					!lsp_doc_is_identifier_char(value[name_start + ZSTR_LEN(short_name)])
				) {
					*name_offset = name_start;

					return true;
				}

				/* Declared name differs: stop scanning this header (extends/
				 * implements names must not match). */
				p = header_end;
				break;
			}
		}

		search_start = body_end + 1;
	}

	return false;
}

/* Nearest ancestor directory (starting at file_dir itself) containing a
 * composer.json; bounded walk so renames outside any project resolve to
 * nothing rather than scanning to the filesystem root. */
static inline zend_string *lsp_workspace_nearest_composer_root(zend_string *file_dir)
{
	zend_string *dir, *composer, *parent;
	uint32_t depth;
	const char *slash;

	dir = zend_string_copy(file_dir);
	for (depth = 0; depth < 16; depth++) {
		composer = lsp_join_path2(dir, "composer.json");
		if (lsp_is_regular_file(composer)) {
			zend_string_release(composer);

			return dir;
		}
		zend_string_release(composer);

		slash = strrchr(ZSTR_VAL(dir), '/');
		if (!slash || slash == ZSTR_VAL(dir)) {
			break;
		}

		parent = zend_string_init(ZSTR_VAL(dir), (size_t) (slash - ZSTR_VAL(dir)), 0);
		zend_string_release(dir);
		dir = parent;
	}

	zend_string_release(dir);

	return NULL;
}

static inline zend_string *lsp_workspace_join_relative(zend_string *base, zend_string *relative)
{
	smart_str joined = {0};
	size_t length = ZSTR_LEN(relative);

	smart_str_appendl(&joined, ZSTR_VAL(base), ZSTR_LEN(base));
	if (length > 0) {
		while (length > 0 && (ZSTR_VAL(relative)[length - 1] == '/' || ZSTR_VAL(relative)[length - 1] == '\\')) {
			length--;
		}
		if (length > 0) {
			smart_str_appendc(&joined, '/');
			smart_str_appendl(&joined, ZSTR_VAL(relative), length);
		}
	}

	smart_str_0(&joined);

	return joined.s ? joined.s : zend_string_copy(base);
}

static inline void lsp_workspace_psr4_consider(zend_string *project_root, zend_string *prefix, zend_string *dir, zend_string *file_dir, zend_string **best, size_t *best_length)
{
	zend_string *mapped_dir;
	smart_str ns = {0};
	size_t p, relative_start;

	mapped_dir = lsp_workspace_join_relative(project_root, dir);
	if (!lsp_path_is_same_or_under(file_dir, mapped_dir) || ZSTR_LEN(mapped_dir) < *best_length) {
		zend_string_release(mapped_dir);

		return;
	}

	smart_str_appendl(&ns, ZSTR_VAL(prefix), ZSTR_LEN(prefix));
	while (ns.s && ZSTR_LEN(ns.s) > 0 && ZSTR_VAL(ns.s)[ZSTR_LEN(ns.s) - 1] == '\\') {
		ZSTR_LEN(ns.s)--;
	}

	if (ZSTR_LEN(file_dir) > ZSTR_LEN(mapped_dir)) {
		relative_start = ZSTR_LEN(mapped_dir) + 1;
		/* A global-namespace mapping ("" prefix) must not produce a leading
		 * backslash. */
		if (ns.s && ZSTR_LEN(ns.s) > 0) {
			smart_str_appendc(&ns, '\\');
		}
		for (p = relative_start; p < ZSTR_LEN(file_dir); p++) {
			smart_str_appendc(&ns, ZSTR_VAL(file_dir)[p] == '/' ? '\\' : ZSTR_VAL(file_dir)[p]);
		}
	}

	smart_str_0(&ns);

	if (*best) {
		zend_string_release(*best);
	}
	*best = ns.s ? ns.s : zend_string_init("", 0, 0);
	*best_length = ZSTR_LEN(mapped_dir);

	zend_string_release(mapped_dir);
}

/* PSR-4 namespace for a directory according to the owning composer project:
 * the longest matching mapped directory wins. NULL when no mapping covers
 * the directory. */
static inline zend_string *lsp_workspace_psr4_namespace_for_dir(lsp_server *server, zend_string *file_dir)
{
	zval *decoded, *autoload_zv, *psr4, *dirs_zv, *dir_zv;
	zend_string *project_root, *prefix, *best = NULL;
	const char *sections[2] = { "autoload", "autoload-dev" };
	size_t best_length = 0, i;

	(void) server;

	project_root = lsp_workspace_nearest_composer_root(file_dir);
	if (!project_root) {
		return NULL;
	}

	decoded = lsp_composer_json_decoded(project_root);
	if (!decoded) {
		zend_string_release(project_root);

		return NULL;
	}

	for (i = 0; i < 2; i++) {
		autoload_zv = zend_hash_str_find(Z_ARRVAL_P(decoded), sections[i], strlen(sections[i]));
		psr4 = autoload_zv && Z_TYPE_P(autoload_zv) == IS_ARRAY
			? zend_hash_str_find(Z_ARRVAL_P(autoload_zv), "psr-4", sizeof("psr-4") - 1)
			: NULL
		;
		if (!psr4 || Z_TYPE_P(psr4) != IS_ARRAY) {
			continue;
		}

		ZEND_HASH_FOREACH_STR_KEY_VAL(Z_ARRVAL_P(psr4), prefix, dirs_zv) {
			if (!prefix) {
				continue;
			}

			if (Z_TYPE_P(dirs_zv) == IS_STRING) {
				lsp_workspace_psr4_consider(project_root, prefix, Z_STR_P(dirs_zv), file_dir, &best, &best_length);
			} else if (Z_TYPE_P(dirs_zv) == IS_ARRAY) {
				ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(dirs_zv), dir_zv) {
					if (Z_TYPE_P(dir_zv) == IS_STRING) {
						lsp_workspace_psr4_consider(project_root, prefix, Z_STR_P(dir_zv), file_dir, &best, &best_length);
					}
				} ZEND_HASH_FOREACH_END();
			}
		} ZEND_HASH_FOREACH_END();
	}

	zend_string_release(project_root);

	return best;
}

/* Locate the first namespace declaration via the token stream (a raw text
 * scan would happily edit the word "namespace" inside a comment). Outputs the
 * name span (for rewrites) and the whole-statement span including the
 * trailing ';' and newline (for removals when the destination maps to the
 * global namespace). */
static inline bool lsp_workspace_namespace_decl_range(zend_string *contents, size_t *start_offset, size_t *end_offset, size_t *stmt_start, size_t *stmt_end)
{
	zval tokens_zv, *token;
	HashTable *tokens;
	zend_string *text;
	zend_long id;
	uint32_t i, count;
	size_t token_offset;
	bool found = false, in_declaration = false;

	*start_offset = 0;
	*end_offset = 0;
	*stmt_start = 0;
	*stmt_end = 0;

	ZVAL_UNDEF(&tokens_zv);
	lsp_lsparrot_tokens_to_zval(&tokens_zv, contents);
	if (Z_TYPE(tokens_zv) != IS_ARRAY) {
		if (!Z_ISUNDEF(tokens_zv)) {
			zval_ptr_dtor(&tokens_zv);
		}

		return false;
	}

	tokens = Z_ARRVAL(tokens_zv);
	count = zend_hash_num_elements(tokens);
	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY) {
			continue;
		}

		id = lsp_token_long(token, "id", 0);
		token_offset = (size_t) lsp_token_long(token, "offset", 0);

		if (!in_declaration) {
			if (id == T_NAMESPACE) {
				in_declaration = true;
				*stmt_start = token_offset;
			}
			continue;
		}

		if (id == T_WHITESPACE || id == T_COMMENT || id == T_DOC_COMMENT) {
			continue;
		}

		if (id == ';' || id == '{') {
			if (id == ';') {
				*stmt_end = token_offset + 1;
				/* Swallow one trailing newline so a removal leaves no blank
				 * line behind. */
				if (*stmt_end < ZSTR_LEN(contents) && ZSTR_VAL(contents)[*stmt_end] == '\n') {
					(*stmt_end)++;
				}
			} else {
				*stmt_end = 0;
			}
			break;
		}

		text = lsp_token_string(token, "text");
		if (!text) {
			break;
		}

		if (!found) {
			*start_offset = token_offset;
			found = true;
		}
		*end_offset = token_offset + ZSTR_LEN(text);
	}

	zval_ptr_dtor(&tokens_zv);

	return found && *end_offset > *start_offset;
}

static inline void lsp_workspace_append_text_edit(zval *changes, zend_string *uri, zend_string *contents, size_t start_offset, size_t end_offset, zend_string *new_text)
{
	zval *entry, edits, edit, range;

	entry = zend_hash_find(Z_ARRVAL_P(changes), uri);
	if (!entry) {
		array_init(&edits);
		entry = zend_hash_update(Z_ARRVAL_P(changes), uri, &edits);
	}

	array_init(&edit);
	lsp_range_from_offsets(contents, start_offset, end_offset, &range);
	add_assoc_zval(&edit, "range", &range);
	add_assoc_str(&edit, "newText", zend_string_copy(new_text));
	add_next_index_zval(entry, &edit);
}

extern void lsp_lsparrot_will_rename_files(lsp_server *server, zval *return_value, zval *params)
{
	lsp_document *document;
	zval *files, *file, *changes_zv, rename_params, rename_result, position, *result_changes, *edits, changes;
	zend_string *old_uri, *new_uri, *old_path, *new_path, *old_name, *new_name, *old_dir, *new_dir, *expected_ns, *current_ns, *uri_key;
	size_t name_offset, ns_start, ns_end, stmt_start, stmt_end;
	zend_long line, character;
	bool any = false;

	files = lsp_array_find(params, "files");
	if (!files || Z_TYPE_P(files) != IS_ARRAY) {
		ZVAL_NULL(return_value);

		return;
	}

	lsp_index_join_worker(server);
	array_init(&changes);

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(files), file) {
		old_uri = lsp_array_string(file, "oldUri");
		new_uri = lsp_array_string(file, "newUri");
		if (!old_uri || !new_uri) {
			continue;
		}

		old_path = lsp_uri_to_path(old_uri);
		new_path = lsp_uri_to_path(new_uri);
		old_name = lsp_workspace_php_basename(old_path);
		new_name = lsp_workspace_php_basename(new_path);

		document = lsp_document_from_uri(server, old_uri);

		/* 1. Renamed basename: rename the class declared under the old
		 * name project-wide through the ordinary rename machinery. */
		if (document && old_name && new_name && !zend_string_equals(old_name, new_name) &&
			lsp_doc_is_identifier_start(ZSTR_VAL(new_name)[0]) &&
			lsp_workspace_declared_name_offset(document->text, old_name, &name_offset)
		) {
			zval range_zv, *start;

			lsp_range_from_offsets(document->text, name_offset, name_offset, &range_zv);
			start = lsp_array_find(&range_zv, "start");
			line = lsp_array_long(start, "line", 0);
			character = lsp_array_long(start, "character", 0);
			zval_ptr_dtor(&range_zv);

			array_init(&rename_params);
			array_init(&position);
			add_assoc_long(&position, "line", line);
			add_assoc_long(&position, "character", character);
			add_assoc_zval(&rename_params, "position", &position);
			add_assoc_str(&rename_params, "newName", zend_string_copy(new_name));

			ZVAL_UNDEF(&rename_result);
			lsp_lsparrot_rename(server, &rename_result, document, &rename_params);
			zval_ptr_dtor(&rename_params);

			result_changes = Z_TYPE(rename_result) == IS_ARRAY ? lsp_array_find(&rename_result, "changes") : NULL;
			if (result_changes && Z_TYPE_P(result_changes) == IS_ARRAY) {
				ZEND_HASH_FOREACH_STR_KEY_VAL(Z_ARRVAL_P(result_changes), uri_key, edits) {
					zval *existing, copy, edit_copy, *single;

					if (!uri_key || Z_TYPE_P(edits) != IS_ARRAY) {
						continue;
					}

					existing = zend_hash_find(Z_ARRVAL(changes), uri_key);
					if (existing) {
						ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(edits), single) {
							ZVAL_COPY(&edit_copy, single);
							add_next_index_zval(existing, &edit_copy);
						} ZEND_HASH_FOREACH_END();
					} else {
						ZVAL_COPY(&copy, edits);
						zend_hash_update(Z_ARRVAL(changes), uri_key, &copy);
					}

					any = true;
				} ZEND_HASH_FOREACH_END();
			}

			if (!Z_ISUNDEF(rename_result)) {
				zval_ptr_dtor(&rename_result);
			}
		}

		/* 2. Moved across PSR-4 directories: rewrite the namespace
		 * declaration to the destination mapping. */
		if (document) {
			old_dir = lsp_workspace_dirname(old_path);
			new_dir = lsp_workspace_dirname(new_path);

			if (!zend_string_equals(old_dir, new_dir)) {
				expected_ns = lsp_workspace_psr4_namespace_for_dir(server, new_dir);
				if (expected_ns) {
					current_ns = lsp_document_namespace(document->text);
					if (!zend_string_equals(current_ns, expected_ns) &&
						lsp_workspace_namespace_decl_range(document->text, &ns_start, &ns_end, &stmt_start, &stmt_end)
					) {
						if (ZSTR_LEN(expected_ns) > 0) {
							lsp_workspace_append_text_edit(&changes, old_uri, document->text, ns_start, ns_end, expected_ns);
							any = true;
						} else if (ZSTR_LEN(current_ns) > 0 && stmt_end > stmt_start) {
							/* Destination maps to the global namespace:
							 * remove the whole declaration statement. */
							lsp_workspace_append_text_edit(&changes, old_uri, document->text, stmt_start, stmt_end, zend_empty_string);
							any = true;
						}
					}
					if (current_ns != zend_empty_string) {
						zend_string_release(current_ns);
					}
					zend_string_release(expected_ns);
				}
			}

			zend_string_release(old_dir);
			zend_string_release(new_dir);
		}

		if (old_name) {
			zend_string_release(old_name);
		}
		if (new_name) {
			zend_string_release(new_name);
		}
		zend_string_release(old_path);
		zend_string_release(new_path);
	} ZEND_HASH_FOREACH_END();

	if (!any) {
		zval_ptr_dtor(&changes);
		ZVAL_NULL(return_value);

		return;
	}

	array_init(return_value);
	changes_zv = &changes;
	add_assoc_zval(return_value, "changes", changes_zv);
}
