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

static inline zend_string *lsp_phpdoc_normalized_signature_slice(const char *start, size_t length)
{
	smart_str signature = {0};
	bool pending_space = false;
	size_t i;
	char c;

	for (i = 0; i < length; i++) {
		c = start[i];
		if (isspace((unsigned char) c)) {
			pending_space = true;

			continue;
		}

		if (pending_space && signature.s && ZSTR_LEN(signature.s) > 0 &&
			c != ')' && c != ',' && c != ':' && c != '|' && c != '&' &&
			ZSTR_VAL(signature.s)[ZSTR_LEN(signature.s) - 1] != '(' &&
			ZSTR_VAL(signature.s)[ZSTR_LEN(signature.s) - 1] != '|'
		) {
			smart_str_appendc(&signature, ' ');
		}

		smart_str_appendc(&signature, c);
		pending_space = false;
	}

	smart_str_0(&signature);

	return signature.s ? signature.s : zend_string_init("", 0, 0);
}

static inline bool lsp_phpdoc_modifier_before(zend_string *text, size_t end, const char *word, size_t *start)
{
	const char *value = ZSTR_VAL(text);
	size_t p, length;

	p = end;
	while (p > 0 && isspace((unsigned char) value[p - 1])) {
		p--;
	}

	length = strlen(word);
	if (p < length) {
		return false;
	}

	*start = p - length;
	if (strncasecmp(value + *start, word, length) != 0) {
		return false;
	}

	if (*start > 0 && lsp_doc_is_identifier_char(value[*start - 1])) {
		return false;
	}

	return p >= ZSTR_LEN(text) || !lsp_doc_is_identifier_char(value[p]);
}

static inline size_t lsp_phpdoc_class_anchor(zend_string *text, size_t class_start)
{
	size_t anchor, modifier_start;

	anchor = class_start;
	while (true) {
		if (lsp_phpdoc_modifier_before(text, anchor, "final", &modifier_start) ||
			lsp_phpdoc_modifier_before(text, anchor, "abstract", &modifier_start) ||
			lsp_phpdoc_modifier_before(text, anchor, "readonly", &modifier_start)
		) {
			anchor = modifier_start;

			continue;
		}

		break;
	}

	return anchor;
}

static inline bool lsp_phpdoc_class_docblock_bounds(zend_string *text, size_t class_start, size_t *doc_start, size_t *doc_end)
{
	const char *value = ZSTR_VAL(text);
	size_t anchor, p, q;

	anchor = lsp_phpdoc_class_anchor(text, class_start);
	p = anchor;
	while (p > 0 && isspace((unsigned char) value[p - 1])) {
		p--;
	}

	if (p < 2 || value[p - 2] != '*' || value[p - 1] != '/') {
		return false;
	}

	*doc_end = p;
	q = p - 2;
	while (q > 1) {
		if (value[q - 2] == '/' && value[q - 1] == '*' && value[q] == '*') {
			*doc_start = q - 2;

			return true;
		}

		q--;
	}

	return false;
}

static inline bool lsp_phpdoc_first_class_start(zend_string *text, size_t *class_start)
{
	const char *keywords[4] = {"class", "interface", "trait", "enum"}, *value = ZSTR_VAL(text);
	size_t i, keyword_index, keyword_length;

	for (i = 0; i + sizeof("class") - 1 < ZSTR_LEN(text); i++) {
		for (keyword_index = 0; keyword_index < sizeof(keywords) / sizeof(keywords[0]); keyword_index++) {
			keyword_length = strlen(keywords[keyword_index]);
			if (i + keyword_length >= ZSTR_LEN(text) || memcmp(value + i, keywords[keyword_index], keyword_length) != 0) {
				continue;
			}

			if (i > 0 && (lsp_doc_is_identifier_char(value[i - 1]) || value[i - 1] == '$')) {
				continue;
			}

			if (!lsp_text_is_word_boundary(text, i + keyword_length)) {
				continue;
			}

			*class_start = i;

			return true;
		}
	}

	return false;
}

