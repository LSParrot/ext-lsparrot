<?php

function lsp_test_extension_path(): string
{
    $extensionDir = rtrim((string) ini_get('extension_dir'), "/\\");
    $suffix = PHP_SHLIB_SUFFIX;
    $names = PHP_OS_FAMILY === 'Windows'
        ? ['php_lsparrot.' . $suffix, 'lsparrot.' . $suffix]
        : ['lsparrot.' . $suffix, 'php_lsparrot.' . $suffix];
    $dirs = [
        $extensionDir,
        dirname(__DIR__) . DIRECTORY_SEPARATOR . 'modules',
    ];

    foreach ($dirs as $dir) {
        if ($dir === '') {
            continue;
        }

        foreach ($names as $name) {
            $candidate = $dir . DIRECTORY_SEPARATOR . $name;
            if (is_file($candidate)) {
                return $candidate;
            }
        }
    }

    return $dirs[0] . DIRECTORY_SEPARATOR . $names[0];
}
