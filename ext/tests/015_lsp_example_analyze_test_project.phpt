--TEST--
LSP server handles the zeriyoshi/analyze_test fixture project
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 91

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file://tests/fixtures"}}Content-Length: 516

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file://tests/fixtures/src/Fixture/Usage.php","languageId":"php","version":1,"text":"<?php\n/**\n * @template TUser\n * @param array{user_id: positive-int, 'display-name': non-empty-string} $payload\n */\nfunction consume(array $payload): void {\n    /** @var \\Zeriyoshi\\AnalyzeTest\\Domain\\Collection<\\Zeriyoshi\\AnalyzeTest\\Domain\\User> $users */\n    $users = [];\n    $payload['dis\n    // ClassMapped\n    // Legacy\n}\n"}}}Content-Length: 176

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file://tests/fixtures/src/Fixture/Usage.php"},"position":{"line":8,"character":17}}}Content-Length: 176

{"jsonrpc":"2.0","id":3,"method":"textDocument/completion","params":{"textDocument":{"uri":"file://tests/fixtures/src/Fixture/Usage.php"},"position":{"line":9,"character":18}}}Content-Length: 177

{"jsonrpc":"2.0","id":4,"method":"textDocument/completion","params":{"textDocument":{"uri":"file://tests/fixtures/src/Fixture/Usage.php"},"position":{"line":10,"character":13}}}Content-Length: 56

{"jsonrpc":"2.0","id":5,"method":"shutdown","params":[]}
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

%A"method":"lsparrot.php/analyzerStatus"%A"analyzer":"index"%AContent-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%A"label":"display-name"%A"detail":"array-shape key"%AContent-Length: %d

%A"jsonrpc":"2.0","id":3,"result"%A"label":"ClassMappedFixture"%A"detail":"class Zeriyoshi\\AnalyzeTest\\Fixture\\ClassMappedFixture"%AContent-Length: %d

%A"jsonrpc":"2.0","id":4,"result"%A"label":"LegacyUser"%A"detail":"class Legacy_AnalyzeTest_LegacyUser"%AContent-Length: %d

{"jsonrpc":"2.0","id":5,"result":null}