static inline const char *lsp_phpdoc_method_tag_in_line(const char *line_start, const char *line_end)
{
	const char *p;
	size_t tag_length;

	p = line_start;
	while (p < line_end && isspace((unsigned char) *p)) {
		p++;
	}

	if (p + sizeof("/**") - 1 <= line_end && memcmp(p, "/**", sizeof("/**") - 1) == 0) {
		p += sizeof("/**") - 1;
	}

	while (p < line_end && isspace((unsigned char) *p)) {
		p++;
	}

	if (p < line_end && *p == '*') {
		p++;
		if (p < line_end && *p == '/') {
			return NULL;
		}

		while (p < line_end && isspace((unsigned char) *p)) {
			p++;
		}
	}

	tag_length = sizeof("@method") - 1;

	if (p + tag_length > line_end || memcmp(p, "@method", tag_length) != 0) {
		return NULL;
	}

	if (p + tag_length < line_end && !isspace((unsigned char) p[tag_length])) {
		return NULL;
	}

	return p + tag_length;
}

static inline const char *lsp_phpdoc_property_tag_in_line(const char *line_start, const char *line_end)
{
	const char *p;
	size_t tag_length;

	p = line_start;
	while (p < line_end && isspace((unsigned char) *p)) {
		p++;
	}

	if (p + sizeof("/**") - 1 <= line_end && memcmp(p, "/**", sizeof("/**") - 1) == 0) {
		p += sizeof("/**") - 1;
	}

	while (p < line_end && isspace((unsigned char) *p)) {
		p++;
	}

	if (p < line_end && *p == '*') {
		p++;

		if (p < line_end && *p == '/') {
			return NULL;
		}

		while (p < line_end && isspace((unsigned char) *p)) {
			p++;
		}
	}

	tag_length = sizeof("@property") - 1;
	if (p + tag_length > line_end || memcmp(p, "@property", tag_length) != 0) {
		return NULL;
	}

	p += tag_length;
	if (p < line_end && *p == '-') {
		p++;
		while (p < line_end && isalpha((unsigned char) *p)) {
			p++;
		}
	}

	if (p < line_end && !isspace((unsigned char) *p)) {
		return NULL;
	}

	return p;
}

static inline const char *lsp_phpdoc_use_tag_in_line(const char *line_start, const char *line_end)
{
	const char *p;
	size_t tag_length;

	p = line_start;
	while (p < line_end && isspace((unsigned char) *p)) {
		p++;
	}

	if (p + sizeof("/**") - 1 <= line_end && memcmp(p, "/**", sizeof("/**") - 1) == 0) {
		p += sizeof("/**") - 1;
	}

	while (p < line_end && isspace((unsigned char) *p)) {
		p++;
	}

	if (p < line_end && *p == '*') {
		p++;
		if (p < line_end && *p == '/') {
			return NULL;
		}

		while (p < line_end && isspace((unsigned char) *p)) {
			p++;
		}
	}

	tag_length = sizeof("@use") - 1;
	if (p + tag_length > line_end || memcmp(p, "@use", tag_length) != 0) {
		return NULL;
	}

	if (p + tag_length < line_end && !isspace((unsigned char) p[tag_length])) {
		return NULL;
	}

	return p + tag_length;
}

static inline zend_string *lsp_phpdoc_use_type_from_line(const char *tag, const char *line_end)
{
	const char *p, *type_start, *type_end;
	int32_t depth;

	p = tag;
	while (p < line_end && isspace((unsigned char) *p)) {
		p++;
	}

	type_start = p;
	depth = 0;
	while (p < line_end) {
		if (p + 1 < line_end && p[0] == '*' && p[1] == '/') {
			break;
		}

		if (*p == '<' || *p == '(' || *p == '[' || *p == '{') {
			depth++;
		} else if ((*p == '>' || *p == ')' || *p == ']' || *p == '}') && depth > 0) {
			depth--;
		} else if (depth == 0 && isspace((unsigned char) *p)) {
			break;
		}

		p++;
	}

	type_end = p;
	while (type_end > type_start && isspace((unsigned char) type_end[-1])) {
		type_end--;
	}

	if (type_end <= type_start) {
		return NULL;
	}

	return lsp_phpdoc_normalized_signature_slice(type_start, type_end - type_start);
}

static inline zend_string *lsp_phpdoc_generic_base_type(zend_string *type)
{
	const char *value, *end, *open;

	value = ZSTR_VAL(type);
	end = value + ZSTR_LEN(type);
	open = memchr(value, '<', ZSTR_LEN(type));
	if (open) {
		end = open;
	}

	while (end > value && isspace((unsigned char) end[-1])) {
		end--;
	}

	return lsp_phpdoc_normalized_signature_slice(value, end - value);
}

