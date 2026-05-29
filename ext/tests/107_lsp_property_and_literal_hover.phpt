--TEST--
LSP hover: no hover inside string literals, typed property declarations, and private $this members
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 102

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-bongo-hover-test"}}Content-Length: 395

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-bongo-hover-test/src/Bongo.php","languageId":"php","version":1,"text":"<?php\nnamespace Fix;\nclass Bongo\n{\n    private string $secret = 'bONGO';\n    protected string $sayText = 'Bongo';\n\n    public function sayBongo(): void\n    {\n        echo $this->sayText, $this->secret;\n    }\n}\n"}}}Content-Length: 174

{"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-bongo-hover-test/src/Bongo.php"},"position":{"line":4,"character":32}}}Content-Length: 174

{"jsonrpc":"2.0","id":3,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-bongo-hover-test/src/Bongo.php"},"position":{"line":4,"character":22}}}Content-Length: 174

{"jsonrpc":"2.0","id":4,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-bongo-hover-test/src/Bongo.php"},"position":{"line":9,"character":38}}}Content-Length: 56

{"jsonrpc":"2.0","id":5,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-bongo-hover-test';
@mkdir($root . '/src', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);
file_put_contents($root . '/vendor/composer/autoload_psr4.php', "<?php\nreturn ['Fix\\\\' => [" . var_export($root . '/src', true) . "]];\n");
file_put_contents($root . '/src/Bongo.php', <<<'PHP'
<?php
namespace Fix;
class Bongo
{
    private string $secret = 'bONGO';
    protected string $sayText = 'Bongo';

    public function sayBongo(): void
    {
        echo $this->sayText, $this->secret;
    }
}

PHP);
LSParrot\start_lsp(['analyzer' => 'lsparrot', 'symbolIndex' => ['size' => '4M']]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-bongo-hover-test';
@unlink($root . '/src/Bongo.php');
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

%A"jsonrpc":"2.0","id":2,"result":null%A"jsonrpc":"2.0","id":3,"result":{"contents":{"kind":"markdown","value":"`property string $secret`%A"jsonrpc":"2.0","id":4,"result":{"contents":{"kind":"markdown","value":"`property string $secret`%AContent-Length: %d

{"jsonrpc":"2.0","id":5,"result":null}
