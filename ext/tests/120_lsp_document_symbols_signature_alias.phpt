--TEST--
LSP returns hierarchical document symbols, named-argument signature help, and alias-aware completion
--EXTENSIONS--
lsparrot
--FILE--
<?php
$root = '/tmp/lsp-symbols-signature-alias-test';
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

function lsp_symbol(?array $symbols, string $name): ?array {
    foreach (($symbols ?? []) as $symbol) {
        if (($symbol['name'] ?? null) === $name) {
            return $symbol;
        }
    }

    return null;
}

function lsp_item_by_detail(?array $result, string $label, string $detailNeedle): ?array {
    foreach (($result['items'] ?? []) as $item) {
        if (($item['label'] ?? null) === $label && str_contains((string) ($item['detail'] ?? ''), $detailNeedle)) {
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
@mkdir($root . '/src/Lib', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);

file_put_contents($root . '/src/Lib/Widget.php', <<<'PHP'
<?php
namespace SymFix\Lib;

final class Widget
{
    public function spin(): int
    {
        return 1;
    }
}
PHP);

$demo = <<<'PHP'
<?php
namespace SymFix;

use SymFix\Lib\Widget as W;

final class Editor
{
    public function make(int $alpha, int $beta, int $gamma = 0): int
    {
        $widget = new W();
        $again = Widge;
        return $this->make(gamma: 1);
    }
}

enum Suit
{
    case Hearts;
}

function topLevel(): void
{
}
PHP;

file_put_contents($root . '/src/Editor.php', $demo);
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n" .
    "    'SymFix\\\\Lib\\\\Widget' => " . var_export($root . '/src/Lib/Widget.php', true) . ",\n" .
    "    'SymFix\\\\Editor' => " . var_export($root . '/src/Editor.php', true) . ",\n" .
    "];\n");
file_put_contents($root . '/vendor/composer/autoload_namespaces.php', "<?php\nreturn [];\n");
file_put_contents($root . '/vendor/composer/autoload_psr4.php', "<?php\nreturn [];\n");
file_put_contents($root . '/composer.json', '{"autoload":{"classmap":["src/"]}}');
file_put_contents($runner, "<?php\nLSParrot\\start_lsp(['analyzer' => 'lsparrot', 'workers' => ['count' => 1], 'symbolIndex' => ['size' => '4M']]);\n");

$uri = 'file://' . $root . '/src/Editor.php';
$messages = [
    ['jsonrpc' => '2.0', 'id' => 1, 'method' => 'initialize', 'params' => ['rootUri' => 'file://' . $root]],
    ['jsonrpc' => '2.0', 'method' => 'textDocument/didOpen', 'params' => ['textDocument' => ['uri' => $uri, 'languageId' => 'php', 'version' => 1, 'text' => $demo]]],
    ['jsonrpc' => '2.0', 'id' => 2, 'method' => 'textDocument/documentSymbol', 'params' => ['textDocument' => ['uri' => $uri]]],
    // Cursor inside the named argument `gamma: 1`.
    ['jsonrpc' => '2.0', 'id' => 3, 'method' => 'textDocument/signatureHelp', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($demo, '$this->make(gamma: 1')]],
    // Widget is imported under the alias W: completion must insert the alias.
    ['jsonrpc' => '2.0', 'id' => 4, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($demo, '$again = Widge')]],
    ['jsonrpc' => '2.0', 'id' => 5, 'method' => 'shutdown', 'params' => []],
    ['jsonrpc' => '2.0', 'method' => 'exit', 'params' => []],
];

[$stdout, $stderr, $code] = run_lsp($extension, $runner, $messages);
$decoded = lsp_messages($stdout);
$symbols = lsp_response($decoded, 2);
$signature = lsp_response($decoded, 3);
$aliasCompletion = lsp_response($decoded, 4);

$editor = lsp_symbol($symbols, 'Editor');
$suit = lsp_symbol($symbols, 'Suit');
$topLevel = lsp_symbol($symbols, 'topLevel');
$make = lsp_symbol($editor['children'] ?? [], 'make');
$hearts = lsp_symbol($suit['children'] ?? [], 'Hearts');
$aliasItem = lsp_item_by_detail($aliasCompletion, 'W', 'SymFix\Lib\Widget');
$aliasNewText = (string) ($aliasItem['textEdit']['newText'] ?? '');

if ($code !== 0) {
    echo "FAILED: process exit\n";
    var_dump($code, $stderr);
} elseif (!$editor || ($editor['kind'] ?? 0) !== 5 || !$make || ($make['kind'] ?? 0) !== 6) {
    echo "FAILED: class symbol should nest its methods as children\n";
    var_dump($symbols);
} elseif (($editor['range']['end']['line'] ?? 0) <= ($editor['range']['start']['line'] ?? 0)) {
    echo "FAILED: class range should span the whole declaration\n";
    var_dump($editor);
} elseif (($editor['selectionRange']['start']['character'] ?? 0) >= ($editor['selectionRange']['end']['character'] ?? 0)) {
    echo "FAILED: selectionRange should cover the symbol name\n";
    var_dump($editor);
} elseif (!$suit || ($suit['kind'] ?? 0) !== 10 || !$hearts || ($hearts['kind'] ?? 0) !== 22) {
    echo "FAILED: enum symbol should nest its cases\n";
    var_dump($suit);
} elseif (!$topLevel || ($topLevel['kind'] ?? 0) !== 12) {
    echo "FAILED: top-level function symbol missing\n";
    var_dump($symbols);
} elseif ((int) ($signature['activeParameter'] ?? -1) !== 2) {
    echo "FAILED: named argument gamma should activate parameter index 2\n";
    var_dump($signature);
} elseif (!$aliasItem || $aliasNewText !== 'W' || isset($aliasItem['additionalTextEdits'])) {
    echo "FAILED: aliased import should complete under its alias without a new use\n";
    var_dump($aliasCompletion);
} else {
    echo "OK\n";
}

rrmdir($root);
?>
--EXPECT--
OK
