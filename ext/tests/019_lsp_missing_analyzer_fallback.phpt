--TEST--
LSP reports explicitly requested missing analyzers and falls back to lsparrot
--EXTENSIONS--
lsparrot
--SKIPIF--
<?php
$srcdir = getenv('TEST_PHP_SRCDIR');
if (!is_string($srcdir) || $srcdir === '') {
    $srcdir = getcwd();
}
require $srcdir . '/tests/valgrind_check.inc';
lsparrot_test_prepare_valgrind_for_empty_path('/tmp/lsp-missing-analyzer-empty-path');
?>
--ENV--
PATH=/tmp/lsp-missing-analyzer-empty-path
--STDIN--
Content-Length: 106

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-missing-phpstan-test"}}Content-Length: 221

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-missing-phpstan-test/file.php","languageId":"php","version":1,"text":"<?php\nfunction alphaFallback(): void {}\n// al\n"}}}Content-Length: 177

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-missing-phpstan-test/file.php"},"position":{"line":2,"character":5}}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
@mkdir('/tmp/lsp-missing-phpstan-test', 0777, true);
@mkdir('/tmp/lsp-missing-analyzer-empty-path', 0777, true);
LSParrot\start_lsp([
    'analyzer' => 'phpstan',
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
@unlink('/tmp/lsp-missing-analyzer-empty-path/valgrind');
@rmdir('/tmp/lsp-missing-phpstan-test');
@rmdir('/tmp/lsp-missing-analyzer-empty-path');
?>
--EXPECTF--
Content-Length: %d

%A"method":"lsparrot.php/analyzerStatus"%A"analyzer":"phpstan"%A"state":"error"%A"missingAnalyzer":"phpstan"%AContent-Length: %d

%A"method":"window/showMessage"%A"type":1%A"message":"PHPStan is not installed; falling back to LSParrot Engine."%AContent-Length: %d

%A"method":"lsparrot.php/analyzerStatus"%A"analyzer":"driver"%A"driver":"lsparrot"%A"label":"LSParrot Engine"%AContent-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%A"label":"alphaFallback"%A"detail":"function alphaFallback(): void"%AContent-Length: %d

{"jsonrpc":"2.0","id":3,"result":null}
