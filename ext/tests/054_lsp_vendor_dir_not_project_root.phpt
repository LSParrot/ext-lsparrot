--TEST--
LSP does not treat Composer vendor-dir packages as analyzer projects
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 109

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-vendor-dir-project-test"}}Content-Length: 264

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-vendor-dir-project-test/third_party/acme/pkg/src/PackageFile.php","languageId":"php","version":1,"text":"<?php\nnamespace Acme\\Pkg;\nfinal class PackageFile {}\n"}}}Content-Length: 56

{"jsonrpc":"2.0","id":2,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-vendor-dir-project-test';
@mkdir($root . '/third_party/bin', 0777, true);
@mkdir($root . '/third_party/acme/pkg/src', 0777, true);
@mkdir($root . '/src', 0777, true);
file_put_contents($root . '/composer.json', json_encode([
    'config' => ['vendor-dir' => 'third_party'],
    'autoload' => ['psr-4' => ['VendorDirFixture\\' => 'src/']],
]));
file_put_contents($root . '/psalm.xml', '<?xml version="1.0"?><psalm><projectFiles><directory name="src" /></projectFiles></psalm>');
file_put_contents($root . '/third_party/acme/pkg/composer.json', '{}');
file_put_contents($root . '/third_party/acme/pkg/src/PackageFile.php', "<?php\nnamespace Acme\\Pkg;\nfinal class PackageFile {}\n");
file_put_contents($root . '/third_party/bin/psalm-language-server', <<<'PHP'
#!/usr/bin/env php
<?php
while (!feof(STDIN)) {
    fread(STDIN, 8192);
}
PHP);
chmod($root . '/third_party/bin/psalm-language-server', 0755);

LSParrot\start_lsp([
    'analyzer' => 'psalm-ls',
    'workers' => ['count' => 0],
    'psalm' => [
        'transport' => 'language-server',
        'maxResponseWaitMs' => 0,
    ],
    'symbolIndex' => ['size' => '4M'],
]);
?>
--CLEAN--
<?php
function lsp_rrmdir(string $path): void
{
    if (!is_dir($path)) {
        return;
    }
    foreach (scandir($path) ?: [] as $name) {
        if ($name === '.' || $name === '..') {
            continue;
        }
        $child = $path . '/' . $name;
        if (is_dir($child) && !is_link($child)) {
            lsp_rrmdir($child);
        } else {
            @unlink($child);
        }
    }
    @rmdir($path);
}
lsp_rrmdir('/tmp/lsp-vendor-dir-project-test');
?>
--EXPECTREGEX--
(?s)\A(?!.*Psalm config file is not found)(?!.*projectRoot":"\/tmp\/lsp-vendor-dir-project-test\/third_party\/acme\/pkg).*"analyzer":"psalm-ls","state":"running","message":"Starting Psalm language server.","projectRoot":"\/tmp\/lsp-vendor-dir-project-test".*"jsonrpc":"2\.0","id":2,"result":null.*\z
