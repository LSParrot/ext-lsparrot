--TEST--
LSP queries analyzer template types from assignment RHS and caches PHPStan/Psalm results
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 108

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-analyzer-lhs-type-test"}}Content-Length: 610

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-analyzer-lhs-type-test/src/Service/Demo.php","languageId":"php","version":1,"text":"<?php\n\nnamespace AnalyzerLhsFixture\\Service;\n\nuse AnalyzerLhsFixture\\Domain\\User;\n\nfinal class Demo\n{\n    /**\n     * @template T\n     * @param T $value\n     * @return T\n     */\n    public function templateTest(mixed $value): mixed\n    {\n        return $value;\n    }\n\n    public function run(): void\n    {\n        $user = new User();\n        $val = $this->templateTest($user);\n        $val;\n    }\n}"}}}Content-Length: 188

{"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-analyzer-lhs-type-test/src/Service/Demo.php"},"position":{"line":21,"character":10}}}Content-Length: 188

{"jsonrpc":"2.0","id":3,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-analyzer-lhs-type-test/src/Service/Demo.php"},"position":{"line":21,"character":10}}}Content-Length: 56

{"jsonrpc":"2.0","id":4,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-analyzer-lhs-type-test';

@mkdir($root . '/src/Domain', 0777, true);
@mkdir($root . '/src/Service', 0777, true);
@mkdir($root . '/vendor/bin', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);
file_put_contents($root . '/composer.json', json_encode([
    'autoload' => [
        'psr-4' => [
            'AnalyzerLhsFixture\\' => 'src/',
        ],
    ],
], JSON_PRETTY_PRINT));
file_put_contents($root . '/src/Domain/User.php', <<<'PHP'
<?php

namespace AnalyzerLhsFixture\Domain;

final class User
{
}
PHP);
file_put_contents($root . '/src/Service/Demo.php', <<<'PHP'
<?php

namespace AnalyzerLhsFixture\Service;

use AnalyzerLhsFixture\Domain\User;

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
    }
}
PHP);
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'AnalyzerLhsFixture\\\\Domain\\\\User' => " . var_export($root . '/src/Domain/User.php', true) . ",\n    'AnalyzerLhsFixture\\\\Service\\\\Demo' => " . var_export($root . '/src/Service/Demo.php', true) . ",\n];\n");
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
$contents = file_get_contents($file) ?: '';
$line = 1;
foreach (explode("\n", $contents) as $i => $text) {
    if (str_contains($text, '\\PHPStan\\dumpType(')) {
        $line = $i + 1;
        break;
    }
}
$type = str_contains($contents, '\\PHPStan\\dumpType($this->templateTest($user))')
    ? 'AnalyzerLhsFixture\\Domain\\User'
    : '*ERROR*';
echo json_encode([
    'totals' => ['errors' => 0, 'file_errors' => 1],
    'files' => [
        $file => [
            'errors' => 1,
            'messages' => [[
                'message' => 'Dumped type: ' . $type,
                'line' => $line,
                'ignorable' => false,
                'identifier' => 'phpstan.dumpType',
            ]],
        ],
    ],
    'errors' => [],
]);
exit(1);
PHP);
file_put_contents($root . '/vendor/bin/psalm', <<<'PHP'
#!/usr/bin/env php
<?php
$file = '';
foreach ($argv as $arg) {
    if (is_file($arg) && str_contains($arg, '/.lsparrot/shadow/psalm-type/')) {
        $file = $arg;
        break;
    }
}
if ($file === '') {
    echo '[]';
    exit(0);
}
$line = 1;
foreach (file($file) ?: [] as $i => $text) {
    if (trim($text) === '$val;') {
        $line = $i + 1;
        break;
    }
}
echo json_encode([[
    'severity' => 'info',
    'line_from' => $line,
    'line_to' => $line,
    'type' => 'Trace',
    'message' => '$val: AnalyzerLhsFixture\\Domain\\User',
    'file_path' => $file,
]]);
exit(0);
PHP);
chmod($root . '/vendor/bin/phpstan', 0755);
chmod($root . '/vendor/bin/psalm', 0755);
LSParrot\start_lsp([
    'analyzer' => ['phpstan', 'psalm'],
    'workers' => ['count' => 0, 'analyzerDiagnosticsTimeout' => 5],
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-analyzer-lhs-type-test';
if (is_dir($root)) {
    $it = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($root, FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::CHILD_FIRST);
    foreach ($it as $file) {
        $file->isDir() ? @rmdir($file->getPathname()) : @unlink($file->getPathname());
    }
    @rmdir($root);
}
?>
--EXPECTREGEX--
[\s\S]*"jsonrpc":"2\.0","id":2,"result"[\s\S]*`User`\\n\\nLSParrot Engine[\s\S]*AnalyzerLhsFixture\\\\Domain\\\\User[\s\S]*PHPStan \/ Psalm(?![\s\S]*\*ERROR\*)[\s\S]*"jsonrpc":"2\.0","id":3,"result"[\s\S]*AnalyzerLhsFixture\\\\Domain\\\\User[\s\S]*\{"jsonrpc":"2\.0","id":4,"result":null\}
