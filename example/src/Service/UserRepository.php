<?php

declare(strict_types=1);

namespace Zeriyoshi\AnalyzeTest\Service;

use Zeriyoshi\AnalyzeTest\Domain\Collection;
use Zeriyoshi\AnalyzeTest\Domain\User;

final class UserRepository
{
    /**
     * @return Collection<User>
     */
    public function findActiveUsers(): Collection
    {
        return new Collection([
            new User(1, 'Go Kudo'),
            new User(2, 'Shinichi Kudo')
        ]);
    }

    /**
     * @template T
     * @param T $value
     * @return T
     */
    public function templateTest(mixed $value): mixed
    {
        return $value;
    }

    /**
     * @psalm-template T
     * @psalm-param T $value
     * @psalm-return T
     */
    public function templateTestPsalm(mixed $value): mixed
    {
        return $value;
    }

    /**
     * @return array{user_id: positive-int, name: non-empty-string, obj: User}
     */
    public function getFirstPayload(): array
    {
        $firstUser = $this->findActiveUsers()->all()[0];

        return [
            'user_id' => $firstUser->id,
            'name' => $firstUser->name,
            'obj' => $firstUser,
        ];
    }

    public function test(): void
    {
        $data = $this->getFirstPayload();
        $user1 = $this->findActiveUsers()->all()[0];
        $val = $this->templateTest($user1);
        $val2 = $this->templateTestPsalm($user1);

        $val3 = $this->getFirstPayload();
        $val3['obj']->forOverride();

        $val4 = $val3['obj'];
    }
}
