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

/* Call hierarchy (prepare / incomingCalls / outgoingCalls) and type
 * hierarchy (prepare / supertypes / subtypes).
 *
 * Callers and callees are matched by name with call-shape verification
 * (`->name(`, `::name(`, bare `name(`); receivers are resolved when the
 * information is cheaply available ($this, self/static, ClassName::) and
 * treated conservatively otherwise -- hierarchy views are informational, so
 * over-reporting a same-named candidate beats silently hiding a real caller.
 * Subtype discovery walks the project side of the symbol index and inspects
 * class headers whose file mentions the target short name. */

#define LSP_HIER_KIND_FILE 1
#define LSP_HIER_KIND_CLASS 5
#define LSP_HIER_KIND_METHOD 6
#define LSP_HIER_KIND_ENUM 10
#define LSP_HIER_KIND_INTERFACE 11
#define LSP_HIER_KIND_FUNCTION 12

#define LSP_HIER_MAX_FILES 4096
#define LSP_HIER_MAX_RESULTS 256

typedef struct _lsp_hier_frame {
	zend_string *name;
	size_t name_offset;
	zend_long depth;
	bool is_class;
	bool valid;
} lsp_hier_frame;

/* PHP class/function names are case-insensitive; the tokenization fast-reject
 * must match the same way or differently-cased references vanish silently. */
static inline bool lsp_hier_contains_ci(zend_string *haystack, const char *needle, size_t needle_length)
{
	const char *p = ZSTR_VAL(haystack), *end = p + ZSTR_LEN(haystack);
	char lower, upper;

	if (needle_length == 0 || ZSTR_LEN(haystack) < needle_length) {
		return false;
	}

	lower = (char) tolower((unsigned char) needle[0]);
	upper = (char) toupper((unsigned char) needle[0]);
	for (; p + needle_length <= end; p++) {
		if ((*p == lower || *p == upper) && strncasecmp(p, needle, needle_length) == 0) {
			return true;
		}
	}

	return false;
}

static inline zend_string *lsp_hier_basename(zend_string *path)
{
	const char *value = ZSTR_VAL(path), *slash;

	slash = strrchr(value, '/');
#ifdef _WIN32
	{
		const char *backslash = strrchr(value, '\\');
		if (!slash || (backslash && backslash > slash)) {
			slash = backslash;
		}
	}
#endif

	return slash ? zend_string_init(slash + 1, ZSTR_LEN(path) - (size_t) (slash + 1 - value), 0) : zend_string_copy(path);
}

/* full_start/full_end give the whole declaration extent (signature + body)
 * when known; pass 0/0 to fall back to the name span. selectionRange is
 * always the bare name, per the LSP contract. */
static inline void lsp_hier_item(zval *item, zend_string *name, zend_long kind, zend_string *path, zend_string *contents, size_t sel_start, size_t sel_end, size_t full_start, size_t full_end, zend_string *detail, zend_string *container)
{
	zend_string *uri;
	zval range, selection, data;

	if (full_end <= full_start || full_start > sel_start || full_end < sel_end) {
		full_start = sel_start;
		full_end = sel_end;
	}

	uri = lsp_uri_from_path(path);
	array_init(item);
	add_assoc_str(item, "name", zend_string_copy(name));
	add_assoc_long(item, "kind", kind);
	add_assoc_str(item, "uri", uri);
	lsp_range_from_offsets(contents, full_start, full_end, &range);
	add_assoc_zval(item, "range", &range);
	lsp_range_from_offsets(contents, sel_start, sel_end, &selection);
	add_assoc_zval(item, "selectionRange", &selection);

	if (detail) {
		add_assoc_str(item, "detail", zend_string_copy(detail));
	}

	array_init(&data);
	if (container) {
		add_assoc_str(&data, "container", zend_string_copy(container));
	}
	add_assoc_zval(item, "data", &data);
}

static inline bool lsp_hier_token_is_significant(zval *token)
{
	zend_long id = lsp_token_long(token, "id", 0);

	return id != T_WHITESPACE && id != T_COMMENT && id != T_DOC_COMMENT;
}

static inline zval *lsp_hier_sig_token(HashTable *tokens, zend_long index, int direction)
{
	zend_long i, count = (zend_long) zend_hash_num_elements(tokens);
	zval *token;

	for (i = index + direction; i >= 0 && i < count; i += direction) {
		token = zend_hash_index_find(tokens, (zend_ulong) i);
		if (token && Z_TYPE_P(token) == IS_ARRAY && lsp_hier_token_is_significant(token)) {
			return token;
		}
	}

	return NULL;
}

/* One linear pass locating the innermost NAMED function and class frames
 * containing a byte offset. Closures contribute no name, so a call inside a
 * closure attributes to the enclosing named method, matching PhpStorm. */
