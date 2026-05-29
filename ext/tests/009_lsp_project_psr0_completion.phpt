--TEST--
LSP lsparrot server completes project classes from Composer PSR-0 directories
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 95

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-psr0-test"}}Content-Length: 171

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-psr0-test/file.php","languageId":"php","version":1,"text":"<?php\nOld"}}}Content-Length: 166

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-psr0-test/file.php"},"position":{"line":1,"character":3}}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-psr0-test';
@mkdir($root . '/vendor/composer', 0777, true);
@mkdir($root . '/src/Legacy/Service', 0777, true);
file_put_contents($root . '/src/Legacy/Service/OldProject.php', "<?php\nclass Legacy_Service_OldProject {}\n");
file_put_contents($root . '/vendor/composer/autoload_namespaces.php', "<?php\nreturn [\n    'Legacy_' => [" . var_export($root . '/src', true) . "],\n];\n");
LSParrot\start_lsp([
    'analyzer' => 'lsparrot',
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-psr0-test';
@unlink($root . '/vendor/composer/autoload_namespaces.php');
@unlink($root . '/src/Legacy/Service/OldProject.php');
@rmdir($root . '/vendor/composer');
@rmdir($root . '/vendor');
@rmdir($root . '/src/Legacy/Service');
@rmdir($root . '/src/Legacy');
@rmdir($root . '/src');
@rmdir($root);
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%A"label":"OldProject"%A"detail":"class Legacy_Service_OldProject"%AContent-Length: %d

{"jsonrpc":"2.0","id":3,"result":null}
