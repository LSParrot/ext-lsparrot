--TEST--
LSP excludes .lsparrot helper files from project symbols
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 107

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-lsparrot-exclude-test"}}Content-Length: 79

{"jsonrpc":"2.0","id":2,"method":"workspace/symbol","params":{"query":"Thing"}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-lsparrot-exclude-test';
@mkdir($root . '/src', 0777, true);
@mkdir($root . '/.lsparrot/shadow/phpstan', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);
file_put_contents($root . '/composer.json', "{\"name\":\"zeriyoshi/lsparrot-exclude-test\"}\n");
file_put_contents($root . '/src/RealThing.php', "<?php\nnamespace LSParrotExcludeFixture;\nfinal class RealThing {}\n");
file_put_contents($root . '/.lsparrot/shadow/phpstan/ShadowThing.php', "<?php\nnamespace LSParrotExcludeFixture;\nfinal class ShadowThing {}\n");
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'LSParrotExcludeFixture\\\\RealThing' => " . var_export($root . '/src/RealThing.php', true) . ",\n    'LSParrotExcludeFixture\\\\ShadowThing' => " . var_export($root . '/.lsparrot/shadow/phpstan/ShadowThing.php', true) . ",\n];\n");
LSParrot\start_lsp([
    'analyzer' => 'lsparrot',
    'workers' => ['count' => 0],
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-lsparrot-exclude-test';
if (is_dir($root)) {
    $it = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($root, FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::CHILD_FIRST);
    foreach ($it as $file) {
        $file->isDir() ? @rmdir($file->getPathname()) : @unlink($file->getPathname());
    }
    @rmdir($root);
}
?>
--EXPECTREGEX--
(?s)\A(?!.*ShadowThing)(?!.*file:\/\/\/tmp\/lsp-lsparrot-exclude-test\/\.lsparrot).*"jsonrpc":"2\.0","id":1,"result".*"jsonrpc":"2\.0","id":2,"result":\[.*"name":"LSParrotExcludeFixture\\\\RealThing".*"uri":"file:\/\/\/tmp\/lsp-lsparrot-exclude-test\/src\/RealThing\.php".*\].*"jsonrpc":"2\.0","id":3,"result":null.*\z
