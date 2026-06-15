--TEST--
LSP uses scoped PHPDoc parameter generics and native array fallback
--EXTENSIONS--
lsparrot
--FILE--
<?php
$root = '/tmp/lsp-parameter-phpdoc-generic-test';
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
@mkdir($root, 0777, true);

$code = <<<'PHP'
<?php

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
        /*cursor*/$ite
    }

    public function replace(array $values): void
    {
        $values;
    }
}
PHP;

file_put_contents($root . '/Collection.php', $code);
file_put_contents($runner, "<?php\nLSParrot\\start_lsp(['analyzer' => 'lsparrot', 'workers' => ['count' => 0], 'symbolIndex' => ['size' => '4M']]);\n");

$uri = 'file://' . $root . '/Collection.php';
$messages = [
    ['jsonrpc' => '2.0', 'id' => 1, 'method' => 'initialize', 'params' => ['rootUri' => 'file://' . $root]],
    ['jsonrpc' => '2.0', 'method' => 'textDocument/didOpen', 'params' => ['textDocument' => ['uri' => $uri, 'languageId' => 'php', 'version' => 1, 'text' => $code]]],
    ['jsonrpc' => '2.0', 'id' => 2, 'method' => 'textDocument/hover', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($code, 'array $ite')]],
    ['jsonrpc' => '2.0', 'id' => 3, 'method' => 'textDocument/hover', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($code, '= $ite')]],
    ['jsonrpc' => '2.0', 'id' => 4, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($code, '/*cursor*/$ite')]],
    ['jsonrpc' => '2.0', 'id' => 5, 'method' => 'textDocument/hover', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($code, 'array $val')]],
    ['jsonrpc' => '2.0', 'id' => 6, 'method' => 'shutdown', 'params' => []],
];

[$stdout, $stderr, $exitCode] = run_lsp($extension, $runner, $messages);
$decoded = lsp_messages($stdout);
$declarationHover = lsp_response($decoded, 2);
$bodyHover = lsp_response($decoded, 3);
$completion = lsp_response($decoded, 4);
$fallbackHover = lsp_response($decoded, 5);
$items = lsp_item($completion, '$items');
$declarationText = (string) ($declarationHover['contents']['value'] ?? '');
$bodyText = (string) ($bodyHover['contents']['value'] ?? '');
$fallbackText = (string) ($fallbackHover['contents']['value'] ?? '');
$itemDetail = (string) ($items['detail'] ?? '');

if ($exitCode !== 0) {
    echo "FAILED: process exit\n";
    var_dump($exitCode, $stderr);
} elseif (!str_contains($declarationText, 'non-empty-list<TItem>') || !str_contains($declarationText, 'LSParrot Engine')) {
    echo "FAILED: missing PHPDoc parameter type on declaration hover\n";
    var_dump($declarationHover);
} elseif (!str_contains($bodyText, 'non-empty-list<TItem>') || !str_contains($bodyText, 'LSParrot Engine')) {
    echo "FAILED: missing PHPDoc parameter type on body hover\n";
    var_dump($bodyHover);
} elseif (!$items || !str_contains($itemDetail, 'non-empty-list<TItem>')) {
    echo "FAILED: missing PHPDoc parameter type in completion detail\n";
    var_dump($completion);
} elseif (!str_contains($fallbackText, 'array') || !str_contains($fallbackText, 'LSParrot Engine')) {
    echo "FAILED: missing native array fallback hover\n";
    var_dump($fallbackHover);
} else {
    echo "OK\n";
}

rrmdir($root);
?>
--EXPECT--
OK
