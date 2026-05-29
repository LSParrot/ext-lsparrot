--TEST--
LSP hover resolves PHPDoc @var types for declared properties
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 112

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-property-phpdoc-hover-test"}}Content-Length: 403

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-property-phpdoc-hover-test/Collection.php","languageId":"php","version":1,"text":"<?php\n\n/**\n * @template TItem of object\n */\nfinal class Collection\n{\n    /** @var non-empty-list<TItem> */\n    private array $items;\n\n    public function first(): void\n    {\n        $this->items;\n    }\n}\n"}}}Content-Length: 185

{"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-property-phpdoc-hover-test/Collection.php"},"position":{"line":8,"character":20}}}Content-Length: 186

{"jsonrpc":"2.0","id":3,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-property-phpdoc-hover-test/Collection.php"},"position":{"line":12,"character":17}}}Content-Length: 56

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
$root = '/tmp/lsp-property-phpdoc-hover-test';
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

%A"jsonrpc":"2.0","id":2,"result"%Anon-empty-list<TItem>%ALSParrot Engine%AContent-Length: %d

%A"jsonrpc":"2.0","id":3,"result"%Anon-empty-list<TItem>%ALSParrot Engine%AContent-Length: %d

{"jsonrpc":"2.0","id":4,"result":null}
