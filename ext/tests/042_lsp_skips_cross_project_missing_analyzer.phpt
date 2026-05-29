--TEST--
LSP skips analyzer diagnostics silently when the tool exists only in another Composer project
--EXTENSIONS--
lsparrot
--SKIPIF--
<?php
$srcdir = getenv('TEST_PHP_SRCDIR');
if (!is_string($srcdir) || $srcdir === '') {
    $srcdir = getcwd();
}
require $srcdir . '/tests/valgrind_check.inc';
lsparrot_test_prepare_valgrind_for_empty_path('/tmp/lsp-cross-project-empty-path');
?>
--ENV--
PATH=/tmp/lsp-cross-project-empty-path
--STDIN--
Content-Length: 109

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-cross-project-tool-test"}}Content-Length: 212

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-cross-project-tool-test/file.php","languageId":"php","version":1,"text":"<?php\nfunction rootFile(): void {}\n"}}}Content-Length: 56

{"jsonrpc":"2.0","id":2,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-cross-project-tool-test';
$nested = $root . '/nested';
@mkdir('/tmp/lsp-cross-project-empty-path', 0777, true);
@mkdir($nested . '/vendor/bin', 0777, true);
file_put_contents($nested . '/composer.json', '{}');
file_put_contents($nested . '/vendor/bin/phpstan', "#!/usr/bin/env php\n<?php echo json_encode(['files' => []]);\n");
chmod($nested . '/vendor/bin/phpstan', 0755);
LSParrot\start_lsp([
    'analyzer' => 'auto',
    'workers' => ['count' => 0, 'analyzerDiagnosticsTimeout' => 5],
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
function lsp_rrmdir(string $path): void
{
    if (!is_dir($path)) {
        return;
    }
    foreach (scandir($path) ?: [] as $name) {
        if ($name === '.' || $name === '..') {
            continue;
        }
        $child = $path . '/' . $name;
        if (is_dir($child) && !is_link($child)) {
            lsp_rrmdir($child);
        } else {
            @unlink($child);
        }
    }
    @rmdir($path);
}
lsp_rrmdir('/tmp/lsp-cross-project-tool-test');
lsp_rrmdir('/tmp/lsp-cross-project-empty-path');
?>
--EXPECTREGEX--
(?s)\A(?!.*PHPStan is not installed)(?!.*Running PHPStan diagnostics).*"analyzer":"driver".*"driver":"lsparrot\+phpstan".*"label":"LSParrot Engine \+ PHPStan".*"textDocument\/publishDiagnostics".*"jsonrpc":"2\.0","id":2,"result":null.*\z
