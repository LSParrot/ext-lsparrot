--TEST--
LSP indexes only root Composer autoload paths for declared project symbols
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 105

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-root-autoload-scope"}}Content-Length: 213

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-root-autoload-scope/src/Demo.php","languageId":"php","version":1,"text":"<?php\nnamespace ScopedAutoload;\nAu\n"}}}Content-Length: 180

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-root-autoload-scope/src/Demo.php"},"position":{"line":2,"character":2}}}Content-Length: 81

{"jsonrpc":"2.0","id":3,"method":"workspace/symbol","params":{"query":"Outside"}}Content-Length: 56

{"jsonrpc":"2.0","id":4,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-root-autoload-scope';
@mkdir($root . '/src', 0777, true);
file_put_contents($root . '/composer.json', json_encode([
    'autoload' => [
        'psr-4' => [
            'ScopedAutoload\\' => 'src/',
        ],
    ],
], JSON_PRETTY_PRINT));
file_put_contents($root . '/src/AutoClass.php', "<?php\nnamespace ScopedAutoload;\nfinal class AutoClass {}\n");
file_put_contents($root . '/OutsideClass.php', "<?php\nnamespace OutsideScope;\nfinal class OutsideClass {}\n");
LSParrot\start_lsp([
    'analyzer' => 'lsparrot',
    'workers' => ['count' => 0],
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-root-autoload-scope';
@unlink($root . '/.lsparrot/lsparrot-index.bin');
@rmdir($root . '/.lsparrot');
@unlink($root . '/src/AutoClass.php');
@unlink($root . '/OutsideClass.php');
@unlink($root . '/composer.json');
@rmdir($root . '/src');
@rmdir($root);
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%A"label":"AutoClass"%A"detail":"class ScopedAutoload\\AutoClass"%A
{"jsonrpc":"2.0","id":3,"result":[]}Content-Length: %d

{"jsonrpc":"2.0","id":4,"result":null}
