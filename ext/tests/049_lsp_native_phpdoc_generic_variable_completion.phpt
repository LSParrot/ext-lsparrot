--TEST--
LSP lsparrot server resolves PHPDoc generic variables before analyzer results
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 107

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-generic-variable-test"}}Content-Length: 604

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-generic-variable-test/src/Service/Demo.php","languageId":"php","version":1,"text":"<?php\n\nnamespace GenericVariableFixture\\Service;\n\nuse GenericVariableFixture\\Domain\\Collection;\nuse GenericVariableFixture\\Domain\\User;\n\nfinal class Demo\n{\n    public function run(): void\n    {\n        /** @var Collection<int, User> $users */\n        $users = new Collection();\n        $users->all()[0]->fo\n        $users->first()->fo\n        $first = $users->all()[0];\n        $first->fo\n    }\n}"}}}Content-Length: 192

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-generic-variable-test/src/Service/Demo.php"},"position":{"line":13,"character":28}}}Content-Length: 192

{"jsonrpc":"2.0","id":3,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-generic-variable-test/src/Service/Demo.php"},"position":{"line":14,"character":27}}}Content-Length: 192

{"jsonrpc":"2.0","id":4,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-generic-variable-test/src/Service/Demo.php"},"position":{"line":16,"character":18}}}Content-Length: 56

{"jsonrpc":"2.0","id":5,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-generic-variable-test';
@mkdir($root . '/src/Domain', 0777, true);
@mkdir($root . '/src/Service', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);
file_put_contents($root . '/composer.json', "{\"name\":\"zeriyoshi/generic-variable-test\"}\n");
file_put_contents($root . '/src/Domain/Collection.php', <<<'PHP'
<?php
namespace GenericVariableFixture\Domain;

/**
 * @template TKey of array-key
 * @template-covariant TValue of object
 */
final class Collection
{
    /**
     * @return non-empty-list<TValue>
     */
    public function all(): array
    {
        return [];
    }

    /**
     * @return TValue|null
     */
    public function first(): ?object
    {
        return null;
    }
}
PHP);
file_put_contents($root . '/src/Domain/User.php', <<<'PHP'
<?php
namespace GenericVariableFixture\Domain;

final class User
{
    public function foo(): void
    {
    }
}
PHP);
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'GenericVariableFixture\\\\Domain\\\\Collection' => " . var_export($root . '/src/Domain/Collection.php', true) . ",\n    'GenericVariableFixture\\\\Domain\\\\User' => " . var_export($root . '/src/Domain/User.php', true) . ",\n];\n");
LSParrot\start_lsp([
    'analyzer' => 'lsparrot',
    'workers' => ['count' => 0],
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-generic-variable-test';
if (is_dir($root)) {
    $it = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($root, FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::CHILD_FIRST);
    foreach ($it as $file) {
        $file->isDir() ? @rmdir($file->getPathname()) : @unlink($file->getPathname());
    }
    @rmdir($root);
}
?>
--EXPECTF--
Content-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%A"label":"foo"%A"detail":"foo(): void"%A"source":"lsparrot"%AContent-Length: %d

%A"jsonrpc":"2.0","id":3,"result"%A"label":"foo"%A"detail":"foo(): void"%A"source":"lsparrot"%AContent-Length: %d

%A"jsonrpc":"2.0","id":4,"result"%A"label":"foo"%A"detail":"foo(): void"%A"source":"lsparrot"%AContent-Length: %d

{"jsonrpc":"2.0","id":5,"result":null}
