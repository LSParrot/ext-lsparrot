--TEST--
LSP returns viewport-scoped variable type inlay hints alongside parameter name hints
--EXTENSIONS--
lsparrot
--FILE--
<?php
$root = '/tmp/lsp-inlay-type-hint-test';
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

function lsp_line_character(string $text, int $offset): array {
    $before = substr($text, 0, $offset);
    $line = substr_count($before, "\n");
    $lineStart = strrpos($before, "\n");

    return ['line' => $line, 'character' => $lineStart === false ? strlen($before) : strlen($before) - $lineStart - 1];
}

function lsp_line_of(string $text, string $needle): int {
    $offset = strpos($text, $needle);
    if ($offset === false) {
        return -1;
    }

    return substr_count(substr($text, 0, $offset), "\n");
}

function lsp_position_after_variable(string $text, string $statementNeedle, string $variable): array {
    $statementOffset = strpos($text, $statementNeedle);
    if ($statementOffset === false) {
        return ['line' => -1, 'character' => -1];
    }

    return lsp_line_character($text, $statementOffset + strlen($variable));
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

function lsp_find_hint(array $hints, string $label, int $kind, ?array $position = null): ?array {
    foreach ($hints as $hint) {
        if (($hint['label'] ?? null) !== $label || ($hint['kind'] ?? null) !== $kind) {
            continue;
        }
        if ($position !== null) {
            $hintPosition = $hint['position'] ?? [];
            if (($hintPosition['line'] ?? null) !== $position['line'] || ($hintPosition['character'] ?? null) !== $position['character']) {
                continue;
            }
        }

        return $hint;
    }

    return null;
}

function lsp_find_hint_on_line(array $hints, string $label, int $kind, int $line): ?array {
    foreach ($hints as $hint) {
        if (($hint['label'] ?? null) === $label && ($hint['kind'] ?? null) === $kind && (($hint['position']['line'] ?? null) === $line)) {
            return $hint;
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

$source = <<<'PHP'
<?php

namespace InlayTypeHintFixture;

class Leaf
{
    public function setNotes(string $notes): void
    {
    }
}

class Container
{
    private Leaf $leaf;

    public function __construct(Leaf $leaf)
    {
        $this->leaf = $leaf;
    }

    public function outOfRange(): void
    {
        $stray = new Leaf();
    }

    public function run(): void
    {
        $leaf = new Leaf();
        $leaf->setNotes('x');
        $copy = $this->leaf;
    }
}
PHP;

file_put_contents($root . '/src/Container.php', $source);
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'InlayTypeHintFixture\\\\Leaf' => " . var_export($root . '/src/Container.php', true) . ",\n    'InlayTypeHintFixture\\\\Container' => " . var_export($root . '/src/Container.php', true) . ",\n];\n");
file_put_contents($root . '/vendor/composer/autoload_namespaces.php', "<?php\nreturn [];\n");
file_put_contents($root . '/vendor/composer/autoload_psr4.php', "<?php\nreturn [];\n");
file_put_contents($root . '/composer.json', '{"autoload":{"classmap":["src/"]}}');
file_put_contents($runner, "<?php\nLSParrot\\start_lsp(['analyzer' => 'lsparrot', 'workers' => ['count' => 1], 'symbolIndex' => ['size' => '4M']]);\n");

$uri = 'file://' . $root . '/src/Container.php';
$rangeStartLine = lsp_line_of($source, 'public function run');
$rangeEndLine = substr_count($source, "\n") + 5;

$messages = [
    ['jsonrpc' => '2.0', 'id' => 1, 'method' => 'initialize', 'params' => ['rootUri' => 'file://' . $root]],
    ['jsonrpc' => '2.0', 'method' => 'textDocument/didOpen', 'params' => ['textDocument' => ['uri' => $uri, 'languageId' => 'php', 'version' => 1, 'text' => $source]]],
    ['jsonrpc' => '2.0', 'id' => 2, 'method' => 'textDocument/inlayHint', 'params' => ['textDocument' => ['uri' => $uri], 'range' => ['start' => ['line' => $rangeStartLine, 'character' => 0], 'end' => ['line' => $rangeEndLine, 'character' => 0]]]],
    ['jsonrpc' => '2.0', 'id' => 3, 'method' => 'shutdown', 'params' => []],
];

[$stdout, $stderr, $code] = run_lsp($extension, $runner, $messages);
$decoded = lsp_messages($stdout);
$hints = lsp_response($decoded, 2) ?? [];

$leafPosition = lsp_position_after_variable($source, '$leaf = new Leaf();', '$leaf');
$copyPosition = lsp_position_after_variable($source, '$copy = $this->leaf;', '$copy');
$strayLine = lsp_line_of($source, '$stray = new Leaf();');

$leafHint = lsp_find_hint($hints, ': Leaf', 1, $leafPosition);
$copyHint = lsp_find_hint($hints, ': Leaf', 1, $copyPosition);
$notesHint = lsp_find_hint($hints, 'notes:', 2);
$strayHint = lsp_find_hint_on_line($hints, ': Leaf', 1, $strayLine);

if ($code !== 0) {
    echo "FAILED: process exit\n";
    var_dump($code, $stderr);
} elseif (!$leafHint) {
    echo "FAILED: missing type hint for \$leaf assignment\n";
    var_dump($hints);
} elseif (!$copyHint) {
    echo "FAILED: missing type hint for \$copy assignment\n";
    var_dump($hints);
} elseif (!$notesHint) {
    echo "FAILED: missing parameter name hint for setNotes call\n";
    var_dump($hints);
} elseif ($strayHint) {
    echo "FAILED: out-of-range assignment unexpectedly received a hint\n";
    var_dump($hints);
} else {
    echo "OK\n";
}

rrmdir($root);
?>
--EXPECT--
OK
