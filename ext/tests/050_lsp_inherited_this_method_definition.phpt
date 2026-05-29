--TEST--
LSP definition resolves $this methods inherited through parent classes
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 111

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-inherited-definition-test"}}Content-Length: 405

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-inherited-definition-test/tests/Unit/ExampleTest.php","languageId":"php","version":1,"text":"<?php\nnamespace Tests\\Unit;\n\nuse Vendor\\Framework\\TestCase;\n\nfinal class ExampleTest extends TestCase\n{\n    public function test_it_is_true(): void\n    {\n        $this->assertTrue(true);\n    }\n}\n"}}}Content-Length: 201

{"jsonrpc":"2.0","id":2,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///tmp/lsp-inherited-definition-test/tests/Unit/ExampleTest.php"},"position":{"line":9,"character":17}}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-inherited-definition-test';
@mkdir($root . '/tests/Unit', 0777, true);
@mkdir($root . '/vendor/framework', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);
file_put_contents($root . '/tests/Unit/ExampleTest.php', <<<'PHP'
<?php
namespace Tests\Unit;

use Vendor\Framework\TestCase;

final class ExampleTest extends TestCase
{
}
PHP);
file_put_contents($root . '/vendor/framework/TestCase.php', <<<'PHP'
<?php
namespace Vendor\Framework;
abstract class TestCase extends Assert
{
}
PHP);
file_put_contents($root . '/vendor/framework/Assert.php', <<<'PHP'
<?php
namespace Vendor\Framework;
abstract class Assert
{
    final public static function assertTrue(bool $condition, string $message = ''): void
    {
    }
}
PHP);
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'Tests\\\\Unit\\\\ExampleTest' => " . var_export($root . '/tests/Unit/ExampleTest.php', true) . ",\n    'Vendor\\\\Framework\\\\TestCase' => " . var_export($root . '/vendor/framework/TestCase.php', true) . ",\n    'Vendor\\\\Framework\\\\Assert' => " . var_export($root . '/vendor/framework/Assert.php', true) . ",\n];\n");
LSParrot\start_lsp([
    'analyzer' => 'lsparrot',
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-inherited-definition-test';
@unlink($root . '/vendor/composer/autoload_classmap.php');
@unlink($root . '/vendor/framework/Assert.php');
@unlink($root . '/vendor/framework/TestCase.php');
@unlink($root . '/tests/Unit/ExampleTest.php');
if (is_dir($root . '/.lsparrot')) {
    $it = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($root . '/.lsparrot', FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::CHILD_FIRST);
    foreach ($it as $file) {
        $file->isDir() ? @rmdir($file->getPathname()) : @unlink($file->getPathname());
    }
    @rmdir($root . '/.lsparrot');
}
@rmdir($root . '/vendor/composer');
@rmdir($root . '/vendor/framework');
@rmdir($root . '/vendor');
@rmdir($root . '/tests/Unit');
@rmdir($root . '/tests');
@rmdir($root);
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%A"uri":"file:///tmp/lsp-inherited-definition-test/vendor/framework/Assert.php"%A"line":4%AContent-Length: %d

{"jsonrpc":"2.0","id":3,"result":null}
