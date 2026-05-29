--TEST--
LSP offers analyzer quick fixes: @var/assert type guarantees and suppression comments
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 99

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-quickfix-test"}}Content-Length: 261

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-quickfix-test/src/Fix.php","languageId":"php","version":1,"text":"<?php\nfunction takesString(string $s): void {}\n$value = getThing();\ntakesString($value);\n"}}}Content-Length: 1019

{"jsonrpc":"2.0","id":2,"method":"textDocument/codeAction","params":{"textDocument":{"uri":"file:///tmp/lsp-quickfix-test/src/Fix.php"},"range":{"start":{"line":3,"character":0},"end":{"line":3,"character":20}},"context":{"diagnostics":[{"source":"phpstan","message":"Parameter #1 $s of function takesString expects string, int given. The value is in $value.","code":"argument.type","severity":2,"range":{"start":{"line":3,"character":0},"end":{"line":3,"character":20}}},{"source":"phpstan","message":"Unknown PHPStan diagnostic","severity":2,"range":{"start":{"line":3,"character":0},"end":{"line":3,"character":20}}},{"source":"psalm","message":"Argument 1 of takesString expects string, int provided","code":"InvalidArgument","severity":2,"range":{"start":{"line":3,"character":0},"end":{"line":3,"character":20}}},{"source":"psalm-ls","message":"Possibly null argument passed to takesString","code":"PossiblyNullArgument","severity":2,"range":{"start":{"line":3,"character":0},"end":{"line":3,"character":20}}}]}}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
@mkdir('/tmp/lsp-quickfix-test/src', 0777, true);
LSParrot\start_lsp(['analyzer' => 'lsparrot']);
?>
--CLEAN--
<?php
@rmdir('/tmp/lsp-quickfix-test/src');
@rmdir('/tmp/lsp-quickfix-test');
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result":[%A"title":"Assume type: @var string $value"%A"newText":"/** @var string $value */\n"%A"title":"Guarantee type: assert(is_string($value))"%A"newText":"assert(is_string($value));\n"%A"title":"Suppress PHPStan: @phpstan-ignore argument.type"%A"newText":"/* @phpstan-ignore argument.type */\n"%A"title":"Suppress PHPStan: @phpstan-ignore-next-line"%A"newText":"/* @phpstan-ignore-next-line */\n"%A"title":"Suppress Psalm: @psalm-suppress InvalidArgument"%A"newText":"/** @psalm-suppress InvalidArgument */\n"%A"title":"Suppress Psalm: @psalm-suppress PossiblyNullArgument"%A"newText":"/** @psalm-suppress PossiblyNullArgument */\n"%AContent-Length: %d

{"jsonrpc":"2.0","id":3,"result":null}
