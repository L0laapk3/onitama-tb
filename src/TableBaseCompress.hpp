
#include "lz4frame.h"
#include "lz4.h"



constexpr LZ4F_preferences_t LZ4Prefs {
	.compressionLevel = 0,
};



template <U16 TB_MEN, bool STORE_WIN>
void RefRowWrapper<TB_MEN, STORE_WIN>::initiateCompress() {
	assert(!isCompressed);
	U64 decompressedSectionSize = ((mem.size() + memComp.size() - 1) / memComp.size()) * sizeof(U64);
	U64 compressedSectionSize = memComp.size() * LZ4F_compressFrameBound(decompressedSectionSize, &LZ4Prefs);  // TODO: do in small blocks instead of one big block
	for (auto& memCompSection : memComp)
		memCompSection = std::vector<unsigned char>(compressedSectionSize);
}

template <U16 TB_MEN, bool STORE_WIN>
void RefRowWrapper<TB_MEN, STORE_WIN>::partialCompress(int section) {
	U64 startI = mem.size() * section / memComp.size();
	U64 stopI = mem.size() * (section + 1) / memComp.size();
	auto& memCompSection = memComp[section];
	size_t compressedSize = LZ4F_compressFrame(
		memCompSection.data(), memCompSection.size(),
		&mem[startI], (stopI - startI) * sizeof(U64),
		&LZ4Prefs);
	// std::cout << memComp.size() << ' ' << compressedSize << std::endl;
	if (LZ4F_isError(compressedSize)) {
		std::cerr << "LZ4F_compressFrame failed: " << LZ4F_getErrorName(compressedSize) << std::endl;
		exit(1);
	}
	memCompSection.resize(compressedSize);
	memCompSection.shrink_to_fit();

	partialDecompress(section);
}
template <U16 TB_MEN, bool STORE_WIN>
void RefRowWrapper<TB_MEN, STORE_WIN>::cleanUpCompress() {
	isCompressed = true;
	mem.~vector<std::atomic<U64>>();
}



template <U16 TB_MEN, bool STORE_WIN>
void RefRowWrapper<TB_MEN, STORE_WIN>::initiateDecompress() {
	mem = std::vector<std::atomic<U64>>(refs.back());
	assert(isCompressed);
}

template <U16 TB_MEN, bool STORE_WIN>
void RefRowWrapper<TB_MEN, STORE_WIN>::partialDecompress(int section) {
	

	U64 startI = mem.size() * section / memComp.size();
	U64 stopI = mem.size() * (section + 1) / memComp.size();
	auto& memCompSection = memComp[section];
	
	LZ4F_decompressionContext_t ctx;
	auto error = LZ4F_createDecompressionContext(&ctx, LZ4F_VERSION);
	if (error) {
		std::cerr << "LZ4F_createDecompressionContext failed: " << LZ4F_getErrorName(error) << std::endl;
		exit(1);
	}

	size_t outBuf = (stopI - startI) * sizeof(U64);
	size_t inBuf = memCompSection.size();

	std::atomic<U64>* dstPtr = &mem[startI];
	unsigned char* srcPtr = memCompSection.data();

	size_t result = LZ4F_decompress(ctx,
		dstPtr, &outBuf,
		srcPtr, &inBuf,
		nullptr);
	if (LZ4F_isError(result)) {
		std::cerr << "LZ4F_decompress failed: " << LZ4F_getErrorName(result) << std::endl;
		exit(1);
	}

	LZ4F_freeDecompressionContext(ctx);
}
template <U16 TB_MEN, bool STORE_WIN>
void RefRowWrapper<TB_MEN, STORE_WIN>::cleanUpDecompress() {
	isCompressed = false;
	for (auto& memCompSection : memComp)
		memCompSection.~vector<unsigned char>();
}



template<U16 TB_MEN, bool STORE_WIN>
long long TableBase<TB_MEN, STORE_WIN>::determineUnloads(U8 cardI, long long mem_remaining, std::function<void(RefRowWrapper<TB_MEN, STORE_WIN>& row)> cb) {
	U8 invCardI = CARDS_INVERT[cardI];
	return determineUnloads<5>(cardI, {
		cardI,
		CARDS_SWAP[invCardI][1][0],
		CARDS_SWAP[invCardI][1][1],
		CARDS_SWAP[invCardI][0][0],
		CARDS_SWAP[invCardI][0][1],
	}, mem_remaining, cb);
}

template<U16 TB_MEN, bool STORE_WIN>
template<U8 numRows>
long long TableBase<TB_MEN, STORE_WIN>::determineUnloads(U8 nextLoadCardI, std::array<U8, numRows> rowsI, long long mem_remaining, std::function<void(RefRowWrapper<TB_MEN, STORE_WIN>& row)> cb) {
	for (U8 rowI : rowsI) {
		auto& row = refTable[rowI];
		if (row.isCompressed)
			mem_remaining -= row.refs.back() * sizeof(U64) - row.memComp.size() * sizeof(unsigned char);
	}

	if (mem_remaining < 0)
		for (U8 i = 0; i < 30 - numRows; i++) {
			auto& rowI = UNLOAD_ORDER[nextLoadCardI][i];
			auto& row = refTable[rowI];
			if (!row.isCompressed) {
				cb(row);
				mem_remaining += row.refs.back() * sizeof(U64) - row.memComp.size() * sizeof(unsigned char);
				if (mem_remaining >= 0)
					break;
			}
		}
	return mem_remaining;
}







// constexpr U64 NUM_BYTES_STORED = 5;


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