--TEST--
LSP completes and resolves public and protected static parent members for parent::
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 115

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-parent-static-completion-test"}}Content-Length: 348

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-parent-static-completion-test/src/Child.php","languageId":"php","version":1,"text":"<?php\nnamespace ParentStaticFixture;\n\nfinal class Child extends Base\n{\n    public function run(): void\n    {\n        parent::protectedStatic();\n    }\n}\n"}}}Content-Length: 192

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-parent-static-completion-test/src/Child.php"},"position":{"line":7,"character":16}}}Content-Length: 192

{"jsonrpc":"2.0","id":3,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///tmp/lsp-parent-static-completion-test/src/Child.php"},"position":{"line":7,"character":20}}}Content-Length: 56

{"jsonrpc":"2.0","id":4,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-parent-static-completion-test';
@mkdir($root . '/src', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);
file_put_contents($root . '/src/Base.php', <<<'PHP'
<?php
namespace ParentStaticFixture;

abstract class Base
{
    public static string $publicName;
    protected static string $protectedName;
    private static string $privateName;

    public static function publicStatic(): void
    {
    }

    protected static function protectedStatic(): void
    {
    }

    private static function privateStatic(): void
    {
    }

    public function instanceOnly(): void
    {
    }
}
PHP);
file_put_contents($root . '/src/Child.php', <<<'PHP'
<?php
namespace ParentStaticFixture;

final class Child extends Base
{
    public function run(): void
    {
        parent::protectedStatic();
    }
}
PHP);
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'ParentStaticFixture\\\\Base' => " . var_export($root . '/src/Base.php', true) . ",\n    'ParentStaticFixture\\\\Child' => " . var_export($root . '/src/Child.php', true) . ",\n];\n");
LSParrot\start_lsp([
    'analyzer' => 'lsparrot',
    'workers' => ['count' => 0],
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-parent-static-completion-test';
if (is_dir($root)) {
    $it = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($root, FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::CHILD_FIRST);
    foreach ($it as $file) {
        $file->isDir() ? @rmdir($file->getPathname()) : @unlink($file->getPathname());
    }
    @rmdir($root);
}
?>
--EXPECTREGEX--
[\s\S]*"jsonrpc":"2\.0","id":2,"result"[\s\S]*"label":"publicStatic"[\s\S]*"label":"protectedStatic"[\s\S]*"label":"\$publicName"[\s\S]*"label":"\$protectedName"(?![\s\S]*"label":"privateStatic")(?![\s\S]*"label":"\$privateName")(?![\s\S]*"label":"instanceOnly")[\s\S]*"jsonrpc":"2\.0","id":3,"result"[\s\S]*"uri":"file:\/\/\/tmp\/lsp-parent-static-completion-test\/src\/Base\.php"[\s\S]*"line":13[\s\S]*\{"jsonrpc":"2\.0","id":4,"result":null\}
