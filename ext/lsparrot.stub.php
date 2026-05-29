<?php

/**
 * @generate-class-entries
 * @generate-legacy-arginfo 80200
 * @undocumentable
 */

namespace LSParrot {
    function start_lsp(array $options = []): void {}

    function lsparrot_parse(string $code, ?string $uri = null): array {}

    function lsparrot_tokens(string $code, ?string $uri = null): array {}

    function lsparrot_version(): array {}
}
