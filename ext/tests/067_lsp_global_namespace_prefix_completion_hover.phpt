--TEST--
LSP completes and hovers explicitly global names with a leading namespace separator
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 104

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-global-prefix-test"}}Content-Length: 291

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-global-prefix-test/file.php","languageId":"php","version":1,"text":"<?php\nconst FOOOOO = 123;\n\\PHP_EOL;\n\\FOOOOO;\n\\strl\nnew \\DateTimeImm\n\\strlen('');\nnew \\DateTimeImmutable();\n"}}}Content-Length: 175

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-global-prefix-test/file.php"},"position":{"line":2,"character":5}}}Content-Length: 175

{"jsonrpc":"2.0","id":3,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-global-prefix-test/file.php"},"position":{"line":4,"character":5}}}Content-Length: 176

{"jsonrpc":"2.0","id":4,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-global-prefix-test/file.php"},"position":{"line":5,"character":16}}}Content-Length: 170

{"jsonrpc":"2.0","id":5,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-global-prefix-test/file.php"},"position":{"line":2,"character":2}}}Content-Length: 170

{"jsonrpc":"2.0","id":6,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-global-prefix-test/file.php"},"position":{"line":3,"character":2}}}Content-Length: 170

{"jsonrpc":"2.0","id":7,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-global-prefix-test/file.php"},"position":{"line":6,"character":2}}}Content-Length: 171

{"jsonrpc":"2.0","id":8,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-global-prefix-test/file.php"},"position":{"line":7,"character":13}}}Content-Length: 56

{"jsonrpc":"2.0","id":9,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-global-prefix-test';
@mkdir($root, 0777, true);
LSParrot\start_lsp(['symbolIndex' => ['size' => '4M']]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-global-prefix-test';
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
[\s\S]*"jsonrpc":"2\.0","id":2,"result"[\s\S]*"label":"PHP_EOL"[\s\S]*"newText":"\\\\PHP_EOL"[\s\S]*"jsonrpc":"2\.0","id":3,"result"[\s\S]*"label":"strlen"[\s\S]*"insertTextFormat":2[\s\S]*"newText":"\\\\\\\\strlen\(\$0\)"[\s\S]*"jsonrpc":"2\.0","id":4,"result"[\s\S]*"label":"DateTimeImmutable"[\s\S]*"newText":"\\\\DateTimeImmutable"[\s\S]*"jsonrpc":"2\.0","id":5,"result"[\s\S]*`string`\\n\\nLSParrot Engine constant \\\\PHP_EOL[\s\S]*"jsonrpc":"2\.0","id":6,"result"[\s\S]*`int`\\n\\nLSParrot Engine constant \\\\FOOOOO[\s\S]*"jsonrpc":"2\.0","id":7,"result"[\s\S]*`function strlen\(\.\.\.\)`\\n\\nLSParrot Engine[\s\S]*"jsonrpc":"2\.0","id":8,"result"[\s\S]*`class DateTimeImmutable`\\n\\nLSParrot Engine[\s\S]*\{"jsonrpc":"2\.0","id":9,"result":null\}
