--TEST--
LSP lsparrot server normalizes PHPDoc param array shapes and completes inferred keys
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 108

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-param-array-shape-test"}}Content-Length: 363

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-param-array-shape-test/file.php","languageId":"php","version":1,"text":"<?php\n/**\n * @param array{user_id: positive-int, score: int<1, max>, name: non-empty-string} $payload\n */\nfunction consume(array $payload): void\n{\n    $payload;\n    $payload['sc\n}\n"}}}Content-Length: 174

{"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-param-array-shape-test/file.php"},"position":{"line":6,"character":7}}}Content-Length: 180

{"jsonrpc":"2.0","id":3,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-param-array-shape-test/file.php"},"position":{"line":7,"character":16}}}Content-Length: 56

{"jsonrpc":"2.0","id":4,"method":"shutdown","params":[]}
--FILE--
<?php
LSParrot\start_lsp([
    'analyzer' => 'lsparrot',
    'workers' => ['count' => 0],
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-param-array-shape-test';
if (is_dir($root)) {
    $it = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($root, FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::CHILD_FIRST);
    foreach ($it as $file) {
        $file->isDir() ? @rmdir($file->getPathname()) : @unlink($file->getPathname());
    }
    @rmdir($root);
}
?>
--EXPECTF--
%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%Aarray{user_id: int, score: int, name: string}%AContent-Length: %d

%A"jsonrpc":"2.0","id":3,"result"%A"label":"score"%A"detail":"array-shape key"%AContent-Length: %d

{"jsonrpc":"2.0","id":4,"result":null}
