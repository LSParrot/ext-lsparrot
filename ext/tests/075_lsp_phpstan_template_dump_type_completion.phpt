--TEST--
LSP uses lsparrot and PHPStan template types for method-template variables
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 112

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-phpstan-template-type-test"}}Content-Length: 644

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-phpstan-template-type-test/src/Service/Demo.php","languageId":"php","version":1,"text":"<?php\n\nnamespace PhpstanTemplateFixture\\Service;\n\nuse PhpstanTemplateFixture\\Domain\\User;\n\nfinal class Demo\n{\n    /**\n     * @template T\n     * @param T $value\n     * @return T\n     */\n    public function templateTest(mixed $value): mixed\n    {\n        return $value;\n    }\n\n    public function run(): void\n    {\n        $user = new User();\n        $val = $this->templateTest($user);\n        $val;\n        $val->actual\n    }\n}"}}}Content-Length: 192

{"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-phpstan-template-type-test/src/Service/Demo.php"},"position":{"line":22,"character":10}}}Content-Length: 197

{"jsonrpc":"2.0","id":3,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-phpstan-template-type-test/src/Service/Demo.php"},"position":{"line":23,"character":20}}}Content-Length: 56

{"jsonrpc":"2.0","id":4,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-phpstan-template-type-test';
$demo = <<<'PHP'
<?php

namespace PhpstanTemplateFixture\Service;

use PhpstanTemplateFixture\Domain\User;

final class Demo
{
    /**
     * @template T
     * @param T $value
     * @return T
     */
    public function templateTest(mixed $value): mixed
    {
        return $value;
    }

    public function run(): void
    {
        $user = new User();
        $val = $this->templateTest($user);
        $val;
        $val->actual
    }
}
PHP;

@mkdir($root . '/src/Domain', 0777, true);
@mkdir($root . '/src/Service', 0777, true);
@mkdir($root . '/vendor/bin', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);
file_put_contents($root . '/composer.json', json_encode([
    'autoload' => [
        'psr-4' => [
            'PhpstanTemplateFixture\\' => 'src/',
        ],
    ],
], JSON_PRETTY_PRINT));
file_put_contents($root . '/src/Domain/User.php', <<<'PHP'
<?php

namespace PhpstanTemplateFixture\Domain;

final class User
{
    public function actualUserMethod(): void
    {
    }
}
PHP);
file_put_contents($root . '/src/Service/Demo.php', $demo);
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'PhpstanTemplateFixture\\\\Domain\\\\User' => " . var_export($root . '/src/Domain/User.php', true) . ",\n    'PhpstanTemplateFixture\\\\Service\\\\Demo' => " . var_export($root . '/src/Service/Demo.php', true) . ",\n];\n");
file_put_contents($root . '/vendor/bin/phpstan', <<<'PHP'
#!/usr/bin/env php
<?php
$file = '';
foreach ($argv as $arg) {
    if (is_file($arg) && str_contains($arg, '/.lsparrot/shadow/phpstan-type/')) {
        $file = $arg;
        break;
    }
}

if ($file === '') {
    echo json_encode(['files' => [], 'errors' => []]);
    exit(0);
}

$line = 1;
foreach (file($file) ?: [] as $i => $text) {
    if (str_contains($text, '\\PHPStan\\dumpType(')) {
        $line = $i + 1;
        break;
    }
}

echo json_encode([
    'totals' => ['errors' => 0, 'file_errors' => 1],
    'files' => [
        $file => [
            'errors' => 1,
            'messages' => [[
                'message' => 'Dumped type: PhpstanTemplateFixture\\Domain\\User',
                'line' => $line,
                'ignorable' => false,
                'identifier' => 'phpstan.dumpType',
            ]],
        ],
    ],
    'errors' => [],
]);
fwrite(STDERR, "Instructions for interpreting errors\n");
exit(1);
PHP);
chmod($root . '/vendor/bin/phpstan', 0755);
LSParrot\start_lsp([
    'analyzer' => 'phpstan',
    'workers' => ['count' => 0, 'analyzerDiagnosticsTimeout' => 5],
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-phpstan-template-type-test';
if (is_dir($root)) {
    $it = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($root, FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::CHILD_FIRST);
    foreach ($it as $file) {
        $file->isDir() ? @rmdir($file->getPathname()) : @unlink($file->getPathname());
    }
    @rmdir($root);
}
?>
--EXPECTREGEX--
[\s\S]*"method":"lsparrot.php\/analyzerStatus"[\s\S]*"driver":"lsparrot\+phpstan"[\s\S]*"jsonrpc":"2\.0","id":2,"result"[\s\S]*`User`\\n\\nLSParrot Engine[\s\S]*PhpstanTemplateFixture\\\\Domain\\\\User[\s\S]*PHPStan[\s\S]*"jsonrpc":"2\.0","id":3,"result"[\s\S]*"label":"actualUserMethod"[\s\S]*"source":"phpstan"[\s\S]*\{"jsonrpc":"2\.0","id":4,"result":null\}
