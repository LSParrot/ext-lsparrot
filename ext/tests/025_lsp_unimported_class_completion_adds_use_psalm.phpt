--TEST--
LSP adds a use import for unimported project classes while Psalm driver is active
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 103

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-import-psalm-test"}}Content-Length: 313

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-import-psalm-test/src/Service/Demo.php","languageId":"php","version":1,"text":"<?php\n\nnamespace ImportFixture\\Service;\n\nfinal class Demo\n{\n    public function make(): void\n    {\n        Collec\n    }\n}"}}}Content-Length: 187

{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-import-psalm-test/src/Service/Demo.php"},"position":{"line":8,"character":14}}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-import-psalm-test';
@mkdir($root . '/src/Domain', 0777, true);
@mkdir($root . '/src/Service', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);
@mkdir($root . '/vendor/bin', 0777, true);
file_put_contents($root . '/src/Domain/Collection.php', "<?php\nnamespace ImportFixture\\Domain;\nfinal class Collection {}\n");
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'ImportFixture\\\\Domain\\\\Collection' => " . var_export($root . '/src/Domain/Collection.php', true) . ",\n];\n");
file_put_contents($root . '/psalm.xml', '<?xml version="1.0"?><psalm></psalm>');
file_put_contents($root . '/vendor/bin/psalm', "#!/usr/bin/env php\n<?php echo json_encode([]);\n");
chmod($root . '/vendor/bin/psalm', 0755);
LSParrot\start_lsp([
    'analyzer' => 'psalm',
    'workers' => ['count' => 1, 'analyzerDiagnosticsTimeout' => 5],
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
$root = '/tmp/lsp-import-psalm-test';
@unlink($root . '/vendor/bin/psalm');
@unlink($root . '/vendor/composer/autoload_classmap.php');
@unlink($root . '/src/Domain/Collection.php');
@unlink($root . '/psalm.xml');
foreach (glob($root . '/.cache/lsp/shadow/psalm/*') ?: [] as $file) {
    @unlink($file);
}
if (is_dir($root . '/.lsparrot')) {
    $it = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($root . '/.lsparrot', FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::CHILD_FIRST);
    foreach ($it as $file) {
        $file->isDir() ? @rmdir($file->getPathname()) : @unlink($file->getPathname());
    }
    @rmdir($root . '/.lsparrot');
}
@rmdir($root . '/vendor/bin');
@rmdir($root . '/vendor/composer');
@rmdir($root . '/vendor');
@rmdir($root . '/.cache/lsp/shadow/psalm');
@rmdir($root . '/.cache/lsp/shadow');
@rmdir($root . '/.cache/lsp');
@rmdir($root . '/.cache');
@rmdir($root . '/src/Service');
@rmdir($root . '/src/Domain');
@rmdir($root . '/src');
@rmdir($root);
?>
--EXPECTF--
Content-Length: %d

%A"method":"lsparrot.php/analyzerStatus"%A"driver":"lsparrot+psalm"%A"label":"LSParrot Engine + Psalm"%AContent-Length: %d

%A"jsonrpc":"2.0","id":1,"result"%AContent-Length: %d

%A"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result"%A"label":"Collection"%A"detail":"class ImportFixture\\Domain\\Collection"%A"textEdit"%A"newText":"Collection"%A"additionalTextEdits"%A"newText":"\nuse ImportFixture\\Domain\\Collection;\n"%AContent-Length: %d

{"jsonrpc":"2.0","id":3,"result":null}
