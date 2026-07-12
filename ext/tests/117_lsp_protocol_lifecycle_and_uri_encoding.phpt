--TEST--
LSP percent-encodes outgoing URIs, applies the last didChange, rejects unknown methods, and survives shutdown until exit
--EXTENSIONS--
lsparrot
--FILE--
<?php
$root = '/tmp/lsp proto lifecycle test';
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

function lsp_position_after(string $text, string $needle): array {
    $offset = strpos($text, $needle);
    if ($offset === false) {
        return ['line' => 0, 'character' => 0];
    }
    $offset += strlen($needle);
    $before = substr($text, 0, $offset);
    $line = substr_count($before, "\n");
    $lineStart = strrpos($before, "\n");

    return ['line' => $line, 'character' => $lineStart === false ? strlen($before) : strlen($before) - $lineStart - 1];
}

function lsp_messages(string $stdout): array {
    $messages = [];
    $offset = 0;
    while (($headerEnd = strpos($stdout, "\r\n\r\n", $offset)) !== false) {
        $header = substr($stdout, $offset, $headerEnd - $offset);
        if (!preg_match('/Content-Length:\s*(\d+)/i', $header, $matches)) {
            break;
        }
        $length = (int) $matches[1];
        $bodyStart = $headerEnd + 4;
        $body = substr($stdout, $bodyStart, $length);
        $decoded = json_decode($body, true);
        if (is_array($decoded)) {
            $messages[] = $decoded;
        }
        $offset = $bodyStart + $length;
    }

    return $messages;
}

function lsp_response(array $messages, int $id): ?array {
    foreach ($messages as $message) {
        if (($message['id'] ?? null) === $id) {
            return $message['result'] ?? null;
        }
    }

    return null;
}

function lsp_error(array $messages, int $id): ?array {
    foreach ($messages as $message) {
        if (($message['id'] ?? null) === $id) {
            return $message['error'] ?? null;
        }
    }

    return null;
}

function lsp_item(?array $result, string $label): ?array {
    foreach (($result['items'] ?? []) as $item) {
        if (($item['label'] ?? null) === $label) {
            return $item;
        }
    }

    return null;
}

function run_lsp(string $extension, string $runner, array $messages): array {
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

    fwrite($pipes[0], lsp_input($messages));
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
@mkdir($root . '/vendor/composer', 0777, true);

file_put_contents($root . '/src/Target.php', <<<'PHP'
<?php
namespace LifecycleFixture;

final class Target
{
    public function fire(): int
    {
        return 1;
    }
}
PHP);

$stale = <<<'PHP'
<?php
namespace LifecycleFixture;

final class Consumer
{
}
PHP;

$fresh = <<<'PHP'
<?php
namespace LifecycleFixture;

final class Consumer
{
    public function run(Target $target): int
    {
        return $target->fir
    }
}
PHP;

file_put_contents($root . '/src/Consumer.php', $stale);
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n" .
    "    'LifecycleFixture\\\\Target' => " . var_export($root . '/src/Target.php', true) . ",\n" .
    "    'LifecycleFixture\\\\Consumer' => " . var_export($root . '/src/Consumer.php', true) . ",\n" .
    "];\n");
file_put_contents($root . '/vendor/composer/autoload_namespaces.php', "<?php\nreturn [];\n");
file_put_contents($root . '/vendor/composer/autoload_psr4.php', "<?php\nreturn [];\n");
file_put_contents($root . '/composer.json', '{"autoload":{"classmap":["src/"]}}');
file_put_contents($runner, "<?php\nLSParrot\\start_lsp(['analyzer' => 'lsparrot', 'workers' => ['count' => 0], 'symbolIndex' => ['size' => '4M']]);\n");

$encodedRoot = str_replace(' ', '%20', $root);
$uri = 'file://' . $encodedRoot . '/src/Consumer.php';
$messages = [
    ['jsonrpc' => '2.0', 'id' => 1, 'method' => 'initialize', 'params' => ['rootUri' => 'file://' . $encodedRoot]],
    ['jsonrpc' => '2.0', 'method' => 'textDocument/didOpen', 'params' => ['textDocument' => ['uri' => $uri, 'languageId' => 'php', 'version' => 1, 'text' => $stale]]],
    // Two full-sync change events in one notification: the LAST one wins.
    ['jsonrpc' => '2.0', 'method' => 'textDocument/didChange', 'params' => ['textDocument' => ['uri' => $uri, 'version' => 2], 'contentChanges' => [['text' => $stale], ['text' => $fresh]]]],
    ['jsonrpc' => '2.0', 'id' => 2, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($fresh, '$target->fir')]],
    // Definition into a path containing spaces must return a percent-encoded URI.
    ['jsonrpc' => '2.0', 'id' => 3, 'method' => 'textDocument/definition', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($fresh, 'public function run(Targ')]],
    // Unknown request methods must produce MethodNotFound, not a null result.
    ['jsonrpc' => '2.0', 'id' => 4, 'method' => 'textDocument/definitelyNotAMethod', 'params' => ['textDocument' => ['uri' => $uri], 'position' => ['line' => 0, 'character' => 0]]],
    ['jsonrpc' => '2.0', 'id' => 5, 'method' => 'shutdown', 'params' => []],
    // Requests after shutdown are invalid; only exit is honored.
    ['jsonrpc' => '2.0', 'id' => 6, 'method' => 'textDocument/hover', 'params' => ['textDocument' => ['uri' => $uri], 'position' => ['line' => 0, 'character' => 0]]],
    ['jsonrpc' => '2.0', 'method' => 'exit', 'params' => []],
];

[$stdout, $stderr, $code] = run_lsp($extension, $runner, $messages);
$decoded = lsp_messages($stdout);
$completion = lsp_response($decoded, 2);
$definition = lsp_response($decoded, 3);
$unknownError = lsp_error($decoded, 4);
$shutdownResult = null;
$sawShutdownResponse = false;
foreach ($decoded as $message) {
    if (($message['id'] ?? null) === 5 && array_key_exists('result', $message)) {
        $sawShutdownResponse = true;
    }
}
$afterShutdownError = lsp_error($decoded, 6);

$fire = lsp_item($completion, 'fire');
$definitionUri = (string) ($definition['uri'] ?? ($definition[0]['uri'] ?? ''));

if ($code !== 0) {
    echo "FAILED: process exit\n";
    var_dump($code, $stderr);
} elseif (!$fire) {
    echo "FAILED: completion should reflect the LAST didChange content change\n";
    var_dump($completion);
} elseif (!str_contains($definitionUri, '/lsp%20proto%20lifecycle%20test/src/Target.php')) {
    echo "FAILED: outgoing definition URI is not percent-encoded\n";
    var_dump($definition);
} elseif (($unknownError['code'] ?? null) !== -32601) {
    echo "FAILED: unknown request method should produce MethodNotFound\n";
    var_dump($unknownError);
} elseif (!$sawShutdownResponse) {
    echo "FAILED: shutdown request was not answered\n";
} elseif (($afterShutdownError['code'] ?? null) !== -32600) {
    echo "FAILED: requests after shutdown should be InvalidRequest until exit\n";
    var_dump($afterShutdownError);
} else {
    echo "OK\n";
}

rrmdir($root);
?>
--EXPECT--
OK
