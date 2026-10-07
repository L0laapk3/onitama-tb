"""Search for a layout of the 30 card permutations where swapping players maps
bit i of every 6-bit block to bit (i + 3) % 6.

A permutation is (A hand, B hand, C side card). The 30 permutations are split
into 10 groups of 3 that share the same A hand. The layout is a choice of group
order and, per group, one of the 6 orders of its 3 members. Group 0 stays first
in its original order.

Full enumeration is 9! * 6^9 ~= 3.7e12 layouts, so this backtracks: a 6-bit
block is checked as soon as both of its groups are placed, which skips
exactly the layouts that would fail anyway.
"""

from itertools import combinations, permutations

CARDS = range(5)


def make_groups():
    groups = []
    for a in combinations(CARDS, 2):
        rest = [c for c in CARDS if c not in a]
        members = []
        for b in combinations(rest, 2):
            (c,) = [x for x in rest if x not in b]
            members.append((frozenset(a), frozenset(b), c))
        groups.append(members)
    return groups


def swap(perm):
    a, b, c = perm
    return (b, a, c)


def search(groups):
    solutions = []
    placed = [groups[0]]
    used = {0}

    def block_ok(lo, hi):
        return all(swap(lo[j]) == hi[j] for j in range(3))

    def rec():
        if len(placed) == len(groups):
            solutions.append(list(placed))
            return
        for g in range(1, len(groups)):
            if g in used:
                continue
            for order in permutations(groups[g]):
                if len(placed) % 2 == 1 and not block_ok(placed[-1], order):
                    continue
                used.add(g)
                placed.append(list(order))
                rec()
                placed.pop()
                used.discard(g)

    rec()
    return solutions


def fmt(perm):
    a, b, c = perm
    return f"A{sorted(a)} B{sorted(b)} C{c}"


if __name__ == "__main__":
    groups = make_groups()
    first = groups[0]
    print("Group 0 swaps to:")
    for p in first:
        target = swap(p)
        home = next(i for i, g in enumerate(groups) if target in g)
        print(f"  {fmt(p)} -> {fmt(target)} (group {home})")

    solutions = search(groups)
    print(f"Solutions: {len(solutions)}")
    for sol in solutions[:5]:
        print([[fmt(p) for p in g] for g in sol])
