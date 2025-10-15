for bittables (1 bit per board, every possible board iterated in the table)

goal: for a set of boards that are local to eachother in memory. ensure that if any single random move applied to all of those boards, they are still as local as possible to eachother in the memory
-> z order curve


x, y dimensions for each piece -> 20 dimensions

combine x and y to one dimension with hand crafted curve -> 10 dimensions. Bitboard can be permanently reordered

for 4 pawns of each player: LUT space filling curve -> 25 choose 4 = 12k. 

```cpp
ip0 = _tzcnt_u32(bbpc);
ip1 = _tzcnt_u32(bbpc &= bbpc-1);
ip2 = _tzcnt_u32(bbpc &= bbpc-1);
ip3 = _tzcnt_u32(bbpc &= bbpc-1);

raw_index = ip0 + ip1*(ip1-1) / 2 + ip2*(ip2-1)*(ip2-2) / 6 + ..

index = LUT[raw_index]
```

to combine the pawns curve with kings curve: 'rectangle'/'weighted' z curve