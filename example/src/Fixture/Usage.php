<?php

declare(strict_types=1);

namespace Zeriyoshi\AnalyzeTest\Fixture;

use Zeriyoshi\AnalyzeTest\Domain\Collection;
use Zeriyoshi\AnalyzeTest\Domain\User;
use Zeriyoshi\AnalyzeTest\Service\UserRepository;

/**
 * @template TUser of User
 * @param array{user_id: positive-int, 'display-name': non-empty-string} $payload
 */
function consume(array $payload): void
{
    $repository = new UserRepository();
    /** @var Collection<User> $users */
    $users = $repository->findActiveUsers();
    /** @var list<User> $userList */
    $userList = $users->all();
    /** @var positive-int $count */
    $count = 1;
    $payload['display-name'];
}
