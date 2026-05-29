<?php

declare(strict_types=1);

namespace Zeriyoshi\AnalyzeTest\Domain;

final class User extends AbstractUser implements FooInterface
{
    /**
     * @param positive-int $id
     * @param non-empty-string $name
     */
    public function __construct(
        public readonly int $id,
        public readonly string $name,
    ) {
    }

    public function foo(): void
    {
        parent::staticFunction();
    }

    public function forOverride(): void
    {
    }
}
