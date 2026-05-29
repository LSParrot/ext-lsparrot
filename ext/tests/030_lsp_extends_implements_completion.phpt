--TEST--
LSP completes known classes and interfaces after extends and implements
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 112

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-constraint-completion-test"}}Content-Length: 320

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-constraint-completion-test/src/App/Child.php","languageId":"php","version":1,"text":"<?php\n\nnamespace ConstraintFixture\\App;\n\nfinal class Child extends Shared\n{\n}\n\nfinal class Service implements Shared\n{\n}\n"}}}Content-Length: 193

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-constraint-completion-test/src/App/Child.php"},"position":{"line":4,"character":32}}}Content-Length: 193

{"jsonrpc":"2.0","id":3,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-constraint-completion-test/src/App/Child.php"},"position":{"line":8,"character":37}}}Content-Length: 56

{"jsonrpc":"2.0","id":4,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-constraint-completion-test';
@mkdir($root . '/src/App', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);
file_put_contents($root . '/src/App/SharedParent.php', "<?php\nnamespace ConstraintFixture\\App;\nabstract class SharedParent {}\n");
file_put_contents($root . '/src/App/SharedContract.php', "<?php\nnamespace ConstraintFixture\\App;\ninterface SharedContract {}\n");
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'ConstraintFixture\\\\App\\\\SharedParent' => " . var_export($root . '/src/App/SharedParent.php', true) . ",\n    'ConstraintFixture\\\\App\\\\SharedContract' => " . var_export($root . '/src/App/SharedContract.php', true) . ",\n];\n");
LSParrot\start_lsp([
    'analyzer' => 'lsparrot',
    'workers' => ['count' => 1],
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-constraint-completion-test';
@unlink($root . '/vendor/composer/autoload_classmap.php');
@unlink($root . '/src/App/SharedContract.php');
@unlink($root . '/src/App/SharedParent.php');
@rmdir($root . '/vendor/composer');
@rmdir($root . '/vendor');
@rmdir($root . '/src/App');
@rmdir($root . '/src');
@rmdir($root);
?>
--EXPECTREGEX--
[\s\S]*"jsonrpc":"2\.0","id":1,"result"[\s\S]*"textDocument\/publishDiagnostics"[\s\S]*"jsonrpc":"2\.0","id":2,"result":\{"isIncomplete":false,"items":\[\{"label":"SharedParent"(?:(?!\},\{)[\s\S])*"detail":"class ConstraintFixture\\\\App\\\\SharedParent"(?:(?!\},\{)[\s\S])*\}\]\}[\s\S]*"jsonrpc":"2\.0","id":3,"result":\{"isIncomplete":false,"items":\[\{"label":"SharedContract"(?:(?!\},\{)[\s\S])*"detail":"interface ConstraintFixture\\\\App\\\\SharedContract"(?:(?!\},\{)[\s\S])*\}\]\}[\s\S]*\{"jsonrpc":"2\.0","id":4,"result":null\}
