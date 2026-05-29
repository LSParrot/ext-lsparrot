--TEST--
LSP completion keeps the richest typed duplicate candidate when analyzers are available
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 97

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-dedupe-test"}}Content-Length: 230

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-dedupe-test/file.php","languageId":"php","version":1,"text":"<?php\n/** @var \\Vendor\\Box<int> $value */\n$value = null;\n// va"}}}Content-Length: 168

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-dedupe-test/file.php"},"position":{"line":3,"character":5}}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-dedupe-test';
@mkdir($root . '/vendor/bin', 0777, true);
file_put_contents($root . '/vendor/bin/phpstan', "#!/usr/bin/env php\n<?php echo json_encode(['files' => []]);\n");
chmod($root . '/vendor/bin/phpstan', 0755);
LSParrot\start_lsp([
    'analyzer' => 'phpstan',
    'workers' => ['analyzerDiagnosticsTimeout' => 5],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-dedupe-test';
@unlink($root . '/vendor/bin/phpstan');
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
@rmdir($root . '/vendor/bin');
@rmdir($root . '/vendor');
@rmdir($root . '/.cache/lsp/shadow/phpstan');
@rmdir($root . '/.cache/lsp/shadow');
@rmdir($root . '/.cache/lsp');
@rmdir($root . '/.cache');
@rmdir($root);
?>
--EXPECTREGEX--
(?s)\A(?!.*"detail":"variable \$value").*"analyzer":"driver".*"driver":"lsparrot\+phpstan".*"label":"LSParrot Engine \+ PHPStan".*"jsonrpc":"2.0","id":2,"result".*"label":"\$value".*"detail":"\\\\Vendor\\\\Box<int>".*"source":"phpstan".*\z
