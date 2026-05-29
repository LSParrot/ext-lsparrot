--TEST--
LSP lsparrot server completes namespaced builtin return types with use imports
--EXTENSIONS--
lsparrot
--SKIPIF--
<?php
if (!class_exists('FTP\\Connection', false)) {
    echo "skip FTP\\Connection is not available in this PHP runtime";
}
?>
--STDIN--
Content-Length: 110

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-return-type-builtin-test"}}Content-Length: 252

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-return-type-builtin-test/file.php","languageId":"php","version":1,"text":"<?php\nnamespace ReturnTypeBuiltinFixture;\nfunction builtin(): Conn\n{\n}\n"}}}Content-Length: 182

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-return-type-builtin-test/file.php"},"position":{"line":2,"character":24}}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-return-type-builtin-test';
if (is_dir($root . '/.lsparrot')) {
    $it = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($root . '/.lsparrot', FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::CHILD_FIRST);
    foreach ($it as $file) {
        $file->isDir() ? @rmdir($file->getPathname()) : @unlink($file->getPathname());
    }
    @rmdir($root . '/.lsparrot');
}
LSParrot\start_lsp([
    'analyzer' => 'lsparrot',
    'workers' => ['count' => 1],
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-return-type-builtin-test';
if (is_dir($root . '/.lsparrot')) {
    $it = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($root . '/.lsparrot', FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::CHILD_FIRST);
    foreach ($it as $file) {
        $file->isDir() ? @rmdir($file->getPathname()) : @unlink($file->getPathname());
    }
    @rmdir($root . '/.lsparrot');
}
@rmdir($root);
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%A"label":"Connection"%A"detail":"class FTP\\Connection"%A"newText":"Connection"%A"additionalTextEdits":[{"range":%A"newText":"%Ause FTP\\Connection;\n"}]%AContent-Length: %d

{"jsonrpc":"2.0","id":3,"result":null}