static inline void lsp_hier_enclosing_decl(HashTable *tokens, zend_string *text, size_t target_offset, lsp_hier_frame *out_function, lsp_hier_frame *out_class)
{
	lsp_hier_frame stack[64];
	zend_string *name;
	zval *token, *name_token;
	zend_long id, depth = 0;
	uint32_t i, count, stack_size = 0, name_index;
	size_t token_offset;
	bool pending_class, pending_function;
	zend_string *pending_name = NULL;
	size_t pending_name_offset = 0;

	memset(out_function, 0, sizeof(*out_function));
	memset(out_class, 0, sizeof(*out_class));
	pending_class = false;
	pending_function = false;

	count = zend_hash_num_elements(tokens);
	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY) {
			continue;
		}

		id = lsp_token_long(token, "id", 0);
		token_offset = (size_t) lsp_token_long(token, "offset", 0);

		if (token_offset > target_offset) {
			break;
		}

		if (lsp_token_is_class_like(token)) {
			pending_class = true;
			pending_function = false;
			pending_name = lsp_next_string_token(tokens, i + 1);
			pending_name_offset = 0;
			continue;
		}

		if (id == T_FUNCTION) {
			pending_function = true;
			pending_class = false;
			name_token = lsp_next_function_name_token_ex(tokens, i + 1, &name_index);
			pending_name = lsp_token_string(name_token, "text");
			pending_name_offset = name_token ? (size_t) lsp_token_long(name_token, "offset", 0) : 0;
			continue;
		}

		if (id == ';' && (pending_class || pending_function)) {
			/* Abstract/interface method or stray keyword: no body opens. */
			pending_class = false;
			pending_function = false;
			pending_name = NULL;
			continue;
		}

		if (id == '{' || id == T_CURLY_OPEN || id == T_DOLLAR_OPEN_CURLY_BRACES) {
			depth++;
			if ((pending_class || pending_function) && stack_size < sizeof(stack) / sizeof(stack[0])) {
				stack[stack_size].name = pending_name;
				stack[stack_size].name_offset = pending_name ? pending_name_offset : 0;
				stack[stack_size].depth = depth;
				stack[stack_size].is_class = pending_class;
				stack[stack_size].valid = true;
				stack_size++;
			}
			pending_class = false;
			pending_function = false;
			pending_name = NULL;
			continue;
		}

		if (id == '}') {
			if (stack_size > 0 && stack[stack_size - 1].depth == depth) {
				stack_size--;
			}
			if (depth > 0) {
				depth--;
			}
			continue;
		}
	}

	for (i = stack_size; i > 0; i--) {
		lsp_hier_frame *frame = &stack[i - 1];

		if (!out_function->valid && !frame->is_class && frame->name) {
			*out_function = *frame;
		}

		if (!out_class->valid && frame->is_class && frame->name) {
			*out_class = *frame;
		}
	}

	(void) text;
	(void) name;
}

/* Find the byte offset of `function NAME` (its name token) in contents,
 * optionally constrained to the body of the class declaring `container`. */
static inline bool lsp_hier_find_function_decl(zend_string *contents, zend_string *name, zend_string *container_fqcn, size_t *name_offset, size_t *body_start, size_t *body_end)
{
	zend_long body_depth = 0, id, depth, open_depth;
	zval tokens_zv, *token, *name_token, *peek;
	HashTable *tokens;
	zend_string *label;
	uint32_t i, j, count, name_index;
	size_t class_start = 0, class_body_start = 0, class_body_end = 0, bound_start = 0, bound_end;
	bool found = false;

	bound_end = ZSTR_LEN(contents);
	if (container_fqcn) {
		if (!lsp_find_class_header_for_name(contents, container_fqcn, &class_start, &class_body_start, &class_body_end, &body_depth)) {
			return false;
		}
		bound_start = class_body_start;
		bound_end = class_body_end;
	}

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
	for (i = 0; i < count && !found; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY || lsp_token_long(token, "id", 0) != T_FUNCTION) {
			continue;
		}

		if (!lsp_token_in_bounds(token, bound_start, bound_end)) {
			continue;
		}

		name_token = lsp_next_function_name_token_ex(tokens, i + 1, &name_index);
		label = lsp_token_string(name_token, "text");
		if (!label || !zend_string_equals_ci(label, name)) {
			continue;
		}

		*name_offset = (size_t) lsp_token_long(name_token, "offset", 0);

		/* Body: first '{' after the signature at its own depth; abstract
		 * declarations end with ';' and have no body. */
		depth = 0;
		open_depth = -1;
		*body_start = 0;
		*body_end = 0;
		for (j = name_index + 1; j < count; j++) {
			peek = zend_hash_index_find(tokens, j);
			if (!peek || Z_TYPE_P(peek) != IS_ARRAY) {
				continue;
			}
			id = lsp_token_long(peek, "id", 0);
			if (id == '(' || id == '[') {
				depth++;
			} else if (id == ')' || id == ']') {
				depth--;
			} else if (id == ';' && depth == 0 && open_depth < 0) {
				/* Signature ended without a body: abstract/interface method. */
				break;
			} else if ((id == '{' || id == T_CURLY_OPEN || id == T_DOLLAR_OPEN_CURLY_BRACES) && depth == 0 && open_depth < 0) {
				*body_start = (size_t) lsp_token_long(peek, "offset", 0) + 1;
				open_depth = 1;
			} else if ((id == '{' || id == T_CURLY_OPEN || id == T_DOLLAR_OPEN_CURLY_BRACES) && open_depth >= 0) {
				open_depth++;
			} else if (id == '}' && open_depth >= 0) {
				open_depth--;
				if (open_depth == 0) {
					*body_end = (size_t) lsp_token_long(peek, "offset", 0);
					break;
				}
			}
		}

		found = true;
	}

	zval_ptr_dtor(&tokens_zv);

	return found;
}

static inline zend_string *lsp_hier_item_data_container(zval *item)
{
	zval *data = lsp_array_find(item, "data");

	return data ? lsp_array_string(data, "container") : NULL;
}

/* ----------------------------------------------------------------------
 * prepareCallHierarchy
 * ---------------------------------------------------------------------- */

