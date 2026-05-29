--TEST--
LSP shows override locations through code lens on base methods
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 109

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-code-lens-override-test"}}Content-Length: 413

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-code-lens-override-test/src/Base.php","languageId":"php","version":1,"text":"<?php\nnamespace CodeLensOverrideFixture;\n\nclass Base\n{\n    public function target(): void\n    {\n    }\n\n    private function privateTarget(): void\n    {\n    }\n\n    final public function finalTarget(): void\n    {\n    }\n}"}}}Content-Length: 146

{"jsonrpc":"2.0","id":2,"method":"textDocument/codeLens","params":{"textDocument":{"uri":"file:///tmp/lsp-code-lens-override-test/src/Base.php"}}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-code-lens-override-test';
@mkdir($root . '/vendor/composer', 0777, true);
@mkdir($root . '/src', 0777, true);
file_put_contents($root . '/src/Base.php', <<<'PHP'
<?php
namespace CodeLensOverrideFixture;

class Base
{
    public function target(): void
    {
    }

    private function privateTarget(): void
    {
    }

    final public function finalTarget(): void
    {
    }
}
PHP);
file_put_contents($root . '/src/ChildOne.php', <<<'PHP'
<?php
namespace CodeLensOverrideFixture;

class ChildOne extends Base
{
    public function target(): void
    {
    }
}
PHP);
file_put_contents($root . '/src/Middle.php', <<<'PHP'
<?php
namespace CodeLensOverrideFixture;

class Middle extends Base
{
    public function target(): void
    {
    }
}
PHP);
file_put_contents($root . '/src/GrandChild.php', <<<'PHP'
<?php
namespace CodeLensOverrideFixture;

class GrandChild extends Middle
{
    public function target(): void
    {
    }
}
PHP);
file_put_contents($root . '/src/Other.php', <<<'PHP'
<?php
namespace CodeLensOverrideFixture;

class Other
{
    public function target(): void
    {
    }
}
PHP);
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'CodeLensOverrideFixture\\\\Base' => " . var_export($root . '/src/Base.php', true) . ",\n    'CodeLensOverrideFixture\\\\ChildOne' => " . var_export($root . '/src/ChildOne.php', true) . ",\n    'CodeLensOverrideFixture\\\\Middle' => " . var_export($root . '/src/Middle.php', true) . ",\n    'CodeLensOverrideFixture\\\\GrandChild' => " . var_export($root . '/src/GrandChild.php', true) . ",\n    'CodeLensOverrideFixture\\\\Other' => " . var_export($root . '/src/Other.php', true) . ",\n];\n");
LSParrot\start_lsp([
    'analyzer' => 'lsparrot',
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-code-lens-override-test';
@unlink($root . '/vendor/composer/autoload_classmap.php');
@unlink($root . '/src/Base.php');
@unlink($root . '/src/ChildOne.php');
@unlink($root . '/src/Middle.php');
@unlink($root . '/src/GrandChild.php');
@unlink($root . '/src/Other.php');
@rmdir($root . '/vendor/composer');
@rmdir($root . '/vendor');
@rmdir($root . '/src');
@rmdir($root);
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%A"codeLensProvider":{"resolveProvider":false}%AContent-Length: %d

%A"method":"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result":[{"range":{"start":{"line":5,"character":20},"end":{"line":5,"character":26}},"command":{"title":"3 overrides","command":"lsparrot.showReferences","arguments":["file:///tmp/lsp-code-lens-override-test/src/Base.php",{"line":5,"character":20},[%A"uri":"file:///tmp/lsp-code-lens-override-test/src/ChildOne.php"%A"uri":"file:///tmp/lsp-code-lens-override-test/src/Middle.php"%A"uri":"file:///tmp/lsp-code-lens-override-test/src/GrandChild.php"%A]]}}]%AContent-Length: %d

{"jsonrpc":"2.0","id":3,"result":null}
