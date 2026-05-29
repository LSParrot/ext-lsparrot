--TEST--
LSP lsparrot server completes namespaced builtin return types with use imports
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 110

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-return-type-builtin-test"}}Content-Length: 252

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-return-type-builtin-test/file.php","languageId":"php","version":1,"text":"<?php\nnamespace ReturnTypeBuiltinFixture;\nfunction builtin(): Rand\n{\n}\n"}}}Content-Length: 182

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-return-type-builtin-test/file.php"},"position":{"line":2,"character":24}}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
LSParrot\start_lsp([
    'analyzer' => 'lsparrot',
    'workers' => ['count' => 1],
    'symbolIndex' => ['size' => '4M'],
]);
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%A"label":"Randomizer"%A"detail":"class Random\\Randomizer"%A"newText":"Randomizer"%A"additionalTextEdits":[{"range":%A"newText":"%Ause Random\\Randomizer;\n"}]%AContent-Length: %d

{"jsonrpc":"2.0","id":3,"result":null}
