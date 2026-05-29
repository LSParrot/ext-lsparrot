--TEST--
LSP returns LSParrot Engine member completions immediately when Psalm LS completion is slow
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 115

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-psalm-ls-slow-completion-test"}}Content-Length: 400

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-psalm-ls-slow-completion-test/src/Demo.php","languageId":"php","version":1,"text":"<?php\n\nnamespace SlowPsalmLsCompletionFixture;\n\nfinal class Demo\n{\n    public function first(): void {}\n    public function second(): void {}\n    public function run(): void\n    {\n        $this->\n    }\n}"}}}Content-Length: 192

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-psalm-ls-slow-completion-test/src/Demo.php"},"position":{"line":10,"character":15}}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-psalm-ls-slow-completion-test';
@mkdir($root . '/src', 0777, true);
@mkdir($root . '/vendor/bin', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);
file_put_contents($root . '/composer.json', "{\"autoload\":{\"psr-4\":{\"SlowPsalmLsCompletionFixture\\\\\\\\\":\"src/\"}}}\n");
file_put_contents($root . '/psalm.xml', '<?xml version="1.0"?><psalm><projectFiles><directory name="src" /></projectFiles></psalm>');
file_put_contents($root . '/src/Demo.php', <<<'PHP'
<?php

namespace SlowPsalmLsCompletionFixture;

final class Demo
{
    public function first(): void {}
    public function second(): void {}
    public function run(): void
    {
        $this->
    }
}
PHP);
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'SlowPsalmLsCompletionFixture\\\\Demo' => " . var_export($root . '/src/Demo.php', true) . ",\n];\n");
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
        frame_write(['jsonrpc' => '2.0', 'id' => $message['id'], 'result' => ['capabilities' => ['hoverProvider' => true, 'completionProvider' => new stdClass()]]]);
        continue;
    }
    if ($method === 'textDocument/didOpen') {
        frame_write(['jsonrpc' => '2.0', 'method' => 'textDocument/publishDiagnostics', 'params' => ['uri' => $message['params']['textDocument']['uri'], 'diagnostics' => []]]);
        continue;
    }
    if ($method === 'textDocument/hover') {
        frame_write(['jsonrpc' => '2.0', 'id' => $message['id'], 'result' => null]);
        continue;
    }
    if ($method === 'textDocument/completion') {
        usleep(800000);
        frame_write(['jsonrpc' => '2.0', 'id' => $message['id'], 'result' => ['items' => [[
            'label' => 'DelayedPsalmCompletion',
            'kind' => 2,
            'detail' => 'from slow Psalm LS',
        ]]]]);
        continue;
    }
    if ($method === 'shutdown') {
        frame_write(['jsonrpc' => '2.0', 'id' => $message['id'], 'result' => null]);
        exit(0);
    }
}
PHP);
chmod($root . '/vendor/bin/psalm-language-server', 0755);
LSParrot\start_lsp([
    'analyzer' => 'psalm-ls',
    'psalm' => [
        'transport' => 'languageServer',
        'maxResponseWaitMs' => 1500,
        'enableHover' => true,
        'enableAutocomplete' => true,
        'enableDiagnostics' => true,
    ],
    'workers' => ['count' => 0],
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-psalm-ls-slow-completion-test';
if (is_dir($root)) {
    $it = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($root, FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::CHILD_FIRST);
    foreach ($it as $file) {
        $file->isDir() ? @rmdir($file->getPathname()) : @unlink($file->getPathname());
    }
    @rmdir($root);
}
?>
--EXPECTREGEX--
(?s)\A(?!.*DelayedPsalmCompletion).*"jsonrpc":"2\.0","id":2,"result".*"isIncomplete":true.*"label":"first".*"label":"second".*\{"jsonrpc":"2\.0","id":3,"result":null\}.*\z