extern void lsp_lsparrot_prepare_call_hierarchy(lsp_server *server, zval *return_value, lsp_document *document, zval *position)
{
	lsp_hier_frame func_frame, class_frame;
	zend_long line, character;
	zend_string *word, *container, *path, *contents, *receiver_word, *receiver_class;
	zval *tokens_zv, item;
	size_t offset, word_start, name_offset, body_start, body_end;
	bool resolved, owns_contents;

	array_init(return_value);

	lsp_position_from_zval(position, &line, &character);
	offset = lsp_offset_at(document->text, line, character);
	word = lsp_word_at(document->text, offset);
	if (ZSTR_LEN(word) == 0 || ZSTR_VAL(word)[0] == '$') {
		zend_string_release(word);

		return;
	}

	lsp_index_join_worker(server);

	word_start = offset;
	while (word_start > 0 && lsp_doc_is_identifier_char(ZSTR_VAL(document->text)[word_start - 1])) {
		word_start--;
	}

	container = NULL;
	resolved = false;

	/* Declaration under the cursor? Verified against the token stream: the
	 * word's own token must be preceded by T_FUNCTION (a comment ending in
	 * the word "function" must not count). */
	tokens_zv = zend_hash_str_find(Z_ARRVAL(document->lsparrot), "tokens", sizeof("tokens") - 1);
	if (tokens_zv && Z_TYPE_P(tokens_zv) == IS_ARRAY) {
		HashTable *doc_tokens = Z_ARRVAL_P(tokens_zv);
		zval *word_token, *prev_token;
		uint32_t token_count = zend_hash_num_elements(doc_tokens), token_index;
		bool is_declaration = false;

		for (token_index = 0; token_index < token_count; token_index++) {
			word_token = zend_hash_index_find(doc_tokens, token_index);
			if (!word_token || Z_TYPE_P(word_token) != IS_ARRAY) {
				continue;
			}

			if ((size_t) lsp_token_long(word_token, "offset", 0) != word_start) {
				continue;
			}

			if (lsp_token_long(word_token, "id", 0) == T_STRING) {
				prev_token = lsp_hier_sig_token(doc_tokens, (zend_long) token_index, -1);
				is_declaration = prev_token && lsp_token_long(prev_token, "id", 0) == T_FUNCTION;
			}

			break;
		}

		if (is_declaration) {
			lsp_hier_enclosing_decl(Z_ARRVAL_P(tokens_zv), document->text, word_start, &func_frame, &class_frame);
			if (class_frame.valid && class_frame.name) {
				container = lsp_resolve_class_name_at(document->text, class_frame.name, word_start);
			}

			lsp_hier_item(&item, word, container ? LSP_HIER_KIND_METHOD : LSP_HIER_KIND_FUNCTION,
				document->path, document->text, word_start, word_start + ZSTR_LEN(word), 0, 0, container, container);
			add_next_index_zval(return_value, &item);
			resolved = true;
		}
	}

	/* Call site: resolve the receiver to find the declaring class. */
	if (!resolved) {
		receiver_class = NULL;
		if (word_start >= 2 && memcmp(ZSTR_VAL(document->text) + word_start - 2, "->", 2) == 0) {
			size_t recv_end = word_start - 2, recv_start;

			if (recv_end > 0 && ZSTR_VAL(document->text)[recv_end - 1] == '?') {
				recv_end--;
			}
			recv_start = recv_end;
			while (recv_start > 0 && (lsp_doc_is_identifier_char(ZSTR_VAL(document->text)[recv_start - 1]) || ZSTR_VAL(document->text)[recv_start - 1] == '$')) {
				recv_start--;
			}
			if (recv_end > recv_start) {
				receiver_word = zend_string_init(ZSTR_VAL(document->text) + recv_start, recv_end - recv_start, 0);
				if (zend_string_equals_literal(receiver_word, "$this")) {
					lsp_hier_enclosing_decl(Z_ARRVAL_P(tokens_zv), document->text, word_start, &func_frame, &class_frame);
					if (class_frame.valid && class_frame.name) {
						receiver_class = lsp_resolve_class_name_at(document->text, class_frame.name, word_start);
					}
				} else if (ZSTR_VAL(receiver_word)[0] == '$') {
					receiver_class = lsp_infer_receiver_class(server, document, receiver_word, word_start);
				}
				zend_string_release(receiver_word);
			}
		} else {
			bool public_only = true;

			if (lsp_static_member_receiver_class(document, word_start, word, &receiver_class, &public_only)) {
				/* receiver_class set by helper. */
			} else {
				receiver_class = NULL;
			}
		}

		if (receiver_class) {
			path = lsp_find_project_symbol_path(server, LSP_SYMBOL_CLASS, receiver_class);
			if (path) {
				lsp_document *target_document = lsp_document_for_path(server, path);

				contents = target_document ? zend_string_copy(target_document->text) : lsp_read_file(path);
				owns_contents = target_document || contents != zend_empty_string;
				if (owns_contents && contents != zend_empty_string &&
					lsp_hier_find_function_decl(contents, word, receiver_class, &name_offset, &body_start, &body_end)
				) {
					lsp_hier_item(&item, word, LSP_HIER_KIND_METHOD, path, contents,
						name_offset, name_offset + ZSTR_LEN(word),
						name_offset, body_end > name_offset ? body_end + 1 : 0,
						receiver_class, receiver_class);
					add_next_index_zval(return_value, &item);
					resolved = true;
				}
				if (owns_contents) {
					zend_string_release(contents);
				}
				zend_string_release(path);
			}
			zend_string_release(receiver_class);
		}
	}

	/* Plain function reference. */
	if (!resolved) {
		zend_string *resolved_name, *namespace_name;

		namespace_name = lsp_document_namespace_at(document->text, word_start);
		resolved_name = namespace_name != zend_empty_string
			? strpprintf(0, "%s\\%s", ZSTR_VAL(namespace_name), ZSTR_VAL(word))
			: zend_string_copy(word)
		;
		if (namespace_name != zend_empty_string) {
			zend_string_release(namespace_name);
		}

		path = lsp_find_project_symbol_path(server, LSP_SYMBOL_FUNCTION, resolved_name);
		if (!path && !zend_string_equals(resolved_name, word)) {
			path = lsp_find_project_symbol_path(server, LSP_SYMBOL_FUNCTION, word);
		}
		zend_string_release(resolved_name);

		if (path) {
			contents = lsp_read_file(path);
			if (contents != zend_empty_string &&
				lsp_hier_find_function_decl(contents, word, NULL, &name_offset, &body_start, &body_end)
			) {
				lsp_hier_item(&item, word, LSP_HIER_KIND_FUNCTION, path, contents,
					name_offset, name_offset + ZSTR_LEN(word),
					name_offset, body_end > name_offset ? body_end + 1 : 0,
					NULL, NULL);
				add_next_index_zval(return_value, &item);
			}
			if (contents != zend_empty_string) {
				zend_string_release(contents);
			}
			zend_string_release(path);
		}
	}

	zend_string_release(word);
}

