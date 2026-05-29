--TEST--
LSP coalesces queued didChange bursts and publishes diagnostics only for the newest revision
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 99

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-coalesce-test"}}Content-Length: 178

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-coalesce-test/a.php","languageId":"php","version":1,"text":"<?php function ("}}}Content-Length: 183

{"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///tmp/lsp-coalesce-test/a.php","version":2},"contentChanges":[{"text":"<?php function (("}]}}Content-Length: 184

{"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///tmp/lsp-coalesce-test/a.php","version":3},"contentChanges":[{"text":"<?php function ((("}]}}Content-Length: 56

{"jsonrpc":"2.0","id":2,"method":"shutdown","params":[]}
--FILE--
<?php
@mkdir('/tmp/lsp-coalesce-test', 0777, true);
LSParrot\start_lsp(['analyzer' => 'lsparrot']);
?>
--CLEAN--
<?php
@rmdir('/tmp/lsp-coalesce-test');
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"textDocument/publishDiagnostics"%A"version":1%AContent-Length: %d

%A"textDocument/publishDiagnostics"%A"version":3%AContent-Length: %d

{"jsonrpc":"2.0","id":2,"result":null}
