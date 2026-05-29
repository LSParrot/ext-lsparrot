<?php

declare(strict_types=1);

namespace Zeriyoshi\AnalyzeTest\Fixture;

final class Test
{
    public function __construct(private mixed $obj)
    {
    }

    public function bar(): void
    {
        $this->obj->foo();
    }
}
