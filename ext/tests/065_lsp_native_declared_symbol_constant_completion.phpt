--TEST--
LSP lsparrot completion indexes declared class-like symbols, inherited protected properties, and constants
--EXTENSIONS--
lsparrot
--FILE--
<?php
$root = '/tmp/lsp-lsparrot-declared-symbol-constant-test';
$extension = dirname(__DIR__) . '/modules/lsparrot.so';
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
@mkdir($root . '/src/Fixture', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);
$psr4 = [
    'DeclaredSymbolFixture\\' => [$root . '/src'],
];
file_put_contents($root . '/composer.json', '{"autoload":{"psr-4":{"DeclaredSymbolFixture\\\\":"src/"}}}');
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [];\n");
file_put_contents($root . '/vendor/composer/autoload_namespaces.php', "<?php\nreturn [];\n");
file_put_contents($root . '/vendor/composer/autoload_psr4.php', "<?php\nreturn " . var_export($psr4, true) . ";\n");

$demo = <<<'PHP'
<?php
namespace DeclaredSymbolFixture\Fixture;

const FOOOOO = 123;

class Bongo
{
    private string $secret = 'Bongo';
    protected string $sayText = 'Bongo';

    public function sayBongo(): void
    {
    }
}

class Jambe extends Bongo
{
    public function __construct()
    {
        /*parent*/$this->
    }
}

final class Conga
{
    public function getBongo(): Bongo
    {
        return new Bongo();
    }

    public function getBongoLike(): Bongo
    {
        return new Jambe();
    }
}

$foo = new Conga();
$foo->get
echo FOO;
echo PHP_VER;
PHP;
file_put_contents($root . '/src/Fixture/Functions.php', $demo);
file_put_contents($runner, "<?php\nLSParrot\\start_lsp(['analyzer' => 'lsparrot', 'workers' => ['count' => 0], 'symbolIndex' => ['size' => '4M']]);\n");

$uri = 'file://' . $root . '/src/Fixture/Functions.php';
$messages = [
    ['jsonrpc' => '2.0', 'id' => 1, 'method' => 'initialize', 'params' => ['rootUri' => 'file://' . $root]],
    ['jsonrpc' => '2.0', 'method' => 'textDocument/didOpen', 'params' => ['textDocument' => ['uri' => $uri, 'languageId' => 'php', 'version' => 1, 'text' => $demo]]],
    ['jsonrpc' => '2.0', 'id' => 2, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($demo, '/*parent*/$this->')]],
    ['jsonrpc' => '2.0', 'id' => 3, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($demo, '$foo->get')]],
    ['jsonrpc' => '2.0', 'id' => 4, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($demo, 'echo FOO')]],
    ['jsonrpc' => '2.0', 'id' => 5, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($demo, 'echo PHP_VER')]],
    ['jsonrpc' => '2.0', 'id' => 6, 'method' => 'shutdown', 'params' => []],
];

[$stdout, $stderr, $code] = run_lsp($extension, $runner, $messages);
$decoded = lsp_messages($stdout);
$parentResult = lsp_response($decoded, 2);
$fooResult = lsp_response($decoded, 3);
$constantResult = lsp_response($decoded, 4);
$builtinConstantResult = lsp_response($decoded, 5);
$sayText = lsp_item($parentResult, 'sayText');
$secret = lsp_item($parentResult, 'secret');
$getBongo = lsp_item($fooResult, 'getBongo');
$getBongoLike = lsp_item($fooResult, 'getBongoLike');
$fooooo = lsp_item($constantResult, 'FOOOOO');
$phpVersion = lsp_item($builtinConstantResult, 'PHP_VERSION');

if ($code !== 0) {
    echo "FAILED: process exit\n";
    var_dump($code, $stderr);
} elseif (!$sayText || !str_contains((string) ($sayText['detail'] ?? ''), 'string')) {
    echo "FAILED: missing inherited protected typed property\n";
    var_dump($parentResult);
} elseif ($secret) {
    echo "FAILED: private parent property leaked\n";
    var_dump($parentResult);
} elseif (!$getBongo || !str_contains((string) ($getBongo['detail'] ?? ''), 'Bongo')) {
    echo "FAILED: missing new-assignment method getBongo\n";
    var_dump($fooResult);
} elseif (!$getBongoLike || !str_contains((string) ($getBongoLike['detail'] ?? ''), 'Bongo')) {
    echo "FAILED: missing new-assignment method getBongoLike\n";
    var_dump($fooResult);
} elseif (!$fooooo || !str_contains((string) ($fooooo['detail'] ?? ''), 'constant')) {
    echo "FAILED: missing user-defined constant\n";
    var_dump($constantResult);
} elseif (!$phpVersion || !str_contains((string) ($phpVersion['detail'] ?? ''), 'constant')) {
    echo "FAILED: missing builtin constant\n";
    var_dump($builtinConstantResult);
} else {
    echo "OK\n";
}

rrmdir($root);
?>
--EXPECT--
OK
