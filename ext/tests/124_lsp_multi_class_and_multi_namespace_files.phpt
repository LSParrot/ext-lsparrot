--TEST--
LSP resolves members, traits, and namespaces correctly in files declaring several classes or namespaces
--EXTENSIONS--
lsparrot
--FILE--
<?php
$root = '/tmp/lsp-multi-class-ns-test';
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
@mkdir($root . '/src', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);

// One file, one namespace, THREE classes: an inheriting second class, a
// trait-using third class, plus a class constant on the second class.
$models = <<<'PHP'
<?php
namespace MultiDecl;

class First
{
    public function firstMethod(): int
    {
        return 1;
    }
}

class Second extends First
{
    public const S_MAX = 5;

    public function secondMethod(): string
    {
        return 'two';
    }
}

class Third
{
    use SomeTrait;

    public function thirdMethod(): void
    {
    }
}
PHP;
file_put_contents($root . '/src/Models.php', $models);

$trait = <<<'PHP'
<?php
namespace MultiDecl;

trait SomeTrait
{
    public function traitMethod(): string
    {
        return 'trait';
    }
}
PHP;
file_put_contents($root . '/src/SomeTrait.php', $trait);

// One file, TWO namespaces, colliding short class name "Widget".
$multi = <<<'PHP'
<?php
namespace NsAlpha {
    class Widget
    {
        public function alphaSpin(): int
        {
            return 1;
        }
    }
}

namespace NsBeta {
    class Widget
    {
        public function betaSpin(): int
        {
            return 2;
        }
    }

    class Consumer
    {
        public function go(Widget $w): void
        {
            $w->betaSpin();
        }
    }
}
PHP;
file_put_contents($root . '/src/Multi.php', $multi);

// Separate file importing the class declared in the multi-namespace file.
$gamma = <<<'PHP'
<?php
namespace NsGamma;

use NsBeta\Widget;

class Runner
{
    public function drive(Widget $w): void
    {
        $w->betaSpin();
    }
}
PHP;
file_put_contents($root . '/src/Gamma.php', $gamma);

$consumer = <<<'PHP'
<?php
namespace MultiDecl;

class Consumer
{
    public function run(Second $s, Third $t): void
    {
        $s->secondMethod();
        $t->thirdMethod();
        $x = Second::S_MAX;
    }
}
PHP;
file_put_contents($root . '/src/Consumer.php', $consumer);

file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n" .
    "    'MultiDecl\\\\First' => " . var_export($root . '/src/Models.php', true) . ",\n" .
    "    'MultiDecl\\\\Second' => " . var_export($root . '/src/Models.php', true) . ",\n" .
    "    'MultiDecl\\\\Third' => " . var_export($root . '/src/Models.php', true) . ",\n" .
    "    'MultiDecl\\\\SomeTrait' => " . var_export($root . '/src/SomeTrait.php', true) . ",\n" .
    "    'MultiDecl\\\\Consumer' => " . var_export($root . '/src/Consumer.php', true) . ",\n" .
    "    'NsAlpha\\\\Widget' => " . var_export($root . '/src/Multi.php', true) . ",\n" .
    "    'NsBeta\\\\Widget' => " . var_export($root . '/src/Multi.php', true) . ",\n" .
    "    'NsBeta\\\\Consumer' => " . var_export($root . '/src/Multi.php', true) . ",\n" .
    "    'NsGamma\\\\Runner' => " . var_export($root . '/src/Gamma.php', true) . ",\n" .
    "];\n");
file_put_contents($root . '/vendor/composer/autoload_namespaces.php', "<?php\nreturn [];\n");
file_put_contents($root . '/vendor/composer/autoload_psr4.php', "<?php\nreturn [];\n");
file_put_contents($root . '/composer.json', '{"autoload":{"classmap":["src/"]}}');
file_put_contents($runner, "<?php\nLSParrot\\start_lsp(['analyzer' => 'lsparrot', 'workers' => ['count' => 1], 'symbolIndex' => ['size' => '4M']]);\n");

$consumerUri = 'file://' . $root . '/src/Consumer.php';
$multiUri = 'file://' . $root . '/src/Multi.php';
$gammaUri = 'file://' . $root . '/src/Gamma.php';
$modelsPath = $root . '/src/Models.php';

