--TEST--
LSP completes trait members after fluent static/$this test-case methods
--EXTENSIONS--
lsparrot
--FILE--
<?php
$root = '/tmp/lsp-trait-fluent-test';
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

function lsp_marker_position(string &$text, string $marker): array {
    $offset = strpos($text, $marker);
    if ($offset === false) {
        return [0, 0];
    }

    $before = substr($text, 0, $offset);
    $line = substr_count($before, "\n");
    $lastNewline = strrpos($before, "\n");
    $character = $lastNewline === false ? strlen($before) : strlen($before) - $lastNewline - 1;
    $text = substr_replace($text, '', $offset, strlen($marker));

    return [$line, $character];
}

function run_lsp(string $extension, string $runner, string $input): array {
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

    fwrite($pipes[0], $input);
    fclose($pipes[0]);
    $stdout = stream_get_contents($pipes[1]);
    $stderr = stream_get_contents($pipes[2]);
    fclose($pipes[1]);
    fclose($pipes[2]);
    $code = proc_close($process);

    return [$stdout, $stderr, $code];
}

rrmdir($root);
@mkdir($root . '/vendor/composer', 0777, true);
@mkdir($root . '/tests/Feature', 0777, true);
@mkdir($root . '/tests/Support', 0777, true);
@mkdir($root . '/vendor-src', 0777, true);

file_put_contents($root . '/vendor/composer/autoload_psr4.php', "<?php\nreturn [\n    'Tests\\\\' => [" . var_export($root . '/tests', true) . "],\n    'Vendor\\\\' => [" . var_export($root . '/vendor-src', true) . "],\n];\n");
file_put_contents($root . '/vendor-src/BaseTestCase.php', "<?php\nnamespace Vendor;\nabstract class BaseTestCase {}\n");
file_put_contents($root . '/tests/Support/ApiRoute.php', "<?php\nnamespace Tests\\Support;\nfinal class ApiRoute { public function friend(): void {} }\n");
file_put_contents($root . '/tests/Support/ApiRequestable.php', "<?php\nnamespace Tests\\Support;\ntrait ApiRequestable { public function requestApi(): ApiRoute { return new ApiRoute(); } }\n");
file_put_contents($root . '/tests/TestCase.php', "<?php\nnamespace Tests;\nuse Tests\\Support\\ApiRequestable;\nuse Vendor\\BaseTestCase;\nabstract class TestCase extends BaseTestCase { use ApiRequestable; /** @return \$this */ public function actingAs(): static { return \$this; } }\n");
file_put_contents($runner, "<?php\nLSParrot\\start_lsp(['analyzer' => 'lsparrot', 'workers' => ['count' => 0], 'symbolIndex' => ['size' => '4M']]);\n");

$uri = 'file://' . $root . '/tests/Feature/FriendControllerTest.php';
$document = <<<'PHP'
<?php
namespace Tests\Feature;
use Tests\TestCase;
final class FriendControllerTest extends TestCase {
    public function test_it(): void {
        $this->acting/*caret1*/;
        $this->actingAs()->request/*caret2*/;
        $this->actingAs()->requestApi()->fr/*caret3*/;
    }
}
PHP;
[$line1, $character1] = lsp_marker_position($document, '/*caret1*/');
[$line2, $character2] = lsp_marker_position($document, '/*caret2*/');
[$line3, $character3] = lsp_marker_position($document, '/*caret3*/');
file_put_contents($root . '/tests/Feature/FriendControllerTest.php', $document);

$messages = [
    ['jsonrpc' => '2.0', 'id' => 1, 'method' => 'initialize', 'params' => ['rootUri' => 'file://' . $root]],
    ['jsonrpc' => '2.0', 'method' => 'textDocument/didOpen', 'params' => ['textDocument' => ['uri' => $uri, 'languageId' => 'php', 'version' => 1, 'text' => $document]]],
    ['jsonrpc' => '2.0', 'id' => 2, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $uri], 'position' => ['line' => $line1, 'character' => $character1]]],
    ['jsonrpc' => '2.0', 'id' => 3, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $uri], 'position' => ['line' => $line2, 'character' => $character2]]],
    ['jsonrpc' => '2.0', 'id' => 4, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $uri], 'position' => ['line' => $line3, 'character' => $character3]]],
    ['jsonrpc' => '2.0', 'id' => 5, 'method' => 'shutdown', 'params' => []],
];

[$stdout, $stderr, $code] = run_lsp($extension, $runner, lsp_input($messages));
if ($code !== 0) {
    echo "FAILED: process exit\n";
    var_dump($code, $stderr);
} elseif (strpos($stdout, '"label":"actingAs"') === false) {
    echo "FAILED: missing fluent method\n";
    echo $stdout;
} elseif (strpos($stdout, '"label":"requestApi"') === false) {
    echo "FAILED: missing trait method after fluent return\n";
    echo $stdout;
} elseif (strpos($stdout, '"label":"friend"') === false) {
    echo "FAILED: missing returned route method after nested fluent chain\n";
    echo $stdout;
} else {
    echo "OK\n";
}

rrmdir($root);
?>
--EXPECT--
OK
