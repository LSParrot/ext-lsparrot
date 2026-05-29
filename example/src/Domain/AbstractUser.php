<?php

declare(strict_types=1);

namespace Zeriyoshi\AnalyzeTest\Domain;

abstract class AbstractUser
{
    public function forOverride(): void
    {
        echo 'HELLO WORLD', \PHP_EOL;
    }

    public function argTests(int $bongo, ?float $conga = null, string... $jambe): void
    {
        var_dump($bongo, $conga, $jambe);
    }

    public static function staticFunction(): void
    {
        echo static::class, \PHP_EOL;
    }
}