/* ----------------------------------------------------------------------
 * incomingCalls
 * ---------------------------------------------------------------------- */

static inline void lsp_hier_scan_file_for_callers(lsp_server *server, zval *results, zend_string *path, zend_string *contents, zend_string *target, bool method_like, uint32_t *result_count)
{
	lsp_hier_frame func_frame, class_frame;
	zval tokens_zv, *token, *prev, *next, *existing, entry, item, ranges, range_zv;
	HashTable *tokens, callers;
	zend_string *label, *caller_key, *file_label;
	zend_long id, prev_id;
	uint32_t i, count;
	size_t token_offset;

	if (!lsp_hier_contains_ci(contents, ZSTR_VAL(target), ZSTR_LEN(target))) {
		return;
	}

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
	zend_hash_init(&callers, 8, NULL, ZVAL_PTR_DTOR, 0);

	for (i = 0; i < count && *result_count < LSP_HIER_MAX_RESULTS; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY || lsp_token_long(token, "id", 0) != T_STRING) {
			continue;
		}

		label = lsp_token_string(token, "text");
		if (!label || !zend_string_equals_ci(label, target)) {
			continue;
		}

		next = lsp_hier_sig_token(tokens, (zend_long) i, 1);
		if (!next || lsp_token_long(next, "id", 0) != '(') {
			continue;
		}

		prev = lsp_hier_sig_token(tokens, (zend_long) i, -1);
		prev_id = prev ? lsp_token_long(prev, "id", 0) : 0;
		if (prev_id == T_FUNCTION) {
			continue;
		}

		if (method_like) {
			if (prev_id != T_OBJECT_OPERATOR && prev_id != T_NULLSAFE_OBJECT_OPERATOR && prev_id != T_PAAMAYIM_NEKUDOTAYIM) {
				continue;
			}
		} else {
			if (prev_id == T_OBJECT_OPERATOR || prev_id == T_NULLSAFE_OBJECT_OPERATOR || prev_id == T_PAAMAYIM_NEKUDOTAYIM || prev_id == T_NEW || prev_id == T_CONST) {
				continue;
			}
		}

		token_offset = (size_t) lsp_token_long(token, "offset", 0);
		lsp_hier_enclosing_decl(tokens, contents, token_offset, &func_frame, &class_frame);

		if (func_frame.valid && func_frame.name) {
			caller_key = strpprintf(0, "%zu", func_frame.name_offset);
		} else {
			caller_key = zend_string_init("file", sizeof("file") - 1, 0);
		}

		existing = zend_hash_find(&callers, caller_key);
		if (!existing) {
			array_init(&entry);

			if (func_frame.valid && func_frame.name) {
				zend_string *container = NULL;

				if (class_frame.valid && class_frame.name) {
					container = lsp_resolve_class_name_at(contents, class_frame.name, func_frame.name_offset);
				}
				lsp_hier_item(&item, func_frame.name,
					container ? LSP_HIER_KIND_METHOD : LSP_HIER_KIND_FUNCTION,
					path, contents, func_frame.name_offset, func_frame.name_offset + ZSTR_LEN(func_frame.name),
					0, 0, container, container);
				if (container) {
					zend_string_release(container);
				}
			} else {
				/* Top-level code: attribute the call to the file. */
				file_label = lsp_hier_basename(path);
				lsp_hier_item(&item, file_label, LSP_HIER_KIND_FILE, path, contents, token_offset, token_offset + ZSTR_LEN(target), 0, 0, NULL, NULL);
				zend_string_release(file_label);
			}

			add_assoc_zval(&entry, "from", &item);
			array_init(&ranges);
			add_assoc_zval(&entry, "fromRanges", &ranges);
			existing = zend_hash_update(&callers, caller_key, &entry);
			(*result_count)++;
		}

		zend_string_release(caller_key);

		lsp_range_from_offsets(contents, token_offset, token_offset + ZSTR_LEN(target), &range_zv);
		{
			zval *ranges_zv = lsp_array_find(existing, "fromRanges");

			if (ranges_zv && Z_TYPE_P(ranges_zv) == IS_ARRAY) {
				add_next_index_zval(ranges_zv, &range_zv);
			} else {
				zval_ptr_dtor(&range_zv);
			}
		}
	}

	ZEND_HASH_FOREACH_VAL(&callers, existing) {
		zval copy;

		ZVAL_COPY(&copy, existing);
		add_next_index_zval(results, &copy);
	} ZEND_HASH_FOREACH_END();
	zend_hash_destroy(&callers);

	zval_ptr_dtor(&tokens_zv);
}

