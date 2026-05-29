--TEST--
LSP shows inherited method parameter signatures in signature help
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 105

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-signature-help-test"}}Content-Length: 376

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-signature-help-test/src/Child.php","languageId":"php","version":1,"text":"<?php\nnamespace SignatureHelpFixture;\n\nfinal class Child extends ParentBase\n{\n    public function test(): void\n    {\n        $this->forOverride(\n        $this->forOverride('name', \n    }\n}\n"}}}Content-Length: 185

{"jsonrpc":"2.0","id":2,"method":"textDocument/signatureHelp","params":{"textDocument":{"uri":"file:///tmp/lsp-signature-help-test/src/Child.php"},"position":{"line":7,"character":27}}}Content-Length: 185

{"jsonrpc":"2.0","id":3,"method":"textDocument/signatureHelp","params":{"textDocument":{"uri":"file:///tmp/lsp-signature-help-test/src/Child.php"},"position":{"line":8,"character":35}}}Content-Length: 56

{"jsonrpc":"2.0","id":4,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-signature-help-test';
@mkdir($root . '/vendor/composer', 0777, true);
@mkdir($root . '/src', 0777, true);
file_put_contents($root . '/src/ParentBase.php', <<<'PHP'
<?php
namespace SignatureHelpFixture;

class ParentBase
{
    public function forOverride(string $name, int $count = 0): void
    {
    }
}
PHP);
file_put_contents($root . '/src/Child.php', <<<'PHP'
<?php
namespace SignatureHelpFixture;

final class Child extends ParentBase
{
}
PHP);
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'SignatureHelpFixture\\\\ParentBase' => " . var_export($root . '/src/ParentBase.php', true) . ",\n    'SignatureHelpFixture\\\\Child' => " . var_export($root . '/src/Child.php', true) . ",\n];\n");
LSParrot\start_lsp([
    'analyzer' => 'lsparrot',
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-signature-help-test';
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

%A"jsonrpc":"2.0","id":3,"result"%A"label":"forOverride(string $name, int $count = 0): void"%A"parameters":[{"label":"string $name"},{"label":"int $count = 0"}]%A"activeParameter":1%AContent-Length: %d

{"jsonrpc":"2.0","id":4,"result":null}
