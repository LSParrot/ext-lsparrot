--TEST--
LSP lsparrot Zend AST parse and parse diagnostics
--EXTENSIONS--
lsparrot
--FILE--
<?php
$valid = LSParrot\lsparrot_parse("<?php\nfunction ok(): int { return 1; }\n", 'file:///tmp/ok.php');
var_dump($valid['ok']);
var_dump($valid['uri']);
var_dump($valid['ast']['kindName']);
var_dump($valid['diagnostics']);

$invalid = LSParrot\lsparrot_parse('<?php function (', 'file:///tmp/unsaved.php');
var_dump($invalid['ok']);
var_dump($invalid['ast']);
var_dump($invalid['diagnostics'][0]['source']);
var_dump($invalid['diagnostics'][0]['message'] !== '');
var_dump($invalid['tokens'][0]['name']);
?>
--EXPECT--
bool(true)
string(18) "file:///tmp/ok.php"
string(9) "STMT_LIST"
array(0) {
}
bool(false)
NULL
string(3) "php"
bool(true)
string(10) "T_OPEN_TAG"
