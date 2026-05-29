--TEST--
LSP inserts call snippets for function and method completions only
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 103

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-call-snippet-test"}}Content-Length: 403

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-call-snippet-test/file.php","languageId":"php","version":1,"text":"<?php\nfunction alphaSnippet(): void {}\nfinal class Demo\n{\n    private string $name;\n    public function runTask(): void {}\n    public function test(): void\n    {\n        $this->run\n        $this->na\n        alpha\n    }\n}\n"}}}Content-Length: 175

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-call-snippet-test/file.php"},"position":{"line":8,"character":18}}}Content-Length: 175

{"jsonrpc":"2.0","id":3,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-call-snippet-test/file.php"},"position":{"line":9,"character":17}}}Content-Length: 176

{"jsonrpc":"2.0","id":4,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-call-snippet-test/file.php"},"position":{"line":10,"character":13}}}Content-Length: 56

{"jsonrpc":"2.0","id":5,"method":"shutdown","params":[]}
--FILE--
<?php
LSParrot\start_lsp(['analyzer' => 'lsparrot']);
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%A"label":"runTask"%A"kind":2%A"insertText":"runTask()$0"%A"insertTextFormat":2%AContent-Length: %d

{"jsonrpc":"2.0","id":3,"result":{"isIncomplete":false,"items":[{"label":"name","kind":10,"detail":"property string $name","filterText":"name","data":{"source":"lsparrot"}}]}}Content-Length: %d

%A"jsonrpc":"2.0","id":4,"result"%A"label":"alphaSnippet"%A"kind":3%A"insertText":"alphaSnippet()$0"%A"insertTextFormat":2%AContent-Length: %d

{"jsonrpc":"2.0","id":5,"result":null}
