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

/* Quick fixes for analyzer (PHPStan/Psalm) diagnostics: suppression comments
 * and type guarantees via "@var" docblocks or assert() statements. The fix
 * material comes from the diagnostics echoed back by the client inside the
 * codeAction request context, so no extra server state is required. */

static inline bool lsp_quick_fix_line_bounds(zend_string *text, zend_long line, size_t *line_start, size_t *line_end, size_t *indent_end)
{
	const char *value = ZSTR_VAL(text);
	size_t offset, length = ZSTR_LEN(text);

	offset = lsp_offset_at(text, line, 0);
	if (offset > length) {
		return false;
	}

	*line_start = offset;
	while (offset < length && value[offset] != '\n') {
		offset++;
	}
	*line_end = offset;

	offset = *line_start;
	while (offset < *line_end && (value[offset] == ' ' || value[offset] == '\t')) {
		offset++;
	}
	*indent_end = offset;

	return true;
}

static inline zend_long lsp_quick_fix_diagnostic_line(zval *diagnostic)
{
	zval *range, *start;

	range = lsp_array_find(diagnostic, "range");
	start = range ? lsp_array_find(range, "start") : NULL;

	return start ? lsp_array_long(start, "line", 0) : 0;
}

static inline void lsp_quick_fix_add_insert_action(zval *actions, lsp_document *document, zval *diagnostic, zend_string *title, zend_long line, zend_string *indent, zend_string *inserted)
{
	zend_string *new_text;
	zval action, diagnostics, diagnostic_copy, edit, changes, edits, text_edit, range, start, end;

	new_text = strpprintf(0, "%s%s\n", ZSTR_VAL(indent), ZSTR_VAL(inserted));

	array_init(&action);
	add_assoc_str(&action, "title", zend_string_copy(title));
	add_assoc_string(&action, "kind", "quickfix");

	array_init(&diagnostics);
	ZVAL_COPY(&diagnostic_copy, diagnostic);
	add_next_index_zval(&diagnostics, &diagnostic_copy);
	add_assoc_zval(&action, "diagnostics", &diagnostics);

	array_init(&start);
	add_assoc_long(&start, "line", line);
	add_assoc_long(&start, "character", 0);
	array_init(&end);
	add_assoc_long(&end, "line", line);
	add_assoc_long(&end, "character", 0);
	array_init(&range);
	add_assoc_zval(&range, "start", &start);
	add_assoc_zval(&range, "end", &end);

	array_init(&text_edit);
	add_assoc_zval(&text_edit, "range", &range);
	add_assoc_str(&text_edit, "newText", new_text);

	array_init(&edits);
	add_next_index_zval(&edits, &text_edit);

	array_init(&changes);
	add_assoc_zval_ex(&changes, ZSTR_VAL(document->uri), ZSTR_LEN(document->uri), &edits);

	array_init(&edit);
	add_assoc_zval(&edit, "changes", &changes);
	add_assoc_zval(&action, "edit", &edit);

	add_next_index_zval(actions, &action);
}

static inline bool lsp_quick_fix_type_char_is_plain(char c)
{
	return lsp_doc_is_identifier_char(c) || c == '\\';
}

/* Extract an "expected type" out of well-known analyzer message templates:
 *   "... expects TYPE, ... given"      (argument.type)
 *   "Property ... (TYPE) does not accept ..."
 *   "... should return TYPE but ..."   (return.type)
 *   "Cannot assign TYPE ..." style messages are intentionally not handled.
 */
static inline zend_string *lsp_quick_fix_expected_type(zend_string *message)
{
	const char *value = ZSTR_VAL(message), *marker, *type_start, *type_end, *limit;

	limit = value + ZSTR_LEN(message);

	marker = strstr(value, " expects ");
	if (marker) {
		type_start = marker + sizeof(" expects ") - 1;
		type_end = type_start;
		while (type_end < limit && *type_end != ',' && *type_end != '\0') {
			type_end++;
		}
		if (type_end > type_start) {
			return zend_string_init(type_start, type_end - type_start, 0);
		}
	}

	marker = strstr(value, ") does not accept ");
	if (marker) {
		type_end = marker;
		type_start = type_end;
		while (type_start > value && *(type_start - 1) != '(') {
			type_start--;
		}
		if (type_start > value && type_end > type_start) {
			return zend_string_init(type_start, type_end - type_start, 0);
		}
	}

	marker = strstr(value, " should return ");
	if (marker) {
		type_start = marker + sizeof(" should return ") - 1;
		type_end = type_start;
		while (type_end < limit && *type_end != ' ' && *type_end != ',' && *type_end != '\0') {
			type_end++;
		}
		if (type_end > type_start) {
			return zend_string_init(type_start, type_end - type_start, 0);
		}
	}

	return NULL;
}

