--TEST--
LSP proxies Psalm Language Server diagnostics and completion
--EXTENSIONS--
lsparrot
--FILE--
<?php
$root = '/tmp/lsp-psalm-ls-test';
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

function lsp_send($pipe, array $message): void {
    fwrite($pipe, lsp_frame($message));
    fflush($pipe);
}

function lsp_parse_messages(string &$buffer): array {
    $messages = [];
    $offset = 0;
    while (($headerEnd = strpos($buffer, "\r\n\r\n", $offset)) !== false) {
        $header = substr($buffer, $offset, $headerEnd - $offset);
        if (!preg_match('/Content-Length:\s*(\d+)/i', $header, $matches)) {
            break;
        }
        $length = (int) $matches[1];
        $bodyStart = $headerEnd + 4;
        if (strlen($buffer) < $bodyStart + $length) {
            break;
        }
        $body = substr($buffer, $bodyStart, $length);
        $decoded = json_decode($body, true);
        if (is_array($decoded)) {
            $messages[] = $decoded;
        }
        $offset = $bodyStart + $length;
    }
    if ($offset > 0) {
        $buffer = substr($buffer, $offset);
    }

    return $messages;
}

function lsp_collect($stream, string &$buffer, array &$messages, float $seconds): void {
    $deadline = microtime(true) + $seconds;
    do {
        $chunk = stream_get_contents($stream);
        if ($chunk !== false && $chunk !== '') {
            $buffer .= $chunk;
            foreach (lsp_parse_messages($buffer) as $message) {
                $messages[] = $message;
            }
        }
        if (microtime(true) >= $deadline) {
            break;
        }
        usleep(10000);
    } while (true);
}

function lsp_wait_for($stream, string &$buffer, array &$messages, callable $predicate, float $seconds): ?array {
    $deadline = microtime(true) + $seconds;
    do {
        foreach ($messages as $message) {
            if ($predicate($message)) {
                return $message;
            }
        }
        lsp_collect($stream, $buffer, $messages, 0.02);
    } while (microtime(true) < $deadline);

    foreach ($messages as $message) {
        if ($predicate($message)) {
            return $message;
        }
    }

    return null;
}

function lsp_response(array $messages, int $id): ?array {
    foreach ($messages as $message) {
        if (($message['id'] ?? null) === $id) {
            return $message;
        }
    }

    return null;
}

function lsp_has_item(?array $response, string $label, ?string $source = null): bool {
    foreach (($response['result']['items'] ?? []) as $item) {
        if (($item['label'] ?? null) !== $label) {
            continue;
        }
        if ($source === null || (($item['data']['source'] ?? null) === $source)) {
            return true;
        }
    }

    return false;
}

function lsp_has_psalm_diagnostic(array $message): bool {
    if (($message['method'] ?? null) !== 'textDocument/publishDiagnostics') {
        return false;
    }
    foreach (($message['params']['diagnostics'] ?? []) as $diagnostic) {
        if (($diagnostic['source'] ?? null) === 'psalm' && ($diagnostic['message'] ?? null) === 'Psalm LS settings ok') {
            return true;
        }
    }

    return false;
}

function lsp_has_completion_ready(array $message): bool {
    return ($message['method'] ?? null) === 'lsparrot.php/completionReady'
        && ($message['params']['analyzer'] ?? null) === 'psalm-ls';
}

function lsp_any_psalm_diagnostic(array $messages): bool {
    foreach ($messages as $message) {
        if (lsp_has_psalm_diagnostic($message)) {
            return true;
        }
    }

    return false;
}

function lsp_any_completion_ready(array $messages): bool {
    foreach ($messages as $message) {
        if (lsp_has_completion_ready($message)) {
            return true;
        }
    }

    return false;
}

