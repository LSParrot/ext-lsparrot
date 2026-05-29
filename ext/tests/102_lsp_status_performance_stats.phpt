--TEST--
LSP status reports per-method performance statistics
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 101

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-perf-stats-test"}}Content-Length: 180

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-perf-stats-test/a.php","languageId":"php","version":1,"text":"<?php\n$a = 1;\n"}}}Content-Length: 169

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-perf-stats-test/a.php"},"position":{"line":1,"character":2}}}Content-Length: 67

{"jsonrpc":"2.0","id":3,"method":"lsparrot.php/status","params":[]}Content-Length: 56

{"jsonrpc":"2.0","id":4,"method":"shutdown","params":[]}
--FILE--
<?php
@mkdir('/tmp/lsp-perf-stats-test', 0777, true);
LSParrot\start_lsp(['analyzer' => 'lsparrot']);
?>
--CLEAN--
<?php
@rmdir('/tmp/lsp-perf-stats-test');
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":3,"result"%A"performance":{%A"initialize":{"count":1,"totalMs":%A"textDocument/completion":{"count":1,%AContent-Length: %d

{"jsonrpc":"2.0","id":4,"result":null}
