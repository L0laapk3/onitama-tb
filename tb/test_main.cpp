import tb;
import std;

auto main() -> int {
	const CardsInfo cards{BOAR, OX, ELEPHANT, HORSE, CRAB};
	return testStepOne<6>(cards) ? 0 : 1;
}
