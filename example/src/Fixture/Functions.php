<?php

declare(strict_types=1);

namespace Zeriyoshi\AnalyzeTest\Fixture;

/**
 * @param non-empty-list<string> $names
 * @return non-empty-list<string>
 */
function collect_names(array $names): array
{
    return $names;
}

const FOOOOO = 123;

class Bongo
{
    private string $secret = 'bONGO';
    protected string $sayText = 'Bongo';

    public function sayBongo(): void
    {
        echo $this->sayText, \PHP_EOL, $this->secret, \PHP_EOL;
    }
}

class Jambe extends Bongo
{
    public function __construct()
    {
        $this->sayText = 'Jambe';
    }
}

final class Conga
{
    public function getBongo(): Bongo
    {
        return new Bongo();
    }

    public function getBongoLike(): Bongo
    {
        return new Jambe();
    }
}

$foo = new Conga();
