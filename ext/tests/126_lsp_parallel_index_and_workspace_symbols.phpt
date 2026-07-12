--TEST--
LSP builds the initial index across multiple workers and answers capped workspace/symbol queries
--EXTENSIONS--
lsparrot
--FILE--
<?php
$root = '/tmp/lsp-parallel-index-test';
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

function collect_symbols(string $root, string $extension, int $workers): array {
    rrmdir($root . '/.lsparrot');
    $runner = $root . '/run.php';
    file_put_contents($runner, "<?php\nLSParrot\\start_lsp(['analyzer' => 'lsparrot', 'workers' => ['count' => $workers], 'symbolIndex' => ['size' => '8M']]);\n");
    $messages = [
        ['jsonrpc' => '2.0', 'id' => 1, 'method' => 'initialize', 'params' => ['rootUri' => 'file://' . $root]],
        ['jsonrpc' => '2.0', 'id' => 2, 'method' => 'workspace/symbol', 'params' => ['query' => '']],
        ['jsonrpc' => '2.0', 'id' => 3, 'method' => 'workspace/symbol', 'params' => ['query' => 'UnqiueZebra']],
        ['jsonrpc' => '2.0', 'id' => 99, 'method' => 'shutdown', 'params' => []],
        ['jsonrpc' => '2.0', 'method' => 'exit', 'params' => []],
    ];
    [$stdout, , $code] = run_lsp($extension, $runner, $messages);
    $decoded = lsp_messages($stdout);
    $all = lsp_response($decoded, 2) ?? [];
    $zebra = lsp_response($decoded, 3) ?? [];
    $names = array_map(static fn($s) => ($s['kind'] ?? 0) . ':' . ($s['name'] ?? ''), $all);
    sort($names);

    return [$names, array_map(static fn($s) => $s['name'] ?? '', $zebra), $code];
}

rrmdir($root);
@mkdir($root . '/src', 0777, true);
@mkdir($root . '/lib', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);

// Enough work items (40 classmap entries + 30 PSR-4 files) to cross the
// parallel threshold; one deliberately unique symbol for the filtered query.
$classmapEntries = '';
for ($i = 0; $i < 40; $i++) {
    file_put_contents($root . "/src/Cm$i.php", "<?php\nnamespace Cm;\n\nclass Cm$i\n{\n    public function m(): int { return $i; }\n}\n");
    $classmapEntries .= "    'Cm\\\\Cm$i' => " . var_export($root . "/src/Cm$i.php", true) . ",\n";
}
for ($i = 0; $i < 30; $i++) {
    $extra = $i === 7 ? "\nclass UnqiueZebraHolder {}\nfunction unqiue_zebra_fn(): int { return 1; }\n" : '';
    file_put_contents($root . "/lib/Lib$i.php", "<?php\nnamespace Lib;\n$extra\ninterface ILib$i {}\n\nenum ELib$i { case One; }\n");
}
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n$classmapEntries];\n");
file_put_contents($root . '/vendor/composer/autoload_psr4.php', "<?php\nreturn ['Lib\\\\' => [" . var_export($root . '/lib', true) . "]];\n");
file_put_contents($root . '/vendor/composer/autoload_namespaces.php', "<?php\nreturn [];\n");
file_put_contents($root . '/composer.json', '{"autoload":{"classmap":["src/"],"psr-4":{"Lib\\\\":"lib/"}}}');

[$serial, $serialZebra, $serialCode] = collect_symbols($root, $extension, 1);
[$parallel, $parallelZebra, $parallelCode] = collect_symbols($root, $extension, 4);

sort($serialZebra);
sort($parallelZebra);

if ($serialCode !== 0 || $parallelCode !== 0) {
    echo "FAILED: process exit\n";
    var_dump($serialCode, $parallelCode);
} elseif (count($serial) < 100) {
    echo "FAILED: expected the full corpus in the serial index\n";
    var_dump(count($serial));
} elseif ($serial !== $parallel) {
    echo "FAILED: parallel index must produce the same symbols as the serial scan\n";
    var_dump(array_values(array_diff($serial, $parallel)), array_values(array_diff($parallel, $serial)));
} elseif ($serialZebra !== $parallelZebra || count($parallelZebra) < 2) {
    echo "FAILED: filtered workspace/symbol query\n";
    var_dump($serialZebra, $parallelZebra);
} else {
    echo "OK\n";
}

rrmdir($root);
?>
--EXPECT--
OK
