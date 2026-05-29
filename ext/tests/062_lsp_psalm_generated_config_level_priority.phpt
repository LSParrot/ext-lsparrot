--TEST--
LSP runs Psalm without psalm.xml using configured and project-scoped levels
--EXTENSIONS--
lsparrot
--FILE--
<?php
$root = '/tmp/lsp-psalm-generated-config-level-test';
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

function lsp_input(array $messages): string {
    $buffer = '';
    foreach ($messages as $message) {
        $json = json_encode($message, JSON_UNESCAPED_SLASHES);
        $buffer .= 'Content-Length: ' . strlen($json) . "\r\n\r\n" . $json;
    }

    return $buffer;
}

function run_lsp(string $extension, string $runner, string $root, bool $waitForGeneratedConfig): array {
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
    while (
        (
            !is_file($root . '/args.log') ||
            ($waitForGeneratedConfig && !is_file($root . '/configs.log'))
        ) &&
        microtime(true) < $deadline
    ) {
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
file_put_contents($root . '/composer.json', '{"autoload":{"psr-4":{"PsalmGeneratedConfigFixture\\\\":"src/"}}}');
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [];\n");
file_put_contents($root . '/vendor/composer/autoload_namespaces.php', "<?php\nreturn [];\n");
file_put_contents($root . '/vendor/composer/autoload_psr4.php', "<?php\nreturn [];\n");
file_put_contents($root . '/src/Demo.php', "<?php\nnamespace PsalmGeneratedConfigFixture;\nfinal class Demo {}\n");
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

file_put_contents($runner, "<?php\nLSParrot\\start_lsp(['analyzer' => 'psalm', 'psalm' => ['level' => 5], 'workers' => ['count' => 0, 'analyzerDiagnosticsTimeout' => 5], 'symbolIndex' => ['size' => '4M']]);\n");
[$stdoutA, $stderrA, $codeA] = run_lsp($extension, $runner, $root, true);
$argsA = is_file($root . '/args.log') ? file_get_contents($root . '/args.log') : '';
$configsA = is_file($root . '/configs.log') ? file_get_contents($root . '/configs.log') : '';
@unlink($root . '/args.log');
@unlink($root . '/configs.log');

@mkdir($root . '/.lsparrot', 0777, true);
file_put_contents($root . '/.lsparrot/vscode_config.json', json_encode(['psalmLevel' => 6], JSON_PRETTY_PRINT));
file_put_contents($runner, "<?php\nLSParrot\\start_lsp(['analyzer' => 'psalm', 'psalm' => ['level' => 5], 'workers' => ['count' => 0, 'analyzerDiagnosticsTimeout' => 5], 'symbolIndex' => ['size' => '4M']]);\n");
[$stdoutB, $stderrB, $codeB] = run_lsp($extension, $runner, $root, true);
$argsB = is_file($root . '/args.log') ? file_get_contents($root . '/args.log') : '';
$configsB = is_file($root . '/configs.log') ? file_get_contents($root . '/configs.log') : '';
@unlink($root . '/args.log');
@unlink($root . '/configs.log');

file_put_contents($root . '/psalm.xml', '<?xml version="1.0"?><psalm errorLevel="1"><projectFiles><directory name="src" /></projectFiles></psalm>');
file_put_contents($runner, "<?php\nLSParrot\\start_lsp(['analyzer' => 'psalm', 'psalm' => ['level' => 4], 'workers' => ['count' => 0, 'analyzerDiagnosticsTimeout' => 5], 'symbolIndex' => ['size' => '4M']]);\n");
[$stdoutC, $stderrC, $codeC] = run_lsp($extension, $runner, $root, false);
$argsC = is_file($root . '/args.log') ? file_get_contents($root . '/args.log') : '';

if ($codeA !== 0 || $codeB !== 0 || $codeC !== 0) {
    echo "FAILED: process exit\n";
    var_dump($codeA, $stderrA, $codeB, $stderrB, $codeC, $stderrC);
} elseif (!str_contains($argsA, "--error-level=5") || !str_contains($configsA, 'errorLevel="5"')) {
    echo "FAILED: missing configured Psalm level\n";
    echo $argsA;
    echo $configsA;
} elseif (!str_contains($argsB, "--error-level=6") || !str_contains($configsB, 'errorLevel="6"')) {
    echo "FAILED: missing project-scoped Psalm level\n";
    echo $argsB;
    echo $configsB;
} elseif (str_contains($argsC, "--error-level=")) {
    echo "FAILED: Psalm level should be omitted when psalm.xml exists\n";
    echo $argsC;
} else {
    echo "OK\n";
}

rrmdir($root);
?>
--EXPECT--
OK
