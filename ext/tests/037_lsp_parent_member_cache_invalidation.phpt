--TEST--
LSP lsparrot server invalidates inherited member cache when an opened parent class changes
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 116

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-parent-cache-invalidation-test"}}Content-Length: 346

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-parent-cache-invalidation-test/src/Child.php","languageId":"php","version":1,"text":"<?php\nnamespace CacheInvalidationFixture;\n\nfinal class Child extends ParentBase\n{\n    public function test(): void\n    {\n        $this->parent\n    }\n}"}}}Content-Length: 193

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-parent-cache-invalidation-test/src/Child.php"},"position":{"line":7,"character":21}}}Content-Length: 313

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-parent-cache-invalidation-test/src/ParentBase.php","languageId":"php","version":2,"text":"<?php\nnamespace CacheInvalidationFixture;\n\nclass ParentBase\n{\n    public function parentNew(): void\n    {\n    }\n}"}}}Content-Length: 351

{"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///tmp/lsp-parent-cache-invalidation-test/src/Child.php","version":2},"contentChanges":[{"text":"<?php\nnamespace CacheInvalidationFixture;\n\nfinal class Child extends ParentBase\n{\n    public function test(): void\n    {\n        $this->parentN\n    }\n}"}]}}Content-Length: 193

{"jsonrpc":"2.0","id":3,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-parent-cache-invalidation-test/src/Child.php"},"position":{"line":7,"character":22}}}Content-Length: 56

{"jsonrpc":"2.0","id":4,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-parent-cache-invalidation-test';
@mkdir($root . '/vendor/composer', 0777, true);
@mkdir($root . '/src', 0777, true);
file_put_contents($root . '/src/ParentBase.php', <<<'PHP'
<?php
namespace CacheInvalidationFixture;

class ParentBase
{
    public function parentOld(): void
    {
    }
}
PHP);
file_put_contents($root . '/src/Child.php', <<<'PHP'
<?php
namespace CacheInvalidationFixture;

final class Child extends ParentBase
{
}
PHP);
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'CacheInvalidationFixture\\\\ParentBase' => " . var_export($root . '/src/ParentBase.php', true) . ",\n    'CacheInvalidationFixture\\\\Child' => " . var_export($root . '/src/Child.php', true) . ",\n];\n");
LSParrot\start_lsp([
    'analyzer' => 'lsparrot',
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-parent-cache-invalidation-test';
@unlink($root . '/vendor/composer/autoload_classmap.php');
@unlink($root . '/src/ParentBase.php');
@unlink($root . '/src/Child.php');
@rmdir($root . '/vendor/composer');
@rmdir($root . '/vendor');
@rmdir($root . '/src');
@rmdir($root);
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%A"label":"parentOld"%A"detail":"parentOld(): void"%A"source":"lsparrot"%AContent-Length: %d

%A"textDocument/publishDiagnostics"%AContent-Length: %d

%A"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":3,"result"%A"label":"parentNew"%A"detail":"parentNew(): void"%A"source":"lsparrot"%AContent-Length: %d

{"jsonrpc":"2.0","id":4,"result":null}