extern void lsp_lsparrot_call_hierarchy_incoming(lsp_server *server, zval *return_value, zval *params)
{
	lsp_symbol_index *region = &server->symbol_index;
	const lsp_symbol_entry *entry;
	lsp_document *open_document;
	zval *item;
	zend_string *target, *container, *path, *contents;
	HashTable visited;
	uint32_t i, result_count = 0, file_count = 0;
	bool method_like, owns_contents;

	array_init(return_value);

	item = lsp_array_find(params, "item");
	target = item ? lsp_array_string(item, "name") : NULL;
	if (!target) {
		return;
	}

	container = lsp_hier_item_data_container(item);
	method_like = container != NULL;

	lsp_index_join_worker(server);
	lsp_symbol_index_table_ensure(region);
	zend_hash_init(&visited, 64, NULL, NULL, 0);

	for (i = 0; i < region->entry_count && result_count < LSP_HIER_MAX_RESULTS && file_count < LSP_HIER_MAX_FILES; i++) {
		entry = &region->entries[i];
		if ((entry->flags & (LSP_SYMBOL_ENTRY_DELETED | LSP_SYMBOL_ENTRY_VENDOR)) != 0) {
			continue;
		}

		path = zend_string_init(lsp_symbol_entry_path(region, entry), entry->path_length, 0);
		if (zend_hash_exists(&visited, path) ||
			lsp_path_value_contains_analysis_helper(ZSTR_VAL(path), ZSTR_LEN(path))
		) {
			zend_string_release(path);
			continue;
		}
		zend_hash_add_empty_element(&visited, path);
		file_count++;

		open_document = lsp_document_for_path(server, path);
		contents = open_document ? zend_string_copy(open_document->text) : lsp_read_file(path);
		owns_contents = open_document || contents != zend_empty_string;
		if (owns_contents && contents != zend_empty_string) {
			lsp_hier_scan_file_for_callers(server, return_value, path, contents, target, method_like, &result_count);
		}
		if (owns_contents) {
			zend_string_release(contents);
		}

		zend_string_release(path);
	}

	zend_hash_destroy(&visited);
}

/* ----------------------------------------------------------------------
 * outgoingCalls
 * ---------------------------------------------------------------------- */

static inline void lsp_hier_add_outgoing(lsp_server *server, HashTable *callees, zend_string *callee, zend_string *callee_container, zend_string *source_contents, size_t site_offset, uint32_t *result_count)
{
	zval *existing, entry, item, ranges, range_zv, *ranges_zv;
	zend_string *key, *path, *contents;
	lsp_document *target_document;
	size_t name_offset, body_start, body_end;
	bool owns_contents, located = false;

	if (callee_container) {
		path = lsp_find_project_symbol_path(server, LSP_SYMBOL_CLASS, callee_container);
	} else {
		path = lsp_find_project_symbol_path(server, LSP_SYMBOL_FUNCTION, callee);
	}

	if (!path) {
		return;
	}

	key = strpprintf(0, "%s:%s", ZSTR_VAL(path), ZSTR_VAL(callee));
	existing = zend_hash_find(callees, key);
	if (!existing) {
		if (*result_count >= LSP_HIER_MAX_RESULTS) {
			zend_string_release(key);
			zend_string_release(path);

			return;
		}

		target_document = lsp_document_for_path(server, path);
		contents = target_document ? zend_string_copy(target_document->text) : lsp_read_file(path);
		owns_contents = target_document || contents != zend_empty_string;

		if (owns_contents && contents != zend_empty_string &&
			lsp_hier_find_function_decl(contents, callee, callee_container, &name_offset, &body_start, &body_end)
		) {
			lsp_hier_item(&item, callee,
				callee_container ? LSP_HIER_KIND_METHOD : LSP_HIER_KIND_FUNCTION,
				path, contents, name_offset, name_offset + ZSTR_LEN(callee),
				name_offset, body_end > name_offset ? body_end + 1 : 0,
				callee_container, callee_container);
			located = true;
		}

		if (owns_contents) {
			zend_string_release(contents);
		}

		if (!located) {
			zend_string_release(key);
			zend_string_release(path);

			return;
		}

		array_init(&entry);
		add_assoc_zval(&entry, "to", &item);
		array_init(&ranges);
		add_assoc_zval(&entry, "fromRanges", &ranges);
		existing = zend_hash_update(callees, key, &entry);
		(*result_count)++;
	}

	zend_string_release(key);
	zend_string_release(path);

	lsp_range_from_offsets(source_contents, site_offset, site_offset + ZSTR_LEN(callee), &range_zv);
	ranges_zv = lsp_array_find(existing, "fromRanges");
	if (ranges_zv && Z_TYPE_P(ranges_zv) == IS_ARRAY) {
		add_next_index_zval(ranges_zv, &range_zv);
	} else {
		zval_ptr_dtor(&range_zv);
	}
}

