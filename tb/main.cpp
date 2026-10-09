import tb;
import std;

template <U16 TB_MEN>
void saveDrawTable(const TableBase<TB_MEN>& tb, std::filesystem::path path) {
	// bazel run executes in the runfiles directory, resolve relative paths against where it was invoked.
	if (const char* cwd = std::getenv("BUILD_WORKING_DIRECTORY"); cwd && path.is_relative())
		path = std::filesystem::path(cwd) / path;

	const auto start = std::chrono::steady_clock::now();
	{
		const DrawTable draws = DrawTable::fromTableBase<TB_MEN>(tb);
		std::ofstream os(path, std::ios::binary);
		draws.toFile(os);
	}
	const std::chrono::duration<double> writeTime = std::chrono::steady_clock::now() - start;

	std::ifstream is(path, std::ios::binary);
	const DrawTable draws = DrawTable::fromFile(is);
	draws.verify<TB_MEN>(tb);
	const std::chrono::duration<double> totalTime = std::chrono::steady_clock::now() - start;
	std::cout << std::format("draw table: {} draws, {:.2f} MB file, {:.2f} MB samples, written in {:.2f}s, verified in {:.2f}s -> {}\n",
		draws.lowBits.size(), std::filesystem::file_size(path) / 1e6, draws.rowSamples.size() * sizeof(U32) / 1e6,
		writeTime.count(), totalTime.count() - writeTime.count(), path.string());
}

auto main(int argc, char** argv) -> int {
	const CardsInfo cards{BOAR, OX, ELEPHANT, HORSE, CRAB};

	U32 men = 8;
	if (argc > 1)
		men = std::stoi(argv[1]);

	const auto run = [&]<U16 TB_MEN> {
		TableBase<TB_MEN> tb(cards);
		if (argc > 2)
			saveDrawTable<TB_MEN>(tb, argv[2]);
	};

	switch (men) {
		case 2: run.template operator()<2>(); break;
		case 4: run.template operator()<4>(); break;
		case 6: run.template operator()<6>(); break;
		case 8: run.template operator()<8>(); break;
		case 10: run.template operator()<10>(); break;
		default:
			std::cerr << "Invalid number of men: " << men << std::endl;
			return 1;
	}
	return 0;
}
