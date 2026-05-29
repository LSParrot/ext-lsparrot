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

#if defined(_WIN32)
static LONG lsp_process_windows_pipe_counter = 0;
#else
extern char **environ;
#endif

static inline void lsp_process_append_shell_quoted(smart_str *line, const char *value)
{
	const char *p;

#if defined(_WIN32)
	size_t backslashes;

	smart_str_appendc(line, '"');
	backslashes = 0;
	for (p = value; *p; p++) {
		if (*p == '\\') {
			backslashes++;
			continue;
		}
		if (*p == '"') {
			while (backslashes > 0) {
				smart_str_appendl(line, "\\\\", sizeof("\\\\") - 1);
				backslashes--;
			}
			smart_str_appendl(line, "\\\"", sizeof("\\\"") - 1);
			continue;
		}
		while (backslashes > 0) {
			smart_str_appendc(line, '\\');
			backslashes--;
		}
		smart_str_appendc(line, *p);
	}
	while (backslashes > 0) {
		smart_str_appendl(line, "\\\\", sizeof("\\\\") - 1);
		backslashes--;
	}
	smart_str_appendc(line, '"');
#else
	smart_str_appendc(line, '\'');
	for (p = value; *p; p++) {
		if (*p == '\'') {
			smart_str_appendl(line, "'\\''", sizeof("'\\''") - 1);
		} else {
			smart_str_appendc(line, *p);
		}
	}
	smart_str_appendc(line, '\'');
#endif
}

static inline zend_string *lsp_process_command_line(lsp_command *command)
{
	smart_str line = {0};
	uint32_t i;

	for (i = 0; i < command->count; i++) {
		if (i > 0) {
			smart_str_appendc(&line, ' ');
		}

		lsp_process_append_shell_quoted(&line, command->argv[i]);
	}

	smart_str_0(&line);

	return line.s ? line.s : zend_empty_string;
}

#if !defined(_WIN32)
static inline zend_string *lsp_process_shell_line(lsp_command *command, zend_string *cwd)
{
	smart_str line = {0};
	uint32_t i;

	if (cwd && ZSTR_LEN(cwd) > 0) {
		smart_str_appendl(&line, "cd ", sizeof("cd ") - 1);
		lsp_process_append_shell_quoted(&line, ZSTR_VAL(cwd));
		smart_str_appendl(&line, " && exec ", sizeof(" && exec ") - 1);
	} else {
		smart_str_appendl(&line, "exec ", sizeof("exec ") - 1);
	}

	for (i = 0; i < command->count; i++) {
		if (i > 0) {
			smart_str_appendc(&line, ' ');
		}

		lsp_process_append_shell_quoted(&line, command->argv[i]);
	}

	smart_str_0(&line);

	return line.s ? line.s : zend_empty_string;
}

static inline bool lsp_process_pipe_pair(lsp_pipe_handle *read_pipe, lsp_pipe_handle *write_pipe)
{
	int fds[2];

	if (pipe(fds) != 0) {
		*read_pipe = LSP_INVALID_PIPE_HANDLE;
		*write_pipe = LSP_INVALID_PIPE_HANDLE;

		return false;
	}

	*read_pipe = (lsp_pipe_handle) fds[0];
	*write_pipe = (lsp_pipe_handle) fds[1];

	return true;
}

static inline bool lsp_process_set_nonblock(lsp_pipe_handle pipe)
{
	int flags;

	if (!lsp_pipe_handle_valid(pipe)) {
		return false;
	}

	flags = fcntl((int) pipe, F_GETFL, 0);
	if (flags < 0) {
		return false;
	}

	return fcntl((int) pipe, F_SETFL, flags | O_NONBLOCK) == 0;
}

static inline bool lsp_process_spawn_shell_with_actions(zend_string *line, posix_spawn_file_actions_t *actions, lsp_process_id *process)
{
	char *argv[4];
	int result;

	argv[0] = (char *) "sh";
	argv[1] = (char *) "-c";
	argv[2] = ZSTR_VAL(line);
	argv[3] = NULL;
	result = posix_spawnp(process, "sh", actions, NULL, argv, environ);

	return result == 0;
}
#endif

