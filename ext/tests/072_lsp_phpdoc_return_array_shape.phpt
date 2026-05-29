--TEST--
LSP lsparrot server normalizes PHPDoc return array shapes and completes inferred keys
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 109

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-array-shape-return-test"}}Content-Length: 527

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-array-shape-return-test/UserRepository.php","languageId":"php","version":1,"text":"<?php\nfinal class UserRepository\n{\n    /**\n     * @return array{user_id: positive-int, name: non-empty-string}\n     */\n    public function getFirstPayload(): array\n    {\n        return [];\n    }\n\n    public function usePayload(): void\n    {\n        $data = $this->getFirstPayload();\n        $data;\n        $data['na\n    }\n}\n"}}}Content-Length: 187

{"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-array-shape-return-test/UserRepository.php"},"position":{"line":14,"character":10}}}Content-Length: 192

{"jsonrpc":"2.0","id":3,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-array-shape-return-test/UserRepository.php"},"position":{"line":15,"character":17}}}Content-Length: 56

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
$root = '/tmp/lsp-array-shape-return-test';
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

%A"jsonrpc":"2.0","id":2,"result"%Aarray{user_id: int, name: string}%AContent-Length: %d

%A"jsonrpc":"2.0","id":3,"result"%A"label":"name"%A"detail":"array-shape key"%AContent-Length: %d

{"jsonrpc":"2.0","id":4,"result":null}
