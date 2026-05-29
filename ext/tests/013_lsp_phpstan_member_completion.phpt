--TEST--
LSP server provides analyzer member completions without the PHP library
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 97

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-member-test"}}Content-Length: 215

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-member-test/member.php","languageId":"php","version":1,"text":"<?php\n$item = new DateTimeImmutable();\n$item->\n"}}}Content-Length: 170

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-member-test/member.php"},"position":{"line":2,"character":7}}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-member-test';
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
$root = '/tmp/lsp-member-test';
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
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%A"label":"format"%A"detail":"format(...)"%A"source":"phpstan"%AContent-Length: %d

{"jsonrpc":"2.0","id":3,"result":null}
