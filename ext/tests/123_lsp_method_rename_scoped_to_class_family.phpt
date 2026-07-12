--TEST--
LSP method rename stays inside the class family instead of renaming same-named methods everywhere
--EXTENSIONS--
lsparrot
--FILE--
<?php
$root = '/tmp/lsp-method-rename-family-test';
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
@mkdir($root . '/src', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);

$alpha = <<<'PHP'
<?php
namespace RenameFamily;

class Alpha
{
    public function process(): int
    {
        return 1;
    }

    public function callIt(): int
    {
        return $this->process();
    }
}
PHP;
file_put_contents($root . '/src/Alpha.php', $alpha);

// Same-named method on a class with no inheritance relation to Alpha.
$beta = <<<'PHP'
<?php
namespace RenameFamily;

class Beta
{
    public function process(): int
    {
        return 2;
    }

    public function callIt(): int
    {
        return $this->process();
    }
}
PHP;
file_put_contents($root . '/src/Beta.php', $beta);

// Call site with an Alpha-typed receiver: must be renamed with the method.
$caller = <<<'PHP'
<?php
namespace RenameFamily;

class Caller
{
    public function run(Alpha $alpha): int
    {
        return $alpha->process();
    }
}
PHP;
file_put_contents($root . '/src/Caller.php', $caller);

file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n" .
    "    'RenameFamily\\\\Alpha' => " . var_export($root . '/src/Alpha.php', true) . ",\n" .
    "    'RenameFamily\\\\Beta' => " . var_export($root . '/src/Beta.php', true) . ",\n" .
    "    'RenameFamily\\\\Caller' => " . var_export($root . '/src/Caller.php', true) . ",\n" .
    "];\n");
file_put_contents($root . '/vendor/composer/autoload_namespaces.php', "<?php\nreturn [];\n");
file_put_contents($root . '/vendor/composer/autoload_psr4.php', "<?php\nreturn [];\n");
file_put_contents($root . '/composer.json', '{"autoload":{"classmap":["src/"]}}');
file_put_contents($runner, "<?php\nLSParrot\\start_lsp(['analyzer' => 'lsparrot', 'workers' => ['count' => 1], 'symbolIndex' => ['size' => '4M']]);\n");

$alphaUri = 'file://' . $root . '/src/Alpha.php';
$betaUri = 'file://' . $root . '/src/Beta.php';
$callerUri = 'file://' . $root . '/src/Caller.php';

$messages = [
    ['jsonrpc' => '2.0', 'id' => 1, 'method' => 'initialize', 'params' => ['rootUri' => 'file://' . $root]],
    ['jsonrpc' => '2.0', 'method' => 'textDocument/didOpen', 'params' => ['textDocument' => ['uri' => $alphaUri, 'languageId' => 'php', 'version' => 1, 'text' => $alpha]]],
    ['jsonrpc' => '2.0', 'id' => 2, 'method' => 'textDocument/rename', 'params' => ['textDocument' => ['uri' => $alphaUri], 'position' => lsp_position_after($alpha, 'public function proc'), 'newName' => 'processRenamed']],
    ['jsonrpc' => '2.0', 'id' => 3, 'method' => 'shutdown', 'params' => []],
    ['jsonrpc' => '2.0', 'method' => 'exit', 'params' => []],
];

[$stdout, $stderr, $code] = run_lsp($extension, $runner, $messages);
$decoded = lsp_messages($stdout);
$rename = lsp_response($decoded, 2);
$changes = $rename['changes'] ?? [];

$alphaEdits = $changes[$alphaUri] ?? [];
$callerEdits = $changes[$callerUri] ?? [];

if ($code !== 0) {
    echo "FAILED: process exit\n";
    var_dump($code, $stderr);
} elseif (isset($changes[$betaUri])) {
    echo "FAILED: rename bled into unrelated class Beta\n";
    var_dump($changes[$betaUri]);
} elseif (count($alphaEdits) !== 2) {
    echo "FAILED: expected declaration + \$this-> call edits in Alpha.php\n";
    var_dump($alphaEdits);
} elseif (count($callerEdits) !== 1) {
    echo "FAILED: expected the Alpha-typed call site in Caller.php to be renamed\n";
    var_dump($changes);
} else {
    echo "OK\n";
}

rrmdir($root);
?>
--EXPECT--
OK
