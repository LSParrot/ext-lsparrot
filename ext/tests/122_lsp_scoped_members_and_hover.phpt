--TEST--
LSP scopes locals vs properties, hovers inherited/colliding members, and infers foreach elements
--EXTENSIONS--
lsparrot
--FILE--
<?php
$root = '/tmp/lsp-scoped-members-test';
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
@mkdir($root . '/src', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);

file_put_contents($root . '/src/BaseBox.php', <<<'PHP'
<?php
namespace ScopedFix;

class BaseBox
{
    public function ping(): int
    {
        return 1;
    }
}
PHP);

file_put_contents($root . '/src/Item.php', <<<'PHP'
<?php
namespace ScopedFix;

final class Item
{
    public const MAX = 10;

    public int $count = 0;

    public function bar(): int
    {
        return 1;
    }
}
PHP);

$demo = <<<'PHP'
<?php
namespace ScopedFix;

final class Box extends BaseBox
{
    public string $name = 'prop';

    /**
     * @param Item[] $items
     */
    public function walk(array $items, Item $one): int
    {
        $name = 'local';
        echo $name;
        echo $this->name;
        $limit = Item::MAX;
        $n = $one->count;
        $p = $this->ping();
        foreach ($items as $item) {
            $item->bar();
        }
        return 0;
    }
}
PHP;

file_put_contents($root . '/src/Box.php', $demo);
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n" .
    "    'ScopedFix\\\\BaseBox' => " . var_export($root . '/src/BaseBox.php', true) . ",\n" .
    "    'ScopedFix\\\\Item' => " . var_export($root . '/src/Item.php', true) . ",\n" .
    "    'ScopedFix\\\\Box' => " . var_export($root . '/src/Box.php', true) . ",\n" .
    "];\n");
file_put_contents($root . '/vendor/composer/autoload_namespaces.php', "<?php\nreturn [];\n");
file_put_contents($root . '/vendor/composer/autoload_psr4.php', "<?php\nreturn [];\n");
file_put_contents($root . '/composer.json', '{"autoload":{"classmap":["src/"]}}');
file_put_contents($runner, "<?php\nLSParrot\\start_lsp(['analyzer' => 'lsparrot', 'workers' => ['count' => 1], 'symbolIndex' => ['size' => '4M']]);\n");

$uri = 'file://' . $root . '/src/Box.php';
$messages = [
    ['jsonrpc' => '2.0', 'id' => 1, 'method' => 'initialize', 'params' => ['rootUri' => 'file://' . $root]],
    ['jsonrpc' => '2.0', 'method' => 'textDocument/didOpen', 'params' => ['textDocument' => ['uri' => $uri, 'languageId' => 'php', 'version' => 1, 'text' => $demo]]],
    // Inherited method hover through $this.
    ['jsonrpc' => '2.0', 'id' => 2, 'method' => 'textDocument/hover', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($demo, '$p = $this->pi')]],
    // Constant colliding with builtin max(); property colliding with count().
    ['jsonrpc' => '2.0', 'id' => 3, 'method' => 'textDocument/hover', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($demo, 'Item::MA')]],
    ['jsonrpc' => '2.0', 'id' => 4, 'method' => 'textDocument/hover', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($demo, '$one->cou')]],
    // foreach element member completion from @param Item[].
    ['jsonrpc' => '2.0', 'id' => 5, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($demo, '$item->')]],
    // Highlight on the local $name must exclude the property and $this->name.
    ['jsonrpc' => '2.0', 'id' => 6, 'method' => 'textDocument/documentHighlight', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($demo, 'echo $na')]],
    ['jsonrpc' => '2.0', 'id' => 7, 'method' => 'shutdown', 'params' => []],
    ['jsonrpc' => '2.0', 'method' => 'exit', 'params' => []],
];

[$stdout, $stderr, $code] = run_lsp($extension, $runner, $messages);
$decoded = lsp_messages($stdout);
$inherited = (string) (lsp_response($decoded, 2)['contents']['value'] ?? '');
$constHover = (string) (lsp_response($decoded, 3)['contents']['value'] ?? '');
$propHover = (string) (lsp_response($decoded, 4)['contents']['value'] ?? '');
$foreachItem = lsp_item(lsp_response($decoded, 5), 'bar');
$highlights = lsp_response($decoded, 6) ?? [];

$propertyDeclLine = 5;
$localLines = array_map(static fn($h) => $h['range']['start']['line'] ?? -1, $highlights);

if ($code !== 0) {
    echo "FAILED: process exit\n";
    var_dump($code, $stderr);
} elseif (!str_contains($inherited, 'ping')) {
    echo "FAILED: inherited method hover\n";
    var_dump($inherited);
} elseif (!str_contains($constHover, 'const MAX') || str_contains($constHover, 'function max')) {
    echo "FAILED: class constant hover shadowed by builtin max()\n";
    var_dump($constHover);
} elseif (!str_contains($propHover, 'count') || str_contains($propHover, 'function count')) {
    echo "FAILED: property hover shadowed by builtin count()\n";
    var_dump($propHover);
} elseif (!$foreachItem) {
    echo "FAILED: foreach element type from @param Item[] did not complete\n";
    var_dump(lsp_response($decoded, 5));
} elseif (count($highlights) !== 2 || in_array($propertyDeclLine, $localLines, true)) {
    echo "FAILED: local variable highlight merged with the same-named property\n";
    var_dump($highlights);
} else {
    echo "OK\n";
}

rrmdir($root);
?>
--EXPECT--
OK
