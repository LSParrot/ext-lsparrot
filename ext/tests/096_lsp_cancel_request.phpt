--TEST--
LSP responds with RequestCancelled when a queued $/cancelRequest targets a pending request
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 97

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-cancel-test"}}Content-Length: 173

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-cancel-test/a.php","languageId":"php","version":1,"text":"<?php $x = 1;"}}}Content-Length: 166

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-cancel-test/a.php"},"position":{"line":0,"character":13}}}Content-Length: 62

{"jsonrpc":"2.0","method":"$/cancelRequest","params":{"id":2}}Content-Length: 64

{"jsonrpc":"2.0","method":"$/cancelRequest","params":{"id":999}}Content-Length: 67

{"jsonrpc":"2.0","id":3,"method":"lsparrot.php/status","params":[]}Content-Length: 56

{"jsonrpc":"2.0","id":4,"method":"shutdown","params":[]}
--FILE--
<?php
@mkdir('/tmp/lsp-cancel-test', 0777, true);
LSParrot\start_lsp(['analyzer' => 'lsparrot']);
?>
--CLEAN--
<?php
@rmdir('/tmp/lsp-cancel-test');
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"textDocument/publishDiagnostics"%A"uri":"file:///tmp/lsp-cancel-test/a.php"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"error":{"code":-32800,"message":"Request cancelled"}%AContent-Length: %d

%A"jsonrpc":"2.0","id":3,"result"%AContent-Length: %d

{"jsonrpc":"2.0","id":4,"result":null}
