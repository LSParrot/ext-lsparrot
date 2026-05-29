--TEST--
LSP indexes files reported by workspace/didChangeWatchedFiles without a restart
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 104

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-watched-index-test"}}Content-Length: 185

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-watched-index-test/edit.php","languageId":"php","version":1,"text":"<?php\nHiddenGe"}}}Content-Length: 175

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-watched-index-test/edit.php"},"position":{"line":1,"character":8}}}Content-Length: 154

{"jsonrpc":"2.0","method":"workspace/didChangeWatchedFiles","params":{"changes":[{"uri":"file:///tmp/lsp-watched-index-test/extra/Hidden.php","type":1}]}}Content-Length: 175

{"jsonrpc":"2.0","id":3,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-watched-index-test/edit.php"},"position":{"line":1,"character":8}}}Content-Length: 56

{"jsonrpc":"2.0","id":4,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-watched-index-test';
@mkdir($root . '/vendor/composer', 0777, true);
@mkdir($root . '/extra', 0777, true);
file_put_contents($root . '/vendor/composer/autoload_psr4.php', "<?php\nreturn [];\n");
file_put_contents($root . '/extra/Hidden.php', "<?php\nnamespace Extra;\nfinal class HiddenGem {}\n");
LSParrot\start_lsp([
    'analyzer' => 'lsparrot',
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-watched-index-test';
@unlink($root . '/vendor/composer/autoload_psr4.php');
@unlink($root . '/extra/Hidden.php');
@unlink($root . '/.lsparrot/lsparrot-index.bin');
@rmdir($root . '/.lsparrot');
@rmdir($root . '/vendor/composer');
@rmdir($root . '/vendor');
@rmdir($root . '/extra');
@rmdir($root);
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":2,"result":{"isIncomplete":false,"items":[]}%AContent-Length: %d

%A"jsonrpc":"2.0","id":3,"result"%A"label":"HiddenGem"%A"detail":"class Extra\\HiddenGem"%AContent-Length: %d

{"jsonrpc":"2.0","id":4,"result":null}