extern void lsp_lsparrot_call_hierarchy_outgoing(lsp_server *server, zval *return_value, zval *params)
{
	lsp_document *source_document;
	zval *item, *uri_zv, tokens_zv, *token, *prev, *next, *existing;
	HashTable *tokens, callees;
	zend_string *name, *container, *path, *contents, *label, *callee_container, *receiver_text, *resolved;
	zend_long id, prev_id;
	uint32_t i, count, result_count = 0;
	size_t name_offset, body_start = 0, body_end = 0, token_offset;
	bool owns_contents;

	array_init(return_value);

	item = lsp_array_find(params, "item");
	name = item ? lsp_array_string(item, "name") : NULL;
	uri_zv = item ? lsp_array_find(item, "uri") : NULL;
	if (!name || !uri_zv || Z_TYPE_P(uri_zv) != IS_STRING) {
		return;
	}

	container = lsp_hier_item_data_container(item);
	lsp_index_join_worker(server);

	path = lsp_uri_to_path(Z_STR_P(uri_zv));
	source_document = lsp_document_for_path(server, path);
	contents = source_document ? zend_string_copy(source_document->text) : lsp_read_file(path);
	owns_contents = source_document || contents != zend_empty_string;
	if (!owns_contents || contents == zend_empty_string ||
		!lsp_hier_find_function_decl(contents, name, container, &name_offset, &body_start, &body_end) ||
		body_end <= body_start
	) {
		if (owns_contents) {
			zend_string_release(contents);
		}
		zend_string_release(path);

		return;
	}

	ZVAL_UNDEF(&tokens_zv);
	lsp_lsparrot_tokens_to_zval(&tokens_zv, contents);
	if (Z_TYPE(tokens_zv) != IS_ARRAY) {
		if (!Z_ISUNDEF(tokens_zv)) {
			zval_ptr_dtor(&tokens_zv);
		}
		if (owns_contents) {
			zend_string_release(contents);
		}
		zend_string_release(path);

		return;
	}

	tokens = Z_ARRVAL(tokens_zv);
	count = zend_hash_num_elements(tokens);
	zend_hash_init(&callees, 8, NULL, ZVAL_PTR_DTOR, 0);

	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY || !lsp_token_in_bounds(token, body_start, body_end)) {
			continue;
		}

		if (lsp_token_long(token, "id", 0) != T_STRING) {
			continue;
		}

		label = lsp_token_string(token, "text");
		next = lsp_hier_sig_token(tokens, (zend_long) i, 1);
		if (!label || !next || lsp_token_long(next, "id", 0) != '(') {
			continue;
		}

		prev = lsp_hier_sig_token(tokens, (zend_long) i, -1);
		prev_id = prev ? lsp_token_long(prev, "id", 0) : 0;
		token_offset = (size_t) lsp_token_long(token, "offset", 0);
		callee_container = NULL;

		if (prev_id == T_OBJECT_OPERATOR || prev_id == T_NULLSAFE_OBJECT_OPERATOR) {
			/* Receiver: the significant token before the arrow. */
			{
				zend_long arrow_index = -1, j;
				zval *probe;

				for (j = (zend_long) i - 1; j >= 0; j--) {
					probe = zend_hash_index_find(tokens, (zend_ulong) j);
					if (probe && Z_TYPE_P(probe) == IS_ARRAY && lsp_hier_token_is_significant(probe)) {
						arrow_index = j;
						break;
					}
				}

				probe = arrow_index > 0 ? lsp_hier_sig_token(tokens, arrow_index, -1) : NULL;
				receiver_text = lsp_token_string(probe, "text");
				id = probe ? lsp_token_long(probe, "id", 0) : 0;

				if (receiver_text && id == T_VARIABLE && zend_string_equals_literal(receiver_text, "$this") && container) {
					callee_container = zend_string_copy(container);
				} else if (receiver_text && id == T_VARIABLE && source_document) {
					callee_container = lsp_infer_receiver_class(server, source_document, receiver_text, token_offset);
				}
			}

			if (callee_container) {
				lsp_hier_add_outgoing(server, &callees, label, callee_container, contents, token_offset, &result_count);
				zend_string_release(callee_container);
			}
			continue;
		}

		if (prev_id == T_PAAMAYIM_NEKUDOTAYIM) {
			zval *probe = lsp_hier_sig_token(tokens, (zend_long) i - 1, -1);
			zend_string *class_text;

			/* Skip back over the :: itself to the class token. */
			{
				zend_long j;

				probe = NULL;
				for (j = (zend_long) i - 1; j >= 0; j--) {
					zval *candidate = zend_hash_index_find(tokens, (zend_ulong) j);

					if (!candidate || Z_TYPE_P(candidate) != IS_ARRAY || !lsp_hier_token_is_significant(candidate)) {
						continue;
					}

					if (lsp_token_long(candidate, "id", 0) == T_PAAMAYIM_NEKUDOTAYIM) {
						probe = lsp_hier_sig_token(tokens, j, -1);
						break;
					}

					break;
				}
			}

			class_text = lsp_token_string(probe, "text");
			if (class_text) {
				if (zend_string_equals_literal_ci(class_text, "self") || zend_string_equals_literal_ci(class_text, "static")) {
					callee_container = container ? zend_string_copy(container) : NULL;
				} else if (zend_string_equals_literal_ci(class_text, "parent")) {
					zend_long body_depth = 0;
					size_t class_start = 0, class_body_start = 0, class_body_end = 0;

					if (container && lsp_find_class_header_for_name(contents, container, &class_start, &class_body_start, &class_body_end, &body_depth)) {
						callee_container = lsp_class_extends_name(contents, class_start, class_body_start);
					}
				} else if (ZSTR_VAL(class_text)[0] != '$') {
					callee_container = lsp_resolve_class_name_at(contents, class_text, token_offset);
				}
			}

			if (callee_container) {
				lsp_hier_add_outgoing(server, &callees, label, callee_container, contents, token_offset, &result_count);
				zend_string_release(callee_container);
			}
			continue;
		}

		if (prev_id == T_FUNCTION || prev_id == T_NEW || prev_id == T_CONST) {
			continue;
		}

		/* Bare function call: lsp_hier_add_outgoing looks the name up in the
		 * function index and drops unresolvable calls. */
		lsp_hier_add_outgoing(server, &callees, label, NULL, contents, token_offset, &result_count);
	}

	ZEND_HASH_FOREACH_VAL(&callees, existing) {
		zval copy;

		ZVAL_COPY(&copy, existing);
		add_next_index_zval(return_value, &copy);
	} ZEND_HASH_FOREACH_END();
	zend_hash_destroy(&callees);

	zval_ptr_dtor(&tokens_zv);
	if (owns_contents) {
		zend_string_release(contents);
	}
	zend_string_release(path);
}