#if defined(_WIN32)
static inline void lsp_process_windows_add_handle(HANDLE *handles, DWORD *handle_count, HANDLE handle)
{
	DWORD i;

	if (!handle || handle == INVALID_HANDLE_VALUE) {
		return;
	}

	for (i = 0; i < *handle_count; i++) {
		if (handles[i] == handle) {
			return;
		}
	}

	handles[*handle_count] = handle;
	(*handle_count)++;
}

static inline bool lsp_process_windows_start(lsp_command *command, zend_string *cwd, HANDLE input, HANDLE output, HANDLE error, lsp_process_id *process)
{
	STARTUPINFOEXA startup;
	PROCESS_INFORMATION info;
	zend_string *line;
	char *command_line;
	HANDLE handles[3], stdin_handle, stdout_handle, stderr_handle;
	SIZE_T attribute_size;
	DWORD creation_flags, handle_count;
	BOOL attributes_initialized, ok;

	line = lsp_process_command_line(command);
	if (line == zend_empty_string) {
		return false;
	}

	command_line = estrndup(ZSTR_VAL(line), ZSTR_LEN(line));
	memset(&startup, 0, sizeof(startup));
	memset(&info, 0, sizeof(info));
	startup.StartupInfo.cb = sizeof(startup);
	startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	stdin_handle = input ? input : GetStdHandle(STD_INPUT_HANDLE);
	stdout_handle = output ? output : GetStdHandle(STD_OUTPUT_HANDLE);
	stderr_handle = error ? error : GetStdHandle(STD_ERROR_HANDLE);
	startup.StartupInfo.hStdInput = stdin_handle;
	startup.StartupInfo.hStdOutput = stdout_handle;
	startup.StartupInfo.hStdError = stderr_handle;
	handles[0] = NULL;
	handles[1] = NULL;
	handles[2] = NULL;
	handle_count = 0;
	lsp_process_windows_add_handle(handles, &handle_count, stdin_handle);
	lsp_process_windows_add_handle(handles, &handle_count, stdout_handle);
	lsp_process_windows_add_handle(handles, &handle_count, stderr_handle);
	creation_flags = CREATE_NO_WINDOW;
	attribute_size = 0;
	InitializeProcThreadAttributeList(NULL, 1, 0, &attribute_size);
	attributes_initialized = FALSE;
	if (attribute_size > 0 && handle_count > 0) {
		startup.lpAttributeList = emalloc(attribute_size);
		attributes_initialized = InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attribute_size);
		if (attributes_initialized &&
			UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(HANDLE) * handle_count, NULL, NULL)
		) {
			creation_flags |= EXTENDED_STARTUPINFO_PRESENT;
		} else {
			if (attributes_initialized) {
				DeleteProcThreadAttributeList(startup.lpAttributeList);
			}
			efree(startup.lpAttributeList);
			startup.lpAttributeList = NULL;
		}
	}
	ok = CreateProcessA(NULL, command_line, NULL, NULL, TRUE, creation_flags, NULL,
		cwd && ZSTR_LEN(cwd) > 0 ? ZSTR_VAL(cwd) : NULL,
		&startup.StartupInfo, &info
	);
	if (startup.lpAttributeList) {
		DeleteProcThreadAttributeList(startup.lpAttributeList);
		efree(startup.lpAttributeList);
	}
	efree(command_line);
	zend_string_release(line);
	if (!ok) {
		return false;
	}

	CloseHandle(info.hThread);
	*process = (lsp_process_id) info.hProcess;

	return true;
}

static inline bool lsp_process_windows_create_pipe(HANDLE *read_pipe, HANDLE *write_pipe)
{
	SECURITY_ATTRIBUTES attributes;

	memset(&attributes, 0, sizeof(attributes));
	attributes.nLength = sizeof(attributes);
	attributes.bInheritHandle = TRUE;
	attributes.lpSecurityDescriptor = NULL;

	return CreatePipe(read_pipe, write_pipe, &attributes, 0) != 0;
}

