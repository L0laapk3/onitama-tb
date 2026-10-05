import tb;
import std;

auto main(int argc, char** argv) -> int {
	const CardsInfo cards{BOAR, OX, ELEPHANT, HORSE, CRAB};

	U32 men = 6;
	if (argc > 1)
		men = std::stoi(argv[1]);

	switch (men) {
		case 2: { TableBase<2> tb(cards); break; }
		case 4: { TableBase<4> tb(cards); break; }
		case 6: { TableBase<6> tb(cards); break; }
		case 8: { TableBase<8> tb(cards); break; }
		// case 10: { TableBase<10> tb(cards); break; }
		default:
			std::cerr << "Invalid number of men: " << men << std::endl;
			return 1;
	}
	return 0;
}
