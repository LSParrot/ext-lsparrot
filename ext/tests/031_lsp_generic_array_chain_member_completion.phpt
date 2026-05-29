--TEST--
LSP resolves PHPDoc generic array element members from chained same-class calls
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 104

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-generic-chain-test"}}Content-Length: 630

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-generic-chain-test/src/Service/UserRepository.php","languageId":"php","version":1,"text":"<?php\n\nnamespace ReturnGenericFixture\\Service;\n\nuse ReturnGenericFixture\\Domain\\Collection;\nuse ReturnGenericFixture\\Domain\\User;\n\nfinal class UserRepository\n{\n    /**\n     * @return Collection<User>\n     */\n    public function findActiveUsers(): Collection\n    {\n        return new Collection();\n    }\n\n    public function acceptPayload(): void\n    {\n        $this->findActiveUsers()->all()[0]->foo();\n    }\n}\n"}}}Content-Length: 199

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-generic-chain-test/src/Service/UserRepository.php"},"position":{"line":19,"character":44}}}Content-Length: 199

{"jsonrpc":"2.0","id":3,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///tmp/lsp-generic-chain-test/src/Service/UserRepository.php"},"position":{"line":19,"character":45}}}Content-Length: 56

{"jsonrpc":"2.0","id":4,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-generic-chain-test';
@mkdir($root . '/src/Domain', 0777, true);
@mkdir($root . '/src/Service', 0777, true);
@mkdir($root . '/vendor/bin', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);
file_put_contents($root . '/src/Domain/Collection.php', "<?php\nnamespace ReturnGenericFixture\\Domain;\n/**\n * @template TItem of object\n */\nfinal class Collection\n{\n    /**\n     * @return non-empty-list<TItem>\n     */\n    public function all(): array\n    {\n        return [];\n    }\n}\n");
file_put_contents($root . '/src/Domain/User.php', "<?php\nnamespace ReturnGenericFixture\\Domain;\nfinal class User\n{\n    public function foo(): void\n    {\n    }\n}\n");
file_put_contents($root . '/src/Service/UserRepository.php', "<?php\nnamespace ReturnGenericFixture\\Service;\nuse ReturnGenericFixture\\Domain\\Collection;\nuse ReturnGenericFixture\\Domain\\User;\nfinal class UserRepository\n{\n    /**\n     * @return Collection<User>\n     */\n    public function findActiveUsers(): Collection\n    {\n        return new Collection();\n    }\n}\n");
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'ReturnGenericFixture\\\\Domain\\\\Collection' => " . var_export($root . '/src/Domain/Collection.php', true) . ",\n    'ReturnGenericFixture\\\\Domain\\\\User' => " . var_export($root . '/src/Domain/User.php', true) . ",\n    'ReturnGenericFixture\\\\Service\\\\UserRepository' => " . var_export($root . '/src/Service/UserRepository.php', true) . ",\n];\n");
file_put_contents($root . '/vendor/bin/phpstan', "#!/usr/bin/env php\n<?php echo json_encode(['files' => []]);\n");
chmod($root . '/vendor/bin/phpstan', 0755);
LSParrot\start_lsp([
    'analyzer' => 'phpstan',
    'workers' => ['analyzerDiagnosticsTimeout' => 5],
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-generic-chain-test';
@unlink($root . '/vendor/bin/phpstan');
@unlink($root . '/vendor/composer/autoload_classmap.php');
@unlink($root . '/src/Service/UserRepository.php');
@unlink($root . '/src/Domain/User.php');
@unlink($root . '/src/Domain/Collection.php');
foreach (glob($root . '/.cache/lsp/shadow/phpstan/*') ?: [] as $file) {
    @unlink($file);
}
if (is_dir($root . '/.lsparrot')) {
    $it = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($root . '/.lsparrot', FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::CHILD_FIRST);
    foreach ($it as $file) {
        $file->isDir() ? @rmdir($file->getPathname()) : @unlink($file->getPathname());
    }
    @rmdir($root . '/.lsparrot');
}
@rmdir($root . '/.cache/lsp/shadow/phpstan');
@rmdir($root . '/.cache/lsp/shadow');
@rmdir($root . '/.cache/lsp');
@rmdir($root . '/.cache');
@rmdir($root . '/vendor/composer');
@rmdir($root . '/vendor/bin');
@rmdir($root . '/vendor');
@rmdir($root . '/src/Service');
@rmdir($root . '/src/Domain');
@rmdir($root . '/src');
@rmdir($root);
?>
--EXPECTF--
Content-Length: %d

%A"method":"lsparrot.php/analyzerStatus"%A"driver":"lsparrot+phpstan"%A"label":"LSParrot Engine + PHPStan"%AContent-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%A"label":"foo"%A"detail":"foo(): void"%A"source":"phpstan"%AContent-Length: %d

%A"jsonrpc":"2.0","id":3,"result"%A"uri":"file:///tmp/lsp-generic-chain-test/src/Domain/User.php"%A"line":4%AContent-Length: %d

{"jsonrpc":"2.0","id":4,"result":null}
