--TEST--
LSP lsparrot server completes function, method, closure, and arrow function return types
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 91

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file://tests/fixtures"}}Content-Length: 553

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file://tests/fixtures/src/Service/ReturnTypeDemo.php","languageId":"php","version":1,"text":"<?php\n\ndeclare(strict_types=1);\n\nnamespace Zeriyoshi\\AnalyzeTest\\Service;\n\nfinal class ReturnTypeDemo\n{\n    public function method(): \n    {\n    }\n\n    protected function imported(): Col\n    {\n    }\n\n    public function closures(): void\n    {\n        $arrow = static fn(): Col => null;\n        $closure = static function(): Col {\n        };\n    }\n}\n"}}}Content-Length: 185

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file://tests/fixtures/src/Service/ReturnTypeDemo.php"},"position":{"line":8,"character":30}}}Content-Length: 186

{"jsonrpc":"2.0","id":3,"method":"textDocument/completion","params":{"textDocument":{"uri":"file://tests/fixtures/src/Service/ReturnTypeDemo.php"},"position":{"line":12,"character":38}}}Content-Length: 186

{"jsonrpc":"2.0","id":4,"method":"textDocument/completion","params":{"textDocument":{"uri":"file://tests/fixtures/src/Service/ReturnTypeDemo.php"},"position":{"line":18,"character":33}}}Content-Length: 186

{"jsonrpc":"2.0","id":5,"method":"textDocument/completion","params":{"textDocument":{"uri":"file://tests/fixtures/src/Service/ReturnTypeDemo.php"},"position":{"line":19,"character":41}}}Content-Length: 56

{"jsonrpc":"2.0","id":6,"method":"shutdown","params":[]}
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

%A"jsonrpc":"2.0","id":2,"result"%A"label":"void"%A"detail":"type void"%A"label":"Collection"%A"additionalTextEdits":[{"range":%A"newText":"%Ause Zeriyoshi\\AnalyzeTest\\Domain\\Collection;\n"}]%AContent-Length: %d

%A"jsonrpc":"2.0","id":3,"result"%A"label":"Collection"%A"additionalTextEdits":[{"range":%A"newText":"%Ause Zeriyoshi\\AnalyzeTest\\Domain\\Collection;\n"}]%AContent-Length: %d

%A"jsonrpc":"2.0","id":4,"result"%A"label":"Collection"%AContent-Length: %d

%A"jsonrpc":"2.0","id":5,"result"%A"label":"Collection"%AContent-Length: %d

{"jsonrpc":"2.0","id":6,"result":null}
