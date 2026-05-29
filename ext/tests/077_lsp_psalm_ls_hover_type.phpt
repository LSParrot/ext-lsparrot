--TEST--
LSP uses Psalm Language Server hover type text
--EXTENSIONS--
lsparrot
--FILE--
<?php
$root = '/tmp/lsp-psalm-ls-hover-test';
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

function lsp_write_messages($pipe, array $messages): void {
    fwrite($pipe, lsp_input($messages));
    fflush($pipe);
}

function lsp_read_available($pipe): string {
    $buffer = '';
    while (($chunk = fread($pipe, 8192)) !== false && $chunk !== '') {
        $buffer .= $chunk;
        if (strlen($chunk) < 8192) {
            break;
        }
    }

    return $buffer;
}

function lsp_wait_for($stdinPipe, $stdoutPipe, $stderrPipe, string &$stdout, string &$stderr, callable $predicate): bool {
    $deadline = microtime(true) + (getenv('USE_ZEND_ALLOC') === '0' ? 45.0 : 5.0);
    $statusId = 1000;
    while (microtime(true) < $deadline) {
        lsp_write_messages($stdinPipe, [['jsonrpc' => '2.0', 'id' => $statusId++, 'method' => 'lsparrot.php/status', 'params' => []]]);
        usleep(250000);
        $stdout .= lsp_read_available($stdoutPipe);
        $stderr .= lsp_read_available($stderrPipe);
        if ($predicate($stdout, $stderr)) {
            return true;
        }
    }

    $stdout .= lsp_read_available($stdoutPipe);
    $stderr .= lsp_read_available($stderrPipe);

    return $predicate($stdout, $stderr);
}

rrmdir($root);
@mkdir($root . '/src/Domain', 0777, true);
@mkdir($root . '/vendor/bin', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);
file_put_contents($root . '/composer.json', "{\"autoload\":{\"psr-4\":{\"PsalmLsHoverFixture\\\\\\\\\":\"src/\"}}}\n");
file_put_contents($root . '/psalm.xml', '<?xml version="1.0"?><psalm><projectFiles><directory name="src" /></projectFiles></psalm>');
file_put_contents($root . '/src/Domain/User.php', <<<'PHP'
<?php
namespace PsalmLsHoverFixture\Domain;
final class User {}
PHP);
file_put_contents($root . '/src/Demo.php', <<<'PHP'
<?php
namespace PsalmLsHoverFixture;
use PsalmLsHoverFixture\Domain\User;
final class Demo
{
    public function run(): void
    {
        $user = new User();
        $val = $user;
        $val;
    }
}
PHP);
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'PsalmLsHoverFixture\\\\Domain\\\\User' => " . var_export($root . '/src/Domain/User.php', true) . ",\n    'PsalmLsHoverFixture\\\\Demo' => " . var_export($root . '/src/Demo.php', true) . ",\n];\n");
file_put_contents($root . '/vendor/bin/psalm-language-server', <<<'PHP'
#!/usr/bin/env php
<?php
function frame_read(): ?array {
    $length = null;
    while (($line = fgets(STDIN)) !== false) {
        $trimmed = rtrim($line, "\r\n");
        if ($trimmed === '') {
            break;
        }
        if (stripos($trimmed, 'Content-Length:') === 0) {
            $length = (int) trim(substr($trimmed, strlen('Content-Length:')));
        }
    }
    if ($length === null) {
        return null;
    }
    $body = '';
    while (strlen($body) < $length && !feof(STDIN)) {
        $body .= fread(STDIN, $length - strlen($body));
    }
    return strlen($body) === $length ? json_decode($body, true) : null;
}
function frame_write(array $message): void {
    $json = json_encode($message, JSON_UNESCAPED_SLASHES);
    fwrite(STDOUT, 'Content-Length: ' . strlen($json) . "\r\n\r\n" . $json);
    fflush(STDOUT);
}
while (($message = frame_read()) !== null) {
    $method = $message['method'] ?? null;
    if ($method === 'initialize') {
        frame_write(['jsonrpc' => '2.0', 'id' => $message['id'], 'result' => ['capabilities' => ['hoverProvider' => true]]]);
        continue;
    }
    if ($method === 'textDocument/didOpen') {
        frame_write(['jsonrpc' => '2.0', 'method' => 'textDocument/publishDiagnostics', 'params' => ['uri' => $message['params']['textDocument']['uri'], 'diagnostics' => []]]);
        continue;
    }
    if ($method === 'textDocument/hover') {
        frame_write([
            'jsonrpc' => '2.0',
            'id' => $message['id'],
            'result' => [
                'contents' => [
                    'kind' => 'markdown',
                    'value' => "```php\n<?php declare(strict_types=1);\nPsalmLsHoverFixture\\Domain\\User\n```",
                ],
            ],
        ]);
        continue;
    }
    if ($method === 'shutdown') {
        frame_write(['jsonrpc' => '2.0', 'id' => $message['id'], 'result' => null]);
        exit(0);
    }
}
PHP);
chmod($root . '/vendor/bin/psalm-language-server', 0755);
file_put_contents($runner, <<<'PHP'
<?php
LSParrot\start_lsp([
    'analyzer' => 'psalm-ls',
    'psalm' => [
        'transport' => 'languageServer',
        'maxResponseWaitMs' => 500,
        'enableHover' => true,
        'enableAutocomplete' => false,
        'enableDiagnostics' => true,
    ],
    'workers' => ['count' => 0],
    'symbolIndex' => ['size' => '4M'],
]);
PHP);

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
    echo "FAILED: process start\n";
    rrmdir($root);
    return;
}

