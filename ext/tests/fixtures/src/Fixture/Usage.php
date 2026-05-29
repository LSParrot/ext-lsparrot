<?php

declare(strict_types=1);

namespace Zeriyoshi\AnalyzeTest\Fixture;

use Zeriyoshi\AnalyzeTest\Domain\Collection;
use Zeriyoshi\AnalyzeTest\Domain\User;

/**
 * @template TUser
 * @param array{user_id: positive-int, 'display-name': non-empty-string} $payload
 */
function consume(array $payload): void
{
    /** @var Collection<User> $users */
    $users = [];
    $payload['display-name'];
}
