--TEST--
LSP resolves local PHPDoc type aliases for hover and array-shape completion
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 103

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-phpdoc-alias-test"}}Content-Length: 332

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-phpdoc-alias-test/file.php","languageId":"php","version":1,"text":"<?php\n/**\n * @phpstan-type Payload array{user_id: positive-int, name: non-empty-string}\n * @var Payload $payload\n */\n$payload = [];\n$payload;\n$payload['na\n"}}}Content-Length: 169

{"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-phpdoc-alias-test/file.php"},"position":{"line":6,"character":2}}}Content-Length: 175

{"jsonrpc":"2.0","id":3,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-phpdoc-alias-test/file.php"},"position":{"line":7,"character":12}}}Content-Length: 56

{"jsonrpc":"2.0","id":4,"method":"shutdown","params":[]}
--FILE--
<?php
LSParrot\start_lsp(['analyzer' => 'lsparrot']);
?>
--EXPECTF--
%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%Aarray{user_id: int, name: string}%ALSParrot Engine%AContent-Length: %d

%A"jsonrpc":"2.0","id":3,"result"%A"label":"name"%A"detail":"array-shape key"%AContent-Length: %d

{"jsonrpc":"2.0","id":4,"result":null}