rrmdir($root);
@mkdir($root . '/src', 0777, true);
@mkdir($root . '/vendor/bin', 0777, true);
file_put_contents($root . '/composer.json', "{\"autoload\":{\"psr-4\":{\"PsalmLsFixture\\\\\\\\\":\"src/\"}}}\n");
file_put_contents($root . '/psalm.xml', '<?xml version="1.0"?><psalm><projectFiles><directory name="src" /></projectFiles></psalm>');
file_put_contents($root . '/src/Demo.php', "<?php\nfinal class Demo {}\n");
file_put_contents($root . '/vendor/bin/psalm-language-server', <<<'PHP'
#!/usr/bin/env php
<?php
$settingsOk = in_array('--disable-on-change', $argv, true)
    && in_array('--on-change-debounce-ms=123', $argv, true)
    && in_array('--enable-autocomplete=true', $argv, true)
    && in_array('--enable-provide-diagnostics=true', $argv, true)
    && in_array('--enable-provide-hover=false', $argv, true)
    && in_array('--enable-provide-definition=false', $argv, true)
    && in_array('--enable-provide-signature-help=false', $argv, true)
    && in_array('--show-diagnostic-warnings=true', $argv, true)
    && in_array('--in-memory=true', $argv, true);

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

while (($message = frame_read()) !== null) {
    $method = $message['method'] ?? null;
    if ($method === 'initialize') {
        frame_write([
            'jsonrpc' => '2.0',
            'id' => (string) $message['id'],
            'result' => ['capabilities' => ['completionProvider' => new stdClass()]],
        ]);
        continue;
    }
    if ($method === 'textDocument/didOpen') {
        $uri = $message['params']['textDocument']['uri'];
        frame_write([
            'jsonrpc' => '2.0',
            'method' => 'textDocument/publishDiagnostics',
            'params' => [
                'uri' => $uri,
                'diagnostics' => [[
                    'range' => [
                        'start' => ['line' => 0, 'character' => 0],
                        'end' => ['line' => 0, 'character' => 5],
                    ],
                    'severity' => 2,
                    'source' => 'psalm',
                    'message' => $settingsOk ? 'Psalm LS settings ok' : 'Psalm LS settings bad',
                ]],
            ],
        ]);
        continue;
    }
    if ($method === 'textDocument/completion') {
        frame_write([
            'jsonrpc' => '2.0',
            'id' => $message['id'],
            'result' => [
                'items' => [[
                    'label' => 'PsalmLanguageServerCompletion',
                    'kind' => 2,
                    'detail' => 'from Psalm LS',
                ]],
            ],
        ]);
        continue;
    }
    if ($method === 'shutdown') {
        frame_write(['jsonrpc' => '2.0', 'id' => $message['id'], 'result' => null]);
        exit(0);
    }
}
PHP
);
chmod($root . '/vendor/bin/psalm-language-server', 0755);
file_put_contents($runner, <<<'PHP'
<?php
LSParrot\start_lsp([
    'analyzer' => 'psalm-ls',
    'psalm' => [
        'transport' => 'languageServer',
        'onChange' => false,
        'onChangeDebounceMs' => 123,
        'maxResponseWaitMs' => 500,
        'enableAutocomplete' => true,
        'enableDiagnostics' => true,
        'enableHover' => false,
        'enableDefinition' => false,
        'enableSignatureHelp' => false,
        'showInfo' => true,
        'inMemory' => true,
    ],
    'symbolIndex' => ['size' => '4M'],
]);
PHP
);

$source = "<?php\nfinal class Demo\n{\n    public function run(): void\n    {\n        \$this->run();\n    }\n}";
$uri = 'file://' . $root . '/src/Demo.php';
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
    echo "FAILED: process start\n";
    rrmdir($root);
    exit;
}

stream_set_blocking($pipes[1], false);
stream_set_blocking($pipes[2], false);
$buffer = '';
$messages = [];
$nextId = 1;

lsp_send($pipes[0], ['jsonrpc' => '2.0', 'id' => $nextId, 'method' => 'initialize', 'params' => ['rootUri' => 'file://' . $root]]);
$initializeId = $nextId++;
lsp_wait_for($pipes[1], $buffer, $messages, fn(array $message): bool => ($message['id'] ?? null) === $initializeId, 3.0);