/* Find the variable a "@var"/assert() guarantee should target: prefer the
 * left-hand side of an assignment on the diagnostic line, otherwise a
 * variable that the analyzer message names and the line actually contains. */
static inline zend_string *lsp_quick_fix_target_variable(zend_string *text, size_t indent_end, size_t line_end, zend_string *message)
{
	const char *value = ZSTR_VAL(text), *cursor, *name_start, *message_value, *dollar, *message_limit;
	zend_string *candidate;
	size_t name_length;
	bool found;

	if (indent_end < line_end && value[indent_end] == '$') {
		name_start = value + indent_end + 1;
		cursor = name_start;
		while ((size_t) (cursor - value) < line_end && lsp_doc_is_identifier_char(*cursor)) {
			cursor++;
		}
		name_length = cursor - name_start;

		while ((size_t) (cursor - value) < line_end && (*cursor == ' ' || *cursor == '\t')) {
			cursor++;
		}

		if (name_length > 0 && (size_t) (cursor - value) < line_end && *cursor == '=' &&
			((size_t) (cursor + 1 - value) >= line_end || *(cursor + 1) != '=')
		) {
			return zend_string_init(name_start - 1, name_length + 1, 0);
		}
	}

	message_value = ZSTR_VAL(message);
	message_limit = message_value + ZSTR_LEN(message);
	dollar = message_value;
	while ((dollar = memchr(dollar, '$', message_limit - dollar)) != NULL) {
		name_start = dollar + 1;
		cursor = name_start;
		while (cursor < message_limit && lsp_doc_is_identifier_char(*cursor)) {
			cursor++;
		}
		name_length = cursor - name_start;
		if (name_length == 0) {
			dollar++;
			continue;
		}

		candidate = zend_string_init(dollar, name_length + 1, 0);

		/* The variable must occur on the diagnostic line itself. */
		found = false;
		for (cursor = value + indent_end; (size_t) (cursor - value) + ZSTR_LEN(candidate) <= line_end; cursor++) {
			if (memcmp(cursor, ZSTR_VAL(candidate), ZSTR_LEN(candidate)) == 0 &&
				((size_t) (cursor - value) + ZSTR_LEN(candidate) >= line_end ||
					!lsp_doc_is_identifier_char(cursor[ZSTR_LEN(candidate)]))
			) {
				found = true;
				break;
			}
		}

		if (found) {
			return candidate;
		}

		zend_string_release(candidate);
		dollar = name_start;
	}

	return NULL;
}

static inline bool lsp_quick_fix_type_is_plain(zend_string *type)
{
	size_t i;

	if (ZSTR_LEN(type) == 0) {
		return false;
	}

	for (i = 0; i < ZSTR_LEN(type); i++) {
		if (!lsp_quick_fix_type_char_is_plain(ZSTR_VAL(type)[i])) {
			return false;
		}
	}

	return true;
}

static inline const char *lsp_quick_fix_scalar_assert_function(zend_string *type)
{
	if (zend_string_equals_literal(type, "string")) {
		return "is_string";
	}
	if (zend_string_equals_literal(type, "int")) {
		return "is_int";
	}
	if (zend_string_equals_literal(type, "float")) {
		return "is_float";
	}
	if (zend_string_equals_literal(type, "bool")) {
		return "is_bool";
	}
	if (zend_string_equals_literal(type, "array")) {
		return "is_array";
	}
	if (zend_string_equals_literal(type, "callable")) {
		return "is_callable";
	}
	if (zend_string_equals_literal(type, "iterable")) {
		return "is_iterable";
	}
	if (zend_string_equals_literal(type, "object")) {
		return "is_object";
	}

	return NULL;
}

static inline bool lsp_quick_fix_type_is_class_like(zend_string *type)
{
	char first;

	if (!lsp_quick_fix_type_is_plain(type) || lsp_quick_fix_scalar_assert_function(type) != NULL ||
		zend_string_equals_literal(type, "null") ||
		zend_string_equals_literal(type, "mixed") ||
		zend_string_equals_literal(type, "void") ||
		zend_string_equals_literal(type, "true") ||
		zend_string_equals_literal(type, "false")
	) {
		return false;
	}

	first = ZSTR_VAL(type)[0];

	return first == '\\' || (first >= 'A' && first <= 'Z');
}

