--TEST--
LSP folding ranges, type definition, declaration alias, live configuration, outline cache, and property-fetch inference
--EXTENSIONS--
lsparrot
--FILE--
<?php
$root = '/tmp/lsp-protocol-additions-test';
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

file_put_contents($root . '/src/Leaf.php', <<<'PHP'
<?php
namespace ProtoAdd;

class Leaf
{
    public function spin(): int
    {
        return 1;
    }

    public function setNotes(string $notes): void
    {
    }
}
PHP);

$host = <<<'PHP'
<?php
namespace ProtoAdd;

/**
 * Multi
 * line doc.
 */
class Host
{
    private Leaf $leaf;

    public function entry(Leaf $param): void
    {
        $var = $this->leaf;
        $var->setNotes('x');
        $var->spin();
    }
}
PHP;
file_put_contents($root . '/src/Host.php', $host);
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n" .
    "    'ProtoAdd\\\\Leaf' => " . var_export($root . '/src/Leaf.php', true) . ",\n" .
    "    'ProtoAdd\\\\Host' => " . var_export($root . '/src/Host.php', true) . ",\n" .
    "];\n");
file_put_contents($root . '/vendor/composer/autoload_namespaces.php', "<?php\nreturn [];\n");
file_put_contents($root . '/vendor/composer/autoload_psr4.php', "<?php\nreturn [];\n");
file_put_contents($root . '/composer.json', '{"autoload":{"classmap":["src/"]}}');
file_put_contents($runner, "<?php\nLSParrot\\start_lsp(['analyzer' => 'lsparrot', 'workers' => ['count' => 1], 'symbolIndex' => ['size' => '4M']]);\n");

$uri = 'file://' . $root . '/src/Host.php';
$messages = [
    ['jsonrpc' => '2.0', 'id' => 1, 'method' => 'initialize', 'params' => ['rootUri' => 'file://' . $root]],
    ['jsonrpc' => '2.0', 'method' => 'textDocument/didOpen', 'params' => ['textDocument' => ['uri' => $uri, 'languageId' => 'php', 'version' => 1, 'text' => $host]]],
    ['jsonrpc' => '2.0', 'id' => 2, 'method' => 'textDocument/foldingRange', 'params' => ['textDocument' => ['uri' => $uri]]],
    // typeDefinition on a variable inferred from a property fetch.
    ['jsonrpc' => '2.0', 'id' => 3, 'method' => 'textDocument/typeDefinition', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($host, "\$var->setNotes('x');\n        \$va") ]],
    ['jsonrpc' => '2.0', 'id' => 4, 'method' => 'textDocument/declaration', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($host, 'public function entry(Le')]],
    // Member completion on the property-fetch-assigned variable.
    ['jsonrpc' => '2.0', 'id' => 5, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($host, "\$var->setNotes('x');\n        \$var->")]],
    // Live configuration switch to tabs must affect the next formatting run.
    ['jsonrpc' => '2.0', 'method' => 'workspace/didChangeConfiguration', 'params' => ['settings' => ['lsparrot' => ['formatting' => ['indentStyle' => 'tab']]]]],
    ['jsonrpc' => '2.0', 'id' => 6, 'method' => 'textDocument/formatting', 'params' => ['textDocument' => ['uri' => $uri], 'options' => ['tabSize' => 4, 'insertSpaces' => true]]],
    // Outline requested twice: the second answer comes from the cache.
    ['jsonrpc' => '2.0', 'id' => 7, 'method' => 'textDocument/documentSymbol', 'params' => ['textDocument' => ['uri' => $uri]]],
    ['jsonrpc' => '2.0', 'id' => 8, 'method' => 'textDocument/documentSymbol', 'params' => ['textDocument' => ['uri' => $uri]]],
    ['jsonrpc' => '2.0', 'id' => 9, 'method' => 'shutdown', 'params' => []],
    ['jsonrpc' => '2.0', 'method' => 'exit', 'params' => []],
];

[$stdout, $stderr, $code] = run_lsp($extension, $runner, $messages);
$decoded = lsp_messages($stdout);
$init = lsp_response($decoded, 1);
$folds = lsp_response($decoded, 2) ?? [];
$typeDefinition = lsp_response($decoded, 3);
$declaration = lsp_response($decoded, 4);
$completion = lsp_response($decoded, 5);
$formatted = lsp_response($decoded, 6);
$outlineFirst = lsp_response($decoded, 7);
$outlineSecond = lsp_response($decoded, 8);

$capabilities = $init['capabilities'] ?? [];
$commentFolds = array_filter($folds, static fn($f) => ($f['kind'] ?? '') === 'comment');
$labels = array_map(static fn($i) => $i['label'] ?? '', $completion['items'] ?? []);

if ($code !== 0) {
    echo "FAILED: process exit\n";
    var_dump($code, $stderr);
} elseif (($capabilities['foldingRangeProvider'] ?? null) !== true ||
    ($capabilities['typeDefinitionProvider'] ?? null) !== true ||
    ($capabilities['declarationProvider'] ?? null) !== true ||
    ($capabilities['positionEncoding'] ?? null) !== 'utf-16'
) {
    echo "FAILED: new capabilities missing from initialize\n";
    var_dump($capabilities);
} elseif (count($folds) < 3 || count($commentFolds) < 1) {
    echo "FAILED: folding ranges (braces + doc comment)\n";
    var_dump($folds);
} elseif (!str_ends_with($typeDefinition['uri'] ?? '', 'Leaf.php')) {
    echo "FAILED: typeDefinition on property-fetch-assigned variable\n";
    var_dump($typeDefinition);
} elseif (!str_ends_with($declaration['uri'] ?? '', 'Leaf.php')) {
    echo "FAILED: declaration alias\n";
    var_dump($declaration);
} elseif (!in_array('spin', $labels, true) || !in_array('setNotes', $labels, true) || count($labels) > 4) {
    echo "FAILED: member completion after \$var = \$this->leaf;\n";
    var_dump($labels);
} elseif (!str_contains($formatted[0]['newText'] ?? '', "\tpublic function entry")) {
    echo "FAILED: didChangeConfiguration must switch formatting to tabs\n";
    var_dump($formatted);
} elseif ($outlineFirst !== $outlineSecond || count($outlineFirst ?? []) === 0) {
    echo "FAILED: cached documentSymbol must match the fresh computation\n";
    var_dump($outlineFirst, $outlineSecond);
} else {
    echo "OK\n";
}

rrmdir($root);
?>
--EXPECT--
OK
