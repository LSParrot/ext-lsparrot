--TEST--
LSP completes the special ::class name resolution pseudo constant
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 114

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-static-class-resolution-test"}}Content-Length: 428

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-static-class-resolution-test/file.php","languageId":"php","version":1,"text":"<?php\nclass BaseClass {}\nclass Example extends BaseClass\n{\n    public static string $name;\n    public function run(): void\n    {\n        self::cla\n        static::cla\n        parent::cla\n        BaseClass::cla\n        self::$\n    }\n}\n"}}}Content-Length: 186

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-static-class-resolution-test/file.php"},"position":{"line":7,"character":17}}}Content-Length: 186

{"jsonrpc":"2.0","id":3,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-static-class-resolution-test/file.php"},"position":{"line":8,"character":19}}}Content-Length: 186

{"jsonrpc":"2.0","id":4,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-static-class-resolution-test/file.php"},"position":{"line":9,"character":19}}}Content-Length: 187

{"jsonrpc":"2.0","id":5,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-static-class-resolution-test/file.php"},"position":{"line":10,"character":22}}}Content-Length: 187

{"jsonrpc":"2.0","id":6,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-static-class-resolution-test/file.php"},"position":{"line":11,"character":15}}}Content-Length: 56

{"jsonrpc":"2.0","id":7,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-static-class-resolution-test';
@mkdir($root, 0777, true);
LSParrot\start_lsp(['symbolIndex' => ['size' => '4M']]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-static-class-resolution-test';
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
[\s\S]*"jsonrpc":"2\.0","id":2,"result"[\s\S]*"label":"class"[\s\S]*"detail":"class name resolution"[\s\S]*"jsonrpc":"2\.0","id":3,"result"[\s\S]*"label":"class"[\s\S]*"detail":"class name resolution"[\s\S]*"jsonrpc":"2\.0","id":4,"result"[\s\S]*"label":"class"[\s\S]*"detail":"class name resolution"[\s\S]*"jsonrpc":"2\.0","id":5,"result"[\s\S]*"label":"class"[\s\S]*"detail":"class name resolution"[\s\S]*"jsonrpc":"2\.0","id":6,"result"(?:(?!"label":"class").)*\{"jsonrpc":"2\.0","id":7,"result":null\}
