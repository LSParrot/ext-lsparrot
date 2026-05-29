--TEST--
LSP resolves PHPDoc import-type utilities, shape attributes, object shapes, and range formatting
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 112

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-phpdoc-advanced-shape-test"}}Content-Length: 1131

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-phpdoc-advanced-shape-test/file.php","languageId":"php","version":1,"text":"<?php\nnamespace ShapeFixture;\n\n/**\n * @phpstan-type ExternalPayload array{token: non-empty-string, count: positive-int}\n */\nfinal class ShapeTypes\n{\n}\n\n/**\n * @phpstan-import-type ExternalPayload from ShapeTypes as ImportedPayload\n * @phpstan-type LocalObject object{name: non-empty-string, score: positive-int}\n * @var ImportedPayload $payload\n * @var key-of<ImportedPayload> $payloadKey\n * @var value-of<ImportedPayload> $payloadValue\n * @var LocalObject $objectPayload\n */\n$payload = [];\n$payloadKey;\n$payloadValue;\n$payload['to'];\n$objectPayload->na;\n\nfinal class ShapeProvider\n{\n    #[ArrayShape(['title' => 'non-empty-string', 'amount' => 'positive-int'])]\n    public function payload(): array\n    {\n        return [];\n    }\n\n    public function run(): void\n    {\n        $attrPayload = $this->payload();\n        $attrPayload['ti'];\n    }\n}\n\nfunction trim_me(): void\n{\n    $x = 1;    \n    $y = 2;\t\t\n}\n"}}}Content-Length: 179

{"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-phpdoc-advanced-shape-test/file.php"},"position":{"line":19,"character":2}}}Content-Length: 179

{"jsonrpc":"2.0","id":3,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-phpdoc-advanced-shape-test/file.php"},"position":{"line":20,"character":2}}}Content-Length: 185

{"jsonrpc":"2.0","id":4,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-phpdoc-advanced-shape-test/file.php"},"position":{"line":21,"character":12}}}Content-Length: 185

{"jsonrpc":"2.0","id":5,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-phpdoc-advanced-shape-test/file.php"},"position":{"line":22,"character":18}}}Content-Length: 185

{"jsonrpc":"2.0","id":6,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-phpdoc-advanced-shape-test/file.php"},"position":{"line":35,"character":24}}}Content-Length: 272

{"jsonrpc":"2.0","id":7,"method":"textDocument/rangeFormatting","params":{"textDocument":{"uri":"file:///tmp/lsp-phpdoc-advanced-shape-test/file.php"},"range":{"start":{"line":39,"character":0},"end":{"line":44,"character":0}},"options":{"tabSize":4,"insertSpaces":true}}}Content-Length: 56

{"jsonrpc":"2.0","id":8,"method":"shutdown","params":[]}
--FILE--
<?php
LSParrot\start_lsp(['analyzer' => 'lsparrot']);
?>
--EXPECTF--
%A"jsonrpc":"2.0","id":1,"result"%A"documentRangeFormattingProvider":true%AContent-Length: %d

%A"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%Atoken|count%ALSParrot Engine%AContent-Length: %d

%A"jsonrpc":"2.0","id":3,"result"%Astring|int%ALSParrot Engine%AContent-Length: %d

%A"jsonrpc":"2.0","id":4,"result"%A"label":"token"%A"detail":"array-shape key"%AContent-Length: %d

%A"jsonrpc":"2.0","id":5,"result"%A"label":"name"%A"detail":"object-shape property: non-empty-string"%AContent-Length: %d

%A"jsonrpc":"2.0","id":6,"result"%A"label":"title"%A"detail":"array-shape key"%AContent-Length: %d

%A"jsonrpc":"2.0","id":7,"result":[%A"newText":"function trim_me(): void\n{\n    $x = 1;\n    $y = 2;\n}\n"%A]%A
{"jsonrpc":"2.0","id":8,"result":null}
