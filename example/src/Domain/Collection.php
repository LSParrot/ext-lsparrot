<?php

declare(strict_types=1);

namespace Zeriyoshi\AnalyzeTest\Domain;

/**
 * @template TItem of object
 */
final class Collection
{
    /** @var non-empty-list<TItem> */
    private array $items;

    /**
     * @param non-empty-list<TItem> $items
     */
    public function __construct(array $items)
    {
        $this->items = $items;
    }

    /**
     * @return non-empty-list<TItem>
     */
    public function all(): array
    {
        return $this->items;
    }

    public function foo(): bool
    {
        return true;
    }
}
