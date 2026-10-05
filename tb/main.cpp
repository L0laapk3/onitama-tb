import tb;
import std;

auto main() -> int {
	const CardsInfo cards{BOAR, OX, ELEPHANT, HORSE, CRAB};
	TableBase<8> tb(cards);
	return 0;
}