static inline bool lsp_phpdoc_method_parse_line(const char *tag, const char *line_end, zend_string **label, zend_string **detail, bool *is_static)
{
	const char *p, *q, *return_start, *return_end, *name_start, *name_end, *args_start, *args_end;
	zend_string *args, *return_type;
	size_t static_length;
	int32_t depth;

	*label = NULL;
	*detail = NULL;
	*is_static = false;

	while (line_end > tag && isspace((unsigned char) line_end[-1])) {
		line_end--;
	}

	if (line_end - tag >= 2 && line_end[-2] == '*' && line_end[-1] == '/') {
		line_end -= 2;
		while (line_end > tag && isspace((unsigned char) line_end[-1])) {
			line_end--;
		}
	}

	p = tag;
	while (p < line_end && isspace((unsigned char) *p)) {
		p++;
	}

	static_length = sizeof("static") - 1;
	if (p + static_length <= line_end &&
		strncasecmp(p, "static", static_length) == 0 &&
		(p + static_length == line_end || isspace((unsigned char) p[static_length]))
	) {
		*is_static = true;
		p += static_length;

		while (p < line_end && isspace((unsigned char) *p)) {
			p++;
		}
	}

	return_start = p;
	args_start = NULL;

	for (q = p; q < line_end; q++) {
		if (*q == '(') {
			args_start = q;
			break;
		}
	}

	if (!args_start) {
		return false;
	}

	name_end = args_start;
	while (name_end > p && isspace((unsigned char) name_end[-1])) {
		name_end--;
	}

	name_start = name_end;
	while (name_start > p && lsp_doc_is_identifier_char(name_start[-1])) {
		name_start--;
	}

	if (name_start == name_end || !lsp_doc_is_identifier_start(*name_start)) {
		return false;
	}

	return_end = name_start;
	while (return_end > return_start && isspace((unsigned char) return_end[-1])) {
		return_end--;
	}

	args_end = NULL;
	depth = 0;
	for (q = args_start; q < line_end; q++) {
		if (*q == '(') {
			depth++;
		} else if (*q == ')') {
			depth--;
			if (depth == 0) {
				args_end = q + 1;
				break;
			}
		}
	}

	if (!args_end) {
		return false;
	}

	*label = zend_string_init(name_start, name_end - name_start, 0);
	args = lsp_phpdoc_normalized_signature_slice(args_start, args_end - args_start);

	if (return_end > return_start) {
		return_type = lsp_phpdoc_normalized_signature_slice(return_start, return_end - return_start);
		*detail = ZSTR_LEN(return_type) > 0
			? strpprintf(0, "%s%s: %s", ZSTR_VAL(*label), ZSTR_VAL(args), ZSTR_VAL(return_type))
			: strpprintf(0, "%s%s", ZSTR_VAL(*label), ZSTR_VAL(args))
		;

		zend_string_release(return_type);
	} else {
		*detail = strpprintf(0, "%s%s", ZSTR_VAL(*label), ZSTR_VAL(args));
	}

	zend_string_release(args);

	return true;
}

static inline bool lsp_phpdoc_property_parse_line(const char *tag, const char *line_end, zend_string **label, zend_string **detail, bool *is_static)
{
	const char *p, *q, *type_start, *type_end, *name_start, *name_end;
	zend_string *type;
	size_t static_length;

	*label = NULL;
	*detail = NULL;
	*is_static = false;

	while (line_end > tag && isspace((unsigned char) line_end[-1])) {
		line_end--;
	}

	if (line_end - tag >= 2 && line_end[-2] == '*' && line_end[-1] == '/') {
		line_end -= 2;
		while (line_end > tag && isspace((unsigned char) line_end[-1])) {
			line_end--;
		}
	}

	p = tag;
	while (p < line_end && isspace((unsigned char) *p)) {
		p++;
	}

	static_length = sizeof("static") - 1;
	if (p + static_length <= line_end &&
		strncasecmp(p, "static", static_length) == 0 &&
		(p + static_length == line_end || isspace((unsigned char) p[static_length]))
	) {
		*is_static = true;
		p += static_length;

		while (p < line_end && isspace((unsigned char) *p)) {
			p++;
		}
	}

	type_start = p;
	name_start = NULL;
	name_end = NULL;
	for (q = p; q < line_end; q++) {
		if (*q != '$' || q + 1 >= line_end || !lsp_doc_is_identifier_start(q[1])) {
			continue;
		}

		name_start = q + 1;
		name_end = name_start + 1;
		while (name_end < line_end && lsp_doc_is_identifier_char(*name_end)) {
			name_end++;
		}

		type_end = q;
		while (type_end > type_start && isspace((unsigned char) type_end[-1])) {
			type_end--;
		}

		*label = zend_string_init(name_start, name_end - name_start, 0);
		type = lsp_phpdoc_normalized_signature_slice(type_start, type_end - type_start);
		*detail = ZSTR_LEN(type) > 0
			? strpprintf(0, "property %s $%s", ZSTR_VAL(type), ZSTR_VAL(*label))
			: strpprintf(0, "property $%s", ZSTR_VAL(*label))
		;

		zend_string_release(type);

		return true;
	}

	return false;
}

