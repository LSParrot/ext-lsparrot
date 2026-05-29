--TEST--
LSP supports implementation lookup, import quick fixes, and organize imports
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 110

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-code-action-imports-test"}}Content-Length: 395

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-code-action-imports-test/src/UseSite.php","languageId":"php","version":1,"text":"<?php\nnamespace NavFixture\\App;\n\nuse NavFixture\\Zed;\nuse NavFixture\\Alpha;\nuse NavFixture\\Alpha;\n\nfinal class UseSite\n{\n    public function make(): Impl\n    {\n        return new Impl();\n    }\n}\n"}}}Content-Length: 194

{"jsonrpc":"2.0","id":2,"method":"textDocument/implementation","params":{"textDocument":{"uri":"file:///tmp/lsp-code-action-imports-test/src/Contract.php"},"position":{"line":3,"character":11}}}Content-Length: 277

{"jsonrpc":"2.0","id":3,"method":"textDocument/codeAction","params":{"textDocument":{"uri":"file:///tmp/lsp-code-action-imports-test/src/UseSite.php"},"range":{"start":{"line":9,"character":28},"end":{"line":9,"character":32}},"context":{"diagnostics":[],"only":["quickfix"]}}}Content-Length: 289

{"jsonrpc":"2.0","id":4,"method":"textDocument/codeAction","params":{"textDocument":{"uri":"file:///tmp/lsp-code-action-imports-test/src/UseSite.php"},"range":{"start":{"line":0,"character":0},"end":{"line":0,"character":0}},"context":{"diagnostics":[],"only":["source.organizeImports"]}}}Content-Length: 56

{"jsonrpc":"2.0","id":5,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-code-action-imports-test';
@mkdir($root . '/vendor/composer', 0777, true);
@mkdir($root . '/src', 0777, true);
file_put_contents($root . '/src/Contract.php', <<<'PHP'
<?php
namespace NavFixture;

interface Contract
{
}
PHP);
file_put_contents($root . '/src/Impl.php', <<<'PHP'
<?php
namespace NavFixture;

final class Impl implements Contract
{
}
PHP);
file_put_contents($root . '/src/UseSite.php', <<<'PHP'
<?php
namespace NavFixture\App;

use NavFixture\Zed;
use NavFixture\Alpha;
use NavFixture\Alpha;

final class UseSite
{
    public function make(): Impl
    {
        return new Impl();
    }
}
PHP);
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'NavFixture\\\\Contract' => " . var_export($root . '/src/Contract.php', true) . ",\n    'NavFixture\\\\Impl' => " . var_export($root . '/src/Impl.php', true) . ",\n    'NavFixture\\\\App\\\\UseSite' => " . var_export($root . '/src/UseSite.php', true) . ",\n];\n");
LSParrot\start_lsp([
    'analyzer' => 'lsparrot',
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-code-action-imports-test';
@unlink($root . '/vendor/composer/autoload_classmap.php');
@unlink($root . '/src/Contract.php');
@unlink($root . '/src/Impl.php');
@unlink($root . '/src/UseSite.php');
@rmdir($root . '/vendor/composer');
@rmdir($root . '/vendor');
@rmdir($root . '/src');
@rmdir($root);
?>
--EXPECTF--
%A"jsonrpc":"2.0","id":1,"result"%A"codeActionProvider":{"codeActionKinds":["quickfix","source.organizeImports"]}%AContent-Length: %d

%A"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result":[%A"uri":"file:///tmp/lsp-code-action-imports-test/src/Impl.php"%AContent-Length: %d

%A"jsonrpc":"2.0","id":3,"result":[%A"title":"Import NavFixture\\Impl"%A"use NavFixture\\Impl;\n"%AContent-Length: %d

%A"jsonrpc":"2.0","id":4,"result":[%A"title":"Organize Imports"%A"use NavFixture\\Alpha;\nuse NavFixture\\Zed;\n"%AContent-Length: %d

{"jsonrpc":"2.0","id":5,"result":null}
