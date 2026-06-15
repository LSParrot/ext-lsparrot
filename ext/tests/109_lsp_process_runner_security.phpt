--TEST--
LSP process spawning avoids shell lookup and runner rejects external include targets
--EXTENSIONS--
lsparrot
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') {
    die("skip POSIX shell lookup test\n");
}
?>
--STDIN--
Content-Length: 114

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-process-runner-security-test"}}Content-Length: 354

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-process-runner-security-test/src/Thing.php","languageId":"php","version":1,"text":"<?php\n\nnamespace RunnerSecurityFixture;\n\nfinal class Thing\n{\n    public function go(): void\n    {\n        $value = $this->mystery();\n        $value;\n    }\n}\n"}}}Content-Length: 185

{"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-process-runner-security-test/src/Thing.php"},"position":{"line":9,"character":9}}}Content-Length: 67

{"jsonrpc":"2.0","id":3,"method":"lsparrot.php/status","params":[]}Content-Length: 56

{"jsonrpc":"2.0","id":4,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-process-runner-security-test';
$fakePath = '/tmp/lsp-process-runner-security-path';
$shellMarker = '/tmp/lsp-process-runner-security-shell-marker';
$includeMarker = '/tmp/lsp-process-runner-security-include-marker';
$evil = '/tmp/lsp-process-runner-security-evil.php';

function rrmdir(string $path): void {
    if (!is_dir($path)) {
        return;
    }
    $it = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($path, FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::CHILD_FIRST);
    foreach ($it as $file) {
        $file->isDir() ? @rmdir($file->getPathname()) : @unlink($file->getPathname());
    }
    @rmdir($path);
}

rrmdir($root);
rrmdir($fakePath);
@unlink($shellMarker);
@unlink($includeMarker);
@unlink($evil);

@mkdir($root . '/src', 0777, true);
@mkdir($root . '/vendor/bin', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);
@mkdir($fakePath, 0777, true);

file_put_contents($fakePath . '/sh', "#!/bin/sh\nprintf shell > " . escapeshellarg($shellMarker) . "\nexit 127\n");
chmod($fakePath . '/sh', 0755);
putenv('PATH=' . $fakePath . PATH_SEPARATOR . (getenv('PATH') ?: ''));

file_put_contents($root . '/composer.json', json_encode([
    'autoload' => ['psr-4' => ['RunnerSecurityFixture\\' => 'src/']],
], JSON_PRETTY_PRINT));
file_put_contents($root . '/vendor/composer/autoload_psr4.php', "<?php\nreturn [\n    'RunnerSecurityFixture\\\\' => [" . var_export($root . '/src', true) . "],\n];\n");
file_put_contents($root . '/vendor/bin/phpstan', <<<'PHP'
#!/usr/bin/env php
<?php
$file = '';
foreach ($argv as $arg) {
    if (is_file($arg) && str_contains($arg, '/.lsparrot/shadow/phpstan-type/')) {
        $file = $arg;
        break;
    }
}
if ($file === '') {
    echo json_encode(['files' => [], 'errors' => []]);
    exit(0);
}
$contents = file_get_contents($file) ?: '';
$line = 1;
foreach (explode("\n", $contents) as $i => $text) {
    if (str_contains($text, '\PHPStan\dumpType(')) {
        $line = $i + 1;
        break;
    }
}
echo json_encode([
    'totals' => ['errors' => 0, 'file_errors' => 1],
    'files' => [
        $file => [
            'errors' => 1,
            'messages' => [[
                'message' => 'Dumped type: RunnerSecurityFixture\Mystery',
                'line' => $line,
                'ignorable' => false,
                'identifier' => 'phpstan.dumpType',
            ]],
        ],
    ],
    'errors' => [],
]);
exit(1);
PHP);
chmod($root . '/vendor/bin/phpstan', 0755);

LSParrot\start_lsp([
    'analyzer' => 'phpstan',
    'workers' => ['count' => 0, 'analyzerDiagnosticsTimeout' => 5],
    'symbolIndex' => ['size' => '4M'],
]);

echo file_exists($shellMarker) ? "SHELL_INVOKED\n" : "NO_SHELL\n";

$runner = $root . '/.lsparrot/runner/lsp-runner.php';
file_put_contents($evil, "<?php\nfile_put_contents(" . var_export($includeMarker, true) . ", 'included');\n");
$payload = json_encode(['id' => 99, 'argv' => [$evil], 'cwd' => '/'], JSON_UNESCAPED_SLASHES);
$frame = 'Content-Length: ' . strlen($payload) . "\r\n\r\n" . $payload;
$process = proc_open([
    PHP_BINARY,
    $runner,
    $root,
    'phpstan',
], [
    0 => ['pipe', 'r'],
    1 => ['pipe', 'w'],
    2 => ['pipe', 'w'],
], $pipes, $root);

$runnerOutput = '';
if (is_resource($process)) {
    fwrite($pipes[0], $frame);
    fclose($pipes[0]);
    $runnerOutput = stream_get_contents($pipes[1]);
    fclose($pipes[1]);
    fclose($pipes[2]);
    proc_close($process);
}

echo (!file_exists($includeMarker) && str_contains($runnerOutput, '"exitCode":127')) ? "RUNNER_REJECTED\n" : "RUNNER_ACCEPTED\n";
?>
--CLEAN--
<?php
$root = '/tmp/lsp-process-runner-security-test';
$fakePath = '/tmp/lsp-process-runner-security-path';
$shellMarker = '/tmp/lsp-process-runner-security-shell-marker';
$includeMarker = '/tmp/lsp-process-runner-security-include-marker';
$evil = '/tmp/lsp-process-runner-security-evil.php';
foreach ([$root, $fakePath] as $dir) {
    if (is_dir($dir)) {
        $it = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($dir, FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::CHILD_FIRST);
        foreach ($it as $file) {
            $file->isDir() ? @rmdir($file->getPathname()) : @unlink($file->getPathname());
        }
        @rmdir($dir);
    }
}
@unlink($shellMarker);
@unlink($includeMarker);
@unlink($evil);
?>
--EXPECTREGEX--
(?s)\A(?!.*SHELL_INVOKED)(?!.*RUNNER_ACCEPTED).*"jsonrpc":"2\.0","id":1,"result".*"jsonrpc":"2\.0","id":2,"result".*RunnerSecurityFixture\\\\Mystery.*"jsonrpc":"2\.0","id":3,"result".*"runners":\[\{"analyzer":"phpstan","projectRoot":"\/tmp\/lsp-process-runner-security-test","jobs":[1-9][0-9]*,"alive":true\}\].*"jsonrpc":"2\.0","id":4,"result":null.*NO_SHELL.*RUNNER_REJECTED\s*\z