static inline bool lsp_process_windows_create_input_pipe(HANDLE *read_pipe, HANDLE *write_pipe)
{
	SECURITY_ATTRIBUTES attributes;
	HANDLE input_read, input_write;
	LONG counter;
	char name[128];
	BOOL connected;

	memset(&attributes, 0, sizeof(attributes));
	attributes.nLength = sizeof(attributes);
	attributes.bInheritHandle = TRUE;
	attributes.lpSecurityDescriptor = NULL;
	counter = InterlockedIncrement(&lsp_process_windows_pipe_counter);
	snprintf(name, sizeof(name), "\\\\.\\pipe\\lsparrot-%lu-%lu-%ld", (unsigned long) GetCurrentProcessId(), (unsigned long) GetTickCount(), (long) counter);

	input_read = CreateNamedPipeA(
		name,
		PIPE_ACCESS_INBOUND,
		PIPE_TYPE_BYTE | PIPE_WAIT,
		1,
		65536,
		65536,
		0,
		&attributes
	);
	if (input_read == INVALID_HANDLE_VALUE) {
		*read_pipe = NULL;
		*write_pipe = NULL;

		return false;
	}

	input_write = CreateFileA(
		name,
		GENERIC_WRITE,
		0,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
		NULL
	);
	if (input_write == INVALID_HANDLE_VALUE) {
		CloseHandle(input_read);
		*read_pipe = NULL;
		*write_pipe = NULL;

		return false;
	}

	connected = ConnectNamedPipe(input_read, NULL) ? TRUE : GetLastError() == ERROR_PIPE_CONNECTED;
	if (!connected) {
		CloseHandle(input_read);
		CloseHandle(input_write);
		*read_pipe = NULL;
		*write_pipe = NULL;

		return false;
	}

	*read_pipe = input_read;
	*write_pipe = input_write;

	return true;
}
#endif

#if defined(_WIN32)
static inline bool lsp_process_windows_write_all_timeout(lsp_pipe_handle pipe, const char *data, size_t length, double timeout)
{
	OVERLAPPED overlapped;
	HANDLE event;
	DWORD n, error, wait_ms, wait_result;
	size_t written, chunk;
	double deadline, now, remaining;
	bool result;

	event = CreateEventA(NULL, TRUE, FALSE, NULL);
	if (!event) {
		return false;
	}

	written = 0;
	result = true;
	deadline = lsp_now_seconds() + (timeout > 0.0 ? timeout : 1.0);
	while (written < length) {
		now = lsp_now_seconds();
		if (now >= deadline) {
			result = false;
			break;
		}

		remaining = deadline - now;
		wait_ms = (DWORD) (remaining * 1000.0);
		if (wait_ms == 0) {
			wait_ms = 1;
		}

		chunk = length - written;
		if (chunk > 65536u) {
			chunk = 65536u;
		}

		memset(&overlapped, 0, sizeof(overlapped));
		ResetEvent(event);
		overlapped.hEvent = event;
		n = 0;
		if (!WriteFile((HANDLE) pipe, data + written, (DWORD) chunk, &n, &overlapped)) {
			error = GetLastError();
			if (error != ERROR_IO_PENDING) {
				result = false;
				break;
			}

			wait_result = WaitForSingleObject(event, wait_ms);
			if (wait_result != WAIT_OBJECT_0) {
				CancelIo((HANDLE) pipe);
				GetOverlappedResult((HANDLE) pipe, &overlapped, &n, TRUE);
				result = false;
				break;
			}

			n = 0;
			if (!GetOverlappedResult((HANDLE) pipe, &overlapped, &n, FALSE)) {
				result = false;
				break;
			}
		}

		if (n == 0) {
			result = false;
			break;
		}

		written += (size_t) n;
	}

	CloseHandle(event);

	return result;
}
#endif