static inline void lsp_phpdoc_add_cached_method(zval *methods, zend_string *label, zend_string *detail, bool is_static)
{
	zval method;

	array_init(&method);
	add_assoc_str(&method, "label", zend_string_copy(label));
	add_assoc_str(&method, "detail", zend_string_copy(detail));
	add_assoc_bool(&method, "static", is_static);
	add_assoc_long(&method, "visibility", (zend_long) LSP_METHOD_VISIBILITY_PUBLIC);
	add_next_index_zval(methods, &method);
}

static inline void lsp_phpdoc_add_cached_property(zval *properties, zend_string *label, zend_string *detail, bool is_static)
{
	zval property;

	array_init(&property);
	add_assoc_str(&property, "label", zend_string_copy(label));
	add_assoc_str(&property, "detail", zend_string_copy(detail));
	add_assoc_bool(&property, "static", is_static);
	add_assoc_long(&property, "visibility", (zend_long) LSP_METHOD_VISIBILITY_PUBLIC);
	add_next_index_zval(properties, &property);
}

static inline zend_string *lsp_phpdoc_template_name_from_line(const char *line_start, const char *line_end)
{
	const char *p, *tag, *name_start, *name_end;
	size_t tag_length;

	p = line_start;
	while (p < line_end && isspace((unsigned char) *p)) {
		p++;
	}

	if (p + sizeof("/**") - 1 <= line_end && memcmp(p, "/**", sizeof("/**") - 1) == 0) {
		p += sizeof("/**") - 1;
	}

	while (p < line_end && isspace((unsigned char) *p)) {
		p++;
	}

	if (p < line_end && *p == '*') {
		p++;
		if (p < line_end && *p == '/') {
			return NULL;
		}

		while (p < line_end && isspace((unsigned char) *p)) {
			p++;
		}
	}

	tag_length = sizeof("@template") - 1;
	if (p + tag_length > line_end || memcmp(p, "@template", tag_length) != 0) {
		return NULL;
	}

	tag = p + tag_length;
	if (tag < line_end && *tag == '-') {
		tag++;
		while (tag < line_end && (islower((unsigned char) *tag) || *tag == '-')) {
			tag++;
		}
	}

	if (tag < line_end && !isspace((unsigned char) *tag)) {
		return NULL;
	}

	p = tag;
	while (p < line_end && isspace((unsigned char) *p)) {
		p++;
	}

	if (p >= line_end || !lsp_doc_is_identifier_start(*p)) {
		return NULL;
	}

	name_start = p++;
	while (p < line_end && lsp_doc_is_identifier_char(*p)) {
		p++;
	}
	name_end = p;

	return zend_string_init(name_start, name_end - name_start, 0);
}

extern void lsp_phpdoc_cache_class_methods(zval *methods, zend_string *contents, size_t class_start)
{
	const char *value = ZSTR_VAL(contents), *doc_end_ptr, *line, *line_end, *tag;
	zend_string *label, *detail;
	size_t doc_start, doc_end;
	bool is_static;

	if (!lsp_phpdoc_class_docblock_bounds(contents, class_start, &doc_start, &doc_end)) {
		return;
	}

	line = value + doc_start;
	doc_end_ptr = value + doc_end;
	while (line < doc_end_ptr) {
		line_end = line;
		while (line_end < doc_end_ptr && *line_end != '\n' && *line_end != '\r') {
			line_end++;
		}

		tag = lsp_phpdoc_method_tag_in_line(line, line_end);
		if (tag && lsp_phpdoc_method_parse_line(tag, line_end, &label, &detail, &is_static)) {
			if (!zend_string_equals_literal(label, "__construct")) {
				lsp_phpdoc_add_cached_method(methods, label, detail, is_static);
			}

			zend_string_release(label);
			zend_string_release(detail);
		}

		while (line_end < doc_end_ptr && (*line_end == '\n' || *line_end == '\r')) {
			line_end++;
		}

		line = line_end;
	}
}

