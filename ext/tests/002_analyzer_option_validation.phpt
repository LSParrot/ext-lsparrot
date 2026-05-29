--TEST--
LSP analyzer option validation
--EXTENSIONS--
lsparrot
--FILE--
<?php
foreach ([
    ['analyser' => 'auto'],
    ['analyzer' => 'analyser'],
    ['analyzer' => ['phpstan', 'lsparrot']],
] as $options) {
    try {
        LSParrot\start_lsp($options);
    } catch (InvalidArgumentException $e) {
        echo $e->getMessage(), "\n";
    }
}
?>
--EXPECT--
Unsupported option "analyser"; use "analyzer" instead.
Option "analyzer" must be "auto", "lsparrot", "phpstan", "psalm", "psalm-ls", or a list containing "phpstan", "psalm" and/or "psalm-ls".
Option "analyzer" must be "auto", "lsparrot", "phpstan", "psalm", "psalm-ls", or a list containing "phpstan", "psalm" and/or "psalm-ls".
