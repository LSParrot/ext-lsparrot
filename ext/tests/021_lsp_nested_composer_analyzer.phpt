--TEST--
LSP resolves PHPStan from nested Composer projects under the workspace root
--EXTENSIONS--
lsparrot
--SKIPIF--
<?php
$srcdir = getenv('TEST_PHP_SRCDIR');
if (!is_string($srcdir) || $srcdir === '') {
    $srcdir = getcwd();
}
require $srcdir . '/tests/valgrind_check.inc';
lsparrot_test_prepare_valgrind_for_empty_path('/tmp/lsp-nested-empty-path');
?>
--ENV--
PATH=/tmp/lsp-nested-empty-path
--STDIN--
Content-Length: 97

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-nested-root"}}Content-Length: 251

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-nested-root/fixture-project/src/bad.php","languageId":"php","version":1,"text":"<?php\n/** @var string $value */\n$value = 'x';\n$value->missing();\n"}}}Content-Length: 56

{"jsonrpc":"2.0","id":2,"method":"shutdown","params":[]}
--FILE--
<?php
$workspace = '/tmp/lsp-nested-root';
$project = $workspace . '/fixture-project';
@mkdir('/tmp/lsp-nested-empty-path', 0777, true);
@mkdir($project . '/src', 0777, true);
@mkdir($project . '/.lsparrot/phpstan', 0777, true);
@mkdir($project . '/vendor/bin', 0777, true);
file_put_contents($project . '/composer.json', json_encode([
    'config' => ['bin-dir' => 'vendor/bin'],
    'autoload' => ['psr-4' => ['NestedAnalyzerFixture\\' => 'src/']],
], JSON_PRETTY_PRINT));
file_put_contents($project . '/src/bad.php', "<?php\n/** @var string \$value */\n\$value = 'x';\n\$value->missing();\n");
file_put_contents($project . '/.lsparrot/phpstan/project-diagnostics.json', json_encode([
    'files' => [
        $project . '/src/bad.php' => [
            'messages' => [[
                'message' => 'Nested PHPStan root ' . $project,
                'line' => 4,
                'identifier' => 'nested.project',
            ]],
        ],
    ],
], JSON_UNESCAPED_SLASHES));
file_put_contents($project . '/vendor/bin/phpstan', <<<'PHP'
#!/usr/bin/env php
<?php
$root = getcwd();
echo json_encode([
    'files' => [
        $root . '/src/bad.php' => [
            'messages' => [[
                'message' => 'Nested PHPStan root ' . getcwd(),
                'line' => 4,
                'identifier' => 'nested.project',
            ]],
        ],
    ],
], JSON_UNESCAPED_SLASHES);
PHP);
chmod($project . '/vendor/bin/phpstan', 0755);
LSParrot\start_lsp([
    'analyzer' => 'phpstan',
    'workers' => ['analyzerDiagnosticsTimeout' => 5],
]);
?>
--CLEAN--
<?php
$workspace = '/tmp/lsp-nested-root';
$project = $workspace . '/fixture-project';
@unlink($project . '/vendor/bin/phpstan');
@unlink($project . '/src/bad.php');
@unlink($project . '/composer.json');
foreach (glob($project . '/.cache/lsp/shadow/phpstan/*') ?: [] as $file) {
    @unlink($file);
}
if (is_dir($project . '/.lsparrot')) {
    $it = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($project . '/.lsparrot', FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::CHILD_FIRST);
    foreach ($it as $file) {
        $file->isDir() ? @rmdir($file->getPathname()) : @unlink($file->getPathname());
    }
    @rmdir($project . '/.lsparrot');
}
if (is_dir($workspace . '/.lsparrot')) {
    $it = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($workspace . '/.lsparrot', FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::CHILD_FIRST);
    foreach ($it as $file) {
        $file->isDir() ? @rmdir($file->getPathname()) : @unlink($file->getPathname());
    }
    @rmdir($workspace . '/.lsparrot');
}
@rmdir($project . '/vendor/bin');
@rmdir($project . '/vendor');
@rmdir($project . '/.cache/lsp/shadow/phpstan');
@rmdir($project . '/.cache/lsp/shadow');
@rmdir($project . '/.cache/lsp');
@rmdir($project . '/.cache');
@rmdir($project . '/src');
@rmdir($project);
@rmdir($workspace);
@unlink('/tmp/lsp-nested-empty-path/valgrind');
@rmdir('/tmp/lsp-nested-empty-path');
?>
--EXPECTREGEX--
(?s)\A(?!.*missingAnalyzer)(?!.*falling back to LSParrot Engine).*"analyzer":"driver".*"driver":"lsparrot\+phpstan".*"label":"LSParrot Engine \+ PHPStan".*"textDocument\/publishDiagnostics".*"source":"phpstan".*"message":"Nested PHPStan root \/tmp\/lsp-nested-root\/fixture-project".*"code":"nested.project".*"line":3.*"jsonrpc":"2.0","id":2,"result":null.*\z
