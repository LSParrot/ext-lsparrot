--TEST--
LSP analyzer prewarm only treats Composer autoload paths as a project scope
--EXTENSIONS--
lsparrot
--FILE--
<?php
$root = '/tmp/lsp-analyzer-composer-scope-test';
require __DIR__ . "/lsp_test_helper.inc";
$extension = lsp_test_extension_path();
$runner = $root . '/run.php';
$nested = $root . '/example';

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

function write_phpstan(string $path, string $marker): void {
    file_put_contents($path, "#!/usr/bin/env php\n<?php\nfile_put_contents(" . var_export($marker, true) . ", implode(\"\\n\", \$argv));\necho '{\"totals\":{\"errors\":0,\"file_errors\":0},\"files\":[],\"errors\":[]}';\n");
    chmod($path, 0755);
}

rrmdir($root);
@mkdir($root . '/vendor/bin', 0777, true);
@mkdir($nested . '/vendor/bin', 0777, true);
@mkdir($nested . '/src', 0777, true);
file_put_contents($root . '/composer.json', "{\n  \"name\": \"scope/root\",\n  \"require-dev\": {\"phpstan/phpstan\": \"*\"}\n}\n");
file_put_contents($nested . '/composer.json', "{\n  \"name\": \"scope/nested\",\n  \"autoload\": {\"psr-4\": {\"Scope\\\\\\\\Nested\\\\\\\\\": \"src/\"}},\n  \"require-dev\": {\"phpstan/phpstan\": \"*\"}\n}\n");
file_put_contents($nested . '/src/Demo.php', "<?php\nnamespace Scope\\Nested;\nfinal class Demo {}\n");
write_phpstan($root . '/vendor/bin/phpstan', $root . '/root-ran');
write_phpstan($nested . '/vendor/bin/phpstan', $root . '/nested-ran');
file_put_contents($runner, "<?php\nLSParrot\\start_lsp(['analyzer' => 'phpstan', 'workers' => ['count' => 0, 'analyzerDiagnosticsTimeout' => 5], 'symbolIndex' => ['size' => '4M']]);\n");

$messages = [
    ['jsonrpc' => '2.0', 'id' => 1, 'method' => 'initialize', 'params' => ['rootUri' => 'file://' . $root]],
    ['jsonrpc' => '2.0', 'id' => 2, 'method' => 'lsparrot.php/status', 'params' => []],
    ['jsonrpc' => '2.0', 'id' => 3, 'method' => 'shutdown', 'params' => []],
];

[$stdout, $stderr, $code] = run_lsp($extension, $runner, lsp_input($messages));
if ($code !== 0) {
    echo "FAILED: process exit\n";
    var_dump($code, $stdout, $stderr);
} elseif (is_file($root . '/root-ran')) {
    echo "FAILED: root analyzer should not run\n";
    echo file_get_contents($root . '/root-ran');
} elseif (strpos($stdout, '"projectRoot":"' . $root . '"') !== false) {
    echo "FAILED: root project was reported as an analyzer project\n";
    echo $stdout;
} elseif (strpos($stdout, '"projectRoot":"' . $nested . '"') === false) {
    echo "FAILED: nested project status missing\n";
    echo $stdout;
} else {
    echo "OK\n";
}

rrmdir($root);
?>
--EXPECT--
OK
