#include "TableBase.h"

#include "lz4frame.h"



constexpr LZ4F_preferences_t LZ4Prefs {
	.compressionLevel = 0,
};

void RefRowWrapper::compress() {
	assert(!isCompressed);
	isBusy = true;
	memComp = std::vector<unsigned char>(LZ4F_compressFrameBound(mem.size() * sizeof(U64), &LZ4Prefs));

	size_t compressedSize = LZ4F_compressFrame(
		memComp.data(), memComp.size() * sizeof(char),
		mem.data(), mem.size() * sizeof(U64),
		&LZ4Prefs);
	// std::cout << memComp.size() << ' ' << compressedSize << std::endl;
	if (LZ4F_isError(compressedSize)) {
		std::cerr << "LZ4F_compressFrame failed: " << LZ4F_getErrorName(compressedSize) << std::endl;
		exit(1);
	}
	memComp.resize(compressedSize);

	isCompressed = true;
	mem.~vector<std::atomic<U64>>();
	isBusy = false;
}

void RefRowWrapper::decompress() {
	if (!isCompressed)
		return;
		

	LZ4F_decompressionContext_t ctx;
	auto error = LZ4F_createDecompressionContext(&ctx, LZ4F_VERSION);
	if (error) {
		std::cerr << "LZ4F_createDecompressionContext failed: " << LZ4F_getErrorName(error) << std::endl;
		exit(1);
	}

	isBusy = true;

	size_t outBuf = refs.back() * sizeof(U64);
	size_t inBuf = memComp.size() * sizeof(char);
	
	mem = std::vector<std::atomic<U64>>(outBuf);

	std::atomic<U64>* dstPtr = mem.data();
	unsigned char* srcPtr = memComp.data();

	size_t result = LZ4F_decompress(ctx,
		dstPtr, &outBuf,
		srcPtr, &inBuf,
		nullptr);
	if (LZ4F_isError(result)) {
		std::cerr << "LZ4F_decompress failed: " << LZ4F_getErrorName(result) << std::endl;
		exit(1);
	}

	LZ4F_freeDecompressionContext(ctx);

	isCompressed = false;
	memComp.~vector<unsigned char>();
	isBusy = false;
}

// void freeSpaceForRow() {
// 	// todo: check if required
	
// }











constexpr U64 NUM_BYTES_STORED = 5;


// std::vector<unsigned char> TableBase::compress() {
// 	const U64 numDraws = mem.size() * NUM_BOARDS_PER_U64 - cnt + cnt_0;
// 	std::cout << "compressing " << numDraws << " draws" << std::endl;
// 	std::vector<unsigned char> bin(numDraws * NUM_BYTES_STORED);
	
// 	U64 binI = 0;
// 	U64 lastIndex = 0;
// 	for (auto it = mem.begin(); it != mem.end(); ++it)
// 		for (auto bits = getResolvedBits(~*it); bits; bits &= bits - 1) {
// 			U64 index = (it - mem.begin()) * NUM_BOARDS_PER_U64 + _tzcnt_u64(bits);
// 			U64 indexDiff = (index - lastIndex);
// 			for (U64 byteI = NUM_BYTES_STORED; byteI--> 0; ) {
// 				bin[binI + byteI * numDraws] = (unsigned char)indexDiff;
// 				indexDiff >>= 8;
// 			}
// 			binI++;
// 			lastIndex = index;
// 		}

// 	return bin;

// 	LZ4F_preferences_t prefs {
// 		.compressionLevel = 0,
// 	};
// 	std::vector<unsigned char> compressed(LZ4F_compressFrameBound(bin.size(), &prefs));
// 	compressed.resize(LZ4F_compressFrame(
// 		&compressed[0], compressed.size(),
// 		&bin[0], bin.size(),
// 		&prefs));

// 	return compressed;
// }

// std::vector<U64> TableBase::decompressToIndices(const std::vector<unsigned char>& compressed) {
// 	const U64 numDraws = compressed.size() / NUM_BYTES_STORED;
// 	std::cout << numDraws << " draws" << std::endl;

// 	std::vector<U64> indices(numDraws);
// 	U64 lastIndex = 0;
// 	for (U64 binI = 0; binI < numDraws; binI++) {
// 		U64 indexDiff = 0;
// 		for (U64 byteI = 0; byteI < NUM_BYTES_STORED; byteI++) {
// 			indexDiff <<= 8;
// 			indexDiff |= compressed[binI + byteI * numDraws];
// 		}
// 		indices[binI] = lastIndex + indexDiff;
// 		lastIndex = indices[binI];
// 	}

// 	return indices;
// }

// void TableBase::testCompression() {
// 	auto compressed = compress();
// 	auto indices = decompressToIndices(compressed);
// 	for (auto& i : indices) {
// 		if ((mem[i / NUM_BOARDS_PER_U64] & (1ULL << (i % NUM_BOARDS_PER_U64))) != 0)
// 			std::cout << "error" << std::endl;
// 	}
// 	std::cout << "compression test passed" << std::endl;
// }