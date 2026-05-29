--TEST--
LSP suggests parent override method stubs while declaring a class method
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 99

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-override-test"}}Content-Length: 273

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-override-test/src/Child.php","languageId":"php","version":1,"text":"<?php\nnamespace OverrideFixture;\nfinal class Child extends ParentBase\n{\n    public function fo\n}\n"}}}Content-Length: 176

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-override-test/src/Child.php"},"position":{"line":4,"character":22}}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-override-test';
@mkdir($root . '/vendor/composer', 0777, true);
@mkdir($root . '/src', 0777, true);
file_put_contents($root . '/src/ParentBase.php', <<<'PHP'
<?php
namespace OverrideFixture;

class ParentBase
{
    public function formatName(string $name): string
    {
        return $name;
    }

    protected function protectMe(int $id): void
    {
    }

    final public function locked(): void
    {
    }
}
PHP);
file_put_contents($root . '/src/Child.php', <<<'PHP'
<?php
namespace OverrideFixture;

final class Child extends ParentBase
{
}
PHP);
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'OverrideFixture\\\\ParentBase' => " . var_export($root . '/src/ParentBase.php', true) . ",\n    'OverrideFixture\\\\Child' => " . var_export($root . '/src/Child.php', true) . ",\n];\n");
LSParrot\start_lsp([
    'analyzer' => 'lsparrot',
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-override-test';
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

%A"method":"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result":{"isIncomplete":false,"items":[{"label":"formatName","kind":2,"detail":"override OverrideFixture\\ParentBase::formatName(string $name): string","filterText":"formatName","sortText":"0000:formatName","insertTextFormat":2,%A"newText":"formatName(string \\$name): string\n{\n    $0\n}"%AContent-Length: %d

{"jsonrpc":"2.0","id":3,"result":null}
