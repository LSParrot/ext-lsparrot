--TEST--
LSP returns parameter name inlay hints for local function calls
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 96

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-inlay-test"}}Content-Length: 238

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-inlay-test/file.php","languageId":"php","version":1,"text":"<?php\nfunction combine($left, $right): void {}\ncombine($first, $second);\n"}}}Content-Length: 204

{"jsonrpc":"2.0","id":2,"method":"textDocument/inlayHint","params":{"textDocument":{"uri":"file:///tmp/lsp-inlay-test/file.php"},"range":{"start":{"line":0,"character":0},"end":{"line":3,"character":0}}}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
LSParrot\start_lsp(['analyzer' => 'lsparrot']);
?>
--EXPECTF--
%A"jsonrpc":"2.0","id":1,"result"%A"inlayHintProvider":true%AContent-Length: %d

%A"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result":[%A"label":"left:"%A"label":"right:"%AContent-Length: %d

{"jsonrpc":"2.0","id":3,"result":null}
