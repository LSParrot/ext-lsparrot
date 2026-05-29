--TEST--
LSP shows parent method signatures while declaring an override method
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 114

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-override-signature-help-test"}}Content-Length: 313

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-override-signature-help-test/src/Child.php","languageId":"php","version":1,"text":"<?php\nnamespace OverrideSignatureHelpFixture;\n\nfinal class Child extends ParentBase\n{\n    public function forOverride(\n}\n"}}}Content-Length: 194

{"jsonrpc":"2.0","id":2,"method":"textDocument/signatureHelp","params":{"textDocument":{"uri":"file:///tmp/lsp-override-signature-help-test/src/Child.php"},"position":{"line":5,"character":32}}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-override-signature-help-test';
@mkdir($root . '/vendor/composer', 0777, true);
@mkdir($root . '/src', 0777, true);
file_put_contents($root . '/src/ParentBase.php', <<<'PHP'
<?php
namespace OverrideSignatureHelpFixture;

class ParentBase
{
    public function forOverride(string $name, int $count = 0): void
    {
    }
}
PHP);
file_put_contents($root . '/src/Child.php', <<<'PHP'
<?php
namespace OverrideSignatureHelpFixture;

final class Child extends ParentBase
{
}
PHP);
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'OverrideSignatureHelpFixture\\\\ParentBase' => " . var_export($root . '/src/ParentBase.php', true) . ",\n    'OverrideSignatureHelpFixture\\\\Child' => " . var_export($root . '/src/Child.php', true) . ",\n];\n");
LSParrot\start_lsp([
    'analyzer' => 'lsparrot',
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-override-signature-help-test';
@unlink($root . '/vendor/composer/autoload_classmap.php');
@unlink($root . '/src/ParentBase.php');
@unlink($root . '/src/Child.php');
@rmdir($root . '/vendor/composer');
@rmdir($root . '/vendor');
@rmdir($root . '/src');
@rmdir($root);
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%A"label":"forOverride(string $name, int $count = 0): void"%A"parameters":[{"label":"string $name"},{"label":"int $count = 0"}]%A"activeParameter":0%AContent-Length: %d

{"jsonrpc":"2.0","id":3,"result":null}
