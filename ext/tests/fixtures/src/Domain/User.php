<?php

declare(strict_types=1);

namespace Zeriyoshi\AnalyzeTest\Domain;

final class User
{
    public function __construct(
        public readonly int $id,
        public readonly string $name
    ) {
    }
}
