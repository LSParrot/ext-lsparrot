--TEST--
LSP indexes project functions and adds use function imports from completion
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 91

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file://tests/fixtures"}}Content-Length: 329

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file://tests/fixtures/src/Service/FunctionDemo.php","languageId":"php","version":1,"text":"<?php\n\nnamespace Zeriyoshi\\AnalyzeTest\\Service;\n\nfinal class FunctionDemo\n{\n    public function make(): void\n    {\n        collect_n\n    }\n}"}}}Content-Length: 183

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file://tests/fixtures/src/Service/FunctionDemo.php"},"position":{"line":8,"character":17}}}Content-Length: 56

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

%A"jsonrpc":"2.0","id":2,"result"%A"label":"collect_names"%A"kind":3%A"detail":"function Zeriyoshi\\AnalyzeTest\\Fixture\\collect_names"%A"insertTextFormat":2%A"textEdit"%A"newText":"collect_names($0)"%A"additionalTextEdits"%A"newText":"\nuse function Zeriyoshi\\AnalyzeTest\\Fixture\\collect_names;\n"%AContent-Length: %d

{"jsonrpc":"2.0","id":3,"result":null}
