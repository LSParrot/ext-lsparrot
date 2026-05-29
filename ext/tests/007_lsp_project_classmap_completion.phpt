--TEST--
LSP lsparrot server completes project classes from Composer classmap in the lsparrot symbol index
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 99

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-classmap-test"}}Content-Length: 174

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-classmap-test/file.php","languageId":"php","version":1,"text":"<?php\nAl"}}}Content-Length: 170

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-classmap-test/file.php"},"position":{"line":1,"character":2}}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-classmap-test';
@mkdir($root . '/vendor/composer', 0777, true);
@mkdir($root . '/src', 0777, true);
file_put_contents($root . '/src/AlphaProject.php', "<?php\nnamespace App\\Service;\nfinal class AlphaProject {}\n");
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'App\\\\Service\\\\AlphaProject' => " . var_export($root . '/src/AlphaProject.php', true) . ",\n];\n");
LSParrot\start_lsp([
    'analyzer' => 'lsparrot',
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-classmap-test';
@unlink($root . '/vendor/composer/autoload_classmap.php');
@unlink($root . '/src/AlphaProject.php');
@rmdir($root . '/vendor/composer');
@rmdir($root . '/vendor');
@rmdir($root . '/src');
@rmdir($root);
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%A"label":"AlphaProject"%A"detail":"class App\\Service\\AlphaProject"%AContent-Length: %d

{"jsonrpc":"2.0","id":3,"result":null}