lsp_send($pipes[0], ['jsonrpc' => '2.0', 'method' => 'textDocument/didOpen', 'params' => ['textDocument' => ['uri' => $uri, 'languageId' => 'php', 'version' => 1, 'text' => $source]]]);
lsp_wait_for($pipes[1], $buffer, $messages, 'lsp_has_psalm_diagnostic', 3.0);

lsp_send($pipes[0], ['jsonrpc' => '2.0', 'id' => $nextId, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $uri], 'position' => ['line' => 5, 'character' => 15]]]);
$firstCompletionId = $nextId++;
lsp_wait_for($pipes[1], $buffer, $messages, fn(array $message): bool => ($message['id'] ?? null) === $firstCompletionId, 3.0);

$psalmCompletionId = null;
for ($attempt = 0; $attempt < 10; $attempt++) {
    lsp_wait_for($pipes[1], $buffer, $messages, 'lsp_has_completion_ready', 0.2);
    lsp_send($pipes[0], ['jsonrpc' => '2.0', 'id' => $nextId, 'method' => 'lsparrot.php/status', 'params' => []]);
    $statusId = $nextId++;
    lsp_wait_for($pipes[1], $buffer, $messages, fn(array $message): bool => ($message['id'] ?? null) === $statusId, 1.0);
    lsp_send($pipes[0], ['jsonrpc' => '2.0', 'id' => $nextId, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $uri], 'position' => ['line' => 5, 'character' => 15]]]);
    $completionId = $nextId++;
    $completion = lsp_wait_for($pipes[1], $buffer, $messages, fn(array $message): bool => ($message['id'] ?? null) === $completionId, 1.0);
    if (lsp_has_item($completion, 'PsalmLanguageServerCompletion', 'psalm-ls')) {
        $psalmCompletionId = $completionId;
        break;
    }
}

lsp_send($pipes[0], ['jsonrpc' => '2.0', 'id' => $nextId, 'method' => 'shutdown', 'params' => []]);
$shutdownId = $nextId++;
lsp_wait_for($pipes[1], $buffer, $messages, fn(array $message): bool => ($message['id'] ?? null) === $shutdownId, 2.0);
fclose($pipes[0]);
lsp_collect($pipes[1], $buffer, $messages, 0.2);
$stderr = stream_get_contents($pipes[2]);
fclose($pipes[1]);
fclose($pipes[2]);
$code = proc_close($process);

$firstCompletion = lsp_response($messages, $firstCompletionId);
$finalCompletion = $psalmCompletionId ? lsp_response($messages, $psalmCompletionId) : null;
$status = null;
foreach ($messages as $message) {
    if (isset($message['result']['analyzers']['psalm-ls'])) {
        $status = $message;
    }
}

if ($code !== 0) {
    echo "FAILED: process exit\n";
    var_dump($code, $stderr);
} elseif (!lsp_any_psalm_diagnostic($messages)) {
    echo "FAILED: missing Psalm LS diagnostic\n";
    var_dump($messages);
} elseif (!lsp_has_item($firstCompletion, 'run', 'lsparrot')) {
    echo "FAILED: missing LSParrot Engine completion\n";
    var_dump($firstCompletion);
} elseif (!lsp_any_completion_ready($messages)) {
    echo "FAILED: missing completionReady\n";
    var_dump($messages);
} elseif (!$status || (($status['result']['analyzers']['psalm-ls']['enabled'] ?? null) !== true) || (($status['result']['analyzers']['psalm-ls']['projects'][$root] ?? null) !== 'ready')) {
    echo "FAILED: missing Psalm LS ready status\n";
    var_dump($status);
} elseif (!lsp_has_item($finalCompletion, 'PsalmLanguageServerCompletion', 'psalm-ls')) {
    echo "FAILED: missing Psalm LS completion\n";
    var_dump($finalCompletion, $messages);
} else {
    echo "OK\n";
}

rrmdir($root);
?>
--EXPECT--
OK
