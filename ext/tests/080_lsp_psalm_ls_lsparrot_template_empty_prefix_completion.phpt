--TEST--
LSP keeps lsparrot PHPDoc template member completions available with Psalm LS only and empty member prefix
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 130

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-psalm-ls-lsparrot-template-empty-prefix-test"}}Content-Length: 668

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-psalm-ls-lsparrot-template-empty-prefix-test/src/Demo.php","languageId":"php","version":1,"text":"<?php\n\nnamespace PsalmLsLSParrotTemplateFixture;\n\nuse PsalmLsLSParrotTemplateFixture\\Domain\\User;\n\nfinal class Demo\n{\n    /**\n     * @psalm-template T\n     * @psalm-param T $value\n     * @psalm-return T\n     */\n    public function templateTestPsalm(mixed $value): mixed\n    {\n        return $value;\n    }\n\n    public function run(): void\n    {\n        $user = new User();\n        $val = $this->templateTestPsalm($user);\n        $val->\n    }\n}"}}}Content-Length: 207

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-psalm-ls-lsparrot-template-empty-prefix-test/src/Demo.php"},"position":{"line":22,"character":14}}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-psalm-ls-lsparrot-template-empty-prefix-test';
@mkdir($root . '/src/Domain', 0777, true);
@mkdir($root . '/vendor/bin', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);
file_put_contents($root . '/composer.json', "{\"autoload\":{\"psr-4\":{\"PsalmLsLSParrotTemplateFixture\\\\\\\\\":\"src/\"}}}\n");
file_put_contents($root . '/psalm.xml', '<?xml version="1.0"?><psalm><projectFiles><directory name="src" /></projectFiles></psalm>');
file_put_contents($root . '/src/Domain/User.php', <<<'PHP'
<?php
namespace PsalmLsLSParrotTemplateFixture\Domain;

final class User
{
    public function foo(): void
    {
    }
}
PHP);
file_put_contents($root . '/src/Demo.php', <<<'PHP'
<?php

namespace PsalmLsLSParrotTemplateFixture;

use PsalmLsLSParrotTemplateFixture\Domain\User;

final class Demo
{
    /**
     * @psalm-template T
     * @psalm-param T $value
     * @psalm-return T
     */
    public function templateTestPsalm(mixed $value): mixed
    {
        return $value;
    }

    public function run(): void
    {
        $user = new User();
        $val = $this->templateTestPsalm($user);
        $val->
    }
}
PHP);
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'PsalmLsLSParrotTemplateFixture\\\\Domain\\\\User' => " . var_export($root . '/src/Domain/User.php', true) . ",\n    'PsalmLsLSParrotTemplateFixture\\\\Demo' => " . var_export($root . '/src/Demo.php', true) . ",\n];\n");
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
        frame_write(['jsonrpc' => '2.0', 'id' => $message['id'], 'result' => ['items' => []]]);
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
        'maxResponseWaitMs' => 500,
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
$root = '/tmp/lsp-psalm-ls-lsparrot-template-empty-prefix-test';
if (is_dir($root)) {
    $it = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($root, FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::CHILD_FIRST);
    foreach ($it as $file) {
        $file->isDir() ? @rmdir($file->getPathname()) : @unlink($file->getPathname());
    }
    @rmdir($root);
}
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%A"label":"foo"%A"detail":"foo(): void"%A"source":"psalm-ls"%AContent-Length: %d

{"jsonrpc":"2.0","id":3,"result":null}
