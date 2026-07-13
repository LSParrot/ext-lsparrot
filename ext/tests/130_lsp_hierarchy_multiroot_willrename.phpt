--TEST--
LSP call/type hierarchy, multi-root workspaces, and workspace/willRenameFiles
--EXTENSIONS--
lsparrot
--FILE--
<?php
$rootA = '/tmp/lsp-hier-mr-test-a';
$rootB = '/tmp/lsp-hier-mr-test-b';
require __DIR__ . "/lsp_test_helper.inc";
$extension = lsp_test_extension_path();

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

foreach ([$rootA, $rootB] as $r) {
    rrmdir($r);
    @mkdir($r . '/src/Sub', 0777, true);
    @mkdir($r . '/vendor/composer', 0777, true);
}

// Root A: interface + implementation + subclass + caller.
file_put_contents($rootA . '/src/Shape.php', <<<'PHP'
<?php
namespace HierA;

interface Shape
{
    public function area(): float;
}
PHP);

$circle = <<<'PHP'
<?php
namespace HierA;

class Circle implements Shape
{
    public function area(): float
    {
        return 3.14 * $this->radius();
    }

    public function radius(): float
    {
        return 2.0;
    }
}
PHP;
file_put_contents($rootA . '/src/Circle.php', $circle);
file_put_contents($rootA . '/src/BigCircle.php', "<?php\nnamespace HierA;\n\nclass BigCircle extends Circle\n{\n}\n");
file_put_contents($rootA . '/src/Report.php', <<<'PHP'
<?php
namespace HierA;

class Report
{
    public function total(Circle $c): float
    {
        return $c->area();
    }
}
PHP);

// Root B: a class only reachable when the second workspace folder is indexed.
file_put_contents($rootB . '/src/Remote.php', "<?php\nnamespace HierB;\n\nclass Remote\n{\n    public function ping(): int { return 1; }\n}\n");

foreach ([[$rootA, 'HierA', ['Shape', 'Circle', 'BigCircle', 'Report']], [$rootB, 'HierB', ['Remote']]] as [$r, $ns, $classes]) {
    $cm = "<?php\nreturn [\n";
    foreach ($classes as $c) {
        $cm .= "    '$ns\\\\$c' => " . var_export("$r/src/$c.php", true) . ",\n";
    }
    $cm .= "];\n";
    file_put_contents($r . '/vendor/composer/autoload_classmap.php', $cm);
    file_put_contents($r . '/vendor/composer/autoload_psr4.php', "<?php\nreturn ['$ns\\\\' => [" . var_export($r . '/src', true) . "]];\n");
    file_put_contents($r . '/vendor/composer/autoload_namespaces.php', "<?php\nreturn [];\n");
    file_put_contents($r . '/composer.json', '{"autoload":{"classmap":["src/"],"psr-4":{"' . $ns . '\\\\":"src/"}}}');
}
$runner = $rootA . '/run.php';
file_put_contents($runner, "<?php\nLSParrot\\start_lsp(['analyzer' => 'lsparrot', 'workers' => ['count' => 1], 'symbolIndex' => ['size' => '4M']]);\n");

$circleUri = 'file://' . $rootA . '/src/Circle.php';
$messages = [
    ['jsonrpc' => '2.0', 'id' => 1, 'method' => 'initialize', 'params' => [
        'rootUri' => 'file://' . $rootA,
        'workspaceFolders' => [
            ['uri' => 'file://' . $rootA, 'name' => 'a'],
            ['uri' => 'file://' . $rootB, 'name' => 'b'],
        ],
    ]],
    ['jsonrpc' => '2.0', 'method' => 'textDocument/didOpen', 'params' => ['textDocument' => ['uri' => $circleUri, 'languageId' => 'php', 'version' => 1, 'text' => $circle]]],
    ['jsonrpc' => '2.0', 'id' => 2, 'method' => 'workspace/symbol', 'params' => ['query' => 'Remote']],
    ['jsonrpc' => '2.0', 'id' => 3, 'method' => 'textDocument/prepareCallHierarchy', 'params' => ['textDocument' => ['uri' => $circleUri], 'position' => lsp_position_after($circle, 'public function ar')]],
    ['jsonrpc' => '2.0', 'id' => 4, 'method' => 'textDocument/prepareTypeHierarchy', 'params' => ['textDocument' => ['uri' => $circleUri], 'position' => lsp_position_after($circle, 'class Circ')]],
    ['jsonrpc' => '2.0', 'id' => 5, 'method' => 'workspace/willRenameFiles', 'params' => ['files' => [
        ['oldUri' => 'file://' . $rootA . '/src/Circle.php', 'newUri' => 'file://' . $rootA . '/src/Disk.php'],
    ]]],
    ['jsonrpc' => '2.0', 'id' => 6, 'method' => 'workspace/willRenameFiles', 'params' => ['files' => [
        ['oldUri' => 'file://' . $rootA . '/src/Circle.php', 'newUri' => 'file://' . $rootA . '/src/Sub/Circle.php'],
    ]]],
    ['jsonrpc' => '2.0', 'id' => 99, 'method' => 'shutdown', 'params' => []],
    ['jsonrpc' => '2.0', 'method' => 'exit', 'params' => []],
];

