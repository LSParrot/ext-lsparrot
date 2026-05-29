--TEST--
LSP hover shows lsparrot constant types
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 105

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-hover-constant-test"}}Content-Length: 228

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-hover-constant-test/file.php","languageId":"php","version":1,"text":"<?php\nconst FOOOOO = 123;\nPHP_EOL;\nPHP_ZTS;\nFOOOOO;\n"}}}Content-Length: 171

{"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-hover-constant-test/file.php"},"position":{"line":2,"character":2}}}Content-Length: 171

{"jsonrpc":"2.0","id":3,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-hover-constant-test/file.php"},"position":{"line":3,"character":2}}}Content-Length: 171

{"jsonrpc":"2.0","id":4,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-hover-constant-test/file.php"},"position":{"line":4,"character":2}}}Content-Length: 56

{"jsonrpc":"2.0","id":5,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-hover-constant-test';
@mkdir($root, 0777, true);
LSParrot\start_lsp(['symbolIndex' => ['size' => '4M']]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-hover-constant-test';
if (is_dir($root . '/.lsparrot')) {
    $it = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($root . '/.lsparrot', FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::CHILD_FIRST);
    foreach ($it as $file) {
        $file->isDir() ? @rmdir($file->getPathname()) : @unlink($file->getPathname());
    }
    @rmdir($root . '/.lsparrot');
}
@rmdir($root);
?>
--EXPECTREGEX--
[\s\S]*"jsonrpc":"2\.0","id":2,"result"[\s\S]*`string`\\n\\nLSParrot Engine constant PHP_EOL[\s\S]*"jsonrpc":"2\.0","id":3,"result"[\s\S]*`(?:bool|int)`\\n\\nLSParrot Engine constant PHP_ZTS[\s\S]*"jsonrpc":"2\.0","id":4,"result"[\s\S]*`int`\\n\\nLSParrot Engine constant FOOOOO[\s\S]*\{"jsonrpc":"2\.0","id":5,"result":null\}
