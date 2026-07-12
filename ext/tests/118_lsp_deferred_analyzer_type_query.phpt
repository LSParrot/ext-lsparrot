--TEST--
LSP defers slow analyzer type queries instead of blocking per keystroke
--EXTENSIONS--
lsparrot
--FILE--
<?php
$root = '/tmp/lsp-deferred-type-query-test';
require __DIR__ . "/lsp_test_helper.inc";
$extension = lsp_test_extension_path();
$runner = $root . '/run.php';

function rrmdir(string $dir): void {
    if (!is_dir($dir)) {
        return;
    }
    foreach (scandir($dir) ?: [] as $entry) {
        if ($entry === '.' || $entry === '..') {
            continue;
        }
        $path = $dir . '/' . $entry;
        if (is_dir($path) && !is_link($path)) {
            rrmdir($path);
        } else {
            @unlink($path);
        }
    }
    @rmdir($dir);
}

function lsp_frame(array $message): string {
    $json = json_encode($message, JSON_UNESCAPED_SLASHES);

    return 'Content-Length: ' . strlen($json) . "\r\n\r\n" . $json;
}

function lsp_read_until_id($pipe, int $id, float $deadline): ?array {
    static $buffer = '';
    while (microtime(true) < $deadline) {
        $chunk = fread($pipe, 65536);
        if ($chunk !== false && $chunk !== '') {
            $buffer .= $chunk;
        }
        while (($headerEnd = strpos($buffer, "\r\n\r\n")) !== false) {
            if (!preg_match('/Content-Length:\s*(\d+)/i', substr($buffer, 0, $headerEnd), $m)) {
                return null;
            }
            $length = (int) $m[1];
            if (strlen($buffer) < $headerEnd + 4 + $length) {
                break;
            }
            $decoded = json_decode(substr($buffer, $headerEnd + 4, $length), true);
            $buffer = substr($buffer, $headerEnd + 4 + $length);
            if (is_array($decoded) && ($decoded['id'] ?? null) === $id) {
                return $decoded;
            }
        }
        usleep(10000);
    }

    return null;
}

rrmdir($root);
@mkdir($root . '/src', 0777, true);
@mkdir($root . '/vendor/bin', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);

file_put_contents($root . '/composer.json', json_encode([
    'autoload' => ['psr-4' => ['DeferredFixture\\' => 'src/']],
], JSON_PRETTY_PRINT));
file_put_contents($root . '/vendor/composer/autoload_psr4.php', "<?php\nreturn [\n    'DeferredFixture\\\\' => [" . var_export($root . '/src', true) . "],\n];\n");
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [];\n");
file_put_contents($root . '/vendor/composer/autoload_namespaces.php', "<?php\nreturn [];\n");

// Fake phpstan that takes ~0.8s per analysis: far above the 0.1s interactive
// budget, far below the diagnostics timeout.
file_put_contents($root . '/vendor/bin/phpstan', <<<'PHP'
#!/usr/bin/env php
<?php
usleep(800000);
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
echo json_encode([
    'totals' => ['errors' => 0, 'file_errors' => 1],
    'files' => [
        $file => [
            'errors' => 1,
            'messages' => [[
                'message' => 'Dumped type: DeferredFixture\\Mystery',
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

$demo = "<?php\n\nnamespace DeferredFixture;\n\nfinal class Thing\n{\n    public function go(): void\n    {\n        \$value = \$this->mystery();\n        \$value;\n    }\n}\n";
file_put_contents($root . '/src/Thing.php', $demo);
file_put_contents($runner, "<?php\nLSParrot\\start_lsp(['analyzer' => 'phpstan', 'workers' => ['count' => 0, 'analyzerDiagnosticsTimeout' => 10, 'analyzerTypeQueryTimeout' => 0.1], 'symbolIndex' => ['size' => '4M']]);\n");

$uri = 'file://' . $root . '/src/Thing.php';
$process = proc_open([
    PHP_BINARY,
    '-n',
    '-d',
    'extension=' . $extension,
    $runner,
], [
    0 => ['pipe', 'r'],
    1 => ['pipe', 'w'],
    2 => ['pipe', 'w'],
], $pipes);

if (!is_resource($process)) {
    echo "FAILED: could not start server\n";
    exit(1);
}

stream_set_blocking($pipes[1], false);

fwrite($pipes[0], lsp_frame(['jsonrpc' => '2.0', 'id' => 1, 'method' => 'initialize', 'params' => ['rootUri' => 'file://' . $root]]));
lsp_read_until_id($pipes[1], 1, microtime(true) + 15);

fwrite($pipes[0], lsp_frame(['jsonrpc' => '2.0', 'method' => 'textDocument/didOpen', 'params' => ['textDocument' => ['uri' => $uri, 'languageId' => 'php', 'version' => 1, 'text' => $demo]]]));

// First hover: the analyzer needs ~0.8s but the interactive budget is 0.1s,
// so the answer must come back quickly WITHOUT the analyzer type.
$firstStarted = microtime(true);
fwrite($pipes[0], lsp_frame(['jsonrpc' => '2.0', 'id' => 2, 'method' => 'textDocument/hover', 'params' => ['textDocument' => ['uri' => $uri], 'position' => ['line' => 9, 'character' => 9]]]));
$first = lsp_read_until_id($pipes[1], 2, microtime(true) + 15);
$firstElapsed = microtime(true) - $firstStarted;
$firstText = (string) ($first['result']['contents']['value'] ?? '');

// Poll follow-up hovers until the deferred result lands in the type cache.
// The fake analyzer needs ~0.8s plus interpreter startup, which can stretch
// well past a fixed sleep on loaded CI runners, so keep asking instead of
// guessing a delay.
$second = null;
$secondText = '';
$hoverId = 3;
$pollDeadline = microtime(true) + 30;
do {
    usleep(300000);
    fwrite($pipes[0], lsp_frame(['jsonrpc' => '2.0', 'id' => $hoverId, 'method' => 'textDocument/hover', 'params' => ['textDocument' => ['uri' => $uri], 'position' => ['line' => 9, 'character' => 9]]]));
    $second = lsp_read_until_id($pipes[1], $hoverId, microtime(true) + 15);
    $secondText = (string) ($second['result']['contents']['value'] ?? '');
    $hoverId++;
} while (!str_contains($secondText, 'Mystery') && microtime(true) < $pollDeadline);

fwrite($pipes[0], lsp_frame(['jsonrpc' => '2.0', 'id' => 900, 'method' => 'shutdown', 'params' => []]));
lsp_read_until_id($pipes[1], 900, microtime(true) + 15);
fwrite($pipes[0], lsp_frame(['jsonrpc' => '2.0', 'method' => 'exit', 'params' => []]));
fclose($pipes[0]);
stream_get_contents($pipes[1]);
fclose($pipes[1]);
fclose($pipes[2]);
$code = proc_close($process);

if ($code !== 0) {
    echo "FAILED: process exit\n";
    var_dump($code);
} elseif ($firstElapsed > 5.0) {
    echo "FAILED: first hover blocked on the analyzer\n";
    var_dump($firstElapsed);
} elseif (str_contains($firstText, 'Mystery')) {
    echo "FAILED: first hover should not include the deferred analyzer type yet\n";
    var_dump($firstText);
} elseif (!str_contains($secondText, 'Mystery')) {
    echo "FAILED: deferred analyzer type never reached the cache\n";
    var_dump($first, $second);
} else {
    echo "OK\n";
}

rrmdir($root);
?>
--EXPECT--
OK