extern void lsp_phpdoc_cache_class_properties(zval *properties, zend_string *contents, size_t class_start)
{
	const char *value = ZSTR_VAL(contents), *doc_end_ptr, *line, *line_end, *tag;
	zend_string *label, *detail;
	size_t doc_start, doc_end;
	bool is_static;

	if (!lsp_phpdoc_class_docblock_bounds(contents, class_start, &doc_start, &doc_end)) {
		return;
	}

	line = value + doc_start;
	doc_end_ptr = value + doc_end;
	while (line < doc_end_ptr) {
		line_end = line;
		while (line_end < doc_end_ptr && *line_end != '\n' && *line_end != '\r') {
			line_end++;
		}

		tag = lsp_phpdoc_property_tag_in_line(line, line_end);
		if (tag && lsp_phpdoc_property_parse_line(tag, line_end, &label, &detail, &is_static)) {
			lsp_phpdoc_add_cached_property(properties, label, detail, is_static);
			zend_string_release(label);
			zend_string_release(detail);
		}

		while (line_end < doc_end_ptr && (*line_end == '\n' || *line_end == '\r')) {
			line_end++;
		}

		line = line_end;
	}
}

extern zend_string *lsp_phpdoc_template_type_at(zend_string *text, uint32_t target_index)
{
	const char *value = ZSTR_VAL(text), *doc_end_ptr, *line, *line_end;
	zend_string *name;
	size_t class_start, doc_start, doc_end;
	uint32_t index;

	if (!lsp_phpdoc_first_class_start(text, &class_start) ||
		!lsp_phpdoc_class_docblock_bounds(text, class_start, &doc_start, &doc_end)
	) {
		return NULL;
	}

	line = value + doc_start;
	doc_end_ptr = value + doc_end;
	index = 0;
	while (line < doc_end_ptr) {
		line_end = line;
		while (line_end < doc_end_ptr && *line_end != '\n' && *line_end != '\r') {
			line_end++;
		}

		name = lsp_phpdoc_template_name_from_line(line, line_end);
		if (name) {
			if (index == target_index) {
				return name;
			}
			zend_string_release(name);
			index++;
		}

		while (line_end < doc_end_ptr && (*line_end == '\n' || *line_end == '\r')) {
			line_end++;
		}

		line = line_end;
	}

	return NULL;
}

extern zend_string *lsp_phpdoc_trait_use_generic_argument(zend_string *text, zend_string *trait_name, uint32_t target_index)
{
	const char *value = ZSTR_VAL(text), *end = value + ZSTR_LEN(text), *line, *line_end, *tag;
	zend_string *use_type, *base_type, *resolved_trait, *argument, *resolved_argument;

	line = value;
	while (line < end) {
		line_end = line;
		while (line_end < end && *line_end != '\n' && *line_end != '\r') {
			line_end++;
		}

		tag = lsp_phpdoc_use_tag_in_line(line, line_end);
		if (tag) {
			use_type = lsp_phpdoc_use_type_from_line(tag, line_end);
			if (use_type) {
				base_type = lsp_phpdoc_generic_base_type(use_type);
				resolved_trait = lsp_resolve_class_name(text, base_type);
				if (resolved_trait && lsp_type_equals(resolved_trait, trait_name)) {
					argument = lsp_type_generic_argument(use_type, target_index);
					if (!argument && target_index > 0) {
						argument = lsp_type_generic_argument(use_type, 0);
					}
					if (argument) {
						resolved_argument = lsp_resolve_class_name(text, argument);
						zend_string_release(argument);
						zend_string_release(resolved_trait);
						zend_string_release(base_type);
						zend_string_release(use_type);

						return resolved_argument;
					}
				}
				if (resolved_trait) {
					zend_string_release(resolved_trait);
				}
				zend_string_release(base_type);
				zend_string_release(use_type);
			}
		}

		while (line_end < end && (*line_end == '\n' || *line_end == '\r')) {
			line_end++;
		}

		line = line_end;
	}

	return NULL;
}
