--TEST--
LSP lsparrot server completes and hovers PHPDoc generics, templates, and array shapes
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 97

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-phpdoc-test"}}Content-Length: 455

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-phpdoc-test/file.php","languageId":"php","version":1,"text":"<?php\n/**\n * @template TItem\n * @param array{foo: positive-int, 'bar-baz': non-empty-string} $shape\n * @param list<TItem> $items\n */\nfunction consume(array $shape, array $items): void {\n    /** @var Collection<User> $users */\n    $users = [];\n    $shape['ba\n    // $u\n    // T\n}\n"}}}Content-Length: 169

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-phpdoc-test/file.php"},"position":{"line":9,"character":14}}}Content-Length: 169

{"jsonrpc":"2.0","id":3,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-phpdoc-test/file.php"},"position":{"line":10,"character":9}}}Content-Length: 169

{"jsonrpc":"2.0","id":4,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-phpdoc-test/file.php"},"position":{"line":11,"character":8}}}Content-Length: 163

{"jsonrpc":"2.0","id":5,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-phpdoc-test/file.php"},"position":{"line":8,"character":6}}}Content-Length: 56

{"jsonrpc":"2.0","id":6,"method":"shutdown","params":[]}
--FILE--
<?php
LSParrot\start_lsp(['analyzer' => 'lsparrot']);
?>
--EXPECTF--
%A"method":"lsparrot.php/analyzerStatus"%A"analyzer":"index"%AContent-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%A"label":"bar-baz"%A"detail":"array-shape key"%AContent-Length: %d

%A"jsonrpc":"2.0","id":3,"result"%A"label":"$users"%A"detail":"variable $users: Collection<User>"%A"filterText":"$users"%AContent-Length: %d

%A"jsonrpc":"2.0","id":4,"result"%A"label":"TItem"%A"detail":"template type"%AContent-Length: %d

%A"jsonrpc":"2.0","id":5,"result"%ACollection<User>%AContent-Length: %d

{"jsonrpc":"2.0","id":6,"result":null}
