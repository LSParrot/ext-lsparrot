--TEST--
LSP server returns memory, symbol index, and analyzer status for the VSCode status bar
--EXTENSIONS--
lsparrot
--INI--
memory_limit=128M
--STDIN--
Content-Length: 97

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-status-test"}}Content-Length: 67

{"jsonrpc":"2.0","id":2,"method":"lsparrot.php/status","params":[]}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
@mkdir('/tmp/lsp-status-test', 0777, true);
LSParrot\start_lsp([
    'analyzer' => 'lsparrot',
    'workers' => ['count' => 2],
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
@rmdir('/tmp/lsp-status-test');
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%A"memory"%A"current":%d%A"max":134217728%A"symbolIndex"%A"used":%d%A"max":4194304%A"processes"%A"active":0%A"configured":2%AContent-Length: %d

{"jsonrpc":"2.0","id":3,"result":null}
