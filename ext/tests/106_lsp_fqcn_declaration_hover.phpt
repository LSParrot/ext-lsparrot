--TEST--
LSP hover shows FQCN with the full declaration header and definition selects the class name
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 101

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-fqcn-hover-test"}}Content-Length: 329

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-fqcn-hover-test/src/Svc.php","languageId":"php","version":1,"text":"<?php\nnamespace App;\nuse App\\Repo\\UserRepository;\nclass Svc {\n    public function go(UserRepository $r): int {\n        return max_retries();\n    }\n}\n"}}}Content-Length: 171

{"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-fqcn-hover-test/src/Svc.php"},"position":{"line":4,"character":30}}}Content-Length: 171

{"jsonrpc":"2.0","id":3,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-fqcn-hover-test/src/Svc.php"},"position":{"line":5,"character":18}}}Content-Length: 176

{"jsonrpc":"2.0","id":4,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///tmp/lsp-fqcn-hover-test/src/Svc.php"},"position":{"line":4,"character":30}}}Content-Length: 56

{"jsonrpc":"2.0","id":5,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-fqcn-hover-test';
@mkdir($root . '/src', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);
file_put_contents($root . '/src/UserRepository.php', "<?php\nnamespace App\\Repo;\n\nabstract class UserRepository implements \\Countable\n{\n    public function count(): int { return 1; }\n}\n");
file_put_contents($root . '/src/helpers.php', "<?php\nnamespace App;\nfunction max_retries(): int {\n    return 3;\n}\n");
file_put_contents($root . '/vendor/composer/autoload_psr4.php', "<?php\nreturn ['App\\\\' => [" . var_export($root . '/src', true) . "]];\n");
LSParrot\start_lsp([
    'analyzer' => 'lsparrot',
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-fqcn-hover-test';
@unlink($root . '/src/UserRepository.php');
@unlink($root . '/src/helpers.php');
@unlink($root . '/vendor/composer/autoload_psr4.php');
@unlink($root . '/.lsparrot/lsparrot-index.bin');
@rmdir($root . '/.lsparrot');
@rmdir($root . '/vendor/composer');
@rmdir($root . '/vendor');
@rmdir($root . '/src');
@rmdir($root);
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":2,"result":{"contents":{"kind":"markdown","value":"`App\\Repo\\UserRepository: abstract class UserRepository implements \\Countable`%A"jsonrpc":"2.0","id":3,"result":{"contents":{"kind":"markdown","value":"`App\\max_retries: function max_retries(): int`%A"jsonrpc":"2.0","id":4,"result":{"uri":"file:///tmp/lsp-fqcn-hover-test/src/UserRepository.php","range":{"start":{"line":3,"character":15},"end":{"line":3,"character":29}}}%AContent-Length: %d

{"jsonrpc":"2.0","id":5,"result":null}
