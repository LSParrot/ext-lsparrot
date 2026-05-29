--TEST--
LSP completes current scope variables with available type details
--EXTENSIONS--
lsparrot
--FILE--
<?php
$root = '/tmp/lsp-scope-variable-completion-test';
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
@mkdir($root . '/src', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);
$psr4 = [
    'ScopeVariableFixture\\' => [$root . '/src'],
];
file_put_contents($root . '/composer.json', '{"autoload":{"psr-4":{"ScopeVariableFixture\\\\":"src/"}}}');
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [];\n");
file_put_contents($root . '/vendor/composer/autoload_namespaces.php', "<?php\nreturn [];\n");
file_put_contents($root . '/vendor/composer/autoload_psr4.php', "<?php\nreturn " . var_export($psr4, true) . ";\n");

$demo = <<<'PHP'
<?php
namespace ScopeVariableFixture;

final class User
{
}

final class Collection
{
}

final class Demo
{
    public function run(string $name, User $user, ?Collection $collection): void
    {
        /** @var Collection<User> $users */
        $users = [];
        $local = new User();
        if (true) {
            $inner = new User();
            /*cursor*/$
        }
    }
}
PHP;
file_put_contents($root . '/src/Demo.php', $demo);
file_put_contents($runner, "<?php\nLSParrot\\start_lsp(['analyzer' => 'lsparrot', 'workers' => ['count' => 0], 'symbolIndex' => ['size' => '4M']]);\n");

$uri = 'file://' . $root . '/src/Demo.php';
$messages = [
    ['jsonrpc' => '2.0', 'id' => 1, 'method' => 'initialize', 'params' => ['rootUri' => 'file://' . $root]],
    ['jsonrpc' => '2.0', 'method' => 'textDocument/didOpen', 'params' => ['textDocument' => ['uri' => $uri, 'languageId' => 'php', 'version' => 1, 'text' => $demo]]],
    ['jsonrpc' => '2.0', 'id' => 2, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($demo, '/*cursor*/$')]],
    ['jsonrpc' => '2.0', 'id' => 3, 'method' => 'shutdown', 'params' => []],
];

[$stdout, $stderr, $code] = run_lsp($extension, $runner, $messages);
$decoded = lsp_messages($stdout);
$result = lsp_response($decoded, 2);
$name = lsp_item($result, '$name');
$user = lsp_item($result, '$user');
$collection = lsp_item($result, '$collection');
$users = lsp_item($result, '$users');
$local = lsp_item($result, '$local');
$inner = lsp_item($result, '$inner');
$thisItem = lsp_item($result, '$this');
$class = lsp_item($result, 'Demo');

if ($code !== 0) {
    echo "FAILED: process exit\n";
    var_dump($code, $stderr);
} elseif (!$name || !str_contains((string) ($name['detail'] ?? ''), 'string')) {
    echo "FAILED: missing typed parameter name\n";
    var_dump($result);
} elseif (($name['filterText'] ?? null) !== '$name' || ($name['textEdit']['newText'] ?? null) !== '$name') {
    echo "FAILED: variable completion is not filtered or edited as a dollar-prefixed item\n";
    var_dump($name);
} elseif (!$user || !str_contains((string) ($user['detail'] ?? ''), 'User')) {
    echo "FAILED: missing typed parameter user\n";
    var_dump($result);
} elseif (!$collection || !str_contains((string) ($collection['detail'] ?? ''), 'Collection')) {
    echo "FAILED: missing nullable typed parameter collection\n";
    var_dump($result);
} elseif (!$users || !str_contains((string) ($users['detail'] ?? ''), 'Collection')) {
    echo "FAILED: missing PHPDoc typed local users\n";
    var_dump($result);
} elseif (!$local || !str_contains((string) ($local['detail'] ?? ''), 'User')) {
    echo "FAILED: missing new-assignment typed local\n";
    var_dump($result);
} elseif (!$inner || !str_contains((string) ($inner['detail'] ?? ''), 'User')) {
    echo "FAILED: missing inner block local\n";
    var_dump($result);
} elseif (!$thisItem || !str_contains((string) ($thisItem['detail'] ?? ''), 'Demo')) {
    echo "FAILED: missing typed this variable\n";
    var_dump($result);
} elseif ($class) {
    echo "FAILED: dollar completion leaked class symbols\n";
    var_dump($result);
} else {
    echo "OK\n";
}

rrmdir($root);
?>
--EXPECT--
OK
