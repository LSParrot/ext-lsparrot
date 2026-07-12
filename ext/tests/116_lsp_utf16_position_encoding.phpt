--TEST--
LSP converts positions between UTF-16 code units and UTF-8 byte offsets
--EXTENSIONS--
lsparrot
--FILE--
<?php
$root = '/tmp/lsp-utf16-position-test';
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

function utf16_units(string $s): int {
    $units = 0;
    $i = 0;
    $n = strlen($s);
    while ($i < $n) {
        $c = ord($s[$i]);
        if ($c < 0x80) {
            $i += 1;
            $units += 1;
        } elseif (($c & 0xE0) === 0xC0) {
            $i += 2;
            $units += 1;
        } elseif (($c & 0xF0) === 0xE0) {
            $i += 3;
            $units += 1;
        } elseif (($c & 0xF8) === 0xF0) {
            $i += 4;
            $units += 2;
        } else {
            $i += 1;
            $units += 1;
        }
    }

    return $units;
}

function lsp_utf16_position_after(string $text, string $needle): array {
    $offset = strpos($text, $needle);
    if ($offset === false) {
        return ['line' => 0, 'character' => 0];
    }
    $offset += strlen($needle);
    $before = substr($text, 0, $offset);
    $line = substr_count($before, "\n");
    $lineStart = strrpos($before, "\n");
    $lineStart = $lineStart === false ? 0 : $lineStart + 1;

    return ['line' => $line, 'character' => utf16_units(substr($text, $lineStart, $offset - $lineStart))];
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

$demo = <<<'PHP'
<?php
namespace Utf16Fixture;

final class Calculator
{
    public int $price = 100;

    public function total(): int
    {
        $メモ = "計算する"; $result = $this->price;
        return $result;
    }
}
PHP;

file_put_contents($root . '/src/Calculator.php', $demo);
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'Utf16Fixture\\\\Calculator' => " . var_export($root . '/src/Calculator.php', true) . ",\n];\n");
file_put_contents($root . '/vendor/composer/autoload_namespaces.php', "<?php\nreturn [];\n");
file_put_contents($root . '/vendor/composer/autoload_psr4.php', "<?php\nreturn [];\n");
file_put_contents($root . '/composer.json', '{"autoload":{"classmap":["src/"]}}');
file_put_contents($runner, "<?php\nLSParrot\\start_lsp(['analyzer' => 'lsparrot', 'workers' => ['count' => 0], 'symbolIndex' => ['size' => '4M']]);\n");

$uri = 'file://' . $root . '/src/Calculator.php';
$messages = [
    ['jsonrpc' => '2.0', 'id' => 1, 'method' => 'initialize', 'params' => ['rootUri' => 'file://' . $root]],
    ['jsonrpc' => '2.0', 'method' => 'textDocument/didOpen', 'params' => ['textDocument' => ['uri' => $uri, 'languageId' => 'php', 'version' => 1, 'text' => $demo]]],
    // Hover over $this->price on a line that contains multibyte text before it.
    ['jsonrpc' => '2.0', 'id' => 2, 'method' => 'textDocument/hover', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_utf16_position_after($demo, '$result = $this->pri')]],
    // Highlight $result on the same multibyte line; ranges must come back in UTF-16 units.
    ['jsonrpc' => '2.0', 'id' => 3, 'method' => 'textDocument/documentHighlight', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_utf16_position_after($demo, '"計算する"; $res')]],
    ['jsonrpc' => '2.0', 'id' => 4, 'method' => 'shutdown', 'params' => []],
    ['jsonrpc' => '2.0', 'method' => 'exit', 'params' => []],
];

[$stdout, $stderr, $code] = run_lsp($extension, $runner, $messages);
$decoded = lsp_messages($stdout);
$hover = lsp_response($decoded, 2);
$highlights = lsp_response($decoded, 3);

$hoverText = (string) ($hover['contents']['value'] ?? '');

$declarationLine = '        $メモ = "計算する"; $result = $this->price;';
$expectedLine = 9;
$expectedStart = utf16_units(substr($declarationLine, 0, strpos($declarationLine, '$result')));
$declarationHighlight = null;
foreach (($highlights ?? []) as $highlight) {
    if (($highlight['range']['start']['line'] ?? -1) === $expectedLine) {
        $declarationHighlight = $highlight;
        break;
    }
}
$start = $declarationHighlight['range']['start']['character'] ?? -1;
$end = $declarationHighlight['range']['end']['character'] ?? -1;

if ($code !== 0) {
    echo "FAILED: process exit\n";
    var_dump($code, $stderr);
} elseif (!str_contains($hoverText, 'price')) {
    echo "FAILED: hover position after multibyte text resolved the wrong word\n";
    var_dump($hover);
} elseif (!$declarationHighlight || $start !== $expectedStart || $end - $start !== strlen('$result')) {
    echo "FAILED: highlight range not reported in UTF-16 code units\n";
    var_dump($expectedStart, $highlights);
} else {
    echo "OK\n";
}

rrmdir($root);
?>
--EXPECT--
OK
