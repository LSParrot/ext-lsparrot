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

#ifndef LSP_PORTABILITY_H
# define LSP_PORTABILITY_H

# ifdef HAVE_CONFIG_H
#  include <config.h>
# endif

# include <php.h>

# include <ctype.h>
# include <errno.h>
# include <stdio.h>
# include <stdint.h>
# include <stdbool.h>
# include <stdlib.h>
# include <string.h>
# include <sys/stat.h>
# include <time.h>

# if defined(_WIN32)
#  include <fcntl.h>
#  include <io.h>
#  include <process.h>
#  include <windows.h>
typedef intptr_t lsp_process_id;
typedef intptr_t lsp_pipe_handle;
#  define LSP_HAVE_POSIX_PROCESS 0
#  define LSP_INVALID_PROCESS_ID ((lsp_process_id) 0)
#  define LSP_INVALID_PIPE_HANDLE ((lsp_pipe_handle) 0)
#  define strcasecmp _stricmp
#  define strncasecmp _strnicmp
# else
#  include <fcntl.h>
#  include <signal.h>
#  include <spawn.h>
#  include <strings.h>
#  include <sys/select.h>
#  include <sys/time.h>
#  include <sys/wait.h>
#  include <unistd.h>
typedef pid_t lsp_process_id;
typedef intptr_t lsp_pipe_handle;
#  define LSP_HAVE_POSIX_PROCESS 1
#  define LSP_INVALID_PROCESS_ID ((lsp_process_id) 0)
#  define LSP_INVALID_PIPE_HANDLE ((lsp_pipe_handle) -1)
# endif

# include <Zend/zend_virtual_cwd.h>

static inline bool lsp_is_path_separator(char c)
{
#if defined(_WIN32)
	return c == '/' || c == '\\';
#else
	return c == '/';
#endif
}

static inline size_t lsp_path_root_length(const char *value, size_t length)
{
#if defined(_WIN32)
	size_t server_end, share_end;

	if (length >= 3 && isalpha((unsigned char) value[0]) && value[1] == ':' && lsp_is_path_separator(value[2])) {
		return 3;
	}
	if (length >= 2 && isalpha((unsigned char) value[0]) && value[1] == ':') {
		return 2;
	}
	if (length >= 2 && lsp_is_path_separator(value[0]) && lsp_is_path_separator(value[1])) {
		server_end = 2;
		while (server_end < length && !lsp_is_path_separator(value[server_end])) {
			server_end++;
		}
		if (server_end >= length) {
			return length;
		}

		share_end = server_end + 1;
		while (share_end < length && !lsp_is_path_separator(value[share_end])) {
			share_end++;
		}

		return share_end < length ? share_end + 1 : share_end;
	}
#endif

	return length > 0 && lsp_is_path_separator(value[0]) ? 1 : 0;
}

static inline bool lsp_path_is_absolute(zend_string *path)
{
	const char *value;
	size_t length;

	value = ZSTR_VAL(path);
	length = ZSTR_LEN(path);

#if defined(_WIN32)
	if (length >= 3 && isalpha((unsigned char) value[0]) && value[1] == ':' && lsp_is_path_separator(value[2])) {
		return true;
	}
	if (length >= 2 && lsp_is_path_separator(value[0]) && lsp_is_path_separator(value[1])) {
		return true;
	}
#endif

	return length > 0 && lsp_is_path_separator(value[0]);
}

static inline bool lsp_path_has_trailing_separator(zend_string *path)
{
	return ZSTR_LEN(path) > 0 && lsp_is_path_separator(ZSTR_VAL(path)[ZSTR_LEN(path) - 1]);
}

static inline char *lsp_last_path_separator(char *path)
{
	char *forward;
#if defined(_WIN32)
	char *backward;
#endif

	forward = strrchr(path, '/');
#if defined(_WIN32)
	backward = strrchr(path, '\\');
	if (forward && backward) {
		return forward > backward ? forward : backward;
	}
	if (backward) {
		return backward;
	}
#endif

	return forward;
}

static inline const char *lsp_last_path_separator_const(const char *path)
{
	const char *forward;
#if defined(_WIN32)
	const char *backward;
#endif

	forward = strrchr(path, '/');
#if defined(_WIN32)
	backward = strrchr(path, '\\');
	if (forward && backward) {
		return forward > backward ? forward : backward;
	}
	if (backward) {
		return backward;
	}
#endif

	return forward;
}

static inline const char *lsp_path_basename(zend_string *path)
{
	const char *slash;

	slash = lsp_last_path_separator_const(ZSTR_VAL(path));

	return slash ? slash + 1 : ZSTR_VAL(path);
}

static inline int lsp_path_compare(const char *left, const char *right, size_t length)
{
#if defined(_WIN32)
	return strncasecmp(left, right, length);
#else
	return strncmp(left, right, length);
#endif
}

static inline bool lsp_path_value_contains_segment(const char *path, size_t path_length, const char *segment)
{
	size_t segment_length, i;
	bool start_ok, end_ok;

	segment_length = strlen(segment);
	if (segment_length == 0 || path_length < segment_length) {
		return false;
	}

	for (i = 0; i + segment_length <= path_length; i++) {
		start_ok = i == 0 || lsp_is_path_separator(path[i - 1]);
		end_ok = i + segment_length == path_length || lsp_is_path_separator(path[i + segment_length]);
		if (start_ok && end_ok && lsp_path_compare(path + i, segment, segment_length) == 0) {
			return true;
		}
	}

	return false;
}

