--TEST--
LSP hover shows distinct PHPStan and Psalm type sources when both analyzers are active
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 101

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-hover-dual-test"}}Content-Length: 590

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-hover-dual-test/src/Service/UserRepository.php","languageId":"php","version":1,"text":"<?php\n\nnamespace HoverDualFixture\\Service;\n\nuse HoverDualFixture\\Domain\\Collection;\n\nfinal class UserRepository\n{\n    /**\n     * @return Collection<User>\n     */\n    public function findActiveUsers(): Collection\n    {\n        return new Collection();\n    }\n\n    public function acceptPayload(): void\n    {\n        $val = $this->findActiveUsers();\n        $val->all();\n    }\n}\n"}}}Content-Length: 191

{"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-hover-dual-test/src/Service/UserRepository.php"},"position":{"line":18,"character":10}}}Content-Length: 56

{"jsonrpc":"2.0","id":3,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-hover-dual-test';
@mkdir($root . '/src/Domain', 0777, true);
@mkdir($root . '/src/Service', 0777, true);
@mkdir($root . '/vendor/bin', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);
file_put_contents($root . '/src/Domain/Collection.php', "<?php\nnamespace HoverDualFixture\\Domain;\nfinal class Collection\n{\n    public function all(): array\n    {\n        return [];\n    }\n}\n");
file_put_contents($root . '/src/Domain/User.php', "<?php\nnamespace HoverDualFixture\\Domain;\nfinal class User\n{\n}\n");
file_put_contents($root . '/src/Service/UserRepository.php', "<?php\nnamespace HoverDualFixture\\Service;\nuse HoverDualFixture\\Domain\\Collection;\nfinal class UserRepository\n{\n    /**\n     * @return Collection<User>\n     */\n    public function findActiveUsers(): Collection\n    {\n        return new Collection();\n    }\n}\n");
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'HoverDualFixture\\\\Domain\\\\Collection' => " . var_export($root . '/src/Domain/Collection.php', true) . ",\n    'HoverDualFixture\\\\Domain\\\\User' => " . var_export($root . '/src/Domain/User.php', true) . ",\n    'HoverDualFixture\\\\Service\\\\UserRepository' => " . var_export($root . '/src/Service/UserRepository.php', true) . ",\n];\n");
file_put_contents($root . '/psalm.xml', '<?xml version="1.0"?><psalm><projectFiles><directory name="src" /></projectFiles></psalm>');
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
lsp_rrmdir('/tmp/lsp-hover-dual-test');
?>
--EXPECTREGEX--
[\s\S]*"method":"lsparrot.php\/analyzerStatus"[\s\S]*"driver":"lsparrot\+phpstan\+psalm"[\s\S]*"label":"LSParrot Engine \+ PHPStan \+ Psalm"[\s\S]*"jsonrpc":"2\.0","id":1,"result"[\s\S]*"jsonrpc":"2\.0","id":2,"result"[\s\S]*Collection<User>[\s\S]*LSParrot Engine \/ PHPStan[\s\S]*Collection[\s\S]*Psalm[\s\S]*\{"jsonrpc":"2\.0","id":3,"result":null\}
