--TEST--
LSP runs Psalm with level 6 when no project config exists
--EXTENSIONS--
lsparrot
--FILE--
<?php
$root = '/tmp/lsp-psalm-default-level-test';
$extension = dirname(__DIR__) . '/modules/lsparrot.so';
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

function lsp_input(array $messages): string {
    $buffer = '';
    foreach ($messages as $message) {
        $json = json_encode($message, JSON_UNESCAPED_SLASHES);
        $buffer .= 'Content-Length: ' . strlen($json) . "\r\n\r\n" . $json;
    }

    return $buffer;
}

function run_lsp(string $extension, string $runner, string $root): array {
    $uri = 'file://' . $root . '/src/Demo.php';
    $text = file_get_contents($root . '/src/Demo.php');
    $firstMessages = [
        ['jsonrpc' => '2.0', 'id' => 1, 'method' => 'initialize', 'params' => ['rootUri' => 'file://' . $root]],
        ['jsonrpc' => '2.0', 'method' => 'textDocument/didOpen', 'params' => ['textDocument' => ['uri' => $uri, 'languageId' => 'php', 'version' => 1, 'text' => $text]]],
    ];
    $secondMessages = [
        ['jsonrpc' => '2.0', 'id' => 2, 'method' => 'shutdown', 'params' => []],
    ];
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
        return ['', 'failed to start', 1];
    }

    fwrite($pipes[0], lsp_input($firstMessages));
    fflush($pipes[0]);
    $deadline = microtime(true) + (getenv('USE_ZEND_ALLOC') === '0' ? 30.0 : 3.0);
    $statusId = 100;
    while ((!is_file($root . '/args.log') || !is_file($root . '/configs.log')) && microtime(true) < $deadline) {
        usleep(100000);
        fwrite($pipes[0], lsp_input([['jsonrpc' => '2.0', 'id' => $statusId++, 'method' => 'lsparrot.php/status', 'params' => []]]));
        fflush($pipes[0]);
    }
    fwrite($pipes[0], lsp_input($secondMessages));
    fclose($pipes[0]);
    $stdout = stream_get_contents($pipes[1]);
    $stderr = stream_get_contents($pipes[2]);
    fclose($pipes[1]);
    fclose($pipes[2]);
    $code = proc_close($process);

    return [$stdout, $stderr, $code];
}

rrmdir($root);
@mkdir($root . '/vendor/bin', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);
@mkdir($root . '/src', 0777, true);
file_put_contents($root . '/composer.json', '{"autoload":{"psr-4":{"PsalmDefaultLevelFixture\\\\":"src/"}}}');
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [];\n");
file_put_contents($root . '/vendor/composer/autoload_namespaces.php', "<?php\nreturn [];\n");
file_put_contents($root . '/vendor/composer/autoload_psr4.php', "<?php\nreturn [];\n");
file_put_contents($root . '/src/Demo.php', "<?php\nnamespace PsalmDefaultLevelFixture;\nfinal class Demo {}\n");
file_put_contents($root . '/vendor/bin/psalm', <<<'PHP'
#!/usr/bin/env php
<?php
$root = dirname(__DIR__, 2);
$config = '';
foreach ($argv as $arg) {
    if (str_starts_with($arg, '--config=')) {
        $config = substr($arg, strlen('--config='));
    }
}
file_put_contents($root . '/args.log', implode("\n", $argv) . "\n---\n", FILE_APPEND);
if ($config !== '' && is_file($config)) {
    file_put_contents($root . '/configs.log', file_get_contents($config) . "\n---\n", FILE_APPEND);
}
echo '[]';
PHP);
chmod($root . '/vendor/bin/psalm', 0755);
file_put_contents($runner, "<?php\nLSParrot\\start_lsp(['analyzer' => 'psalm', 'workers' => ['count' => 0, 'analyzerDiagnosticsTimeout' => 5], 'symbolIndex' => ['size' => '4M']]);\n");

[$stdout, $stderr, $code] = run_lsp($extension, $runner, $root);
$args = is_file($root . '/args.log') ? file_get_contents($root . '/args.log') : '';
$configs = is_file($root . '/configs.log') ? file_get_contents($root . '/configs.log') : '';

if ($code !== 0) {
    echo "FAILED: process exit\n";
    var_dump($code, $stderr);
} elseif (!str_contains($args, "--error-level=6")) {
    echo "FAILED: missing default Psalm CLI level\n";
    echo $args;
} elseif (!str_contains($configs, 'errorLevel="6"')) {
    echo "FAILED: missing default Psalm generated config level\n";
    echo $configs;
} else {
    echo "OK\n";
}

rrmdir($root);
?>
--EXPECT--
OK
