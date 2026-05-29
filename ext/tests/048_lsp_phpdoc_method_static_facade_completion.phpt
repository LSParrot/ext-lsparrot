--TEST--
LSP completes Facade-style static methods from class PHPDoc @method tags
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 111

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-phpdoc-method-facade-test"}}Content-Length: 254

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-phpdoc-method-facade-test/routes.php","languageId":"php","version":1,"text":"<?php\nuse PhpdocMethodFixture\\Facades\\Route;\n\nRoute::ge\nRoute::inst\n"}}}Content-Length: 184

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-phpdoc-method-facade-test/routes.php"},"position":{"line":3,"character":9}}}Content-Length: 185

{"jsonrpc":"2.0","id":3,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-phpdoc-method-facade-test/routes.php"},"position":{"line":4,"character":11}}}Content-Length: 56

{"jsonrpc":"2.0","id":4,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-phpdoc-method-facade-test';
@mkdir($root . '/src/Facades', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);
file_put_contents($root . '/composer.json', "{\"name\":\"zeriyoshi/phpdoc-method-facade-test\"}\n");
file_put_contents($root . '/src/Facades/Route.php', <<<'PHP'
<?php
namespace PhpdocMethodFixture\Facades;

/**
 * @method static \PhpdocMethodFixture\Routing\Route get(string $uri, array|string|callable|null $action = null)
 * @method static void resources(array $resources, array $options = [])
 * @method static void model(string $key, string $class)
 * @method \PhpdocMethodFixture\Routing\Route instanceOnly(string $name)
 */
final class Route
{
}
PHP);
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'PhpdocMethodFixture\\\\Facades\\\\Route' => " . var_export($root . '/src/Facades/Route.php', true) . ",\n];\n");
LSParrot\start_lsp([
    'analyzer' => 'lsparrot',
    'workers' => ['count' => 0],
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-phpdoc-method-facade-test';
if (is_dir($root)) {
    $it = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($root, FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::CHILD_FIRST);
    foreach ($it as $file) {
        $file->isDir() ? @rmdir($file->getPathname()) : @unlink($file->getPathname());
    }
    @rmdir($root);
}
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%A"label":"get"%A"kind":2%A"detail":"get(string $uri, array|string|callable|null $action = null): \\PhpdocMethodFixture\\Routing\\Route"%A"insertText":"get($0)"%A"source":"lsparrot"%AContent-Length: %d

{"jsonrpc":"2.0","id":3,"result":{"isIncomplete":false,"items":[]}}%AContent-Length: %d

{"jsonrpc":"2.0","id":4,"result":null}
