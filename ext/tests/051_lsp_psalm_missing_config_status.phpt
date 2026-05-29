--TEST--
LSP runs Psalm with a generated config when psalm.xml is missing
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 111

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-psalm-config-missing-test"}}Content-Length: 67

{"jsonrpc":"2.0","id":2,"method":"lsparrot.php/status","params":[]}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-psalm-config-missing-test';
@mkdir($root . '/vendor/bin', 0777, true);
@mkdir($root . '/src', 0777, true);
file_put_contents($root . '/composer.json', "{\"autoload\":{\"psr-4\":{\"PsalmMissingConfigFixture\\\\\\\\\":\"src/\"}}}\n");
file_put_contents($root . '/src/Demo.php', "<?php\nnamespace PsalmMissingConfigFixture;\nfinal class Demo {}\n");
file_put_contents($root . '/vendor/bin/psalm', "#!/usr/bin/env php\n<?php echo '[]';\n");
chmod($root . '/vendor/bin/psalm', 0755);
LSParrot\start_lsp([
    'analyzer' => 'psalm',
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-psalm-config-missing-test';
@unlink($root . '/vendor/bin/psalm');
@unlink($root . '/src/Demo.php');
@unlink($root . '/composer.json');
if (is_dir($root . '/.lsparrot')) {
    $it = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($root . '/.lsparrot', FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::CHILD_FIRST);
    foreach ($it as $file) {
        $file->isDir() ? @rmdir($file->getPathname()) : @unlink($file->getPathname());
    }
    @rmdir($root . '/.lsparrot');
}
@rmdir($root . '/vendor/bin');
@rmdir($root . '/vendor');
@rmdir($root . '/src');
@rmdir($root);
?>
--EXPECTF--
Content-Length: %d

%A"analyzer":"driver"%A"driver":"lsparrot+psalm"%AContent-Length: %d

%A"analyzer":"index"%AContent-Length: %d

%A"analyzer":"psalm","state":"running","message":"Prewarming Psalm project diagnostics.","projectRoot":"/tmp/lsp-psalm-config-missing-test"%A"jsonrpc":"2.0","id":2,"result"%A"psalm"%A"enabled":true%A"projects":{"/tmp/lsp-psalm-config-missing-test":"running"}%A"jsonrpc":"2.0","id":3,"result":null}%A
