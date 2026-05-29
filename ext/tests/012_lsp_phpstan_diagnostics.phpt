--TEST--
LSP server runs Composer-located PHPStan diagnostics without the PHP library
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 98

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-phpstan-test"}}Content-Length: 232

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-phpstan-test/bad.php","languageId":"php","version":1,"text":"<?php\n/** @var string $value */\n$value = 'x';\n$value->missing();\n"}}}Content-Length: 56

{"jsonrpc":"2.0","id":2,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-phpstan-test';
@mkdir($root . '/tools/bin', 0777, true);
@mkdir($root . '/.lsparrot/phpstan', 0777, true);
file_put_contents($root . '/composer.json', json_encode([
    'config' => ['bin-dir' => 'tools/bin'],
    'autoload' => ['classmap' => ['bad.php']],
], JSON_PRETTY_PRINT));
file_put_contents($root . '/bad.php', "<?php\n/** @var string \$value */\n\$value = 'x';\n\$value->missing();\n");
file_put_contents($root . '/.lsparrot/phpstan/project-diagnostics.json', json_encode([
    'files' => [
        $root . '/bad.php' => [
            'messages' => [[
                'message' => 'Cannot call method missing() on string. memory 384M',
                'line' => 4,
                'identifier' => 'method.nonObject',
            ]],
        ],
    ],
], JSON_UNESCAPED_SLASHES));
file_put_contents($root . '/tools/bin/phpstan', <<<'PHP'
#!/usr/bin/env php
<?php
$root = getcwd();
echo json_encode([
    'files' => [
        $root . '/bad.php' => [
            'messages' => [[
                'message' => 'Cannot call method missing() on string. memory ' . ini_get('memory_limit'),
                'line' => 4,
                'identifier' => 'method.nonObject',
            ]],
        ],
    ],
], JSON_UNESCAPED_SLASHES);
PHP);
chmod($root . '/tools/bin/phpstan', 0755);
LSParrot\start_lsp([
    'analyzer' => 'phpstan',
    'workerPhpArgs' => ['-dmemory_limit=384M'],
    'workers' => ['analyzerDiagnosticsTimeout' => 5],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-phpstan-test';
@unlink($root . '/tools/bin/phpstan');
@unlink($root . '/bad.php');
@unlink($root . '/composer.json');
foreach (glob($root . '/.cache/lsp/shadow/phpstan/*') ?: [] as $file) {
    @unlink($file);
}
if (is_dir($root . '/.lsparrot')) {
    $it = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($root . '/.lsparrot', FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::CHILD_FIRST);
    foreach ($it as $file) {
        $file->isDir() ? @rmdir($file->getPathname()) : @unlink($file->getPathname());
    }
    @rmdir($root . '/.lsparrot');
}
@rmdir($root . '/tools/bin');
@rmdir($root . '/tools');
@rmdir($root . '/.cache/lsp/shadow/phpstan');
@rmdir($root . '/.cache/lsp/shadow');
@rmdir($root . '/.cache/lsp');
@rmdir($root . '/.cache');
@rmdir($root);
?>
--EXPECTREGEX--
[\s\S]*"method":"lsparrot.php\/analyzerStatus"[\s\S]*"analyzer":"phpstan"[\s\S]*"state":"running"[\s\S]*"jsonrpc":"2\.0","id":1,"result"[\s\S]*"textDocument\/publishDiagnostics"[\s\S]*"source":"phpstan"[\s\S]*"message":"Cannot call method missing\(\) on string\. memory 384M"[\s\S]*"code":"method\.nonObject"[\s\S]*"line":3[\s\S]*\{"jsonrpc":"2\.0","id":2,"result":null\}
