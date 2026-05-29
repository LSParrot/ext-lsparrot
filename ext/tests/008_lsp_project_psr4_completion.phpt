--TEST--
LSP lsparrot server completes project classes from Composer PSR-4 directories
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 95

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-psr4-test"}}Content-Length: 170

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-psr4-test/file.php","languageId":"php","version":1,"text":"<?php\nBe"}}}Content-Length: 166

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-psr4-test/file.php"},"position":{"line":1,"character":2}}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-psr4-test';
@mkdir($root . '/vendor/composer', 0777, true);
@mkdir($root . '/src/Service', 0777, true);
file_put_contents($root . '/src/Service/BetaProject.php', "<?php\nnamespace App\\Service;\nfinal class BetaProject {}\n");
file_put_contents($root . '/vendor/composer/autoload_psr4.php', "<?php\nreturn [\n    'App\\\\' => [" . var_export($root . '/src', true) . "],\n];\n");
LSParrot\start_lsp([
    'analyzer' => 'lsparrot',
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-psr4-test';
@unlink($root . '/vendor/composer/autoload_psr4.php');
@unlink($root . '/src/Service/BetaProject.php');
@rmdir($root . '/vendor/composer');
@rmdir($root . '/vendor');
@rmdir($root . '/src/Service');
@rmdir($root . '/src');
@rmdir($root);
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%A"label":"BetaProject"%A"detail":"class App\\Service\\BetaProject"%AContent-Length: %d

{"jsonrpc":"2.0","id":3,"result":null}