stream_set_blocking($pipes[1], false);
stream_set_blocking($pipes[2], false);
$uri = 'file://' . $root . '/src/Demo.php';
$text = file_get_contents($root . '/src/Demo.php');
$stdout = '';
$stderr = '';
lsp_write_messages($pipes[0], [
    ['jsonrpc' => '2.0', 'id' => 1, 'method' => 'initialize', 'params' => ['rootUri' => 'file://' . $root]],
    ['jsonrpc' => '2.0', 'method' => 'textDocument/didOpen', 'params' => ['textDocument' => ['uri' => $uri, 'languageId' => 'php', 'version' => 1, 'text' => $text]]],
]);

$ready = lsp_wait_for($pipes[0], $pipes[1], $pipes[2], $stdout, $stderr, static function (string $stdout): bool {
    return strpos($stdout, '"analyzer":"psalm-ls"') !== false
        && strpos($stdout, '"state":"idle"') !== false
        && strpos($stdout, 'Psalm language server ready.') !== false;
});

if ($ready) {
    lsp_write_messages($pipes[0], [
        ['jsonrpc' => '2.0', 'id' => 2, 'method' => 'textDocument/hover', 'params' => ['textDocument' => ['uri' => $uri], 'position' => ['line' => 12, 'character' => 10]]],
    ]);
}

$hover = $ready && lsp_wait_for($pipes[0], $pipes[1], $pipes[2], $stdout, $stderr, static function (string $stdout): bool {
    return strpos($stdout, '"jsonrpc":"2.0","id":2,"result"') !== false
        && strpos($stdout, 'PsalmLsHoverFixture\\\\Domain\\\\User') !== false
        && strpos($stdout, 'Psalm-LS') !== false;
});

lsp_write_messages($pipes[0], [
    ['jsonrpc' => '2.0', 'id' => 3, 'method' => 'shutdown', 'params' => []],
]);
fclose($pipes[0]);
stream_set_blocking($pipes[1], true);
stream_set_blocking($pipes[2], true);
$stdout .= stream_get_contents($pipes[1]);
$stderr .= stream_get_contents($pipes[2]);
fclose($pipes[1]);
fclose($pipes[2]);
$code = proc_close($process);

if ($code !== 0) {
    echo "FAILED: process exit\n";
    var_dump($code, $stderr);
} elseif (!$ready) {
    echo "FAILED: Psalm LS not ready\n";
    echo $stdout;
    echo $stderr;
} elseif (!$hover) {
    echo "FAILED: missing Psalm LS hover\n";
    echo $stdout;
    echo $stderr;
} elseif (strpos($stdout, '<?php declare') !== false) {
    echo "FAILED: hover still contains PHP wrapper\n";
    echo $stdout;
} else {
    echo "OK\n";
}

rrmdir($root);
?>
--EXPECT--
OK
