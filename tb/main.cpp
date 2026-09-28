import onitama_tb;
import std;

auto main() -> int {
	const CardsInfo cards{BOAR, OX, ELEPHANT, HORSE, CRAB};
	auto tb = std::make_unique<TableBase<6>>(cards);
	return 0;
}
