--TEST--
LSP lsparrot server completes $this methods inherited from parent classes
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 102

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-this-parent-test"}}Content-Length: 377

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-this-parent-test/src/Child.php","languageId":"php","version":1,"text":"<?php\nnamespace ThisParentFixture;\n\nfinal class Child extends ParentBase\n{\n    public function childOnly(): void\n    {\n    }\n\n    public function test(): void\n    {\n        $this->par\n    }\n}"}}}Content-Length: 180

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-this-parent-test/src/Child.php"},"position":{"line":11,"character":18}}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-this-parent-test';
@mkdir($root . '/vendor/composer', 0777, true);
@mkdir($root . '/src', 0777, true);
file_put_contents($root . '/src/RootBase.php', <<<'PHP'
<?php
namespace ThisParentFixture;

class RootBase
{
    public function parentFromRoot(): void
    {
    }
}
PHP);
file_put_contents($root . '/src/GrandBase.php', <<<'PHP'
<?php
namespace ThisParentFixture;

class GrandBase extends RootBase
{
    public function parentFromGrand(): void
    {
    }
}
PHP);
file_put_contents($root . '/src/ParentBase.php', <<<'PHP'
<?php
namespace ThisParentFixture;

class ParentBase extends GrandBase
{
    public function parentOnly(): void
    {
    }

    public function parentWithArgs(string $name): string
    {
        return $name;
    }

    protected function parentProtected(): void
    {
    }
}
PHP);
file_put_contents($root . '/src/Child.php', <<<'PHP'
<?php
namespace ThisParentFixture;

final class Child extends ParentBase
{
}
PHP);
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'ThisParentFixture\\\\RootBase' => " . var_export($root . '/src/RootBase.php', true) . ",\n    'ThisParentFixture\\\\GrandBase' => " . var_export($root . '/src/GrandBase.php', true) . ",\n    'ThisParentFixture\\\\ParentBase' => " . var_export($root . '/src/ParentBase.php', true) . ",\n    'ThisParentFixture\\\\Child' => " . var_export($root . '/src/Child.php', true) . ",\n];\n");
LSParrot\start_lsp([
    'analyzer' => 'lsparrot',
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-this-parent-test';
@unlink($root . '/vendor/composer/autoload_classmap.php');
@unlink($root . '/src/RootBase.php');
@unlink($root . '/src/GrandBase.php');
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

%A"jsonrpc":"2.0","id":2,"result"%A"label":"parentOnly"%A"detail":"parentOnly(): void"%A"source":"lsparrot"%A"label":"parentWithArgs"%A"detail":"parentWithArgs(string $name): string"%A"source":"lsparrot"%A"label":"parentProtected"%A"detail":"parentProtected(): void"%A"source":"lsparrot"%A"label":"parentFromGrand"%A"detail":"parentFromGrand(): void"%A"source":"lsparrot"%A"label":"parentFromRoot"%A"detail":"parentFromRoot(): void"%A"source":"lsparrot"%AContent-Length: %d

{"jsonrpc":"2.0","id":3,"result":null}