static inline void lsp_quick_fix_add_type_guarantee_actions(zval *actions, lsp_document *document, zval *diagnostic, zend_string *message, zend_long line, zend_string *indent, size_t indent_end, size_t line_end)
{
	const char *assert_function;
	zend_string *expected_type, *variable, *title, *inserted;

	expected_type = lsp_quick_fix_expected_type(message);
	if (!expected_type) {
		return;
	}

	variable = lsp_quick_fix_target_variable(document->text, indent_end, line_end, message);
	if (!variable) {
		zend_string_release(expected_type);

		return;
	}

	title = strpprintf(0, "Assume type: @var %s %s", ZSTR_VAL(expected_type), ZSTR_VAL(variable));
	inserted = strpprintf(0, "/** @var %s %s */", ZSTR_VAL(expected_type), ZSTR_VAL(variable));
	lsp_quick_fix_add_insert_action(actions, document, diagnostic, title, line, indent, inserted);
	zend_string_release(inserted);
	zend_string_release(title);

	if (lsp_quick_fix_type_is_class_like(expected_type)) {
		title = strpprintf(0, "Guarantee type: assert(%s instanceof %s)", ZSTR_VAL(variable), ZSTR_VAL(expected_type));
		inserted = strpprintf(0, "assert(%s instanceof %s);", ZSTR_VAL(variable), ZSTR_VAL(expected_type));
		lsp_quick_fix_add_insert_action(actions, document, diagnostic, title, line, indent, inserted);
		zend_string_release(inserted);
		zend_string_release(title);
	} else {
		assert_function = lsp_quick_fix_scalar_assert_function(expected_type);
		if (assert_function) {
			title = strpprintf(0, "Guarantee type: assert(%s(%s))", assert_function, ZSTR_VAL(variable));
			inserted = strpprintf(0, "assert(%s(%s));", assert_function, ZSTR_VAL(variable));
			lsp_quick_fix_add_insert_action(actions, document, diagnostic, title, line, indent, inserted);
			zend_string_release(inserted);
			zend_string_release(title);
		}
	}

	zend_string_release(variable);
	zend_string_release(expected_type);
}

static inline void lsp_quick_fix_add_suppress_action(zval *actions, lsp_document *document, zval *diagnostic, zend_string *source, zend_string *code, zend_long line, zend_string *indent)
{
	zend_string *title, *inserted;

	if (zend_string_equals_literal(source, "phpstan")) {
		if (code) {
			title = strpprintf(0, "Suppress PHPStan: @phpstan-ignore %s", ZSTR_VAL(code));
			inserted = strpprintf(0, "/* @phpstan-ignore %s */", ZSTR_VAL(code));
		} else {
			title = zend_string_init(ZEND_STRL("Suppress PHPStan: @phpstan-ignore-next-line"), 0);
			inserted = zend_string_init(ZEND_STRL("/* @phpstan-ignore-next-line */"), 0);
		}
	} else if (zend_string_equals_literal(source, "psalm") || zend_string_equals_literal(source, "psalm-ls")) {
		if (!code) {
			return;
		}

		title = strpprintf(0, "Suppress Psalm: @psalm-suppress %s", ZSTR_VAL(code));
		inserted = strpprintf(0, "/** @psalm-suppress %s */", ZSTR_VAL(code));
	} else {
		return;
	}

	lsp_quick_fix_add_insert_action(actions, document, diagnostic, title, line, indent, inserted);
	zend_string_release(inserted);
	zend_string_release(title);
}

extern void lsp_add_analyzer_quick_fixes(zval *actions, lsp_document *document, zval *params)
{
	zend_long line;
	zend_string *source, *code, *message, *indent;
	zval *context, *diagnostics, *diagnostic;
	size_t line_start = 0, line_end = 0, indent_end = 0;

	context = lsp_array_find(params, "context");
	diagnostics = context ? lsp_array_find(context, "diagnostics") : NULL;
	if (!diagnostics || Z_TYPE_P(diagnostics) != IS_ARRAY) {
		return;
	}

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(diagnostics), diagnostic) {
		if (Z_TYPE_P(diagnostic) != IS_ARRAY) {
			continue;
		}

		source = lsp_array_string(diagnostic, "source");
		if (!source || (
			!zend_string_equals_literal(source, "phpstan") &&
			!zend_string_equals_literal(source, "psalm") &&
			!zend_string_equals_literal(source, "psalm-ls")
		)) {
			continue;
		}

		message = lsp_array_string(diagnostic, "message");
		code = lsp_array_string(diagnostic, "code");
		line = lsp_quick_fix_diagnostic_line(diagnostic);
		if (!message || !lsp_quick_fix_line_bounds(document->text, line, &line_start, &line_end, &indent_end)) {
			continue;
		}

		indent = zend_string_init(ZSTR_VAL(document->text) + line_start, indent_end - line_start, 0);

		lsp_quick_fix_add_type_guarantee_actions(actions, document, diagnostic, message, line, indent, indent_end, line_end);
		lsp_quick_fix_add_suppress_action(actions, document, diagnostic, source, code, line, indent);

		zend_string_release(indent);
	} ZEND_HASH_FOREACH_END();
}
