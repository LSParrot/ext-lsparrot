--TEST--
LSP lsparrot server completes $this members inside the current class
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 91

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file://tests/fixtures"}}Content-Length: 700

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file://tests/fixtures/src/Domain/Collection.php","languageId":"php","version":1,"text":"<?php\n\ndeclare(strict_types=1);\n\nnamespace Zeriyoshi\\AnalyzeTest\\Domain;\n\n/**\n * @template TItem of object\n */\nfinal class Collection\n{\n    /** @var non-empty-list<TItem> */\n    private array $items;\n\n    /**\n     * @param non-empty-list<TItem> $items\n     */\n    public function __construct(array $items)\n    {\n        $this->items = $items;\n    }\n\n    /**\n     * @return non-empty-list<TItem>\n     */\n    public function all(): array\n    {\n        $this->\n        return $this->items;\n    }\n}"}}}Content-Length: 181

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file://tests/fixtures/src/Domain/Collection.php"},"position":{"line":27,"character":15}}}Content-Length: 56

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

%A"jsonrpc":"2.0","id":2,"result"%A"label":"items"%A"detail":"property array $items"%A"source":"lsparrot"%A"label":"all"%A"detail":"all(): non-empty-list<TItem>"%A"source":"lsparrot"%AContent-Length: %d

{"jsonrpc":"2.0","id":3,"result":null}
