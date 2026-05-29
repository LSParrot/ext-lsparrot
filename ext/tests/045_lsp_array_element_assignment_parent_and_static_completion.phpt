--TEST--
LSP infers generic array element assignments, inherited members, and static methods
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 103

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-array-static-test"}}Content-Length: 733

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-array-static-test/src/Service/UserRepository.php","languageId":"php","version":1,"text":"<?php\n\nnamespace ArrayStaticFixture\\Service;\n\nuse ArrayStaticFixture\\Domain\\Collection;\nuse ArrayStaticFixture\\Domain\\User;\nuse ArrayStaticFixture\\Fixture\\ClassMappedFixture;\n\nfinal class UserRepository\n{\n    /**\n     * @return Collection<User>\n     */\n    public function findActiveUsers(): Collection\n    {\n        return new Collection();\n    }\n\n    public function acceptPayload(): void\n    {\n        $user = $this->findActiveUsers()->all()[0];\n        $user->arg;\n        ClassMappedFixture::sta;\n    }\n}\n"}}}Content-Length: 193

{"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/lsp-array-static-test/src/Service/UserRepository.php"},"position":{"line":20,"character":10}}}Content-Length: 198

{"jsonrpc":"2.0","id":3,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-array-static-test/src/Service/UserRepository.php"},"position":{"line":21,"character":18}}}Content-Length: 198

{"jsonrpc":"2.0","id":4,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///tmp/lsp-array-static-test/src/Service/UserRepository.php"},"position":{"line":22,"character":31}}}Content-Length: 56

{"jsonrpc":"2.0","id":5,"method":"shutdown","params":[]}
--FILE--
<?php
$root = '/tmp/lsp-array-static-test';
@mkdir($root . '/src/Domain', 0777, true);
@mkdir($root . '/src/Fixture', 0777, true);
@mkdir($root . '/src/Service', 0777, true);
@mkdir($root . '/vendor/bin', 0777, true);
@mkdir($root . '/vendor/composer', 0777, true);
file_put_contents($root . '/src/Domain/Collection.php', <<<'PHP'
<?php
namespace ArrayStaticFixture\Domain;

/**
 * @template TItem of object
 */
final class Collection
{
    /**
     * @return non-empty-list<TItem>
     */
    public function all(): array
    {
        return [];
    }
}
PHP);
file_put_contents($root . '/src/Domain/AbstractUser.php', <<<'PHP'
<?php
namespace ArrayStaticFixture\Domain;

abstract class AbstractUser
{
    public function argTests(int $bongo, ?float $conga = null, string ...$jambe): void
    {
    }
}
PHP);
file_put_contents($root . '/src/Domain/User.php', <<<'PHP'
<?php
namespace ArrayStaticFixture\Domain;

final class User extends AbstractUser
{
    public function foo(): void
    {
    }
}
PHP);
file_put_contents($root . '/src/Fixture/ClassMappedFixture.php', <<<'PHP'
<?php
namespace ArrayStaticFixture\Fixture;

final class ClassMappedFixture
{
    public static function staticFunction(): void
    {
    }

    public function instanceFunction(): void
    {
    }
}
PHP);
file_put_contents($root . '/src/Service/UserRepository.php', <<<'PHP'
<?php
namespace ArrayStaticFixture\Service;

use ArrayStaticFixture\Domain\Collection;
use ArrayStaticFixture\Domain\User;
use ArrayStaticFixture\Fixture\ClassMappedFixture;

final class UserRepository
{
    /**
     * @return Collection<User>
     */
    public function findActiveUsers(): Collection
    {
        return new Collection();
    }
}
PHP);
file_put_contents($root . '/vendor/composer/autoload_classmap.php', "<?php\nreturn [\n    'ArrayStaticFixture\\\\Domain\\\\Collection' => " . var_export($root . '/src/Domain/Collection.php', true) . ",\n    'ArrayStaticFixture\\\\Domain\\\\AbstractUser' => " . var_export($root . '/src/Domain/AbstractUser.php', true) . ",\n    'ArrayStaticFixture\\\\Domain\\\\User' => " . var_export($root . '/src/Domain/User.php', true) . ",\n    'ArrayStaticFixture\\\\Fixture\\\\ClassMappedFixture' => " . var_export($root . '/src/Fixture/ClassMappedFixture.php', true) . ",\n    'ArrayStaticFixture\\\\Service\\\\UserRepository' => " . var_export($root . '/src/Service/UserRepository.php', true) . ",\n];\n");
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
$root = '/tmp/lsp-array-static-test';
@unlink($root . '/vendor/bin/phpstan');
@unlink($root . '/vendor/composer/autoload_classmap.php');
@unlink($root . '/src/Service/UserRepository.php');
@unlink($root . '/src/Fixture/ClassMappedFixture.php');
@unlink($root . '/src/Domain/User.php');
@unlink($root . '/src/Domain/AbstractUser.php');
@unlink($root . '/src/Domain/Collection.php');
if (is_dir($root . '/.lsparrot')) {
    $it = new RecursiveIteratorIterator(new RecursiveDirectoryIterator($root . '/.lsparrot', FilesystemIterator::SKIP_DOTS), RecursiveIteratorIterator::CHILD_FIRST);
    foreach ($it as $file) {
        $file->isDir() ? @rmdir($file->getPathname()) : @unlink($file->getPathname());
    }
    @rmdir($root . '/.lsparrot');
}
@rmdir($root . '/vendor/composer');
@rmdir($root . '/vendor/bin');
@rmdir($root . '/vendor');
@rmdir($root . '/src/Service');
@rmdir($root . '/src/Fixture');
@rmdir($root . '/src/Domain');
@rmdir($root . '/src');
@rmdir($root);
?>
--EXPECTREGEX--
[\s\S]*"method":"lsparrot.php\/analyzerStatus"[\s\S]*"driver":"lsparrot\+phpstan"[\s\S]*"label":"LSParrot Engine \+ PHPStan"[\s\S]*"jsonrpc":"2\.0","id":1,"result"[\s\S]*"textDocument\/publishDiagnostics"[\s\S]*"jsonrpc":"2\.0","id":2,"result"[\s\S]*ArrayStaticFixture\\\\Domain\\\\User[\s\S]*PHPStan[\s\S]*"jsonrpc":"2\.0","id":3,"result"[\s\S]*"isIncomplete":true[\s\S]*"label":"argTests"[\s\S]*"detail":"argTests\(int \$bongo, \?float \$conga = null, string \.\.\.\$jambe\): void"[\s\S]*"jsonrpc":"2\.0","id":4,"result":\{"isIncomplete":true,"items":\[\{"label":"staticFunction","kind":2,"detail":"staticFunction\(\): void","filterText":"staticFunction","insertText":"staticFunction\(\)\$0","insertTextFormat":2,"data":\{"source":"phpstan"\}\}\]\}[\s\S]*\{"jsonrpc":"2\.0","id":5,"result":null\}
