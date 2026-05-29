--TEST--
LSP server returns workspace symbols from the Composer symbol index
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 107

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-workspace-symbol-test"}}Content-Length: 183

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-workspace-symbol-test/file.php","languageId":"php","version":1,"text":"<?php\nSym"}}}Content-Length: 79

{"jsonrpc":"2.0","id":2,"method":"workspace/symbol","params":{"query":"Omega"}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-workspace-symbol-test';
@mkdir($root . '/vendor/composer', 0777, true);
@mkdir($root . '/src', 0777, true);
file_put_contents($root . '/src/OmegaSymbol.php', "<?php\nnamespace WorkspaceFixture;\nfinal class OmegaSymbol {}\n");
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'WorkspaceFixture\\\\OmegaSymbol' => " . var_export($root . '/src/OmegaSymbol.php', true) . ",\n];\n");
LSParrot\start_lsp([
    'analyzer' => 'lsparrot',
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-workspace-symbol-test';
@unlink($root . '/vendor/composer/autoload_classmap.php');
@unlink($root . '/src/OmegaSymbol.php');
@rmdir($root . '/vendor/composer');
@rmdir($root . '/vendor');
@rmdir($root . '/src');
@rmdir($root);
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%A"name":"WorkspaceFixture\\OmegaSymbol"%A"kind":5%A"uri":"file:///tmp/lsp-workspace-symbol-test/src/OmegaSymbol.php"%AContent-Length: %d

{"jsonrpc":"2.0","id":3,"result":null}
