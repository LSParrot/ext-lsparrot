--TEST--
LSP completes project classes while typing a qualified use statement
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 91

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file://tests/fixtures"}}Content-Length: 270

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file://tests/fixtures/src/Service/UseSmoke.php","languageId":"php","version":1,"text":"<?php\n\nnamespace Zeriyoshi\\AnalyzeTest\\Service;\n\nuse Zeriyoshi\\AnalyzeTest\\Domain\\Collec"}}}Content-Length: 179

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file://tests/fixtures/src/Service/UseSmoke.php"},"position":{"line":4,"character":39}}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
LSParrot\start_lsp([
    'analyzer' => 'lsparrot',
    'workers' => ['count' => 1],
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$dir = 'tests/fixtures/.lsparrot';
if (is_dir($dir)) {
    $it = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($dir, FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::CHILD_FIRST);
    foreach ($it as $file) {
        $file->isDir() ? @rmdir($file->getPathname()) : @unlink($file->getPathname());
    }
    @rmdir($dir);
}
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%A"label":"Collection"%A"detail":"class Zeriyoshi\\AnalyzeTest\\Domain\\Collection"%A"textEdit"%A"start":{"line":4,"character":4}%A"end":{"line":4,"character":39}%A"newText":"Zeriyoshi\\AnalyzeTest\\Domain\\Collection"%AContent-Length: %d

{"jsonrpc":"2.0","id":3,"result":null}
