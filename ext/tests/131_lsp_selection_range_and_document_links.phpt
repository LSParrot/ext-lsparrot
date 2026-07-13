--TEST--
LSP selection range chains and require/include document links
--EXTENSIONS--
lsparrot
--FILE--
<?php
$root = '/tmp/lsp-selection-links-test';
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
file_put_contents($root . '/composer.json', '{}');
file_put_contents($root . '/helper.php', "<?php\nreturn 1;\n");

$doc = <<<'PHP'
<?php
require __DIR__ . '/helper.php';
require 'helper.php';
require 'missing.php';

class A
{
    public function f(int $count): int
    {
        return max($count, 1);
    }
}
PHP;
file_put_contents($root . '/Main.php', $doc);
file_put_contents($runner, "<?php\nLSParrot\\start_lsp(['analyzer' => 'lsparrot', 'workers' => ['count' => 1], 'symbolIndex' => ['size' => '4M']]);\n");

$uri = 'file://' . $root . '/Main.php';
$messages = [
    ['jsonrpc' => '2.0', 'id' => 1, 'method' => 'initialize', 'params' => ['rootUri' => 'file://' . $root]],
    ['jsonrpc' => '2.0', 'method' => 'textDocument/didOpen', 'params' => ['textDocument' => ['uri' => $uri, 'languageId' => 'php', 'version' => 1, 'text' => $doc]]],
    ['jsonrpc' => '2.0', 'id' => 2, 'method' => 'textDocument/selectionRange', 'params' => ['textDocument' => ['uri' => $uri], 'positions' => [lsp_position_after($doc, 'return max($cou')]]],
    ['jsonrpc' => '2.0', 'id' => 3, 'method' => 'textDocument/documentLink', 'params' => ['textDocument' => ['uri' => $uri]]],
    ['jsonrpc' => '2.0', 'id' => 9, 'method' => 'shutdown', 'params' => []],
    ['jsonrpc' => '2.0', 'method' => 'exit', 'params' => []],
];

[$stdout, $stderr, $code] = run_lsp($extension, $runner, $messages);
$decoded = lsp_messages($stdout);
$init = lsp_response($decoded, 1);
$selection = lsp_response($decoded, 2);
$links = lsp_response($decoded, 3) ?? [];

$capabilities = $init['capabilities'] ?? [];

// Walk the nested chain from the innermost node.
$chain = [];
$node = $selection[0] ?? null;
while (is_array($node)) {
    $chain[] = $node['range'] ?? null;
    $node = $node['parent'] ?? null;
}

$innermost = $chain[0] ?? null;
$outermost = $chain[count($chain) - 1] ?? null;
$widening = true;
for ($i = 1; $i < count($chain); $i++) {
    $prev = $chain[$i - 1];
    $next = $chain[$i];
    $startsEarlier = $next['start']['line'] < $prev['start']['line'] ||
        ($next['start']['line'] === $prev['start']['line'] && $next['start']['character'] <= $prev['start']['character']);
    $endsLater = $next['end']['line'] > $prev['end']['line'] ||
        ($next['end']['line'] === $prev['end']['line'] && $next['end']['character'] >= $prev['end']['character']);
    if (!$startsEarlier || !$endsLater) {
        $widening = false;
        break;
    }
}

$linkTargets = array_map(static fn($l) => basename($l['target'] ?? ''), $links);
sort($linkTargets);

if ($code !== 0) {
    echo "FAILED: process exit\n";
    var_dump($code, $stderr);
} elseif (($capabilities['selectionRangeProvider'] ?? null) !== true || !isset($capabilities['documentLinkProvider'])) {
    echo "FAILED: capabilities\n";
    var_dump($capabilities);
} elseif (count($chain) < 4) {
    echo "FAILED: selection chain should nest word -> args -> body -> class -> document\n";
    var_dump($chain);
} elseif (($innermost['start']['line'] ?? -1) !== 9 || !$widening) {
    echo "FAILED: chain must start at the word under the cursor and strictly widen\n";
    var_dump($chain);
} elseif (($outermost['start']['line'] ?? -1) !== 0) {
    echo "FAILED: outermost range must cover the document\n";
    var_dump($outermost);
} elseif ($linkTargets !== ['helper.php', 'helper.php']) {
    echo "FAILED: document links must resolve existing require targets only\n";
    var_dump($linkTargets);
} else {
    echo "OK\n";
}

rrmdir($root);
?>
--EXPECT--
OK
