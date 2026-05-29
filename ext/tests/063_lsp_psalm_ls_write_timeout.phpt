--TEST--
Psalm LS proxy does not block forever when the language server stops reading
--EXTENSIONS--
lsparrot
--FILE--
<?php
$root = '/tmp/lsp-psalm-ls-write-timeout-test';
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
        '-d',
        'memory_limit=-1',
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
@mkdir($root . '/src', 0777, true);
@mkdir($root . '/vendor/bin', 0777, true);
file_put_contents($root . '/composer.json', "{\"autoload\":{\"psr-4\":{\"PsalmLsTimeoutFixture\\\\\\\\\":\"src/\"}}}\n");
file_put_contents($root . '/psalm.xml', '<?xml version="1.0"?><psalm><projectFiles><directory name="src" /></projectFiles></psalm>');
file_put_contents($root . '/src/Demo.php', "<?php\nfinal class Demo {}\n");
file_put_contents($root . '/vendor/bin/psalm-language-server', <<<'PHP'
#!/usr/bin/env php
<?php
function frame_read(): ?array {
    $length = null;
    while (($line = fgets(STDIN)) !== false) {
        $trimmed = rtrim($line, "\r\n");
        if ($trimmed === '') {
            break;
        }
        if (stripos($trimmed, 'Content-Length:') === 0) {
            $length = (int) trim(substr($trimmed, strlen('Content-Length:')));
        }
    }
    if ($length === null) {
        return null;
    }
    $body = '';
    while (strlen($body) < $length && !feof(STDIN)) {
        $body .= fread(STDIN, $length - strlen($body));
    }
    if (strlen($body) !== $length) {
        return null;
    }
    return json_decode($body, true);
}

function frame_write(array $message): void {
    $json = json_encode($message, JSON_UNESCAPED_SLASHES);
    fwrite(STDOUT, 'Content-Length: ' . strlen($json) . "\r\n\r\n" . $json);
    fflush(STDOUT);
}

$message = frame_read();
if (($message['method'] ?? null) === 'initialize') {
    frame_write([
        'jsonrpc' => '2.0',
        'id' => $message['id'],
        'result' => ['capabilities' => ['completionProvider' => new stdClass()]],
    ]);
}

sleep(30);
PHP
);
chmod($root . '/vendor/bin/psalm-language-server', 0755);
file_put_contents($runner, "<?php\nLSParrot\\start_lsp(['analyzer' => 'psalm-ls', 'psalm' => ['transport' => 'languageServer'], 'workers' => ['count' => 0], 'symbolIndex' => ['size' => '4M']]);\n");

$largeText = "<?php\nfinal class Demo\n{\n    public function run(): void\n    {\n        $" . "this->run();\n" . str_repeat("        // padding\n", 100000) . "    }\n}\n";
$messages = [
    ['jsonrpc' => '2.0', 'id' => 1, 'method' => 'initialize', 'params' => ['rootUri' => 'file://' . $root]],
    ['jsonrpc' => '2.0', 'method' => 'textDocument/didOpen', 'params' => ['textDocument' => ['uri' => 'file://' . $root . '/src/Demo.php', 'languageId' => 'php', 'version' => 1, 'text' => $largeText]]],
    ['jsonrpc' => '2.0', 'id' => 2, 'method' => 'shutdown', 'params' => []],
];

[$stdout, $stderr, $code] = run_lsp($extension, $runner, lsp_input($messages));
if ($code !== 0) {
    echo "FAILED: process exit\n";
    var_dump($code, $stdout, $stderr);
} elseif (strpos($stdout, '"id":2,"result":null') === false) {
    echo "FAILED: shutdown response missing\n";
    echo $stdout;
} else {
    echo "OK\n";
}

rrmdir($root);
?>
--EXPECT--
OK
