--TEST--
LSP incrementally reindexes saved files so new classes complete without restart
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 101

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-incr-index-test"}}Content-Length: 230

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-incr-index-test/src/GammaSaved.php","languageId":"php","version":1,"text":"<?php\nnamespace App;\nfinal class GammaSavedOld {}\n"}}}Content-Length: 231

{"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///tmp/lsp-incr-index-test/src/GammaSaved.php","version":2},"contentChanges":[{"text":"<?php\nnamespace App;\nfinal class GammaSaved {}\n"}]}}Content-Length: 196

{"jsonrpc":"2.0","method":"textDocument/didSave","params":{"textDocument":{"uri":"file:///tmp/lsp-incr-index-test/src/GammaSaved.php"},"text":"<?php\nnamespace App;\nfinal class GammaSaved {}\n"}}Content-Length: 182

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-incr-index-test/edit.php","languageId":"php","version":1,"text":"<?php\nGammaSav"}}}Content-Length: 172

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-incr-index-test/edit.php"},"position":{"line":1,"character":8}}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-incr-index-test';
@mkdir($root . '/vendor/composer', 0777, true);
@mkdir($root . '/src', 0777, true);
file_put_contents($root . '/vendor/composer/autoload_psr4.php', "<?php\nreturn [\n    'App\\\\' => [" . var_export($root . '/src', true) . "],\n];\n");
LSParrot\start_lsp([
    'analyzer' => 'lsparrot',
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-incr-index-test';
@unlink($root . '/vendor/composer/autoload_psr4.php');
@unlink($root . '/.lsparrot/lsparrot-index.bin');
@rmdir($root . '/.lsparrot');
@rmdir($root . '/vendor/composer');
@rmdir($root . '/vendor');
@rmdir($root . '/src');
@rmdir($root);
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%A"label":"GammaSaved"%A"detail":"class App\\GammaSaved"%AContent-Length: %d

{"jsonrpc":"2.0","id":3,"result":null}
