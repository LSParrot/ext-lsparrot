--TEST--
LSP places PHPStan and Psalm analyzer caches under .lsparrot
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 105

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-analyzer-cache-test"}}Content-Length: 193

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-analyzer-cache-test/src/Ok.php","languageId":"php","version":1,"text":"<?php\n$value = 1;\n"}}}Content-Length: 56

{"jsonrpc":"2.0","id":2,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-analyzer-cache-test';
@mkdir($root . '/src', 0777, true);
@mkdir($root . '/.lsparrot/phpstan', 0777, true);
@mkdir($root . '/.lsparrot/psalm', 0777, true);
@mkdir($root . '/vendor/bin', 0777, true);
file_put_contents($root . '/composer.json', json_encode([
    'autoload' => ['psr-4' => ['CacheFixture\\' => 'src/']],
], JSON_PRETTY_PRINT));
file_put_contents($root . '/src/Ok.php', "<?php\n\$value = 1;\n");
file_put_contents($root . '/.lsparrot/phpstan/project-diagnostics.json', json_encode([
    'files' => [
        $root . '/src/Ok.php' => [
            'messages' => [[
                'message' => 'phpstan cache ok',
                'line' => 1,
                'identifier' => 'cache.ok',
            ]],
        ],
    ],
], JSON_UNESCAPED_SLASHES));
file_put_contents($root . '/.lsparrot/psalm/project-diagnostics.json', json_encode([[
    'file_path' => $root . '/src/Ok.php',
    'message' => 'psalm cache ok',
    'line_from' => 1,
    'type' => 'cache.ok',
    'severity' => 'error',
]], JSON_UNESCAPED_SLASHES));
file_put_contents($root . '/phpstan.neon', "parameters:\n    level: 1\n");
file_put_contents($root . '/psalm.xml', '<?xml version="1.0"?><psalm cacheDirectory="/tmp/psalm-old-cache"><projectFiles><directory name="src" /></projectFiles></psalm>');
file_put_contents($root . '/vendor/bin/phpstan', <<<'PHP'
#!/usr/bin/env php
<?php
$file = $argv[2] ?? '';
$config = null;
for ($i = 1; $i < count($argv) - 1; $i++) {
    if ($argv[$i] === '-c') {
        $config = $argv[$i + 1];
    }
}
$root = dirname(__DIR__, 2);
$expected = $root . '/.lsparrot/phpstan/cache';
$contents = is_string($config) && is_file($config) ? file_get_contents($config) : '';
$ok = is_string($config)
    && str_starts_with($config, $root . '/.lsparrot/phpstan/')
    && is_dir($expected)
    && str_contains($contents, $expected);
echo json_encode([
    'files' => [
        $file => [
            'messages' => [[
                'message' => $ok ? 'phpstan cache ok' : 'phpstan cache bad',
                'line' => 1,
                'identifier' => 'cache.ok',
            ]],
        ],
    ],
], JSON_UNESCAPED_SLASHES);
PHP);
file_put_contents($root . '/vendor/bin/psalm', <<<'PHP'
#!/usr/bin/env php
<?php
$config = null;
foreach ($argv as $arg) {
    if (str_starts_with($arg, '--config=')) {
        $config = substr($arg, strlen('--config='));
    }
}
$root = dirname(__DIR__, 2);
$expected = $root . '/.lsparrot/psalm/cache';
$contents = is_string($config) && is_file($config) ? file_get_contents($config) : '';
$ok = is_string($config)
    && str_starts_with($config, $root . '/.lsparrot/psalm/')
    && is_dir($expected)
    && str_contains($contents, 'cacheDirectory="' . $expected . '"')
    && str_contains($contents, 'resolveFromConfigFile="false"');
$file = end($argv);
echo json_encode([[
    'file_path' => $file,
    'message' => $ok ? 'psalm cache ok' : 'psalm cache bad',
    'line_from' => 1,
    'type' => 'cache.ok',
    'severity' => 'error',
]], JSON_UNESCAPED_SLASHES);
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
function lsp_rrmdir(string $path): void
{
    if (!is_dir($path)) {
        return;
    }
    foreach (scandir($path) ?: [] as $name) {
        if ($name === '.' || $name === '..') {
            continue;
        }
        $child = $path . '/' . $name;
        if (is_dir($child) && !is_link($child)) {
            lsp_rrmdir($child);
        } else {
            @unlink($child);
        }
    }
    @rmdir($path);
}
lsp_rrmdir('/tmp/lsp-analyzer-cache-test');
?>
--EXPECTREGEX--
(?s)\A.*"analyzer":"driver".*"driver":"lsparrot\+phpstan\+psalm".*"label":"LSParrot Engine \+ PHPStan \+ Psalm".*"source":"phpstan".*"message":"phpstan cache ok".*"source":"psalm".*"message":"psalm cache ok".*"jsonrpc":"2\.0","id":2,"result":null.*\z
