--TEST--
LSP semantic tokens legend advertises modifiers and full response tags declarations with static/readonly/deprecated bits
--EXTENSIONS--
lsparrot
--FILE--
<?php
$root = '/tmp/lsp-semtok-range-delta-modifiers-test';
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

/* Decodes the flat, delta-encoded semanticTokens `data` array into absolute
 * (line, column) quintuples: [deltaLine, deltaColumn, length, type, modifiers]. */
function lsp_decode_semantic_tokens(array $data): array {
    $tokens = [];
    $line = 0;
    $column = 0;
    $count = count($data);
    for ($i = 0; $i + 4 < $count; $i += 5) {
        $deltaLine = $data[$i];
        $deltaColumn = $data[$i + 1];
        if ($deltaLine === 0) {
            $column += $deltaColumn;
        } else {
            $line += $deltaLine;
            $column = $deltaColumn;
        }
        $tokens[] = [
            'line' => $line,
            'column' => $column,
            'length' => $data[$i + 2],
            'type' => $data[$i + 3],
            'modifiers' => $data[$i + 4],
        ];
    }

    return $tokens;
}

function lsp_find_token_on_line(array $tokens, int $line, int $type): ?array {
    foreach ($tokens as $token) {
        if ($token['line'] === $line && $token['type'] === $type) {
            return $token;
        }
    }

    return null;
}

function lsp_line_of(string $text, string $needle): int {
    $offset = strpos($text, $needle);
    if ($offset === false) {
        return -1;
    }

    return substr_count(substr($text, 0, $offset), "\n");
}

/* Legend token type indexes, mirrored from lsp_server_semantic_tokens.c. */
const SEM_CLASS = 1;
const SEM_METHOD = 5;
const SEM_VARIABLE = 7;

/* Legend token modifier bits, mirrored from lsp_server_semantic_tokens.c. */
const MOD_STATIC = 1;
const MOD_READONLY = 2;
const MOD_DEPRECATED = 4;
const MOD_DECLARATION = 8;

rrmdir($root);
@mkdir($root, 0777, true);

$demo = <<<'PHP'
<?php

/**
 * Demo class.
 */
class Widget
{
    public readonly string $name;

    private static int $counter = 0;

    public function __construct(string $name)
    {
        $this->name = $name;
        self::$counter++;
    }

    public static function make(string $name): self
    {
        return new self($name);
    }

    /**
     * @deprecated Use make() instead.
     */
    public function legacyCreate(string $name): self
    {
        return new self($name);
    }
}
PHP;
file_put_contents($root . '/Widget.php', $demo);
file_put_contents($runner, "<?php\nLSParrot\\start_lsp(['analyzer' => 'lsparrot', 'workers' => ['count' => 0]]);\n");

$uri = 'file://' . $root . '/Widget.php';
$messages = [
    ['jsonrpc' => '2.0', 'id' => 1, 'method' => 'initialize', 'params' => ['rootUri' => 'file://' . $root]],
    ['jsonrpc' => '2.0', 'method' => 'textDocument/didOpen', 'params' => ['textDocument' => ['uri' => $uri, 'languageId' => 'php', 'version' => 1, 'text' => $demo]]],
    ['jsonrpc' => '2.0', 'id' => 2, 'method' => 'textDocument/semanticTokens/full', 'params' => ['textDocument' => ['uri' => $uri]]],
    ['jsonrpc' => '2.0', 'id' => 3, 'method' => 'shutdown', 'params' => []],
];

[$stdout, $stderr, $code] = run_lsp($extension, $runner, $messages);
$decoded = lsp_messages($stdout);

$initResponse = lsp_response($decoded, 1);
$legend = $initResponse['result']['capabilities']['semanticTokensProvider']['legend'] ?? null;
$tokenModifiers = $legend['tokenModifiers'] ?? null;

$fullResponse = lsp_response($decoded, 2);
$resultId = $fullResponse['result']['resultId'] ?? null;
$data = $fullResponse['result']['data'] ?? null;

if ($code !== 0) {
    echo "FAILED: process exit\n";
    var_dump($code, $stderr);
    rrmdir($root);
    return;
}

if ($tokenModifiers !== ['static', 'readonly', 'deprecated', 'declaration']) {
    echo "FAILED: legend tokenModifiers mismatch\n";
    var_dump($tokenModifiers);
    rrmdir($root);
    return;
}

if (!is_string($resultId) || $resultId === '') {
    echo "FAILED: full response missing resultId\n";
    var_dump($fullResponse);
    rrmdir($root);
    return;
}

if (!is_array($data) || count($data) === 0 || count($data) % 5 !== 0) {
    echo "FAILED: full response data is empty or not a multiple of 5\n";
    var_dump($data);
    rrmdir($root);
    return;
}

$tokens = lsp_decode_semantic_tokens($data);

$classLine = lsp_line_of($demo, 'class Widget');
$readonlyPropertyLine = lsp_line_of($demo, 'public readonly string $name;');
$staticPropertyLine = lsp_line_of($demo, 'private static int $counter = 0;');
$constructLine = lsp_line_of($demo, 'function __construct(string $name)');
$staticMethodLine = lsp_line_of($demo, 'function make(string $name): self');
$deprecatedMethodLine = lsp_line_of($demo, 'function legacyCreate(string $name): self');

$classToken = lsp_find_token_on_line($tokens, $classLine, SEM_CLASS);
$readonlyPropertyToken = lsp_find_token_on_line($tokens, $readonlyPropertyLine, SEM_VARIABLE);
$staticPropertyToken = lsp_find_token_on_line($tokens, $staticPropertyLine, SEM_VARIABLE);
$constructToken = lsp_find_token_on_line($tokens, $constructLine, SEM_METHOD);
$staticMethodToken = lsp_find_token_on_line($tokens, $staticMethodLine, SEM_METHOD);
$deprecatedMethodToken = lsp_find_token_on_line($tokens, $deprecatedMethodLine, SEM_METHOD);

if (!$classToken || $classToken['modifiers'] !== MOD_DECLARATION) {
    echo "FAILED: class declaration modifiers mismatch\n";
    var_dump($classToken);
} elseif (!$readonlyPropertyToken || $readonlyPropertyToken['modifiers'] !== (MOD_DECLARATION | MOD_READONLY)) {
    echo "FAILED: readonly property modifiers mismatch\n";
    var_dump($readonlyPropertyToken);
} elseif (!$staticPropertyToken || $staticPropertyToken['modifiers'] !== (MOD_DECLARATION | MOD_STATIC)) {
    echo "FAILED: static property modifiers mismatch\n";
    var_dump($staticPropertyToken);
} elseif (!$constructToken || $constructToken['modifiers'] !== MOD_DECLARATION) {
    echo "FAILED: constructor modifiers mismatch\n";
    var_dump($constructToken);
} elseif (!$staticMethodToken || $staticMethodToken['modifiers'] !== (MOD_DECLARATION | MOD_STATIC)) {
    echo "FAILED: static method modifiers mismatch\n";
    var_dump($staticMethodToken);
} elseif (!$deprecatedMethodToken || $deprecatedMethodToken['modifiers'] !== (MOD_DECLARATION | MOD_DEPRECATED)) {
    echo "FAILED: deprecated method modifiers mismatch\n";
    var_dump($deprecatedMethodToken);
} else {
    echo "OK\n";
}

rrmdir($root);
?>
--EXPECT--
OK