static inline bool lsp_path_is_same_or_under(zend_string *path, zend_string *root)
{
	size_t root_length, root_min_length;

	root_length = ZSTR_LEN(root);
	root_min_length = lsp_path_root_length(ZSTR_VAL(root), root_length);
	while (root_length > root_min_length && lsp_is_path_separator(ZSTR_VAL(root)[root_length - 1])) {
		root_length--;
	}

	if (root_length == 0 || ZSTR_LEN(path) < root_length) {
		return false;
	}

	if (root_length == 1 && lsp_is_path_separator(ZSTR_VAL(root)[0])) {
		return ZSTR_LEN(path) > 0 && lsp_is_path_separator(ZSTR_VAL(path)[0]);
	}

	if (lsp_path_compare(ZSTR_VAL(path), ZSTR_VAL(root), root_length) != 0) {
		return false;
	}

	if (root_length == root_min_length && root_length > 0 && lsp_is_path_separator(ZSTR_VAL(root)[root_length - 1])) {
		return true;
	}

	return ZSTR_LEN(path) == root_length || lsp_is_path_separator(ZSTR_VAL(path)[root_length]);
}

static inline char lsp_path_list_separator(void)
{
#if defined(_WIN32)
	return ';';
#else
	return ':';
#endif
}

static inline uint32_t lsp_executable_extension_count(void)
{
#if defined(_WIN32)
	return 4u;
#else
	return 1u;
#endif
}

static inline const char *lsp_executable_extension(uint32_t index)
{
#if defined(_WIN32)
	static const char *extensions[] = {"", ".bat", ".cmd", ".exe"};

	if (index < sizeof(extensions) / sizeof(extensions[0])) {
		return extensions[index];
	}
#else
	(void) index;
#endif

	return "";
}

static inline bool lsp_string_ends_with_ci(zend_string *value, const char *suffix)
{
	size_t suffix_length;

	suffix_length = strlen(suffix);
	if (ZSTR_LEN(value) < suffix_length) {
		return false;
	}

	return strncasecmp(ZSTR_VAL(value) + ZSTR_LEN(value) - suffix_length, suffix, suffix_length) == 0;
}

static inline bool lsp_path_is_windows_batch_file(zend_string *path)
{
#if defined(_WIN32)
	return lsp_string_ends_with_ci(path, ".bat") || lsp_string_ends_with_ci(path, ".cmd");
#else
	(void) path;

	return false;
#endif
}

static inline bool lsp_is_regular_file(zend_string *path)
{
	zend_stat_t st;

	return VCWD_STAT(ZSTR_VAL(path), &st) == 0 && S_ISREG(st.st_mode);
}

static inline bool lsp_path_is_executable_file(zend_string *path)
{
#if defined(_WIN32)
	return lsp_is_regular_file(path);
#else
	return lsp_is_regular_file(path) && access(ZSTR_VAL(path), X_OK) == 0;
#endif
}

static inline const char *lsp_php_binary(void)
{
	static char binary[MAXPATHLEN];

	if (binary[0] == '\0') {
#if defined(_WIN32)
		snprintf(binary, sizeof(binary), "%s/php.exe", PHP_BINDIR);
#else
		snprintf(binary, sizeof(binary), "%s/php", PHP_BINDIR);
#endif
	}

	return binary;
}

static inline void lsp_set_lsp_stdio_binary(void)
{
#if defined(_WIN32)
	_setmode(_fileno(stdin), _O_BINARY);
	_setmode(_fileno(stdout), _O_BINARY);
#endif
}

static inline long lsp_current_process_id(void)
{
#if defined(_WIN32)
	return (long) _getpid();
#else
	return (long) getpid();
#endif
}

static inline bool lsp_process_id_valid(lsp_process_id process)
{
	return process != LSP_INVALID_PROCESS_ID;
}

static inline bool lsp_pipe_handle_valid(lsp_pipe_handle pipe)
{
	return pipe != LSP_INVALID_PIPE_HANDLE;
}

static inline zend_long lsp_stat_mtime_nsec(zend_stat_t *st)
{
# if defined(__APPLE__)
	return (zend_long) st->st_mtimespec.tv_nsec;
# elif defined(__linux__)
	return (zend_long) st->st_mtim.tv_nsec;
# else
	return 0;
# endif
}

static inline void lsp_mkdir_p(zend_string *path)
{
	char *buffer, *p;
	size_t root_length;

	if (ZSTR_LEN(path) == 0) {
		return;
	}

	buffer = estrndup(ZSTR_VAL(path), ZSTR_LEN(path));
	root_length = lsp_path_root_length(buffer, ZSTR_LEN(path));
	p = buffer + (root_length > 0 ? root_length : 1);
	for (; *p; p++) {
		if (lsp_is_path_separator(*p)) {
			*p = '\0';
			VCWD_MKDIR(buffer, 0777);
			*p = ZSTR_VAL(path)[p - buffer];
		}
	}

	VCWD_MKDIR(buffer, 0777);
	efree(buffer);
}

static inline bool lsp_write_string_file(zend_string *path, zend_string *contents)
{
	FILE *handle;
	size_t written;

	handle = fopen(ZSTR_VAL(path), "wb");
	if (!handle) {
		return false;
	}

	written = fwrite(ZSTR_VAL(contents), 1, ZSTR_LEN(contents), handle);
	fclose(handle);

	return written == ZSTR_LEN(contents);
}

static inline void lsp_sleep_milliseconds(uint32_t milliseconds)
{
#if defined(_WIN32)
	Sleep((DWORD) milliseconds);
#else
	usleep((unsigned int) milliseconds * 1000u);
#endif
}

#endif  /* LSP_PORTABILITY_H */
