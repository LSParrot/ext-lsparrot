--TEST--
LSP resolves promoted properties, traits, parent:: instance members, constants, and enum cases
--EXTENSIONS--
lsparrot
--FILE--
<?php
$root = '/tmp/lsp-modern-member-test';
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
@mkdir($root . '/src/Concerns', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);

file_put_contents($root . '/src/Repo.php', <<<'PHP'
<?php
namespace MemberFix;

final class Repo
{
    public function find(int $id): int
    {
        return $id;
    }
}
PHP);

file_put_contents($root . '/src/Concerns/Greets.php', <<<'PHP'
<?php
namespace MemberFix\Concerns;

trait Greets
{
    public function greet(): string
    {
        return 'hi';
    }
}
PHP);

file_put_contents($root . '/src/BasePresenter.php', <<<'PHP'
<?php
namespace MemberFix;

abstract class BasePresenter
{
    public function __construct(string $name)
    {
    }

    protected function render(): string
    {
        return '';
    }
}
PHP);

file_put_contents($root . '/src/Suit.php', <<<'PHP'
<?php
namespace MemberFix;

enum Suit: string
{
    case Hearts = 'H';
    case Spades = 'S';
}
PHP);

file_put_contents($root . '/src/Config.php', <<<'PHP'
<?php
namespace MemberFix;

final class Config
{
    public const VERSION = '1.0';
}
PHP);

$demo = <<<'PHP'
<?php
namespace MemberFix;

use MemberFix\Concerns\Greets;

final class Presenter extends BasePresenter
{
    use Greets;

    public function __construct(private readonly Repo $repo)
    {
        parent::__construct('presenter');
    }

    public function show(): int
    {
        $g = $this->greet();
        $v = Config::VERSION;
        $x = Suit::
        return $this->repo->fi
    }
}
PHP;

file_put_contents($root . '/src/Presenter.php', $demo);
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n" .
    "    'MemberFix\\\\Repo' => " . var_export($root . '/src/Repo.php', true) . ",\n" .
    "    'MemberFix\\\\Concerns\\\\Greets' => " . var_export($root . '/src/Concerns/Greets.php', true) . ",\n" .
    "    'MemberFix\\\\BasePresenter' => " . var_export($root . '/src/BasePresenter.php', true) . ",\n" .
    "    'MemberFix\\\\Suit' => " . var_export($root . '/src/Suit.php', true) . ",\n" .
    "    'MemberFix\\\\Config' => " . var_export($root . '/src/Config.php', true) . ",\n" .
    "    'MemberFix\\\\Presenter' => " . var_export($root . '/src/Presenter.php', true) . ",\n" .
    "];\n");
file_put_contents($root . '/vendor/composer/autoload_namespaces.php', "<?php\nreturn [];\n");
file_put_contents($root . '/vendor/composer/autoload_psr4.php', "<?php\nreturn [];\n");
file_put_contents($root . '/composer.json', '{"autoload":{"classmap":["src/"]}}');
file_put_contents($runner, "<?php\nLSParrot\\start_lsp(['analyzer' => 'lsparrot', 'workers' => ['count' => 1], 'symbolIndex' => ['size' => '4M']]);\n");

$uri = 'file://' . $root . '/src/Presenter.php';
$messages = [
    ['jsonrpc' => '2.0', 'id' => 1, 'method' => 'initialize', 'params' => ['rootUri' => 'file://' . $root]],
    ['jsonrpc' => '2.0', 'method' => 'textDocument/didOpen', 'params' => ['textDocument' => ['uri' => $uri, 'languageId' => 'php', 'version' => 1, 'text' => $demo]]],
    // Promoted readonly property receiver chain.
    ['jsonrpc' => '2.0', 'id' => 2, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($demo, '$this->repo->fi')]],
    // Trait method definition through $this.
    ['jsonrpc' => '2.0', 'id' => 3, 'method' => 'textDocument/definition', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($demo, '$g = $this->gre')]],
    // parent::__construct definition resolves to the parent, not the child.
    ['jsonrpc' => '2.0', 'id' => 4, 'method' => 'textDocument/definition', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($demo, 'parent::__cons')]],
    // Class constant definition.
    ['jsonrpc' => '2.0', 'id' => 5, 'method' => 'textDocument/definition', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($demo, 'Config::VERS')]],
    // Enum case completion after Suit::
    ['jsonrpc' => '2.0', 'id' => 6, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $uri], 'position' => lsp_position_after($demo, '$x = Suit::')]],
    ['jsonrpc' => '2.0', 'id' => 7, 'method' => 'shutdown', 'params' => []],
    ['jsonrpc' => '2.0', 'method' => 'exit', 'params' => []],
];

[$stdout, $stderr, $code] = run_lsp($extension, $runner, $messages);
$decoded = lsp_messages($stdout);
$promoted = lsp_item(lsp_response($decoded, 2), 'find');
$traitDefinition = lsp_response($decoded, 3);
$parentCtor = lsp_response($decoded, 4);
$constDefinition = lsp_response($decoded, 5);
$enumCompletion = lsp_response($decoded, 6);
$hearts = lsp_item($enumCompletion, 'Hearts');

$traitUri = (string) ($traitDefinition['uri'] ?? ($traitDefinition[0]['uri'] ?? ''));
$ctorUri = (string) ($parentCtor['uri'] ?? ($parentCtor[0]['uri'] ?? ''));
$constUri = (string) ($constDefinition['uri'] ?? ($constDefinition[0]['uri'] ?? ''));

if ($code !== 0) {
    echo "FAILED: process exit\n";
    var_dump($code, $stderr);
} elseif (!$promoted) {
    echo "FAILED: promoted readonly property receiver did not complete find()\n";
    var_dump(lsp_response($decoded, 2));
} elseif (!str_contains($traitUri, '/src/Concerns/Greets.php')) {
    echo "FAILED: trait method definition did not resolve to the trait file\n";
    var_dump($traitDefinition);
} elseif (!str_contains($ctorUri, '/src/BasePresenter.php')) {
    echo "FAILED: parent::__construct did not resolve to the parent class\n";
    var_dump($parentCtor);
} elseif (!str_contains($constUri, '/src/Config.php')) {
    echo "FAILED: class constant definition did not resolve\n";
    var_dump($constDefinition);
} elseif (!$hearts || ($hearts['kind'] ?? 0) !== 20 || !lsp_item($enumCompletion, 'Spades')) {
    echo "FAILED: enum cases missing from Suit:: completion\n";
    var_dump($enumCompletion);
} else {
    echo "OK\n";
}

rrmdir($root);
?>
--EXPECT--
OK
