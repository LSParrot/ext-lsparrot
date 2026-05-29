--TEST--
LSP removes deleted classes from the symbol index when the defining file is saved
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 100

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-tombstone-test"}}Content-Length: 218

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-tombstone-test/src/Zeta.php","languageId":"php","version":1,"text":"<?php\nnamespace App;\nfinal class ZetaGone {}\n"}}}Content-Length: 187

{"jsonrpc":"2.0","method":"textDocument/didSave","params":{"textDocument":{"uri":"file:///tmp/lsp-tombstone-test/src/Zeta.php"},"text":"<?php\nnamespace App;\nfinal class ZetaGone {}\n"}}Content-Length: 180

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-tombstone-test/edit.php","languageId":"php","version":1,"text":"<?php\nZetaGon"}}}Content-Length: 171

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-tombstone-test/edit.php"},"position":{"line":1,"character":7}}}Content-Length: 197

{"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///tmp/lsp-tombstone-test/src/Zeta.php","version":2},"contentChanges":[{"text":"<?php\nnamespace App;\n"}]}}Content-Length: 162

{"jsonrpc":"2.0","method":"textDocument/didSave","params":{"textDocument":{"uri":"file:///tmp/lsp-tombstone-test/src/Zeta.php"},"text":"<?php\nnamespace App;\n"}}Content-Length: 171

{"jsonrpc":"2.0","id":3,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-tombstone-test/edit.php"},"position":{"line":1,"character":7}}}Content-Length: 56

{"jsonrpc":"2.0","id":4,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-tombstone-test';
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
$root = '/tmp/lsp-tombstone-test';
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

%A"jsonrpc":"2.0","id":2,"result"%A"label":"ZetaGone"%A"detail":"class App\\ZetaGone"%AContent-Length: %d

%A"jsonrpc":"2.0","id":3,"result":{"isIncomplete":false,"items":[]}%AContent-Length: %d

{"jsonrpc":"2.0","id":4,"result":null}
