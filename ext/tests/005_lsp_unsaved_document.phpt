--TEST--
LSP server parses unsaved document text and publishes versioned diagnostics
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 78

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file://."}}Content-Length: 166

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/unsaved.php","languageId":"php","version":7,"text":"<?php function ("}}}Content-Length: 56

{"jsonrpc":"2.0","id":2,"method":"shutdown","params":[]}
--FILE--
<?php
chdir(dirname(__DIR__, 2));
LSParrot\start_lsp(['analyzer' => 'lsparrot']);
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"textDocument/publishDiagnostics"%A"uri":"file:///tmp/unsaved.php"%A"source":"php"%A"version":7%AContent-Length: %d

{"jsonrpc":"2.0","id":2,"result":null}
