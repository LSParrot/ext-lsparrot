--TEST--
LSP hover joins all source labels when enabled analyzers report the same type
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 106

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-hover-same-dual-test"}}Content-Length: 234

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-hover-same-dual-test/file.php","languageId":"php","version":1,"text":"<?php\n/** @var Collection $same */\n$same = make();\n$same;\n"}}}Content-Length: 172

{"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-hover-same-dual-test/file.php"},"position":{"line":3,"character":2}}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-hover-same-dual-test';
@mkdir($root . '/vendor/bin', 0777, true);
file_put_contents($root . '/psalm.xml', '<?xml version="1.0"?><psalm></psalm>');
file_put_contents($root . '/vendor/bin/phpstan', "#!/usr/bin/env php\n<?php echo json_encode(['files' => []]);\n");
file_put_contents($root . '/vendor/bin/psalm', "#!/usr/bin/env php\n<?php echo json_encode([]);\n");
chmod($root . '/vendor/bin/phpstan', 0755);
chmod($root . '/vendor/bin/psalm', 0755);
LSParrot\start_lsp([
    'analyzer' => ['phpstan', 'psalm'],
    'workers' => ['analyzerDiagnosticsTimeout' => 5],
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
lsp_rrmdir('/tmp/lsp-hover-same-dual-test');
?>
--EXPECTREGEX--
[\s\S]*"method":"lsparrot.php\/analyzerStatus"[\s\S]*"driver":"lsparrot\+phpstan\+psalm"[\s\S]*"jsonrpc":"2\.0","id":2,"result"[\s\S]*`Collection`\\n\\nLSParrot Engine \/ PHPStan \/ Psalm[\s\S]*\{"jsonrpc":"2\.0","id":3,"result":null\}
