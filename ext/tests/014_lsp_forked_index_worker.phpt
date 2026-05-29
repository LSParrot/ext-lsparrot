--TEST--
LSP server builds Composer index and reports progress
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 103

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-worker-index-test"}}Content-Length: 178

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-worker-index-test/file.php","languageId":"php","version":1,"text":"<?php\nGa"}}}Content-Length: 174

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-worker-index-test/file.php"},"position":{"line":1,"character":2}}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-worker-index-test';
@mkdir($root . '/vendor/composer', 0777, true);
@mkdir($root . '/src', 0777, true);
file_put_contents($root . '/src/GammaProject.php', "<?php\nnamespace WorkerFixture;\nfinal class GammaProject {}\n");
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'WorkerFixture\\\\GammaProject' => " . var_export($root . '/src/GammaProject.php', true) . ",\n];\n");
LSParrot\start_lsp([
    'analyzer' => 'lsparrot',
    'workers' => ['count' => 1],
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-worker-index-test';
@unlink($root . '/vendor/composer/autoload_classmap.php');
@unlink($root . '/src/GammaProject.php');
@rmdir($root . '/vendor/composer');
@rmdir($root . '/vendor');
@rmdir($root . '/src');
@rmdir($root);
?>
--EXPECTF--
Content-Length: %d

%A"method":"lsparrot.php/analyzerStatus"%A"analyzer":"index"%A"state":"running"%AContent-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%A"state":"idle"%A"jsonrpc":"2.0","id":2,"result"%A"label":"GammaProject"%A"detail":"class WorkerFixture\\GammaProject"%AContent-Length: %d

{"jsonrpc":"2.0","id":3,"result":null}