[$stdout, $stderr, $code] = run_lsp($extension, $runner, $messages);
$decoded = lsp_messages($stdout);
$init = lsp_response($decoded, 1);
$remote = lsp_response($decoded, 2) ?? [];
$callPrep = lsp_response($decoded, 3) ?? [];
$typePrep = lsp_response($decoded, 4) ?? [];
$renameEdit = lsp_response($decoded, 5);
$moveEdit = lsp_response($decoded, 6);

$capabilities = $init['capabilities'] ?? [];
$callItem = $callPrep[0] ?? null;
$typeItem = $typePrep[0] ?? null;

// Second session drives incoming/outgoing/super/subtypes with the items.
$messages = [
    ['jsonrpc' => '2.0', 'id' => 1, 'method' => 'initialize', 'params' => ['rootUri' => 'file://' . $rootA]],
    ['jsonrpc' => '2.0', 'id' => 2, 'method' => 'callHierarchy/incomingCalls', 'params' => ['item' => $callItem]],
    ['jsonrpc' => '2.0', 'id' => 3, 'method' => 'callHierarchy/outgoingCalls', 'params' => ['item' => $callItem]],
    ['jsonrpc' => '2.0', 'id' => 4, 'method' => 'typeHierarchy/supertypes', 'params' => ['item' => $typeItem]],
    ['jsonrpc' => '2.0', 'id' => 5, 'method' => 'typeHierarchy/subtypes', 'params' => ['item' => $typeItem]],
    ['jsonrpc' => '2.0', 'id' => 99, 'method' => 'shutdown', 'params' => []],
    ['jsonrpc' => '2.0', 'method' => 'exit', 'params' => []],
];
[$stdout2, , $code2] = run_lsp($extension, $runner, $messages);
$decoded2 = lsp_messages($stdout2);
$incoming = array_map(static fn($c) => $c['from']['name'] ?? '', lsp_response($decoded2, 2) ?? []);
$outgoing = array_map(static fn($c) => $c['to']['name'] ?? '', lsp_response($decoded2, 3) ?? []);
$supertypes = array_map(static fn($i) => $i['name'] ?? '', lsp_response($decoded2, 4) ?? []);
$subtypes = array_map(static fn($i) => $i['name'] ?? '', lsp_response($decoded2, 5) ?? []);

$renameFiles = array_map('basename', array_keys($renameEdit['changes'] ?? []));
sort($renameFiles);
$namespaceEdits = $moveEdit['changes']['file://' . $rootA . '/src/Circle.php'] ?? [];
$namespaceNewText = $namespaceEdits[0]['newText'] ?? '';

if ($code !== 0 || $code2 !== 0) {
    echo "FAILED: process exit\n";
    var_dump($code, $code2, $stderr);
} elseif (($capabilities['callHierarchyProvider'] ?? null) !== true ||
    ($capabilities['typeHierarchyProvider'] ?? null) !== true ||
    ($capabilities['workspace']['workspaceFolders']['supported'] ?? null) !== true ||
    !isset($capabilities['workspace']['fileOperations']['willRename'])
) {
    echo "FAILED: hierarchy/workspace capabilities missing\n";
    var_dump($capabilities);
} elseif (count(array_filter($remote, static fn($s) => ($s['name'] ?? '') === 'HierB\\Remote')) !== 1) {
    echo "FAILED: second workspace folder must be indexed\n";
    var_dump($remote);
} elseif (($callItem['name'] ?? '') !== 'area' || ($callItem['kind'] ?? 0) !== 6) {
    echo "FAILED: prepareCallHierarchy on a method declaration\n";
    var_dump($callPrep);
} elseif (!in_array('total', $incoming, true)) {
    echo "FAILED: incoming calls must include Report::total\n";
    var_dump($incoming);
} elseif (!in_array('radius', $outgoing, true)) {
    echo "FAILED: outgoing calls must include \$this->radius()\n";
    var_dump($outgoing);
} elseif ($supertypes !== ['HierA\\Shape']) {
    echo "FAILED: supertypes of Circle\n";
    var_dump($supertypes);
} elseif ($subtypes !== ['HierA\\BigCircle']) {
    echo "FAILED: subtypes of Circle\n";
    var_dump($subtypes);
} elseif ($renameFiles !== ['BigCircle.php', 'Circle.php', 'Report.php']) {
    echo "FAILED: willRenameFiles must rename the class across declaration, subclass, and caller\n";
    var_dump($renameFiles);
} elseif ($namespaceNewText !== 'HierA\\Sub') {
    echo "FAILED: moving into a PSR-4 subdirectory must rewrite the namespace\n";
    var_dump($moveEdit);
} else {
    echo "OK\n";
}

rrmdir($rootA);
rrmdir($rootB);
?>
--EXPECT--
OK
