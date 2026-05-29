--TEST--
LSP lsparrot server saves and loads the serialized .lsparrot project index cache
--EXTENSIONS--
lsparrot
--FILE--
<?php
$root = '/tmp/lsp-lsparrot-index-cache-test';
require __DIR__ . "/lsp_test_helper.inc";
$extension = lsp_test_extension_path();
$runner = $root . '/run.php';
$indexDisabled = getenv('LSPARROT_NO_INDEX') === '1';

function rrmdir(string $dir): void {
    if (!is_dir($dir)) {
        return;
    }
    foreach (scandir($dir) ?: [] as $entry) {
        if ($entry === '.' || $entry === '..') {
            continue;
        }
        $path = $dir . '/' . $entry;
        if (is_dir($path)) {
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

function run_lsp(string $extension, string $runner, string $input): array {
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

    fwrite($pipes[0], $input);
    fclose($pipes[0]);
    $stdout = stream_get_contents($pipes[1]);
    $stderr = stream_get_contents($pipes[2]);
    fclose($pipes[1]);
    fclose($pipes[2]);
    $code = proc_close($process);

    return [$stdout, $stderr, $code];
}

rrmdir($root);
@mkdir($root . '/vendor/composer', 0777, true);
@mkdir($root . '/src', 0777, true);
file_put_contents($root . '/src/AlphaCached.php', "<?php\nnamespace CacheFixture;\nfinal class AlphaCached {}\n");
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'CacheFixture\\\\AlphaCached' => " . var_export($root . '/src/AlphaCached.php', true) . ",\n];\n");
file_put_contents($runner, "<?php\nLSParrot\\start_lsp(['analyzer' => 'lsparrot', 'workers' => ['count' => 0], 'symbolIndex' => ['size' => '4M']]);\n");

$messages = [
    ['jsonrpc' => '2.0', 'id' => 1, 'method' => 'initialize', 'params' => ['rootUri' => 'file://' . $root]],
    ['jsonrpc' => '2.0', 'id' => 2, 'method' => 'workspace/symbol', 'params' => ['query' => 'Alpha']],
    ['jsonrpc' => '2.0', 'id' => 3, 'method' => 'shutdown', 'params' => []],
];
$input = lsp_input($messages);
[$firstOut, $firstErr, $firstCode] = run_lsp($extension, $runner, $input);
$cache = $root . '/.lsparrot/lsparrot-index.bin';
if (!$indexDisabled && is_file($cache)) {
    $handle = fopen($cache, 'r+b');
    if ($handle) {
        fseek($handle, 8);
        fwrite($handle, "\0\0\0\0");
        fclose($handle);
    }
}
[$secondOut, $secondErr, $secondCode] = run_lsp($extension, $runner, $input);
[$thirdOut, $thirdErr, $thirdCode] = run_lsp($extension, $runner, $input);

if ($firstCode !== 0 || $secondCode !== 0 || $thirdCode !== 0) {
    echo "FAILED: process exit\n";
    var_dump($firstCode, $secondCode, $thirdCode, $firstErr, $secondErr, $thirdErr);
} elseif ($indexDisabled) {
    if (is_file($cache)) {
        echo "FAILED: disabled cache was created\n";
    } elseif (strpos($firstOut, 'AlphaCached') === false || strpos($secondOut, 'AlphaCached') === false || strpos($thirdOut, 'AlphaCached') === false) {
        echo "FAILED: disabled cache run missing symbol\n";
        echo $firstOut . $secondOut . $thirdOut;
    } elseif (
        strpos($firstOut, 'Project index loaded from .lsparrot cache.') !== false ||
        strpos($secondOut, 'Project index loaded from .lsparrot cache.') !== false ||
        strpos($thirdOut, 'Project index loaded from .lsparrot cache.') !== false
    ) {
        echo "FAILED: disabled cache was loaded\n";
    } else {
        echo "OK\n";
    }
} elseif (!is_file($cache)) {
    echo "FAILED: cache missing\n";
} elseif (strpos($firstOut, 'AlphaCached') === false) {
    echo "FAILED: first index missing symbol\n";
    echo $firstOut;
} elseif (strpos($secondOut, 'Project index loaded from .lsparrot cache.') !== false) {
    echo "FAILED: incompatible cache was reused\n";
    echo $secondOut;
} elseif (strpos($secondOut, 'AlphaCached') === false) {
    echo "FAILED: second index missing symbol\n";
    echo $secondOut;
} elseif (strpos($thirdOut, 'Project index loaded from .lsparrot cache.') === false) {
    echo "FAILED: cache load status missing\n";
    echo $thirdOut;
} elseif (strpos($thirdOut, 'AlphaCached') === false) {
    echo "FAILED: third index missing symbol\n";
    echo $thirdOut;
} else {
    echo "OK\n";
}

rrmdir($root);
?>
--EXPECT--
OK
