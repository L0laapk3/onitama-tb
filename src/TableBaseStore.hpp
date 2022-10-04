

template <U16 TB_MEN, bool STORE_WIN>
void TableBase<TB_MEN, STORE_WIN>::storeSparse(std::ostream& os, TableBase<TB_MEN, STORE_WIN>& tb, const CardsInfo& cards) {
	for (U64 i = CARDSMULT; i--> 0; ) {
		U8 cardI = UNLOAD_ORDER[0][i];
		auto& row = tb.refTable[cardI];
        if (!row.isDecompressed) {
            row.initiateDecompress(tb);
            for (int j = 0; j < tb.numThreads; j++) // TODO: thread
                row.partialDecompress(j);
            row.finishDecompress(tb, false);
        }

        U64 cnt = 0;
		for (auto& entry : row.mem)
			cnt += countOfInterestBits<STORE_WIN>(entry);
		os.write(reinterpret_cast<char*>(&cnt), sizeof(U64));

        U64 lastIndex = 0;
        for (U64 i = 0; i < row.mem.size(); i++) {
		    auto entry = getOfInterestBits<STORE_WIN>(row.mem[i]);
            while (entry) {
                U64 bitIndex = STORE_WIN ? _tzcnt_u32(entry) : _tzcnt_u64(entry);
                entry &= entry - 1;
                U64 index = (STORE_WIN ? 32 : 64) * i + bitIndex;
                U64 diff = index - lastIndex;
                lastIndex = index;

                constexpr static U16 CODE_LARGE = 0xFFFF;
                if (diff < CODE_LARGE) {
		            os.write(reinterpret_cast<char*>(&diff), sizeof(U16));
                } else {
                    assert(diff < (1ULL << 48)); // shouldnt be possible
                    os.write(reinterpret_cast<const char*>(&CODE_LARGE), sizeof(U16));
                    os.write(reinterpret_cast<char*>(&diff), sizeof(U8) * 6);
                }
            }
        }
    }
}




template <U16 TB_MEN, bool STORE_WIN>
std::unique_ptr<TableBase<TB_MEN, false>> TableBase<TB_MEN, STORE_WIN>::loadSparse(std::istream& is, const CardsInfo& cards, U64 memoryAllowance) {
	auto tb = initiateTableBase<TB_MEN, false>(cards, memoryAllowance);

    for (U64 i = CARDSMULT; i--> 0; ) {
        U8 cardI = UNLOAD_ORDER[0][i];
        auto& row = tb->refTable[cardI];
        if (!row.isDecompressed) {
            row.initiateDecompress(*tb);
            for (int j = 0; j < tb->numThreads; j++) // TODO: thread
                row.partialDecompress(j);
            row.finishDecompress(*tb, false);
        }

        U64 cnt = 0;
        is.read(reinterpret_cast<char*>(&cnt), sizeof(U64));

        U64 index = 0;
        for (; cnt --> 0; ) {
            U16 diff;
            is.read(reinterpret_cast<char*>(&diff), sizeof(U16));
            if (diff == 0xFFFF) {
                U64 diff2;
                is.read(reinterpret_cast<char*>(&diff2), sizeof(U8) * 6);
                index += diff2;
            } else {
                index += diff;
            }
            if (index / 64 >= row.mem.size())
                throw std::runtime_error("index out of bounds: " + std::to_string(index));
            row.mem[index / 64] |= 1ULL << (index % 64);
        }
    }

    std::cout << "restored tablebase" << std::endl;

    return tb;
}