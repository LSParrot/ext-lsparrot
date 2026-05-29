--TEST--
LSP keeps a resident analyzer runner per project, reuses it for type queries, and reports it in status
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 97

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-runner-test"}}Content-Length: 329

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-runner-test/src/Thing.php","languageId":"php","version":1,"text":"<?php\n\nnamespace RunnerFixture;\n\nfinal class Thing\n{\n    public function go(): void\n    {\n        $value = $this->mystery();\n        $value;\n    }\n}\n"}}}Content-Length: 168

{"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-runner-test/src/Thing.php"},"position":{"line":9,"character":9}}}Content-Length: 67

{"jsonrpc":"2.0","id":3,"method":"lsparrot.php/status","params":[]}Content-Length: 56

{"jsonrpc":"2.0","id":4,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-runner-test';
@mkdir($root . '/src', 0777, true);
@mkdir($root . '/vendor/bin', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);
file_put_contents($root . '/composer.json', json_encode([
    'autoload' => ['psr-4' => ['RunnerFixture\\' => 'src/']],
], JSON_PRETTY_PRINT));
file_put_contents($root . '/vendor/composer/autoload_psr4.php', "<?php\nreturn [\n    'RunnerFixture\\\\' => [" . var_export($root . '/src', true) . "],\n];\n");
file_put_contents($root . '/vendor/bin/phpstan', <<<'PHP'
#!/usr/bin/env php
<?php
$file = '';
foreach ($argv as $arg) {
    if (is_file($arg) && str_contains($arg, '/.lsparrot/shadow/phpstan-type/')) {
        $file = $arg;
        break;
    }
}
if ($file === '') {
    echo json_encode(['files' => [], 'errors' => []]);
    exit(0);
}
$contents = file_get_contents($file) ?: '';
$line = 1;
foreach (explode("\n", $contents) as $i => $text) {
    if (str_contains($text, '\\PHPStan\\dumpType(')) {
        $line = $i + 1;
        break;
    }
}
echo json_encode([
    'totals' => ['errors' => 0, 'file_errors' => 1],
    'files' => [
        $file => [
            'errors' => 1,
            'messages' => [[
                'message' => 'Dumped type: RunnerFixture\\Mystery',
                'line' => $line,
                'ignorable' => false,
                'identifier' => 'phpstan.dumpType',
            ]],
        ],
    ],
    'errors' => [],
]);
exit(1);
PHP);
chmod($root . '/vendor/bin/phpstan', 0755);
LSParrot\start_lsp([
    'analyzer' => 'phpstan',
    'workers' => ['count' => 0, 'analyzerDiagnosticsTimeout' => 5],
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-runner-test';
if (is_dir($root)) {
    $it = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($root, FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::CHILD_FIRST);
    foreach ($it as $file) {
        $file->isDir() ? @rmdir($file->getPathname()) : @unlink($file->getPathname());
    }
    @rmdir($root);
}
?>
--EXPECTREGEX--
[\s\S]*"jsonrpc":"2\.0","id":1,"result"[\s\S]*"jsonrpc":"2\.0","id":2,"result"[\s\S]*RunnerFixture\\\\Mystery[\s\S]*"jsonrpc":"2\.0","id":3,"result"[\s\S]*"runners":\[\{"analyzer":"phpstan","projectRoot":"\/tmp\/lsp-runner-test","jobs":[1-9][0-9]*,"alive":true\}\][\s\S]*\{"jsonrpc":"2\.0","id":4,"result":null\}
