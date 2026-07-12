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

/* Token-based whitespace formatter: re-indents lines from the brace/paren/
 * bracket depth carried by the token stream, keeps switch bodies one level
 * inside their case labels, indents continuation lines of split expressions,
 * aligns doc-comment continuation lines, trims trailing whitespace, and
 * guarantees a final newline. Everything the lexer marks as string, heredoc,
 * or inline-HTML interior is copied verbatim -- the formatter must never
 * change runtime values. Alternative-syntax control flow (endif/endforeach/
 * ...) has no brace anchors, so files using it fall back to trim-only. */

#define LSP_FORMAT_LINE_UNSET 0xFFu
#define LSP_FORMAT_LINE_CODE 0u
#define LSP_FORMAT_LINE_LOCKED 1u
#define LSP_FORMAT_LINE_BLANK 2u
#define LSP_FORMAT_LINE_COMMENT 3u

#define LSP_FORMAT_MAX_INDENT_UNITS 64

typedef struct _lsp_format_options {
	bool reindent;
	bool trim_trailing;
	bool final_newline;
	bool insert_spaces;
	uint32_t indent_size;
} lsp_format_options;

typedef struct _lsp_format_line {
	int32_t indent;
	uint8_t kind;
} lsp_format_line;

static inline bool lsp_format_token_is_whitespace(zval *token)
{
	return lsp_token_long(token, "id", 0) == T_WHITESPACE;
}

static inline bool lsp_format_token_is_comment(zend_long id)
{
	return id == T_COMMENT || id == T_DOC_COMMENT;
}

static inline bool lsp_format_id_locks_interior(zend_long id)
{
	return id == T_CONSTANT_ENCAPSED_STRING ||
		id == T_ENCAPSED_AND_WHITESPACE ||
		id == T_INLINE_HTML
	;
}

static inline bool lsp_format_id_disables_reindent(zend_long id)
{
	return id == T_ENDIF ||
		id == T_ENDFOR ||
		id == T_ENDFOREACH ||
		id == T_ENDWHILE ||
		id == T_ENDSWITCH ||
		id == T_ENDDECLARE
	;
}

/* Operators that signal a statement continued from (or onto) another line;
 * used to give split expressions one extra indent level. ':' stays out of the
 * previous-line set because 'case 1:' legitimately ends a line. */
static inline bool lsp_format_text_is_continuation_op(zend_string *text, bool line_start)
{
	static const char *shared[] = {
		"->", "?->", "::", ".", "=>", "?\?", "&&", "||",
		"=", ".=", "+=", "-=", "*=", "/=", "%=", "**=", "?\?=",
		"|=", "&=", "^=", "<<=", ">>=",
		"+", "-", "*", "/", "%", "**",
		"==", "===", "!=", "!==", "<=>", "<=", ">=",
		"?", "and", "or", "xor", "instanceof",
	};
	size_t i, length;

	if (!text || ZSTR_LEN(text) == 0 || ZSTR_LEN(text) > sizeof("instanceof") - 1) {
		return false;
	}

	for (i = 0; i < sizeof(shared) / sizeof(shared[0]); i++) {
		length = strlen(shared[i]);
		if (ZSTR_LEN(text) == length && strncasecmp(ZSTR_VAL(text), shared[i], length) == 0) {
			return true;
		}
	}

	return line_start && zend_string_equals_literal(text, ":");
}

static inline bool lsp_format_char_is_opener(zend_long id)
{
	return id == '(' || id == '[' || id == '{';
}

static inline bool lsp_format_char_is_closer(zend_long id)
{
	return id == ')' || id == ']' || id == '}';
}

static inline size_t lsp_format_line_of_offset(const size_t *line_starts, size_t line_count, size_t offset)
{
	size_t low = 0, high = line_count;

	while (low + 1 < high) {
		size_t mid = low + (high - low) / 2;
		if (line_starts[mid] <= offset) {
			low = mid;
		} else {
			high = mid;
		}
	}

	return low;
}

static inline void lsp_format_mark_interior(lsp_format_line *lines, size_t line_count, size_t from_line, size_t to_line, uint8_t kind, int32_t indent)
{
	size_t line;

	for (line = from_line; line <= to_line && line < line_count; line++) {
		if (lines[line].kind == LSP_FORMAT_LINE_UNSET) {
			lines[line].kind = kind;
			lines[line].indent = indent;
		}
	}
}

static inline size_t lsp_format_count_newlines(zend_string *text)
{
	const char *p = ZSTR_VAL(text), *end = p + ZSTR_LEN(text);
	size_t count = 0;

	while (p < end) {
		p = memchr(p, '\n', (size_t) (end - p));
		if (!p) {
			break;
		}
		count++;
		p++;
	}

	return count;
}

