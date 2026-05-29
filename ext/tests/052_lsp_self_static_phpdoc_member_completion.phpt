--TEST--
LSP completes self/static members and PHPDoc pseudo members
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 102

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-self-static-test"}}Content-Length: 832

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-self-static-test/tests/Unit/ExampleTest.php","languageId":"php","version":1,"text":"<?php\nnamespace StaticSelfFixture\\Tests;\n\nuse StaticSelfFixture\\Framework\\TestCase;\n\n/**\n * @method static void magicStaticMethod(string $name)\n * @method void instanceMagicMethod()\n * @property static string $magicStaticName\n * @property string $magicInstanceName\n */\nfinal class ExampleTest extends TestCase\n{\n    private static string $localStaticName;\n    public static function localStaticHelper(int $count): void {}\n\n    public function test(): void\n    {\n        self::localSta\n        static::assertTr\n        self::magicSta\n        self::$magicSta\n        $this->magicInst\n        $this->parentMagic\n    }\n}\n"}}}Content-Length: 193

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-self-static-test/tests/Unit/ExampleTest.php"},"position":{"line":18,"character":22}}}Content-Length: 193

{"jsonrpc":"2.0","id":3,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-self-static-test/tests/Unit/ExampleTest.php"},"position":{"line":19,"character":24}}}Content-Length: 193

{"jsonrpc":"2.0","id":4,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-self-static-test/tests/Unit/ExampleTest.php"},"position":{"line":20,"character":22}}}Content-Length: 193

{"jsonrpc":"2.0","id":5,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-self-static-test/tests/Unit/ExampleTest.php"},"position":{"line":21,"character":23}}}Content-Length: 193

{"jsonrpc":"2.0","id":6,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-self-static-test/tests/Unit/ExampleTest.php"},"position":{"line":22,"character":24}}}Content-Length: 193

{"jsonrpc":"2.0","id":7,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-self-static-test/tests/Unit/ExampleTest.php"},"position":{"line":23,"character":26}}}Content-Length: 56

{"jsonrpc":"2.0","id":8,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-self-static-test';
@mkdir($root . '/tests/Unit', 0777, true);
@mkdir($root . '/src/Framework', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);
file_put_contents($root . '/tests/Unit/ExampleTest.php', <<<'PHP'
<?php
namespace StaticSelfFixture\Tests;

use StaticSelfFixture\Framework\TestCase;

final class ExampleTest extends TestCase
{
}
PHP);
file_put_contents($root . '/src/Framework/TestCase.php', <<<'PHP'
<?php
namespace StaticSelfFixture\Framework;

/**
 * @method static void parentMagicStatic(string $name)
 * @property string $parentMagicName
 */
class TestCase extends AssertBase
{
}
PHP);
file_put_contents($root . '/src/Framework/AssertBase.php', <<<'PHP'
<?php
namespace StaticSelfFixture\Framework;

class AssertBase
{
    final public static function assertTrue(bool $condition, string $message = ''): void
    {
    }
}
PHP);
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'StaticSelfFixture\\\\Tests\\\\ExampleTest' => " . var_export($root . '/tests/Unit/ExampleTest.php', true) . ",\n    'StaticSelfFixture\\\\Framework\\\\TestCase' => " . var_export($root . '/src/Framework/TestCase.php', true) . ",\n    'StaticSelfFixture\\\\Framework\\\\AssertBase' => " . var_export($root . '/src/Framework/AssertBase.php', true) . ",\n];\n");
LSParrot\start_lsp([
    'analyzer' => 'lsparrot',
    'workers' => ['count' => 0],
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-self-static-test';
@unlink($root . '/vendor/composer/autoload_classmap.php');
@unlink($root . '/src/Framework/AssertBase.php');
@unlink($root . '/src/Framework/TestCase.php');
@unlink($root . '/tests/Unit/ExampleTest.php');
@rmdir($root . '/vendor/composer');
@rmdir($root . '/vendor');
@rmdir($root . '/src/Framework');
@rmdir($root . '/src');
@rmdir($root . '/tests/Unit');
@rmdir($root . '/tests');
@rmdir($root);
?>
--EXPECTF--
%A"jsonrpc":"2.0","id":1,"result"%A"jsonrpc":"2.0","id":2,"result"%A"label":"localStaticHelper"%A"kind":2%A"detail":"localStaticHelper(int $count): void"%A"insertText":"localStaticHelper($0)"%A"jsonrpc":"2.0","id":3,"result"%A"label":"assertTrue"%A"kind":2%A"detail":"assertTrue(bool $condition, string $message = ''): void"%A"insertText":"assertTrue($0)"%A"jsonrpc":"2.0","id":4,"result"%A"label":"magicStaticMethod"%A"kind":2%A"detail":"magicStaticMethod(string $name): void"%A"insertText":"magicStaticMethod($0)"%A"jsonrpc":"2.0","id":5,"result"%A"label":"$magicStaticName"%A"kind":10%A"detail":"property string $magicStaticName"%A"filterText":"$magicStaticName"%A"jsonrpc":"2.0","id":6,"result"%A"label":"magicInstanceName"%A"kind":10%A"detail":"property string $magicInstanceName"%A"jsonrpc":"2.0","id":7,"result"%A"label":"parentMagicName"%A"kind":10%A"detail":"property string $parentMagicName"%A"jsonrpc":"2.0","id":8,"result":null}
