--TEST--
LSP lsparrot server returns completion items without the PHP library
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 78

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file://."}}Content-Length: 210

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsparrot.php","languageId":"php","version":1,"text":"<?php\nfunction alphaExample(): void {}\n$apple = 1;\n// al"}}}Content-Length: 156

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsparrot.php"},"position":{"line":3,"character":5}}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
LSParrot\start_lsp(['analyzer' => 'lsparrot']);
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%A"label":"alphaExample"%A"detail":"function alphaExample(): void"%AContent-Length: %d

{"jsonrpc":"2.0","id":3,"result":null}