static inline bool lsp_format_array_bool(zval *array, const char *key, bool fallback)
{
	zval *value = lsp_array_find(array, key);

	if (!value) {
		return fallback;
	}

	if (Z_TYPE_P(value) == IS_TRUE) {
		return true;
	}

	if (Z_TYPE_P(value) == IS_FALSE) {
		return false;
	}

	return fallback;
}

/* Resolve the effective formatting behavior: LSP FormattingOptions from the
 * request (tabSize/insertSpaces) filtered through the server-side overrides
 * configured via start_lsp(['formatting' => ...]). */
static inline void lsp_format_options_resolve(lsp_server *server, zval *params, lsp_format_options *options)
{
	zval *client;
	zend_long tab_size;

	options->reindent = server->options.formatting_reindent;
	options->trim_trailing = server->options.formatting_trim_trailing_whitespace;
	options->final_newline = server->options.formatting_insert_final_newline;
	options->insert_spaces = true;
	options->indent_size = 4;

	client = params ? lsp_array_find(params, "options") : NULL;
	if (client && Z_TYPE_P(client) == IS_ARRAY) {
		options->insert_spaces = lsp_format_array_bool(client, "insertSpaces", true);
		tab_size = lsp_array_long(client, "tabSize", 4);
		if (tab_size >= 1 && tab_size <= 16) {
			options->indent_size = (uint32_t) tab_size;
		}
	}

	if (server->options.formatting_indent_style == LSP_INDENT_STYLE_SPACE) {
		options->insert_spaces = true;
	} else if (server->options.formatting_indent_style == LSP_INDENT_STYLE_TAB) {
		options->insert_spaces = false;
	}

	if (server->options.formatting_indent_size > 0) {
		options->indent_size = (uint32_t) server->options.formatting_indent_size;
	}
}

/* One pass over the token stream fills the per-line plan: target indent units
 * for code lines, locked spans for string/HTML interiors and heredocs, and
 * doc-comment continuations. Returns false when the file uses alternative
 * syntax and only trims may be applied. */