#if !defined(_WIN32)
static inline bool lsp_process_wait_pipe_writable(lsp_pipe_handle pipe, double deadline)
{
	fd_set write_fds;
	struct timeval timeout;
	double now, remaining;
	int fd, result;

	fd = (int) pipe;
	for (;;) {
		now = lsp_now_seconds();
		if (now >= deadline) {
			return false;
		}

		remaining = deadline - now;
		timeout.tv_sec = (long) remaining;
		timeout.tv_usec = (long) ((remaining - (double) timeout.tv_sec) * 1000000.0);
		if (timeout.tv_sec == 0 && timeout.tv_usec <= 0) {
			timeout.tv_usec = 1000;
		}

		FD_ZERO(&write_fds);
		FD_SET(fd, &write_fds);
		result = select(fd + 1, NULL, &write_fds, NULL, &timeout);
		if (result > 0) {
			return true;
		}
		if (result == 0) {
			return false;
		}
		if (errno != EINTR) {
			return false;
		}
	}
}
#endif

extern double lsp_now_seconds(void)
{
#if defined(_WIN32)
	ULONGLONG ticks;

	ticks = GetTickCount64();

	return (double) ticks / 1000.0;
#elif LSP_HAVE_POSIX_PROCESS
	struct timeval tv;

	gettimeofday(&tv, NULL);

	return (double) tv.tv_sec + ((double) tv.tv_usec / 1000000.0);
#else
	return (double) time(NULL);
#endif
}

extern void lsp_pipe_close(lsp_pipe_handle *pipe)
{
	if (!pipe || !lsp_pipe_handle_valid(*pipe)) {
		return;
	}

#if defined(_WIN32)
	CloseHandle((HANDLE) *pipe);
#else
	close((int) *pipe);
#endif
	*pipe = LSP_INVALID_PIPE_HANDLE;
}

extern bool lsp_pipe_write_all_timeout(lsp_pipe_handle pipe, const char *data, size_t length, double timeout)
{
#if defined(_WIN32)
	return lsp_process_windows_write_all_timeout(pipe, data, length, timeout);
#else
	size_t written;
	ssize_t n;
	double deadline;

	written = 0;
	deadline = lsp_now_seconds() + (timeout > 0.0 ? timeout : 1.0);
	while (written < length) {
		n = write((int) pipe, data + written, length - written);
		if (n > 0) {
			written += (size_t) n;
			continue;
		}
		if (n == 0) {
			if (!lsp_process_wait_pipe_writable(pipe, deadline)) {
				return false;
			}
			continue;
		}
		if (errno == EINTR) {
			continue;
		}
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			if (!lsp_process_wait_pipe_writable(pipe, deadline)) {
				return false;
			}
			continue;
		}

		return false;
	}

	return true;
#endif
}

extern bool lsp_pipe_read_available(lsp_pipe_handle pipe, smart_str *output, bool *closed)
{
	char buffer[4096];
#if defined(_WIN32)
	DWORD available, n, wanted;
#else
	ssize_t n;
#endif

	*closed = false;
	if (!lsp_pipe_handle_valid(pipe)) {
		*closed = true;

		return true;
	}

#if defined(_WIN32)
	for (;;) {
		available = 0;
		if (!PeekNamedPipe((HANDLE) pipe, NULL, 0, NULL, &available, NULL)) {
			*closed = true;

			return true;
		}
		if (available == 0) {
			return true;
		}

		wanted = available < sizeof(buffer) ? available : (DWORD) sizeof(buffer);
		n = 0;
		if (!ReadFile((HANDLE) pipe, buffer, wanted, &n, NULL)) {
			*closed = GetLastError() == ERROR_BROKEN_PIPE;

			return *closed;
		}
		if (n == 0) {
			*closed = true;

			return true;
		}
		smart_str_appendl(output, buffer, (size_t) n);
	}
#else
	for (;;) {
		n = read((int) pipe, buffer, sizeof(buffer));
		if (n > 0) {
			smart_str_appendl(output, buffer, (size_t) n);
			continue;
		}
		if (n == 0) {
			*closed = true;

			return true;
		}
		if (errno == EINTR) {
			continue;
		}
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			return true;
		}

		*closed = true;

		return false;
	}
