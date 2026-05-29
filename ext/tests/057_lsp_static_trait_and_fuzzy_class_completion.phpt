--TEST--
LSP completes vendor trait static members and fuzzy imported classes
--EXTENSIONS--
lsparrot
--FILE--
<?php
$root = '/tmp/lsp-static-trait-completion-test';
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

function lsp_labels(?array $result): array {
    $labels = [];
    foreach (($result['items'] ?? []) as $item) {
        if (isset($item['label'])) {
            $labels[] = $item['label'];
        }
    }

    return $labels;
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
@mkdir($root . '/src/App', 0777, true);
@mkdir($root . '/src/Database', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);
@mkdir($root . '/vendor/pkg/src', 0777, true);
$psr4 = [
    'StaticTraitFixture\\' => [$root . '/src'],
    'Vendor\\' => [$root . '/vendor/pkg/src'],
];
file_put_contents($root . '/composer.json', '{"autoload":{"psr-4":{"StaticTraitFixture\\\\":"src/","Vendor\\\\":"vendor/pkg/src/"}}}');
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [];\n");
file_put_contents($root . '/vendor/composer/autoload_namespaces.php', "<?php\nreturn [];\n");
file_put_contents($root . '/vendor/composer/autoload_psr4.php', "<?php\nreturn " . var_export($psr4, true) . ";\n");
file_put_contents($root . '/vendor/pkg/src/HasFactory.php', <<<'PHP'
<?php
namespace Vendor;

/**
 * @template TFactory of \StaticTraitFixture\Database\Factory
 */
trait HasFactory
{
    /**
     * @return TFactory
     */
    public static function factory($count = null, $state = [])
    {
        return new \StaticTraitFixture\Database\UserFactory();
    }

    public function traitInstanceOnly(): void
    {
    }
}
PHP);
file_put_contents($root . '/src/App/AbstractUser.php', <<<'PHP'
<?php
namespace StaticTraitFixture\App;

use StaticTraitFixture\Database\UserFactory;
use Vendor\HasFactory;

abstract class AbstractUser
{
    /** @use HasFactory<UserFactory> */
    use HasFactory;

    public static string $publicStatic;
    public string $instanceProperty;
}
PHP);
file_put_contents($root . '/src/Database/Factory.php', <<<'PHP'
<?php
namespace StaticTraitFixture\Database;

abstract class Factory
{
    public function create(): object
    {
        return new \stdClass();
    }
}
PHP);
file_put_contents($root . '/src/App/User.php', <<<'PHP'
<?php
namespace StaticTraitFixture\App;

final class User extends AbstractUser
{
    public static function register(): void
    {
    }

    public function instanceOnly(): void
    {
    }
}
PHP);
file_put_contents($root . '/src/Database/UserFactory.php', <<<'PHP'
<?php
namespace StaticTraitFixture\Database;

final class UserFactory extends Factory
{
}
PHP);
$demo = <<<'PHP'
<?php
namespace StaticTraitFixture\Service;

use StaticTraitFixture\App\User;

final class Demo
{
    public function run(): void
    {
        User::fac;
        User::trait;
        User::factory()->cre;
        $factory = UF;
    }
}
PHP;
file_put_contents($root . '/src/Demo.php', $demo);
file_put_contents($runner, "<?php\nLSParrot\\start_lsp(['analyzer' => 'lsparrot', 'workers' => ['count' => 0], 'symbolIndex' => ['size' => '4M']]);\n");

$uri = 'file://' . $root . '/src/Demo.php';
$messages = [
    ['jsonrpc' => '2.0', 'id' => 1, 'method' => 'initialize', 'params' => ['rootUri' => 'file://' . $root]],
    ['jsonrpc' => '2.0', 'method' => 'textDocument/didOpen', 'params' => ['textDocument' => ['uri' => $uri, 'languageId' => 'php', 'version' => 1, 'text' => $demo]]],
    ['jsonrpc' => '2.0', 'id' => 2, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($demo, 'User::fac')]],
    ['jsonrpc' => '2.0', 'id' => 3, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($demo, 'User::trait')]],
    ['jsonrpc' => '2.0', 'id' => 4, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($demo, 'UF')]],
    ['jsonrpc' => '2.0', 'id' => 5, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($demo, 'User::factory()->cre')]],
    ['jsonrpc' => '2.0', 'id' => 6, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($demo, 'User::')]],
    ['jsonrpc' => '2.0', 'id' => 7, 'method' => 'shutdown', 'params' => []],
];

[$stdout, $stderr, $code] = run_lsp($extension, $runner, $messages);
$decoded = lsp_messages($stdout);
$factoryResult = lsp_response($decoded, 2);
$staticOnlyResult = lsp_response($decoded, 3);
$fuzzyResult = lsp_response($decoded, 4);
$factoryChainResult = lsp_response($decoded, 5);
$emptyStaticResult = lsp_response($decoded, 6);
$factoryLabels = lsp_labels($factoryResult);
$staticOnlyLabels = lsp_labels($staticOnlyResult);
$factoryChainLabels = lsp_labels($factoryChainResult);
$emptyStaticLabels = lsp_labels($emptyStaticResult);
$fuzzyItem = lsp_item($fuzzyResult, 'UserFactory');

if ($code !== 0) {
    echo "FAILED: process exit\n";
    var_dump($code, $stderr);
} elseif (!in_array('factory', $factoryLabels, true)) {
    echo "FAILED: missing static trait factory\n";
    var_dump($factoryLabels);
    echo $stdout;
} elseif (in_array('traitInstanceOnly', $staticOnlyLabels, true) || in_array('UserFactory', $staticOnlyLabels, true)) {
    echo "FAILED: static access returned non-static or general candidates\n";
    var_dump($staticOnlyLabels);
    echo $stdout;
} elseif (!$fuzzyItem || empty($fuzzyItem['additionalTextEdits'])) {
    echo "FAILED: missing fuzzy class import completion\n";
    var_dump($fuzzyResult);
    echo $stdout;
} elseif (!in_array('create', $factoryChainLabels, true)) {
    echo "FAILED: missing chained factory create completion\n";
    var_dump($factoryChainLabels);
    echo $stdout;
} elseif (!in_array('factory', $emptyStaticLabels, true)) {
    echo "FAILED: missing empty static access completion\n";
    var_dump($emptyStaticLabels);
    echo $stdout;
} else {
    echo "OK\n";
}

rrmdir($root);
?>
--EXPECT--
OK