static inline bool lsp_format_plan_lines(zend_string *text, const size_t *line_starts, size_t line_count, lsp_format_line *lines)
{
	zval tokens_zv, *token, *peek;
	HashTable *tokens;
	zend_string *token_text, *prev_code_text;
	zend_long id, peek_id, depth, curly, paren, switch_size, switch_capacity, *switch_stack;
	zend_long tmp_depth, tmp_curly, tmp_switch, pending_switch_paren;
	size_t token_offset, token_line, token_end_line, newlines, seen_line, prev_code_line, peek_offset;
	uint32_t i, j, count;
	int32_t units;
	bool reindent_supported, pending_switch, first_on_line;

	ZVAL_UNDEF(&tokens_zv);
	lsp_lsparrot_tokens_to_zval(&tokens_zv, text);
	if (Z_TYPE(tokens_zv) != IS_ARRAY) {
		if (!Z_ISUNDEF(tokens_zv)) {
			zval_ptr_dtor(&tokens_zv);
		}

		return false;
	}

	tokens = Z_ARRVAL(tokens_zv);
	count = zend_hash_num_elements(tokens);
	depth = 0;
	curly = 0;
	paren = 0;
	switch_size = 0;
	switch_capacity = 8;
	switch_stack = emalloc(sizeof(zend_long) * (size_t) switch_capacity);
	pending_switch = false;
	pending_switch_paren = 0;
	prev_code_text = NULL;
	prev_code_line = (size_t) -1;
	seen_line = (size_t) -1;
	reindent_supported = true;

	for (i = 0; i < count; i++) {
		token = zend_hash_index_find(tokens, i);
		if (!token || Z_TYPE_P(token) != IS_ARRAY || lsp_format_token_is_whitespace(token)) {
			continue;
		}

		id = lsp_token_long(token, "id", 0);
		token_text = lsp_token_string(token, "text");
		token_offset = (size_t) lsp_token_long(token, "offset", 0);
		token_line = lsp_format_line_of_offset(line_starts, line_count, token_offset);

		if (lsp_format_id_disables_reindent(id)) {
			reindent_supported = false;
		}

		/* First significant token on a not-yet-planned line decides that
		 * line's indent. */
		first_on_line = token_line != seen_line;
		if (first_on_line) {
			seen_line = token_line;
		}

		if (first_on_line && token_line < line_count && lines[token_line].kind == LSP_FORMAT_LINE_UNSET) {
			if (lsp_format_char_is_closer(id)) {
				/* Consume the run of closers that starts the line so
				 * `));` outdents past every bracket it closes. */
				tmp_depth = depth;
				tmp_curly = curly;
				tmp_switch = switch_size;
				for (j = i; j < count; j++) {
					peek = zend_hash_index_find(tokens, j);
					if (!peek || Z_TYPE_P(peek) != IS_ARRAY) {
						continue;
					}
					if (lsp_format_token_is_whitespace(peek)) {
						continue;
					}
					peek_id = lsp_token_long(peek, "id", 0);
					peek_offset = (size_t) lsp_token_long(peek, "offset", 0);
					if (lsp_format_line_of_offset(line_starts, line_count, peek_offset) != token_line || !lsp_format_char_is_closer(peek_id)) {
						break;
					}
					if (peek_id == '}') {
						if (tmp_switch > 0 && switch_stack[tmp_switch - 1] == tmp_curly) {
							tmp_switch--;
						}
						tmp_curly--;
					}
					tmp_depth--;
				}

				units = (int32_t) (tmp_depth + tmp_switch);
			} else {
				units = (int32_t) (depth + switch_size);
				if ((id == T_CASE || id == T_DEFAULT) && switch_size > 0 && switch_stack[switch_size - 1] == curly) {
					/* case/default labels sit at the switch level; only
					 * their bodies take the extra unit. Enum cases and
					 * match arms never satisfy the stack check. */
					units -= 1;
				} else if (prev_code_text && prev_code_line != (size_t) -1 && prev_code_line < token_line && (
					lsp_format_text_is_continuation_op(prev_code_text, false) ||
					(!lsp_format_token_is_comment(id) && lsp_format_text_is_continuation_op(token_text, true))
				)) {
					units += 1;
				}
			}

			if (units < 0) {
				units = 0;
			}
			if (units > LSP_FORMAT_MAX_INDENT_UNITS) {
				units = LSP_FORMAT_MAX_INDENT_UNITS;
			}

			lines[token_line].kind = LSP_FORMAT_LINE_CODE;
			lines[token_line].indent = units;
		}

		/* Multi-line tokens dictate what may happen to the lines they
		 * cover. */
		if (token_text) {
			newlines = lsp_format_count_newlines(token_text);
			if (newlines > 0) {
				token_end_line = token_line + newlines;
				if (lsp_format_token_is_comment(id) && ZSTR_LEN(token_text) >= 2 && ZSTR_VAL(token_text)[1] == '*') {
					lsp_format_mark_interior(lines, line_count, token_line + 1, token_end_line,
						LSP_FORMAT_LINE_COMMENT,
						token_line < line_count && lines[token_line].kind == LSP_FORMAT_LINE_CODE ? lines[token_line].indent : -1);
				} else if (lsp_format_id_locks_interior(id)) {
					lsp_format_mark_interior(lines, line_count, token_line + 1, token_end_line, LSP_FORMAT_LINE_LOCKED, -1);
				}
			}
		}

		if (id == T_START_HEREDOC) {
			/* Everything through the closing label is semantic content;
			 * the label's own indentation defines the stripped margin. */
			for (j = i + 1; j < count; j++) {
				peek = zend_hash_index_find(tokens, j);
				if (!peek || Z_TYPE_P(peek) != IS_ARRAY) {
					continue;
				}
				if (lsp_token_long(peek, "id", 0) == T_END_HEREDOC) {
					peek_offset = (size_t) lsp_token_long(peek, "offset", 0);
					token_end_line = lsp_format_line_of_offset(line_starts, line_count, peek_offset);
					lsp_format_mark_interior(lines, line_count, token_line + 1, token_end_line, LSP_FORMAT_LINE_LOCKED, -1);
					break;
				}
			}
		}

		if (id == T_INLINE_HTML && token_line < line_count && lines[token_line].kind == LSP_FORMAT_LINE_CODE) {
			/* Template markup owns its own line layout. */
			lines[token_line].kind = LSP_FORMAT_LINE_LOCKED;
			lines[token_line].indent = -1;
		}

		/* State updates. */
		if (id == T_SWITCH) {
			pending_switch = true;
			pending_switch_paren = paren;
		} else if (pending_switch && id == ';') {
			pending_switch = false;
		}

		if (id == '(') {
			paren++;
			depth++;
		} else if (id == ')') {
			paren--;
			depth--;
		} else if (id == '[' || id == T_ATTRIBUTE) {
			depth++;
		} else if (id == ']') {
			depth--;
		} else if (id == '{' || id == T_CURLY_OPEN || id == T_DOLLAR_OPEN_CURLY_BRACES) {
			curly++;
			depth++;
			if (pending_switch && id == '{' && paren == pending_switch_paren) {
				if (switch_size == switch_capacity) {
					switch_capacity *= 2;
					switch_stack = erealloc(switch_stack, sizeof(zend_long) * (size_t) switch_capacity);
				}
				switch_stack[switch_size++] = curly;
				pending_switch = false;
			}
		} else if (id == '}') {
			if (switch_size > 0 && switch_stack[switch_size - 1] == curly) {
				switch_size--;
			}
			curly--;
			depth--;
		}

		if (!lsp_format_token_is_comment(id)) {
			prev_code_text = token_text;
			prev_code_line = token_line;
		}
	}

	efree(switch_stack);
	zval_ptr_dtor(&tokens_zv);

	return reindent_supported;
}

