--TEST--
LSP lsparrot server completes constructor promoted public readonly properties
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 91

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file://tests/fixtures"}}Content-Length: 418

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file://tests/fixtures/src/Fixture/PromotedPropertyUsage.php","languageId":"php","version":1,"text":"<?php\n\ndeclare(strict_types=1);\n\nnamespace Zeriyoshi\\AnalyzeTest\\Fixture;\n\nuse Zeriyoshi\\AnalyzeTest\\Domain\\User;\n\nfunction promotedPropertyCompletion(): void\n{\n    $firstUser = new User(1, 'Go');\n    $firstUser->\n}"}}}Content-Length: 193

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file://tests/fixtures/src/Fixture/PromotedPropertyUsage.php"},"position":{"line":11,"character":16}}}Content-Length: 56

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

%A"method":"lsparrot.php/analyzerStatus"%A"analyzer":"index"%AContent-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%A"label":"id"%A"detail":"property int $id"%A"source":"lsparrot"%A"label":"name"%A"detail":"property string $name"%A"source":"lsparrot"%AContent-Length: %d

{"jsonrpc":"2.0","id":3,"result":null}
