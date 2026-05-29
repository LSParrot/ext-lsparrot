--TEST--
LSP lsparrot tokens, version info, and CRLF line map
--EXTENSIONS--
lsparrot
--FILE--
<?php
$version = LSParrot\lsparrot_version();
var_dump($version['cli']);
var_dump(is_int($version['phpVersionId']));
var_dump(array_key_exists('processControl', $version));

$tokens = LSParrot\lsparrot_tokens("<?php\r\nfunction x() {}\r\n");
var_dump($tokens[0]['name']);
var_dump($tokens[1]['name']);

$parsed = LSParrot\lsparrot_parse("<?php\r\nfunction x() {}\r\n");
var_dump(count($parsed['lineMap']));
var_dump($parsed['lineMap'][1]);
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
string(10) "T_OPEN_TAG"
string(10) "T_FUNCTION"
int(3)
int(7)
