--TEST--
LSP server returns document and workspace symbols without the PHP library
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 98

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-symbols-test"}}Content-Length: 231

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-symbols-test/file.php","languageId":"php","version":1,"text":"<?php\nfunction alphaSymbol(): void {}\nfinal class BetaSymbol {}\n"}}}Content-Length: 137

{"jsonrpc":"2.0","id":2,"method":"textDocument/documentSymbol","params":{"textDocument":{"uri":"file:///tmp/lsp-symbols-test/file.php"}}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
LSParrot\start_lsp(['analyzer' => 'lsparrot']);
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%A"documentSymbolProvider":true%A"workspaceSymbolProvider":true%AContent-Length: %d

%A"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%A"name":"alphaSymbol"%A"kind":12%A"name":"BetaSymbol"%A"kind":5%AContent-Length: %d

{"jsonrpc":"2.0","id":3,"result":null}