/* ----------------------------------------------------------------------
 * Type hierarchy
 * ---------------------------------------------------------------------- */

static inline zend_long lsp_hier_type_kind(char symbol_kind)
{
	switch (symbol_kind) {
		case LSP_SYMBOL_INTERFACE:
			return LSP_HIER_KIND_INTERFACE;
		case LSP_SYMBOL_ENUM:
			return LSP_HIER_KIND_ENUM;
		default:
			return LSP_HIER_KIND_CLASS;
	}
}

static inline char lsp_hier_symbol_kind_for_fqcn(lsp_server *server, zend_string *fqcn, zend_string **path_out)
{
	static const char kinds[3] = { LSP_SYMBOL_CLASS, LSP_SYMBOL_INTERFACE, LSP_SYMBOL_ENUM };
	zend_string *path;
	size_t i;

	for (i = 0; i < sizeof(kinds); i++) {
		path = lsp_find_project_symbol_path(server, kinds[i], fqcn);
		if (path) {
			*path_out = path;

			return kinds[i];
		}
	}

	*path_out = NULL;

	return 0;
}

static inline bool lsp_hier_type_item_for_fqcn(lsp_server *server, zend_string *fqcn, zval *item)
{
	zend_string *path, *contents, *short_name;
	const char *base;
	size_t base_length, class_start = 0, body_start = 0, body_end = 0, name_offset;
	zend_long body_depth = 0;
	lsp_document *document;
	char kind;
	bool owns_contents, located = false;

	kind = lsp_hier_symbol_kind_for_fqcn(server, fqcn, &path);
	if (!path) {
		return false;
	}

	document = lsp_document_for_path(server, path);
	contents = document ? zend_string_copy(document->text) : lsp_read_file(path);
	owns_contents = document || contents != zend_empty_string;

	if (owns_contents && contents != zend_empty_string &&
		lsp_find_class_header_for_name(contents, fqcn, &class_start, &body_start, &body_end, &body_depth)
	) {
		base = lsp_basename_from_fqcn(ZSTR_VAL(fqcn), ZSTR_LEN(fqcn), &base_length);
		short_name = zend_string_init(base, base_length, 0);

		/* Anchor the selection on the declared name inside the header. */
		name_offset = class_start;
		{
			const char *found = NULL, *cursor = ZSTR_VAL(contents) + class_start, *header_end = ZSTR_VAL(contents) + (body_start > 0 ? body_start - 1 : body_start);

			while (cursor < header_end) {
				found = strstr(cursor, ZSTR_VAL(short_name));
				if (!found || found >= header_end) {
					found = NULL;
					break;
				}
				if ((found == ZSTR_VAL(contents) || !lsp_doc_is_identifier_char(found[-1])) &&
					!lsp_doc_is_identifier_char(found[base_length])
				) {
					break;
				}
				cursor = found + 1;
			}

			if (found) {
				name_offset = (size_t) (found - ZSTR_VAL(contents));
			}
		}

		lsp_hier_item(item, fqcn, lsp_hier_type_kind(kind), path, contents, name_offset, name_offset + base_length,
			class_start, body_end > class_start ? body_end + 1 : 0, NULL, NULL);
		zend_string_release(short_name);
		located = true;
	}

	if (owns_contents) {
		zend_string_release(contents);
	}
	zend_string_release(path);

	return located;
}

extern void lsp_lsparrot_prepare_type_hierarchy(lsp_server *server, zval *return_value, lsp_document *document, zval *position)
{
	zend_long line, character;
	zend_string *word, *resolved;
	zval item;
	size_t offset;

	array_init(return_value);

	lsp_position_from_zval(position, &line, &character);
	offset = lsp_offset_at(document->text, line, character);
	word = lsp_word_at(document->text, offset);
	if (ZSTR_LEN(word) == 0 || ZSTR_VAL(word)[0] == '$') {
		zend_string_release(word);

		return;
	}

	lsp_index_join_worker(server);

	resolved = lsp_resolve_class_name_at(document->text, word, offset);
	if (!resolved) {
		resolved = zend_string_copy(word);
	}

	if (lsp_hier_type_item_for_fqcn(server, resolved, &item)) {
		add_next_index_zval(return_value, &item);
	}

	zend_string_release(resolved);
	zend_string_release(word);
}

/* Collect the resolved names after `extends`/`implements` in a class header. */
static inline void lsp_hier_collect_header_names(zend_string *contents, size_t class_start, size_t body_start, const char *keyword, zval *out)
{
	const char *value = ZSTR_VAL(contents), *name_start, *name_end;
	zend_string *raw, *resolved;
	size_t p, header_end, keyword_end;

	header_end = body_start > 0 ? body_start - 1 : body_start;
	for (p = class_start; p < header_end; p++) {
		if (!lsp_keyword_at_slice(value, p, header_end, keyword, &keyword_end)) {
			continue;
		}

		p = keyword_end;
		for (;;) {
			while (p < header_end && (isspace((unsigned char) value[p]) || value[p] == ',')) {
				p++;
			}

			name_start = value + p;
			while (p < header_end && (lsp_doc_is_identifier_char(value[p]) || value[p] == '\\')) {
				p++;
			}
			name_end = value + p;

			if (name_end <= name_start) {
				break;
			}

			raw = zend_string_init(name_start, (size_t) (name_end - name_start), 0);
			resolved = lsp_resolve_class_name_at(contents, raw, (size_t) (name_start - value));
			zend_string_release(raw);
			if (resolved) {
				add_next_index_str(out, resolved);
			}

			while (p < header_end && isspace((unsigned char) value[p])) {
				p++;
			}
			if (p >= header_end || value[p] != ',') {
				break;
			}
		}

		break;
	}
}

