--TEST--
LSP completes fuzzy unimported project classes with use edits
--EXTENSIONS--
lsparrot
--FILE--
<?php
$root = '/tmp/lsp-fuzzy-import-completion-test';
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

function lsp_item(?array $result, string $label, string $qualifiedName): ?array {
    foreach (($result['items'] ?? []) as $item) {
        if (($item['label'] ?? null) !== $label) {
            continue;
        }

        if (($item['data']['qualifiedName'] ?? null) === $qualifiedName) {
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
@mkdir($root . '/src/Domain', 0777, true);
@mkdir($root . '/src/Service', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);
$psr4 = [
    'FuzzyImportFixture\\' => [$root . '/src'],
];
file_put_contents($root . '/composer.json', '{"autoload":{"psr-4":{"FuzzyImportFixture\\\\":"src/"}}}');
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [];\n");
file_put_contents($root . '/vendor/composer/autoload_namespaces.php', "<?php\nreturn [];\n");
file_put_contents($root . '/vendor/composer/autoload_psr4.php', "<?php\nreturn " . var_export($psr4, true) . ";\n");
file_put_contents($root . '/src/Domain/User.php', "<?php\nnamespace FuzzyImportFixture\\Domain;\nfinal class User {}\n");
file_put_contents($root . '/src/Domain/UserItem.php', "<?php\nnamespace FuzzyImportFixture\\Domain;\nfinal class UserItem {}\n");

$demo = <<<'PHP'
<?php
namespace FuzzyImportFixture\Service;

final class Demo
{
    public User

    public function make(User

    public function run(): void
    {
        User
        UI
    }
}
PHP;

file_put_contents($root . '/src/Service/Demo.php', $demo);
file_put_contents($runner, "<?php\nLSParrot\\start_lsp(['analyzer' => 'lsparrot', 'workers' => ['count' => 0], 'symbolIndex' => ['size' => '4M']]);\n");

$uri = 'file://' . $root . '/src/Service/Demo.php';
$messages = [
    ['jsonrpc' => '2.0', 'id' => 1, 'method' => 'initialize', 'params' => ['rootUri' => 'file://' . $root]],
    ['jsonrpc' => '2.0', 'method' => 'textDocument/didOpen', 'params' => ['textDocument' => ['uri' => $uri, 'languageId' => 'php', 'version' => 1, 'text' => $demo]]],
    ['jsonrpc' => '2.0', 'id' => 2, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($demo, 'User')]],
    ['jsonrpc' => '2.0', 'id' => 3, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($demo, 'UI')]],
    ['jsonrpc' => '2.0', 'id' => 4, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($demo, 'public User')]],
    ['jsonrpc' => '2.0', 'id' => 5, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($demo, 'function make(User')]],
    ['jsonrpc' => '2.0', 'id' => 6, 'method' => 'shutdown', 'params' => []],
];

[$stdout, $stderr, $code] = run_lsp($extension, $runner, $messages);
$decoded = lsp_messages($stdout);
$prefixCompletion = lsp_response($decoded, 2);
$fuzzyCompletion = lsp_response($decoded, 3);
$propertyTypeCompletion = lsp_response($decoded, 4);
$parameterTypeCompletion = lsp_response($decoded, 5);
$user = lsp_item($prefixCompletion, 'User', 'FuzzyImportFixture\Domain\User');
$userItem = lsp_item($fuzzyCompletion, 'UserItem', 'FuzzyImportFixture\Domain\UserItem');
$propertyTypeUser = lsp_item($propertyTypeCompletion, 'User', 'FuzzyImportFixture\Domain\User');
$parameterTypeUser = lsp_item($parameterTypeCompletion, 'User', 'FuzzyImportFixture\Domain\User');

if ($code !== 0) {
    echo "FAILED: process exit\n";
    var_dump($code, $stderr);
} elseif (!$user || empty($user['additionalTextEdits'])) {
    echo "FAILED: missing prefix class import completion\n";
    var_dump($prefixCompletion);
} elseif (!$userItem || empty($userItem['additionalTextEdits'])) {
    echo "FAILED: missing fuzzy class import completion\n";
    var_dump($fuzzyCompletion);
} elseif (!$propertyTypeUser || empty($propertyTypeUser['additionalTextEdits'])) {
    echo "FAILED: missing property type class import completion\n";
    var_dump($propertyTypeCompletion);
} elseif (!$parameterTypeUser || empty($parameterTypeUser['additionalTextEdits'])) {
    echo "FAILED: missing parameter type class import completion\n";
    var_dump($parameterTypeCompletion);
} else {
    echo "OK\n";
}

rrmdir($root);
?>
--EXPECT--
OK
