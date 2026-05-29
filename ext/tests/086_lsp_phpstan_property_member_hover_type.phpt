--TEST--
LSP queries PHPStan hover types for object property member expressions
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 113

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-phpstan-property-hover-test"}}Content-Length: 622

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-phpstan-property-hover-test/src/Domain/Collection.php","languageId":"php","version":1,"text":"<?php\n\nnamespace PhpstanPropertyHoverFixture\\Domain;\n\n/**\n * @template TItem of object\n */\nfinal class Collection\n{\n    /** @var non-empty-list<TItem> */\n    private array $items;\n\n    /**\n     * @param non-empty-list<TItem> $items\n     */\n    public function __construct(array $items)\n    {\n        $this->items = $items;\n    }\n\n    public function first(): void\n    {\n        $this->items;\n    }\n}\n"}}}Content-Length: 198

{"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-phpstan-property-hover-test/src/Domain/Collection.php"},"position":{"line":22,"character":17}}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-phpstan-property-hover-test';
@mkdir($root . '/src/Domain', 0777, true);
@mkdir($root . '/vendor/bin', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);
file_put_contents($root . '/composer.json', json_encode([
    'autoload' => [
        'psr-4' => [
            'PhpstanPropertyHoverFixture\\' => 'src/',
        ],
    ],
], JSON_PRETTY_PRINT));
file_put_contents($root . '/src/Domain/Collection.php', <<<'PHP'
<?php

namespace PhpstanPropertyHoverFixture\Domain;

/**
 * @template TItem of object
 */
final class Collection
{
    /** @var non-empty-list<TItem> */
    private array $items;

    /**
     * @param non-empty-list<TItem> $items
     */
    public function __construct(array $items)
    {
        $this->items = $items;
    }

    public function first(): void
    {
        $this->items;
    }
}
PHP);
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'PhpstanPropertyHoverFixture\\\\Domain\\\\Collection' => " . var_export($root . '/src/Domain/Collection.php', true) . ",\n];\n");
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
$type = str_contains($contents, '\\PHPStan\\dumpType($this->items)')
    ? 'non-empty-list<PhpstanPropertyHoverFixture\\Domain\\User>'
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
chmod($root . '/vendor/bin/phpstan', 0755);
LSParrot\start_lsp([
    'analyzer' => 'phpstan',
    'workers' => ['count' => 0, 'analyzerDiagnosticsTimeout' => 5],
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-phpstan-property-hover-test';
if (is_dir($root)) {
    $it = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($root, FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::CHILD_FIRST);
    foreach ($it as $file) {
        $file->isDir() ? @rmdir($file->getPathname()) : @unlink($file->getPathname());
    }
    @rmdir($root);
}
?>
--EXPECTREGEX--
[\s\S]*"method":"lsparrot.php\/analyzerStatus"[\s\S]*"driver":"lsparrot\+phpstan"[\s\S]*"jsonrpc":"2\.0","id":2,"result"[\s\S]*`non-empty-list<TItem>`\\n\\nLSParrot Engine[\s\S]*non-empty-list<PhpstanPropertyHoverFixture\\\\Domain\\\\User>[\s\S]*PHPStan(?![\s\S]*\*ERROR\*)[\s\S]*\{"jsonrpc":"2\.0","id":3,"result":null\}
