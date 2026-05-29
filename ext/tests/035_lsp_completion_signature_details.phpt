--TEST--
LSP shows function and method parameter signatures in completion details
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 107

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-signature-detail-test"}}Content-Length: 545

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-signature-detail-test/file.php","languageId":"php","version":1,"text":"<?php\nfinal class Collection {}\nfinal class User {}\nfunction alphaSnippet(string $input, int $count = 0): void {}\nfinal class Demo\n{\n    /**\n     * @return Collection<User>\n     */\n    public function runTask(int $count, string $name): Collection { return new Collection(); }\n    public function test(): void\n    {\n        $this->run\n        alpha\n    }\n}\n"}}}Content-Length: 180

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-signature-detail-test/file.php"},"position":{"line":12,"character":18}}}Content-Length: 180

{"jsonrpc":"2.0","id":3,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-signature-detail-test/file.php"},"position":{"line":13,"character":13}}}Content-Length: 56

{"jsonrpc":"2.0","id":4,"method":"shutdown","params":[]}
--FILE--
<?php
LSParrot\start_lsp([
    'analyzer' => 'lsparrot',
    'symbolIndex' => ['size' => '4M'],
]);
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"method":"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%A"label":"runTask"%A"detail":"runTask(int $count, string $name): Collection<User>"%A"insertText":"runTask($0)"%AContent-Length: %d

%A"jsonrpc":"2.0","id":3,"result"%A"label":"alphaSnippet"%A"detail":"function alphaSnippet(string $input, int $count = 0): void"%A"insertText":"alphaSnippet($0)"%AContent-Length: %d

{"jsonrpc":"2.0","id":4,"result":null}