extern void lsp_lsparrot_type_hierarchy_supertypes(lsp_server *server, zval *return_value, zval *params)
{
	zval *item, *uri_zv, names, *name_zv, super_item;
	zend_string *fqcn, *path, *contents;
	lsp_document *document;
	size_t class_start = 0, body_start = 0, body_end = 0;
	zend_long body_depth = 0;
	bool owns_contents;

	array_init(return_value);

	item = lsp_array_find(params, "item");
	fqcn = item ? lsp_array_string(item, "name") : NULL;
	uri_zv = item ? lsp_array_find(item, "uri") : NULL;
	if (!fqcn || !uri_zv || Z_TYPE_P(uri_zv) != IS_STRING) {
		return;
	}

	lsp_index_join_worker(server);

	path = lsp_uri_to_path(Z_STR_P(uri_zv));
	document = lsp_document_for_path(server, path);
	contents = document ? zend_string_copy(document->text) : lsp_read_file(path);
	owns_contents = document || contents != zend_empty_string;

	if (owns_contents && contents != zend_empty_string &&
		lsp_find_class_header_for_name(contents, fqcn, &class_start, &body_start, &body_end, &body_depth)
	) {
		array_init(&names);
		lsp_hier_collect_header_names(contents, class_start, body_start, "extends", &names);
		lsp_hier_collect_header_names(contents, class_start, body_start, "implements", &names);

		ZEND_HASH_FOREACH_VAL(Z_ARRVAL(names), name_zv) {
			if (Z_TYPE_P(name_zv) == IS_STRING && lsp_hier_type_item_for_fqcn(server, Z_STR_P(name_zv), &super_item)) {
				add_next_index_zval(return_value, &super_item);
			}
		} ZEND_HASH_FOREACH_END();

		zval_ptr_dtor(&names);
	}

	if (owns_contents) {
		zend_string_release(contents);
	}
	zend_string_release(path);
}

extern void lsp_lsparrot_type_hierarchy_subtypes(lsp_server *server, zval *return_value, zval *params)
{
	lsp_symbol_index *region = &server->symbol_index;
	const lsp_symbol_entry *entry;
	lsp_document *document;
	zval *item, names, *name_zv, sub_item;
	zend_string *target, *entry_fqcn, *path, *contents;
	const char *target_base;
	HashTable visited;
	size_t target_base_length, class_start, body_start, body_end;
	zend_long body_depth;
	uint32_t i, result_count = 0;
	bool owns_contents, matched;

	array_init(return_value);

	item = lsp_array_find(params, "item");
	target = item ? lsp_array_string(item, "name") : NULL;
	if (!target) {
		return;
	}

	target_base = lsp_basename_from_fqcn(ZSTR_VAL(target), ZSTR_LEN(target), &target_base_length);

	lsp_index_join_worker(server);
	lsp_symbol_index_table_ensure(region);
	zend_hash_init(&visited, 64, NULL, NULL, 0);

	for (i = 0; i < region->entry_count && result_count < LSP_HIER_MAX_RESULTS; i++) {
		entry = &region->entries[i];
		if ((entry->flags & (LSP_SYMBOL_ENTRY_DELETED | LSP_SYMBOL_ENTRY_VENDOR)) != 0 ||
			(entry->kind != LSP_SYMBOL_CLASS && entry->kind != LSP_SYMBOL_INTERFACE && entry->kind != LSP_SYMBOL_ENUM)
		) {
			continue;
		}

		entry_fqcn = zend_string_init(lsp_symbol_entry_fqcn(region, entry), entry->fqcn_length, 0);
		if (zend_string_equals_ci(entry_fqcn, target) || zend_hash_exists(&visited, entry_fqcn)) {
			zend_string_release(entry_fqcn);
			continue;
		}
		zend_hash_add_empty_element(&visited, entry_fqcn);

		path = zend_string_init(lsp_symbol_entry_path(region, entry), entry->path_length, 0);
		document = lsp_document_for_path(server, path);
		contents = document ? zend_string_copy(document->text) : lsp_read_file(path);
		owns_contents = document || contents != zend_empty_string;
		matched = false;

		if (owns_contents && contents != zend_empty_string &&
			/* Fast reject: files that never mention the short name cannot
			 * extend/implement it. */
			lsp_hier_contains_ci(contents, target_base, target_base_length)
		) {
			class_start = 0;
			body_start = 0;
			body_end = 0;
			body_depth = 0;
			if (lsp_find_class_header_for_name(contents, entry_fqcn, &class_start, &body_start, &body_end, &body_depth)) {
				array_init(&names);
				lsp_hier_collect_header_names(contents, class_start, body_start, "extends", &names);
				lsp_hier_collect_header_names(contents, class_start, body_start, "implements", &names);

				ZEND_HASH_FOREACH_VAL(Z_ARRVAL(names), name_zv) {
					if (Z_TYPE_P(name_zv) == IS_STRING && zend_string_equals_ci(Z_STR_P(name_zv), target)) {
						matched = true;
						break;
					}
				} ZEND_HASH_FOREACH_END();

				zval_ptr_dtor(&names);
			}
		}

		if (matched && lsp_hier_type_item_for_fqcn(server, entry_fqcn, &sub_item)) {
			add_next_index_zval(return_value, &sub_item);
			result_count++;
		}

		if (owns_contents) {
			zend_string_release(contents);
		}
		zend_string_release(path);
		zend_string_release(entry_fqcn);
	}

	zend_hash_destroy(&visited);
}
