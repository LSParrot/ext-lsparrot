<?php

function lsparrot_test_php_configure_command(): string
{
    $info = '';
    $matches = [];

    ob_start();
    phpinfo(INFO_GENERAL);
    $info = (string) ob_get_clean();

    if (preg_match('/^Configure Command =>\s*(.*)$/m', $info, $matches) !== 1) {
        return '';
    }

    return $matches[1];
}

function lsparrot_test_php_configured_with_valgrind(): bool
{
    $command = '';

    $command = lsparrot_test_php_configure_command();
    return strpos($command, '--with-valgrind') !== false;
}

function lsparrot_test_run_tests_requested_valgrind(): bool
{
    $args = getenv('TEST_PHP_ARGS');
    $parts = [];
    $arg = '';
    $pid = 0;

    if (is_string($args) && $args !== '') {
        $parts = preg_split('/\s+/', trim($args)) ?: [];
        foreach ($parts as $arg) {
            if ($arg === '-m' || $arg === '-M') {
                return true;
            }
        }
    }

    if (PHP_OS_FAMILY !== 'Windows' && PHP_OS_FAMILY !== 'Darwin') {
        $pid = getmypid();
        while ($pid > 1) {
            $pid = lsparrot_test_linux_parent_pid($pid);
            if ($pid === null) {
                break;
            }

            if (lsparrot_test_linux_cmdline_has_valgrind_flag($pid)) {
                return true;
            }
        }
    }

    return false;
}

function lsparrot_test_valgrind_is_enabled(): bool
{
    if (!lsparrot_test_php_configured_with_valgrind()) {
        return false;
    }

    return lsparrot_test_run_tests_requested_valgrind();
}

function lsparrot_test_linux_parent_pid(int $pid): ?int
{
    $stat = '';
    $tail = '';
    $parts = [];
    $pos = 0;
    $ppid = 0;

    if (PHP_OS_FAMILY === 'Windows' || PHP_OS_FAMILY === 'Darwin') {
        return null;
    }

    $stat = @file_get_contents('/proc/' . $pid . '/stat');
    if (!is_string($stat) || $stat === '') {
        return null;
    }

    $pos = strrpos($stat, ') ');
    if ($pos === false) {
        return null;
    }

    $tail = substr($stat, $pos + 2);
    $parts = preg_split('/\s+/', trim($tail)) ?: [];
    if (!isset($parts[1]) || !ctype_digit($parts[1])) {
        return null;
    }

    $ppid = (int) $parts[1];
    return $ppid > 0 ? $ppid : null;
}

function lsparrot_test_linux_cmdline_has_valgrind_flag(int $pid): bool
{
    $cmdline = '';
    $parts = [];
    $part = '';
    $hasRunTests = false;

    if (PHP_OS_FAMILY === 'Windows' || PHP_OS_FAMILY === 'Darwin') {
        return false;
    }

    $cmdline = @file_get_contents('/proc/' . $pid . '/cmdline');
    if (!is_string($cmdline) || $cmdline === '') {
        return false;
    }

    $parts = array_values(array_filter(explode("\0", $cmdline), static function ($value): bool {
        return $value !== '';
    }));

    foreach ($parts as $part) {
        if (basename($part) === 'run-tests.php') {
            $hasRunTests = true;
            break;
        }
    }

    if (!$hasRunTests) {
        return false;
    }

    foreach ($parts as $part) {
        if ($part === '-m' || $part === '-M') {
            return true;
        }
    }

    return false;
}

function lsparrot_test_find_executable_on_path(string $name, string $path): ?string
{
    $directories = [];
    $directory = '';
    $candidate = '';

    if ($path === '') {
        return null;
    }

    $directories = explode(PATH_SEPARATOR, $path);
    foreach ($directories as $directory) {
        if ($directory === '') {
            continue;
        }

        $candidate = rtrim($directory, DIRECTORY_SEPARATOR) . DIRECTORY_SEPARATOR . $name;
        if (is_file($candidate) && is_executable($candidate)) {
            return $candidate;
        }
    }

    return null;
}

function lsparrot_test_valgrind_search_path(): string
{
    $ancestorPath = '';
    $path = '';

    $ancestorPath = lsparrot_test_linux_ancestor_path_with_executable('valgrind');
    if ($ancestorPath !== '') {
        return $ancestorPath;
    }

    $path = getenv('PATH');

    return is_string($path) ? $path : '';
}

function lsparrot_test_find_valgrind_executable(): ?string
{
    $candidates = [
        '/usr/bin/valgrind',
        '/usr/local/bin/valgrind',
        '/bin/valgrind',
    ];
    $candidate = '';
    $path = '';

    foreach ($candidates as $candidate) {
        if (is_file($candidate) && is_executable($candidate)) {
            return $candidate;
        }
    }

    $path = lsparrot_test_valgrind_search_path();

    return lsparrot_test_find_executable_on_path('valgrind', $path);
}

function lsparrot_test_linux_ancestor_path_with_executable(string $name): string
{
    $pid = 0;
    $path = '';

    if (PHP_OS_FAMILY === 'Windows' || PHP_OS_FAMILY === 'Darwin') {
        return '';
    }

    $pid = getmypid();
    while ($pid > 1) {
        $pid = lsparrot_test_linux_parent_pid($pid);
        if ($pid === null) {
            break;
        }

        $path = lsparrot_test_linux_environ_value($pid, 'PATH');
        if ($path !== '' && lsparrot_test_find_executable_on_path($name, $path) !== null) {
            return $path;
        }
    }

    return '';
}

function lsparrot_test_linux_environ_value(int $pid, string $name): string
{
    $environ = '';
    $entries = [];
    $entry = '';
    $prefix = '';

    if (PHP_OS_FAMILY === 'Windows' || PHP_OS_FAMILY === 'Darwin') {
        return '';
    }

    $environ = @file_get_contents('/proc/' . $pid . '/environ');
    if (!is_string($environ) || $environ === '') {
        return '';
    }

    $prefix = $name . '=';
    $entries = explode("\0", $environ);
    foreach ($entries as $entry) {
        if (strncmp($entry, $prefix, strlen($prefix)) === 0) {
            return substr($entry, strlen($prefix));
        }
    }

    return '';
}

function lsparrot_test_prepare_valgrind_for_empty_path(string $path): void
{
    $valgrind = null;

    @mkdir($path, 0777, true);

    if (!lsparrot_test_valgrind_is_enabled()) {
        return;
    }

    if (PHP_OS_FAMILY === 'Windows' || PHP_OS_FAMILY === 'Darwin') {
        echo "skip Valgrind is not supported on this platform\n";
        return;
    }

    $valgrind = lsparrot_test_find_valgrind_executable();
    if ($valgrind === null) {
        echo "skip valgrind executable is not available on PATH\n";

        return;
    }

    @unlink($path . DIRECTORY_SEPARATOR . 'valgrind');
    if (!@symlink($valgrind, $path . DIRECTORY_SEPARATOR . 'valgrind')) {
        echo "skip unable to expose valgrind in the test PATH\n";
    }
}
