--TEST--
LSP hover keeps complex array-shape PHPDoc types for PHPStan and Psalm backends
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 111

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-analyzer-array-shape-test"}}Content-Length: 596

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-analyzer-array-shape-test/file.php","languageId":"php","version":1,"text":"<?php\nfinal class PayloadDemo\n{\n    /**\n     * @return array{items: Collection<User>, meta: array{count: positive-int, labels: non-empty-list<string>}}\n     */\n    public function complexPayload(): array\n    {\n        return [];\n    }\n\n    public function run(): void\n    {\n        $payload = $this->complexPayload();\n        $payload;\n    }\n}\n\nfinal class Collection\n{\n}\n\nfinal class User\n{\n}\n"}}}Content-Length: 179

{"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-analyzer-array-shape-test/file.php"},"position":{"line":14,"character":10}}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-analyzer-array-shape-test';
@mkdir($root . '/vendor/bin', 0777, true);
file_put_contents($root . '/psalm.xml', '<?xml version="1.0"?><psalm></psalm>');
file_put_contents($root . '/vendor/bin/phpstan', "#!/usr/bin/env php\n<?php echo json_encode(['files' => []]);\n");
file_put_contents($root . '/vendor/bin/psalm', "#!/usr/bin/env php\n<?php echo json_encode([]);\n");
chmod($root . '/vendor/bin/phpstan', 0755);
chmod($root . '/vendor/bin/psalm', 0755);
LSParrot\start_lsp([
    'analyzer' => ['phpstan', 'psalm'],
    'workers' => ['count' => 0, 'analyzerDiagnosticsTimeout' => 5],
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-analyzer-array-shape-test';
if (is_dir($root)) {
    $it = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($root, FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::CHILD_FIRST);
    foreach ($it as $file) {
        $file->isDir() ? @rmdir($file->getPathname()) : @unlink($file->getPathname());
    }
    @rmdir($root);
}
?>
--EXPECTREGEX--
[\s\S]*"method":"lsparrot.php\/analyzerStatus"[\s\S]*"driver":"lsparrot\+phpstan\+psalm"[\s\S]*"jsonrpc":"2\.0","id":2,"result"[\s\S]*array\{items: Collection<User>, meta: array\{count: int, labels: non-empty-list<string>\}\}[\s\S]*LSParrot Engine[\s\S]*array\{items: Collection<User>, meta: array\{count: positive-int, labels: non-empty-list<string>\}\}[\s\S]*PHPStan \/ Psalm[\s\S]*\{"jsonrpc":"2\.0","id":3,"result":null\}
