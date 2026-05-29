--TEST--
LSP hover shows inferred PHPDoc generics and definition resolves imported classes
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 100

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-hover-use-test"}}Content-Length: 587

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-hover-use-test/src/Service/UserRepository.php","languageId":"php","version":1,"text":"<?php\n\nnamespace HoverUseFixture\\Service;\n\nuse HoverUseFixture\\Domain\\Collection;\n\nfinal class UserRepository\n{\n    /**\n     * @return Collection<User>\n     */\n    public function findActiveUsers(): Collection\n    {\n        return new Collection();\n    }\n\n    public function acceptPayload(): void\n    {\n        $val = $this->findActiveUsers();\n        $val->all();\n    }\n}\n"}}}Content-Length: 190

{"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-hover-use-test/src/Service/UserRepository.php"},"position":{"line":18,"character":10}}}Content-Length: 194

{"jsonrpc":"2.0","id":3,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///tmp/lsp-hover-use-test/src/Service/UserRepository.php"},"position":{"line":4,"character":30}}}Content-Length: 56

{"jsonrpc":"2.0","id":4,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-hover-use-test';
@mkdir($root . '/src/Domain', 0777, true);
@mkdir($root . '/src/Service', 0777, true);
@mkdir($root . '/vendor/bin', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);
file_put_contents($root . '/src/Domain/Collection.php', "<?php\nnamespace HoverUseFixture\\Domain;\nfinal class Collection\n{\n    public function all(): array\n    {\n        return [];\n    }\n}\n");
file_put_contents($root . '/src/Domain/User.php', "<?php\nnamespace HoverUseFixture\\Domain;\nfinal class User\n{\n}\n");
file_put_contents($root . '/src/Service/UserRepository.php', "<?php\nnamespace HoverUseFixture\\Service;\nuse HoverUseFixture\\Domain\\Collection;\nfinal class UserRepository\n{\n    /**\n     * @return Collection<User>\n     */\n    public function findActiveUsers(): Collection\n    {\n        return new Collection();\n    }\n}\n");
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'HoverUseFixture\\\\Domain\\\\Collection' => " . var_export($root . '/src/Domain/Collection.php', true) . ",\n    'HoverUseFixture\\\\Domain\\\\User' => " . var_export($root . '/src/Domain/User.php', true) . ",\n    'HoverUseFixture\\\\Service\\\\UserRepository' => " . var_export($root . '/src/Service/UserRepository.php', true) . ",\n];\n");
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
$root = '/tmp/lsp-hover-use-test';
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
--EXPECTREGEX--
[\s\S]*"method":"lsparrot.php\/analyzerStatus"[\s\S]*"driver":"lsparrot\+phpstan"[\s\S]*"label":"LSParrot Engine \+ PHPStan"[\s\S]*"jsonrpc":"2\.0","id":1,"result"[\s\S]*"textDocument\/publishDiagnostics"[\s\S]*"jsonrpc":"2\.0","id":2,"result"[\s\S]*Collection<User>[\s\S]*LSParrot Engine \/ PHPStan[\s\S]*"jsonrpc":"2\.0","id":3,"result"[\s\S]*"uri":"file:\/\/\/tmp\/lsp-hover-use-test\/src\/Domain\/Collection\.php"[\s\S]*"line":2[\s\S]*\{"jsonrpc":"2\.0","id":4,"result":null\}