$messages = [
    ['jsonrpc' => '2.0', 'id' => 1, 'method' => 'initialize', 'params' => ['rootUri' => 'file://' . $root]],
    ['jsonrpc' => '2.0', 'method' => 'textDocument/didOpen', 'params' => ['textDocument' => ['uri' => $consumerUri, 'languageId' => 'php', 'version' => 1, 'text' => $consumer]]],
    // Definition of members declared by the SECOND and THIRD classes in Models.php.
    ['jsonrpc' => '2.0', 'id' => 2, 'method' => 'textDocument/definition', 'params' => ['textDocument' => ['uri' => $consumerUri], 'position' => lsp_position_after($consumer, '$s->secondMeth')]],
    ['jsonrpc' => '2.0', 'id' => 3, 'method' => 'textDocument/definition', 'params' => ['textDocument' => ['uri' => $consumerUri], 'position' => lsp_position_after($consumer, 'Second::S_MA')]],
    // Trait members of the third class must survive multi-class files.
    ['jsonrpc' => '2.0', 'id' => 4, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $consumerUri], 'position' => lsp_position_after($consumer, '$t->')]],
    ['jsonrpc' => '2.0', 'method' => 'textDocument/didOpen', 'params' => ['textDocument' => ['uri' => $multiUri, 'languageId' => 'php', 'version' => 1, 'text' => $multi]]],
    // Bare Widget inside the NsBeta block resolves to NsBeta\Widget.
    ['jsonrpc' => '2.0', 'id' => 5, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $multiUri], 'position' => lsp_position_after($multi, '$w->')]],
    ['jsonrpc' => '2.0', 'id' => 6, 'method' => 'textDocument/definition', 'params' => ['textDocument' => ['uri' => $multiUri], 'position' => lsp_position_after($multi, 'public function go(Widget')]],
    ['jsonrpc' => '2.0', 'method' => 'textDocument/didOpen', 'params' => ['textDocument' => ['uri' => $gammaUri, 'languageId' => 'php', 'version' => 1, 'text' => $gamma]]],
    // Importing a class declared inside a multi-namespace file from elsewhere.
    ['jsonrpc' => '2.0', 'id' => 7, 'method' => 'textDocument/completion', 'params' => ['textDocument' => ['uri' => $gammaUri], 'position' => lsp_position_after($gamma, '$w->')]],
    ['jsonrpc' => '2.0', 'id' => 8, 'method' => 'shutdown', 'params' => []],
    ['jsonrpc' => '2.0', 'method' => 'exit', 'params' => []],
];

[$stdout, $stderr, $code] = run_lsp($extension, $runner, $messages);
$decoded = lsp_messages($stdout);

$secondDef = lsp_response($decoded, 2);
$constDef = lsp_response($decoded, 3);
$thirdCompletion = lsp_response($decoded, 4);
$betaCompletion = lsp_response($decoded, 5);
$widgetDef = lsp_response($decoded, 6);
$gammaCompletion = lsp_response($decoded, 7);

$secondMethodLine = lsp_position_after($models, 'public function secondMeth')['line'];
$constLine = lsp_position_after($models, 'public const S_MA')['line'];
$betaWidgetLine = lsp_position_after($multi, "namespace NsBeta {\n    class Widg")['line'];

if ($code !== 0) {
    echo "FAILED: process exit\n";
    var_dump($code, $stderr);
} elseif (($secondDef['range']['start']['line'] ?? -1) !== $secondMethodLine) {
    echo "FAILED: definition of Second::secondMethod (second class in file)\n";
    var_dump($secondDef, $secondMethodLine);
} elseif (($constDef['range']['start']['line'] ?? -1) !== $constLine) {
    echo "FAILED: definition of Second::S_MAX (constant on second class in file)\n";
    var_dump($constDef, $constLine);
} elseif (!lsp_item($thirdCompletion, 'traitMethod') || !lsp_item($thirdCompletion, 'thirdMethod')) {
    echo "FAILED: trait members missing for the third class in a multi-class file\n";
    var_dump($thirdCompletion);
} elseif (lsp_item($thirdCompletion, 'firstMethod')) {
    echo "FAILED: Third completion leaked members of an unrelated class in the same file\n";
    var_dump($thirdCompletion);
} elseif (!lsp_item($betaCompletion, 'betaSpin') || lsp_item($betaCompletion, 'alphaSpin')) {
    echo "FAILED: bare Widget inside the NsBeta block must be NsBeta\\Widget\n";
    var_dump($betaCompletion);
} elseif (($widgetDef['range']['start']['line'] ?? -1) !== $betaWidgetLine) {
    echo "FAILED: definition on bare Widget type hint picked the wrong namespace's class\n";
    var_dump($widgetDef, $betaWidgetLine);
} elseif (!lsp_item($gammaCompletion, 'betaSpin')) {
    echo "FAILED: use-import of a class declared in a multi-namespace file\n";
    var_dump($gammaCompletion);
} else {
    echo "OK\n";
}

rrmdir($root);
?>
--EXPECT--
OK