static inline void lsp_format_append_indent(smart_str *out, const lsp_format_options *options, int32_t units)
{
	int32_t i;

	if (units <= 0) {
		return;
	}

	if (options->insert_spaces) {
		for (i = 0; i < units; i++) {
			smart_str_appendl(out, "                ", options->indent_size <= 16 ? options->indent_size : 16);
		}
	} else {
		for (i = 0; i < units; i++) {
			smart_str_appendc(out, '\t');
		}
	}
}

/* Emit lines [from_line, to_line] of the formatting plan. */
static inline zend_string *lsp_format_emit(zend_string *text, const size_t *line_starts, size_t line_count, const lsp_format_line *lines, const lsp_format_options *options, bool reindent, size_t from_line, size_t to_line, bool whole_document)
{
	smart_str out = {0};
	const char *value = ZSTR_VAL(text), *line_start, *line_end, *content_start, *trim_end;
	size_t line, next_start;
	bool has_newline;

	for (line = from_line; line <= to_line && line < line_count; line++) {
		line_start = value + line_starts[line];
		next_start = line + 1 < line_count ? line_starts[line + 1] : ZSTR_LEN(text);
		line_end = value + next_start;
		has_newline = line_end > line_start && line_end[-1] == '\n';
		if (has_newline) {
			line_end--;
		}

		if (lines[line].kind == LSP_FORMAT_LINE_LOCKED) {
			smart_str_appendl(&out, line_start, (size_t) (line_end - line_start));
			if (has_newline) {
				smart_str_appendc(&out, '\n');
			}
			continue;
		}

		trim_end = line_end;
		if (options->trim_trailing || lines[line].kind == LSP_FORMAT_LINE_BLANK || lines[line].kind == LSP_FORMAT_LINE_UNSET) {
			if (trim_end > line_start && trim_end[-1] == '\r') {
				trim_end--;
			}
			while (trim_end > line_start && (trim_end[-1] == ' ' || trim_end[-1] == '\t')) {
				trim_end--;
			}
		} else if (trim_end > line_start && trim_end[-1] == '\r') {
			trim_end--;
		}

		content_start = line_start;
		while (content_start < trim_end && (*content_start == ' ' || *content_start == '\t')) {
			content_start++;
		}

		if (content_start >= trim_end) {
			/* Blank (or whitespace-only) line. */
			if (!options->trim_trailing) {
				smart_str_appendl(&out, line_start, (size_t) (trim_end - line_start));
			}
			if (has_newline) {
				smart_str_appendc(&out, '\n');
			}
			continue;
		}

		if (reindent && lines[line].kind == LSP_FORMAT_LINE_CODE && lines[line].indent >= 0) {
			lsp_format_append_indent(&out, options, lines[line].indent);
			smart_str_appendl(&out, content_start, (size_t) (trim_end - content_start));
		} else if (reindent && lines[line].kind == LSP_FORMAT_LINE_COMMENT && lines[line].indent >= 0 && *content_start == '*') {
			lsp_format_append_indent(&out, options, lines[line].indent);
			smart_str_appendc(&out, ' ');
			smart_str_appendl(&out, content_start, (size_t) (trim_end - content_start));
		} else {
			/* Keep the author's leading whitespace, only trim the tail. */
			smart_str_appendl(&out, line_start, (size_t) (trim_end - line_start));
		}

		if (has_newline) {
			smart_str_appendc(&out, '\n');
		}
	}

	if (whole_document && options->final_newline) {
		if (!out.s || ZSTR_LEN(out.s) == 0 || ZSTR_VAL(out.s)[ZSTR_LEN(out.s) - 1] != '\n') {
			smart_str_appendc(&out, '\n');
		}
	}

	smart_str_0(&out);

	return out.s ? out.s : zend_empty_string;
}

