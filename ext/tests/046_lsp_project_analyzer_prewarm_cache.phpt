--TEST--
LSP reuses project-level PHPStan diagnostics cache for opened documents
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 112

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-project-prewarm-cache-test"}}Content-Length: 67

{"jsonrpc":"2.0","id":2,"method":"lsparrot.php/status","params":[]}Content-Length: 67

{"jsonrpc":"2.0","id":3,"method":"lsparrot.php/status","params":[]}Content-Length: 196

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-project-prewarm-cache-test/src/A.php","languageId":"php","version":1,"text":"<?php\n\nfoo();\n"}}}Content-Length: 196

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-project-prewarm-cache-test/src/B.php","languageId":"php","version":1,"text":"<?php\n\nbar();\n"}}}Content-Length: 67

{"jsonrpc":"2.0","id":5,"method":"lsparrot.php/status","params":[]}Content-Length: 56

{"jsonrpc":"2.0","id":6,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-project-prewarm-cache-test';
@mkdir($root . '/src', 0777, true);
@mkdir($root . '/.lsparrot/phpstan', 0777, true);
@mkdir($root . '/vendor/bin', 0777, true);
file_put_contents($root . '/composer.json', "{\"name\":\"zeriyoshi/prewarm-cache-test\",\"autoload\":{\"psr-4\":{\"PrewarmCacheFixture\\\\\\\\\":\"src/\"}}}\n");
file_put_contents($root . '/src/A.php', "<?php\n\nfoo();\n");
file_put_contents($root . '/src/B.php', "<?php\n\nbar();\n");
file_put_contents($root . '/.lsparrot/phpstan/project-diagnostics.json', json_encode([
    'files' => [
        $root . '/src/A.php' => [
            'messages' => [[
                'line' => 3,
                'message' => 'prewarm run 1 A',
                'identifier' => 'prewarm.a',
            ]],
        ],
        $root . '/src/B.php' => [
            'messages' => [[
                'line' => 3,
                'message' => 'prewarm run 1 B',
                'identifier' => 'prewarm.b',
            ]],
        ],
    ],
], JSON_UNESCAPED_SLASHES));
file_put_contents($root . '/vendor/bin/phpstan', <<<'PHP'
#!/usr/bin/env php
<?php
$root = getcwd();
$countFile = $root . '/.lsparrot/phpstan/run-count.txt';
@mkdir(dirname($countFile), 0777, true);
$count = ((int) @file_get_contents($countFile)) + 1;
file_put_contents($countFile, (string) $count);
echo json_encode([
    'files' => [
        $root . '/src/A.php' => [
            'messages' => [[
                'line' => 3,
                'message' => 'prewarm run ' . $count . ' A',
                'identifier' => 'prewarm.a',
            ]],
        ],
        $root . '/src/B.php' => [
            'messages' => [[
                'line' => 3,
                'message' => 'prewarm run ' . $count . ' B',
                'identifier' => 'prewarm.b',
            ]],
        ],
    ],
]);
PHP);
chmod($root . '/vendor/bin/phpstan', 0755);
LSParrot\start_lsp([
    'analyzer' => 'phpstan',
    'workers' => ['analyzerDiagnosticsTimeout' => 5],
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-project-prewarm-cache-test';
if (is_dir($root)) {
    $it = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($root, FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::CHILD_FIRST);
    foreach ($it as $file) {
        $file->isDir() ? @rmdir($file->getPathname()) : @unlink($file->getPathname());
    }
    @rmdir($root);
}
?>
--EXPECTREGEX--
[\s\S]*"method":"lsparrot.php\/analyzerStatus"[\s\S]*"analyzer":"phpstan"[\s\S]*"state":"running"[\s\S]*Prewarming PHPStan project diagnostics\.[\s\S]*"projectRoot":"\/tmp\/lsp-project-prewarm-cache-test"[\s\S]*"jsonrpc":"2\.0","id":1,"result"[\s\S]*"jsonrpc":"2\.0","id":2,"result"[\s\S]*"jsonrpc":"2\.0","id":3,"result"[\s\S]*"textDocument\/publishDiagnostics"[\s\S]*"source":"phpstan"[\s\S]*"message":"prewarm run 1 A"[\s\S]*"code":"prewarm\.a"[\s\S]*"textDocument\/publishDiagnostics"[\s\S]*"source":"phpstan"[\s\S]*"message":"prewarm run 1 B"[\s\S]*"code":"prewarm\.b"[\s\S]*"jsonrpc":"2\.0","id":5,"result"[\s\S]*"analyzers"[\s\S]*"phpstan"[\s\S]*"enabled":true[\s\S]*"projects"[\s\S]*lsp-project-prewarm-cache-test[\s\S]*"(running|ready)"[\s\S]*\{"jsonrpc":"2\.0","id":6,"result":null\}
