--TEST--
LSP orders workspace project symbols before vendor symbols
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 106

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-vendor-priority-test"}}Content-Length: 216

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-vendor-priority-test/src/Demo.php","languageId":"php","version":1,"text":"<?php\nnamespace PriorityFixture;\nAlp\n"}}}Content-Length: 181

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-vendor-priority-test/src/Demo.php"},"position":{"line":2,"character":3}}}Content-Length: 76

{"jsonrpc":"2.0","id":3,"method":"workspace/symbol","params":{"query":"AP"}}Content-Length: 56

{"jsonrpc":"2.0","id":4,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-vendor-priority-test';
@mkdir($root . '/src', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);
@mkdir($root . '/vendor/package/src', 0777, true);
file_put_contents($root . '/src/Demo.php', "<?php\nnamespace PriorityFixture;\n");
file_put_contents($root . '/src/AlphaProject.php', "<?php\nnamespace PriorityFixture;\nfinal class AlphaProject {}\n");
file_put_contents($root . '/vendor/package/src/AlphaVendor.php', "<?php\nnamespace VendorPackage;\nfinal class AlphaVendor {}\n");
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'VendorPackage\\\\AlphaVendor' => " . var_export($root . '/vendor/package/src/AlphaVendor.php', true) . ",\n    'PriorityFixture\\\\AlphaProject' => " . var_export($root . '/src/AlphaProject.php', true) . ",\n];\n");
LSParrot\start_lsp([
    'analyzer' => 'lsparrot',
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-vendor-priority-test';
@unlink($root . '/vendor/composer/autoload_classmap.php');
@unlink($root . '/vendor/package/src/AlphaVendor.php');
@unlink($root . '/src/AlphaProject.php');
@unlink($root . '/src/Demo.php');
@rmdir($root . '/vendor/package/src');
@rmdir($root . '/vendor/package');
@rmdir($root . '/vendor/composer');
@rmdir($root . '/vendor');
@rmdir($root . '/src');
@rmdir($root);
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%A"label":"AlphaProject"%A"detail":"class PriorityFixture\\AlphaProject"%A"sortText":"0:AlphaProject"%A"label":"AlphaVendor"%A"detail":"class VendorPackage\\AlphaVendor"%A"sortText":"1:AlphaVendor"%AContent-Length: %d

%A"jsonrpc":"2.0","id":3,"result"%A"name":"PriorityFixture\\AlphaProject"%A"kind":5%A"uri":"file:///tmp/lsp-vendor-priority-test/src/AlphaProject.php"%A"name":"VendorPackage\\AlphaVendor"%A"kind":5%A"uri":"file:///tmp/lsp-vendor-priority-test/vendor/package/src/AlphaVendor.php"%AContent-Length: %d

{"jsonrpc":"2.0","id":4,"result":null}
