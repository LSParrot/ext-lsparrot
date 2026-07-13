--TEST--
LSP keeps same-short-name classes from different namespaces apart in completion
--EXTENSIONS--
lsparrot
--FILE--
<?php
$root = '/tmp/lsp-same-short-name-test';
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
@mkdir($root . '/src/Remote', 0777, true);
@mkdir($root . '/src/App', 0777, true);
@mkdir($root . '/src/Globals', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);

file_put_contents($root . '/src/Lib/Widget.php', <<<'PHP'
<?php
namespace ShortNameFixture\Lib;

final class Widget
{
    public function spin(): int
    {
        return 1;
    }
}
PHP);

file_put_contents($root . '/src/Remote/Widget.php', <<<'PHP'
<?php
namespace ShortNameFixture\Remote;

final class Widget
{
    public function orbit(): int
    {
        return 2;
    }
}
PHP);

file_put_contents($root . '/src/Globals/LegacyThing.php', <<<'PHP'
<?php
final class LegacyThing
{
    public function crank(): int
    {
        return 3;
    }
}
PHP);

$demo = <<<'PHP'
<?php
namespace ShortNameFixture\App;

use ShortNameFixture\Lib\Widget;

final class Editor
{
    public function fly(\ShortNameFixture\Lib\Widget $gadget): int
    {
        $a = Widge;
        $b = LegacyThi;
        return $gadget->sp
    }
}
PHP;

file_put_contents($root . '/src/App/Editor.php', $demo);
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n" .
    "    'ShortNameFixture\\\\Lib\\\\Widget' => " . var_export($root . '/src/Lib/Widget.php', true) . ",\n" .
    "    'ShortNameFixture\\\\Remote\\\\Widget' => " . var_export($root . '/src/Remote/Widget.php', true) . ",\n" .
    "    'LegacyThing' => " . var_export($root . '/src/Globals/LegacyThing.php', true) . ",\n" .
    "    'ShortNameFixture\\\\App\\\\Editor' => " . var_export($root . '/src/App/Editor.php', true) . ",\n" .
    "];\n");
file_put_contents($root . '/vendor/composer/autoload_namespaces.php', "<?php\nreturn [];\n");
file_put_contents($root . '/vendor/composer/autoload_psr4.php', "<?php\nreturn [];\n");
file_put_contents($root . '/composer.json', '{"autoload":{"classmap":["src/"]}}');
file_put_contents($runner, "<?php\nLSParrot\\start_lsp(['analyzer' => 'lsparrot', 'workers' => ['count' => 1], 'symbolIndex' => ['size' => '4M']]);\n");

$uri = 'file://' . $root . '/src/App/Editor.php';
$messages = [
    ['jsonrpc' => '2.0', 'id' => 1, 'method' => 'initialize', 'params' => ['rootUri' => 'file://' . $root]],
    ['jsonrpc' => '2.0', 'method' => 'textDocument/didOpen', 'params' => ['textDocument' => ['uri' => $uri, 'languageId' => 'php', 'version' => 1, 'text' => $demo]]],
    ['jsonrpc' => '2.0', 'id' => 2, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($demo, '$gadget->sp')]],
    ['jsonrpc' => '2.0', 'id' => 3, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($demo, '$a = Widge')]],
    ['jsonrpc' => '2.0', 'id' => 4, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($demo, '$b = LegacyThi')]],
    ['jsonrpc' => '2.0', 'id' => 5, 'method' => 'shutdown', 'params' => []],
];

[$stdout, $stderr, $code] = run_lsp($extension, $runner, $messages);
$decoded = lsp_messages($stdout);
$memberCompletion = lsp_response($decoded, 2);
$widgetCompletion = lsp_response($decoded, 3);
$legacyCompletion = lsp_response($decoded, 4);

$spin = lsp_item($memberCompletion, 'spin');
$libWidget = lsp_item_by_detail($widgetCompletion, 'Widget', 'ShortNameFixture\Lib\Widget');
$remoteWidget = lsp_item_by_detail($widgetCompletion, 'Widget', 'ShortNameFixture\Remote\Widget');
$legacy = lsp_item($legacyCompletion, 'LegacyThing');

$remoteNewText = (string) ($remoteWidget['textEdit']['newText'] ?? '');
$remoteImports = $remoteWidget['additionalTextEdits'] ?? [];
$libNewText = (string) ($libWidget['textEdit']['newText'] ?? '');
$libImports = $libWidget['additionalTextEdits'] ?? [];
$legacyImport = (string) ($legacy['additionalTextEdits'][0]['newText'] ?? '');

if ($code !== 0) {
    echo "FAILED: process exit\n";
    var_dump($code, $stderr);
} elseif (!$spin) {
    echo "FAILED: fully qualified parameter type did not resolve to Lib\\Widget\n";
    var_dump($memberCompletion);
} elseif (!$libWidget || $libNewText !== 'Widget' || $libImports !== []) {
    echo "FAILED: imported Lib\\Widget should insert its short name without a new import\n";
    var_dump($libWidget);
} elseif (!$remoteWidget || $remoteNewText !== '\ShortNameFixture\Remote\Widget' || $remoteImports !== []) {
    echo "FAILED: conflicting Remote\\Widget should insert its FQCN without a use edit\n";
    var_dump($remoteWidget);
} elseif (!$legacy || !str_contains($legacyImport, 'use LegacyThing;')) {
    echo "FAILED: global class completed inside a namespace should add a use import\n";
    var_dump($legacy);
} else {
    echo "OK\n";
}

rrmdir($root);
?>
--EXPECT--
OK