static inline size_t *lsp_format_line_starts(zend_string *text, size_t *line_count)
{
	const char *value = ZSTR_VAL(text), *p, *end = value + ZSTR_LEN(text);
	size_t *starts, count, i;

	count = 1;
	for (p = value; p < end; p++) {
		if (*p == '\n') {
			count++;
		}
	}

	starts = emalloc(sizeof(size_t) * count);
	starts[0] = 0;
	i = 1;
	for (p = value; p < end; p++) {
		if (*p == '\n' && i < count) {
			starts[i++] = (size_t) (p - value) + 1;
		}
	}

	*line_count = count;

	return starts;
}

static inline zend_string *lsp_format_document_lines(lsp_server *server, lsp_document *document, zval *params, size_t from_line, size_t to_line, size_t *out_start_offset, size_t *out_end_offset, bool whole_document)
{
	lsp_format_options options;
	lsp_format_line *lines;
	zend_string *formatted;
	size_t *line_starts, line_count, i;
	bool reindent;

	lsp_format_options_resolve(server, params, &options);
	line_starts = lsp_format_line_starts(document->text, &line_count);
	lines = emalloc(sizeof(lsp_format_line) * line_count);
	for (i = 0; i < line_count; i++) {
		lines[i].kind = LSP_FORMAT_LINE_UNSET;
		lines[i].indent = -1;
	}

	/* The plan must ALWAYS run: it is what marks string/heredoc/inline-HTML
	 * interiors as locked so trimming can never alter runtime values. Only
	 * the reindent decision depends on its verdict. */
	reindent = lsp_format_plan_lines(document->text, line_starts, line_count, lines) && options.reindent;

	if (to_line >= line_count) {
		to_line = line_count > 0 ? line_count - 1 : 0;
	}
	if (from_line > to_line) {
		from_line = to_line;
	}

	*out_start_offset = line_starts[from_line];
	*out_end_offset = to_line + 1 < line_count ? line_starts[to_line + 1] : ZSTR_LEN(document->text);

	formatted = lsp_format_emit(document->text, line_starts, line_count, lines, &options, reindent, from_line, to_line, whole_document);

	efree(lines);
	efree(line_starts);

	return formatted;
}

static inline void lsp_format_reply_edit(zval *return_value, lsp_document *document, zend_string *formatted, size_t start_offset, size_t end_offset)
{
	zval edit, range;

	array_init(return_value);
	if (ZSTR_LEN(formatted) == end_offset - start_offset &&
		memcmp(ZSTR_VAL(formatted), ZSTR_VAL(document->text) + start_offset, ZSTR_LEN(formatted)) == 0
	) {
		zend_string_release(formatted);

		return;
	}

	array_init(&edit);
	lsp_range_from_offsets(document->text, start_offset, end_offset, &range);
	add_assoc_zval(&edit, "range", &range);
	add_assoc_str(&edit, "newText", formatted);
	add_next_index_zval(return_value, &edit);
}

extern void lsp_lsparrot_formatting(lsp_server *server, zval *return_value, lsp_document *document, zval *params)
{
	zend_string *formatted;
	size_t start_offset, end_offset;

	if (!server->options.formatting_enabled) {
		ZVAL_NULL(return_value);

		return;
	}

	formatted = lsp_format_document_lines(server, document, params, 0, SIZE_MAX - 1, &start_offset, &end_offset, true);
	lsp_format_reply_edit(return_value, document, formatted, 0, ZSTR_LEN(document->text));
}

extern void lsp_lsparrot_range_formatting(lsp_server *server, zval *return_value, lsp_document *document, zval *params)
{
	zval *range, *start, *end;
	zend_string *formatted;
	zend_long start_line, end_line, end_character;
	size_t start_offset, end_offset;

	if (!server->options.formatting_enabled) {
		ZVAL_NULL(return_value);

		return;
	}

	range = lsp_array_find(params, "range");
	start = range ? lsp_array_find(range, "start") : NULL;
	end = range ? lsp_array_find(range, "end") : NULL;
	if (!start || !end) {
		array_init(return_value);

		return;
	}

	start_line = lsp_array_long(start, "line", 0);
	end_line = lsp_array_long(end, "line", 0);
	end_character = lsp_array_long(end, "character", 0);
	if (start_line < 0) {
		start_line = 0;
	}
	if (end_line < start_line) {
		end_line = start_line;
	}
	/* A range ending at column 0 of a later line excludes that line. */
	if (end_character == 0 && end_line > start_line) {
		end_line--;
	}

	formatted = lsp_format_document_lines(server, document, params, (size_t) start_line, (size_t) end_line, &start_offset, &end_offset, false);
	lsp_format_reply_edit(return_value, document, formatted, start_offset, end_offset);
}
