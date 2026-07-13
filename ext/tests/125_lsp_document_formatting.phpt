--TEST--
LSP formats documents (indentation, switch bodies, doc comments, heredoc safety) with configurable options
--EXTENSIONS--
lsparrot
--FILE--
<?php
$root = '/tmp/lsp-formatting-test';
require __DIR__ . "/lsp_test_helper.inc";
$extension = lsp_test_extension_path();

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

function lsp_response_message(array $messages, int $id): ?array {
    foreach ($messages as $message) {
        if (($message['id'] ?? null) === $id) {
            return $message;
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

function format_once(string $root, string $extension, string $optionsPhp, string $doc, array $extraRequests = []): array {
    $runner = $root . '/run.php';
    file_put_contents($runner, "<?php\nLSParrot\\start_lsp($optionsPhp);\n");
    $uri = 'file://' . $root . '/Target.php';
    $messages = [
        ['jsonrpc' => '2.0', 'id' => 1, 'method' => 'initialize', 'params' => ['rootUri' => 'file://' . $root]],
        ['jsonrpc' => '2.0', 'method' => 'textDocument/didOpen', 'params' => ['textDocument' => ['uri' => $uri, 'languageId' => 'php', 'version' => 1, 'text' => $doc]]],
        ['jsonrpc' => '2.0', 'id' => 2, 'method' => 'textDocument/formatting', 'params' => ['textDocument' => ['uri' => $uri], 'options' => ['tabSize' => 4, 'insertSpaces' => true]]],
    ];
    foreach ($extraRequests as $request) {
        $request['params']['textDocument'] = ['uri' => $uri];
        $messages[] = $request;
    }
    $messages[] = ['jsonrpc' => '2.0', 'id' => 99, 'method' => 'shutdown', 'params' => []];
    $messages[] = ['jsonrpc' => '2.0', 'method' => 'exit', 'params' => []];

    [$stdout, , $code] = run_lsp($extension, $runner, $messages);

    return [lsp_messages($stdout), $code];
}

rrmdir($root);
@mkdir($root, 0777, true);
file_put_contents($root . '/composer.json', '{}');

$doc = "<?php\nclass Demo\n{\npublic function run(array \$items): int\n{\n\$total = 0;   \nforeach (\$items as \$item) {\nif (\$item > 0) {\n\$total += \$item;\n}\n}\nswitch (\$total) {\ncase 0:\nreturn 0;\ndefault:\nreturn \$total;\n}\n}\n\n/**\n* Doc line.\n*/\npublic function chain(): string\n{\n\$sql = 'SELECT *'\n. ' FROM t';\n\$text = <<<EOT\n  keep\n    heredoc\nEOT;\nreturn \$sql . \$text;\n}\n}";

$expected = <<<'PHP'
<?php
class Demo
{
    public function run(array $items): int
    {
        $total = 0;
        foreach ($items as $item) {
            if ($item > 0) {
                $total += $item;
            }
        }
        switch ($total) {
            case 0:
                return 0;
            default:
                return $total;
        }
    }

    /**
     * Doc line.
     */
    public function chain(): string
    {
        $sql = 'SELECT *'
            . ' FROM t';
        $text = <<<EOT
  keep
    heredoc
EOT;
        return $sql . $text;
    }
}

PHP;

// 1. Default config: full reindent honoring client tabSize=4/spaces.
[$decoded, $code] = format_once($root, $extension, "['analyzer' => 'lsparrot']", $doc, [
    ['jsonrpc' => '2.0', 'id' => 3, 'method' => 'textDocument/rangeFormatting', 'params' => ['range' => ['start' => ['line' => 6, 'character' => 0], 'end' => ['line' => 10, 'character' => 1]], 'options' => ['tabSize' => 2, 'insertSpaces' => true]]],
]);
$init = lsp_response_message($decoded, 1)['result'] ?? [];
$full = lsp_response_message($decoded, 2)['result'] ?? null;
$range = lsp_response_message($decoded, 3)['result'] ?? null;
$rangeText = $range[0]['newText'] ?? '';

// 2. Disabled: capability off, request answers null.
[$decodedOff, $codeOff] = format_once($root, $extension, "['analyzer' => 'lsparrot', 'formatting' => ['enabled' => false]]", $doc);
$initOff = lsp_response_message($decodedOff, 1)['result'] ?? [];
$fullOffMessage = lsp_response_message($decodedOff, 2);

// 3. Tab style override ignores the client's insertSpaces.
[$decodedTab, $codeTab] = format_once($root, $extension, "['analyzer' => 'lsparrot', 'formatting' => ['indentStyle' => 'tab']]", $doc);
$fullTab = lsp_response_message($decodedTab, 2)['result'] ?? null;

if ($code !== 0 || $codeOff !== 0 || $codeTab !== 0) {
    echo "FAILED: process exit\n";
    var_dump($code, $codeOff, $codeTab);
} elseif (($init['capabilities']['documentFormattingProvider'] ?? null) !== true) {
    echo "FAILED: formatting capability should be advertised by default\n";
} elseif (($full[0]['newText'] ?? '') !== $expected) {
    echo "FAILED: full document formatting\n";
    var_dump($full[0]['newText'] ?? null);
} elseif (!str_contains($rangeText, "    foreach (\$items as \$item) {\n      if (\$item > 0) {\n        \$total += \$item;\n      }\n    }")) {
    echo "FAILED: range formatting with tabSize 2 limited to requested lines\n";
    var_dump($rangeText);
} elseif (($initOff['capabilities']['documentFormattingProvider'] ?? null) !== false) {
    echo "FAILED: formatting.enabled=false must clear the capability\n";
} elseif (!$fullOffMessage || array_key_exists('result', $fullOffMessage) === false || $fullOffMessage['result'] !== null) {
    echo "FAILED: disabled formatting must answer null\n";
    var_dump($fullOffMessage);
} elseif (!str_contains($fullTab[0]['newText'] ?? '', "\tpublic function run(array \$items): int\n\t{\n\t\t\$total = 0;")) {
    echo "FAILED: indentStyle=tab override\n";
    var_dump($fullTab[0]['newText'] ?? null);
} else {
    echo "OK\n";
}

rrmdir($root);
?>
--EXPECT--
OK