#endif
}

extern bool lsp_process_spawn_piped(lsp_command *command, zend_string *cwd, lsp_process_pipes *pipes)
{
#if defined(_WIN32)
	HANDLE child_input_read, child_input_write, child_output_read, child_output_write, child_error_read, child_error_write;
	bool ok;
#else
	posix_spawn_file_actions_t actions;
	zend_string *line;
	lsp_pipe_handle input_read, input_write, output_read, output_write, error_read, error_write;
	bool actions_initialized, ok;
#endif

	memset(pipes, 0, sizeof(*pipes));
	pipes->process = LSP_INVALID_PROCESS_ID;
	pipes->input = LSP_INVALID_PIPE_HANDLE;
	pipes->output = LSP_INVALID_PIPE_HANDLE;
	pipes->error = LSP_INVALID_PIPE_HANDLE;
	if (!command->argv || command->count == 0) {
		return false;
	}

#if defined(_WIN32)
	child_input_read = NULL;
	child_input_write = NULL;
	child_output_read = NULL;
	child_output_write = NULL;
	child_error_read = NULL;
	child_error_write = NULL;
	ok = lsp_process_windows_create_input_pipe(&child_input_read, &child_input_write) &&
		lsp_process_windows_create_pipe(&child_output_read, &child_output_write) &&
		lsp_process_windows_create_pipe(&child_error_read, &child_error_write)
	;
	if (!ok) {
		if (child_input_read) {
			CloseHandle(child_input_read);
		}
		if (child_input_write) {
			CloseHandle(child_input_write);
		}
		if (child_output_read) {
			CloseHandle(child_output_read);
		}
		if (child_output_write) {
			CloseHandle(child_output_write);
		}
		if (child_error_read) {
			CloseHandle(child_error_read);
		}
		if (child_error_write) {
			CloseHandle(child_error_write);
		}

		return false;
	}

	SetHandleInformation(child_input_write, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(child_output_read, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(child_error_read, HANDLE_FLAG_INHERIT, 0);
	if (!lsp_process_windows_start(command, cwd, child_input_read, child_output_write, child_error_write, &pipes->process)) {
		CloseHandle(child_input_read);
		CloseHandle(child_input_write);
		CloseHandle(child_output_read);
		CloseHandle(child_output_write);
		CloseHandle(child_error_read);
		CloseHandle(child_error_write);

		return false;
	}

	CloseHandle(child_input_read);
	CloseHandle(child_output_write);
	CloseHandle(child_error_write);
	pipes->input = (lsp_pipe_handle) child_input_write;
	pipes->output = (lsp_pipe_handle) child_output_read;
	pipes->error = (lsp_pipe_handle) child_error_read;

	return true;
#else
	actions_initialized = false;
	input_read = LSP_INVALID_PIPE_HANDLE;
	input_write = LSP_INVALID_PIPE_HANDLE;
	output_read = LSP_INVALID_PIPE_HANDLE;
	output_write = LSP_INVALID_PIPE_HANDLE;
	error_read = LSP_INVALID_PIPE_HANDLE;
	error_write = LSP_INVALID_PIPE_HANDLE;
	line = NULL;
	if (!lsp_process_pipe_pair(&input_read, &input_write) ||
		!lsp_process_pipe_pair(&output_read, &output_write) ||
		!lsp_process_pipe_pair(&error_read, &error_write)
	) {
		goto fail;
	}

	if (posix_spawn_file_actions_init(&actions) != 0) {
		goto fail;
	}
	actions_initialized = true;
	posix_spawn_file_actions_adddup2(&actions, (int) input_read, STDIN_FILENO);
	posix_spawn_file_actions_adddup2(&actions, (int) output_write, STDOUT_FILENO);
	posix_spawn_file_actions_adddup2(&actions, (int) error_write, STDERR_FILENO);
	posix_spawn_file_actions_addclose(&actions, (int) input_write);
	posix_spawn_file_actions_addclose(&actions, (int) output_read);
	posix_spawn_file_actions_addclose(&actions, (int) error_read);
	line = lsp_process_shell_line(command, cwd);
	if (line == zend_empty_string) {
		goto fail;
	}

	ok = lsp_process_spawn_shell_with_actions(line, &actions, &pipes->process);
	if (!ok) {
		goto fail;
	}

	posix_spawn_file_actions_destroy(&actions);
	zend_string_release(line);
	lsp_pipe_close(&input_read);
	lsp_pipe_close(&output_write);
	lsp_pipe_close(&error_write);
	lsp_process_set_nonblock(input_write);
	lsp_process_set_nonblock(output_read);
	lsp_process_set_nonblock(error_read);
	pipes->input = input_write;
	pipes->output = output_read;
	pipes->error = error_read;

	return true;

fail:
	if (actions_initialized) {
		posix_spawn_file_actions_destroy(&actions);
	}
	if (line && line != zend_empty_string) {
		zend_string_release(line);
	}
	lsp_pipe_close(&input_read);
	lsp_pipe_close(&input_write);
	lsp_pipe_close(&output_read);
	lsp_pipe_close(&output_write);
	lsp_pipe_close(&error_read);
	lsp_pipe_close(&error_write);
	pipes->process = LSP_INVALID_PROCESS_ID;

	return false;
#endif
}

extern lsp_process_id lsp_process_spawn_to_file(lsp_command *command, zend_string *cwd, zend_string *output_file)
{
#if defined(_WIN32)
	SECURITY_ATTRIBUTES attributes;
	HANDLE file, input;
	lsp_process_id process;
#else
	posix_spawn_file_actions_t actions;
	zend_string *line;
	lsp_process_id process;
	bool actions_initialized;
	int result;
#endif

	if (!command->argv || command->count == 0) {
		return LSP_INVALID_PROCESS_ID;
	}

#if defined(_WIN32)
	memset(&attributes, 0, sizeof(attributes));
	attributes.nLength = sizeof(attributes);
	attributes.bInheritHandle = TRUE;
	attributes.lpSecurityDescriptor = NULL;
	input = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ, &attributes, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (input == INVALID_HANDLE_VALUE) {
		return LSP_INVALID_PROCESS_ID;
	}
	file = CreateFileA(ZSTR_VAL(output_file), GENERIC_WRITE, FILE_SHARE_READ, &attributes, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE) {
		CloseHandle(input);

		return LSP_INVALID_PROCESS_ID;
	}

	process = LSP_INVALID_PROCESS_ID;
	if (!lsp_process_windows_start(command, cwd, input, file, file, &process)) {
		CloseHandle(input);
		CloseHandle(file);

		return LSP_INVALID_PROCESS_ID;
	}
	CloseHandle(input);
	CloseHandle(file);

	return process;
#else
	process = LSP_INVALID_PROCESS_ID;
	actions_initialized = false;
	line = lsp_process_shell_line(command, cwd);
	if (line == zend_empty_string) {
		return LSP_INVALID_PROCESS_ID;
	}
	if (posix_spawn_file_actions_init(&actions) != 0) {
		zend_string_release(line);

		return LSP_INVALID_PROCESS_ID;
	}
	actions_initialized = true;
	posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
	posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, ZSTR_VAL(output_file), O_WRONLY | O_CREAT | O_TRUNC, 0666);
	posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
	result = lsp_process_spawn_shell_with_actions(line, &actions, &process) ? 0 : -1;
	if (actions_initialized) {
		posix_spawn_file_actions_destroy(&actions);
	}
	zend_string_release(line);

	return result == 0 ? process : LSP_INVALID_PROCESS_ID;
#endif
}

extern zend_string *lsp_process_run_capture(lsp_command *command, zend_string *cwd, double timeout)
{
	smart_str output = {0};
	lsp_process_pipes pipes;
	bool output_closed, error_closed, exited;
	int status;
	double deadline;

	if (!lsp_process_spawn_piped(command, cwd, &pipes)) {
		return zend_empty_string;
	}

	lsp_pipe_close(&pipes.input);
	output_closed = false;
	error_closed = false;
	exited = false;
	deadline = lsp_now_seconds() + (timeout > 0.0 ? timeout : 30.0);
	while ((!output_closed || !error_closed) || !exited) {
		if (!output_closed) {
			lsp_pipe_read_available(pipes.output, &output, &output_closed);
		}
		if (!error_closed) {
			lsp_pipe_read_available(pipes.error, &output, &error_closed);
		}
		if (!exited && lsp_process_wait_nonblocking(pipes.process, &status)) {
			exited = true;
		}
		if ((output_closed && error_closed) && exited) {
			break;
		}
		if (lsp_now_seconds() >= deadline) {
			lsp_process_terminate(pipes.process);
			lsp_process_wait(pipes.process, &status);
			break;
		}
		lsp_sleep_milliseconds(10);
	}

	lsp_pipe_close(&pipes.output);
	lsp_pipe_close(&pipes.error);
	lsp_process_close(pipes.process);
	if (!output.s) {
		return zend_empty_string;
	}

	smart_str_0(&output);

	return output.s;
}

extern bool lsp_process_wait_nonblocking(lsp_process_id process, int *status)
{
#if defined(_WIN32)
	DWORD result, exit_code;
#else
	lsp_process_id waited;
	int local_status;
#endif

	if (!lsp_process_id_valid(process)) {
		return true;
	}

#if defined(_WIN32)
	result = WaitForSingleObject((HANDLE) process, 0);
	if (result != WAIT_OBJECT_0) {
		return false;
	}
	exit_code = 0;
	GetExitCodeProcess((HANDLE) process, &exit_code);
	if (status) {
		*status = (int) exit_code;
	}

	return true;
#else
	local_status = 0;
	waited = waitpid(process, &local_status, WNOHANG);
	if (waited == 0) {
		return false;
	}
	if (status) {
		*status = local_status;
	}

	return waited == process || waited < 0;
#endif
}

extern bool lsp_process_wait_timeout(lsp_process_id process, int *status, double timeout)
{
	double deadline;

	if (!lsp_process_id_valid(process)) {
		return true;
	}

	deadline = lsp_now_seconds() + (timeout > 0.0 ? timeout : 0.0);
	for (;;) {
		if (lsp_process_wait_nonblocking(process, status)) {
			return true;
		}
		if (timeout <= 0.0 || lsp_now_seconds() >= deadline) {
			return false;
		}
		lsp_sleep_milliseconds(10);
	}
}

extern void lsp_process_wait(lsp_process_id process, int *status)
{
#if defined(_WIN32)
	DWORD exit_code;
#else
	int local_status;
#endif

	if (!lsp_process_id_valid(process)) {
		return;
	}

#if defined(_WIN32)
	WaitForSingleObject((HANDLE) process, INFINITE);
	exit_code = 0;
	GetExitCodeProcess((HANDLE) process, &exit_code);
	if (status) {
		*status = (int) exit_code;
	}
#else
	local_status = 0;
	waitpid(process, &local_status, 0);
	if (status) {
		*status = local_status;
	}
#endif
}

extern void lsp_process_terminate(lsp_process_id process)
{
	if (!lsp_process_id_valid(process)) {
		return;
	}

#if defined(_WIN32)
	TerminateProcess((HANDLE) process, 1);
#else
	kill(process, SIGTERM);
#endif
}

extern void lsp_process_terminate_force(lsp_process_id process)
{
	if (!lsp_process_id_valid(process)) {
		return;
	}

#if defined(_WIN32)
	TerminateProcess((HANDLE) process, 1);
#else
	kill(process, SIGKILL);
#endif
}

extern void lsp_process_close(lsp_process_id process)
{
	if (!lsp_process_id_valid(process)) {
		return;
	}

#if defined(_WIN32)
	CloseHandle((HANDLE) process);
#else
	(void) process;
#endif
}
