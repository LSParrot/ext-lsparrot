--TEST--
LSP didClose drops the document and publishes empty diagnostics for encoded URIs
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 96

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-close-test"}}Content-Length: 180

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-close-test/note%2Ephp","languageId":"php","version":1,"text":"<?php function ("}}}Content-Length: 124

{"jsonrpc":"2.0","method":"textDocument/didClose","params":{"textDocument":{"uri":"file:///tmp/lsp-close-test/note%2Ephp"}}}Content-Length: 56

{"jsonrpc":"2.0","id":2,"method":"shutdown","params":[]}
--FILE--
<?php
@mkdir('/tmp/lsp-close-test', 0777, true);
LSParrot\start_lsp(['analyzer' => 'lsparrot']);
?>
--CLEAN--
<?php
@rmdir('/tmp/lsp-close-test');
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"textDocument/publishDiagnostics"%A"uri":"file:///tmp/lsp-close-test/note%2Ephp"%A"source":"php"%AContent-Length: %d

%A"textDocument/publishDiagnostics"%A"diagnostics":[]%AContent-Length: %d

{"jsonrpc":"2.0","id":2,"result":null}
