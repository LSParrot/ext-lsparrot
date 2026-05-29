--TEST--
LSP returns semantic tokens for identifiers while leaving comments and strings to the client grammar
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 97

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-semtok-test"}}Content-Length: 247

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-semtok-test/a.php","languageId":"php","version":1,"text":"<?php\nclass Foo {\n    public function bar(): void {\n        $a = 1; // c\n    }\n}\n"}}}Content-Length: 138

{"jsonrpc":"2.0","id":2,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"file:///tmp/lsp-semtok-test/a.php"}}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
@mkdir('/tmp/lsp-semtok-test', 0777, true);
LSParrot\start_lsp(['analyzer' => 'lsparrot']);
?>
--CLEAN--
<?php
@rmdir('/tmp/lsp-semtok-test');
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%A"semanticTokensProvider":{"legend":{"tokenTypes":["namespace","class"%A"full":true}%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result":{"data":[1,0,5,9,0,0,6,3,1,0,1,4,6,9,0,0,7,8,9,0,0,9,3,5,0,1,8,2,7,0,0,5,1,12,0]}%AContent-Length: %d

{"jsonrpc":"2.0","id":3,"result":null}
