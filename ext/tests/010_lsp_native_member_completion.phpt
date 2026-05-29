--TEST--
LSP lsparrot completion stays empty after object operator without external analyzers
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 106

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-lsparrot-member-test"}}Content-Length: 222

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-lsparrot-member-test/member.php","languageId":"php","version":1,"text":"<?php\n$item = new stdClass();\n$val = $item->\n"}}}Content-Length: 180

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-lsparrot-member-test/member.php"},"position":{"line":2,"character":14}}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
LSParrot\start_lsp(['analyzer' => 'lsparrot']);
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result":{"isIncomplete":false,"items":[]}%AContent-Length: %d

{"jsonrpc":"2.0","id":3,"result":null}
