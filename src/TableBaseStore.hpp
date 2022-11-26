

template <U16 TB_MEN, bool STORE_WIN>
std::vector<uint64_t> TableBase<TB_MEN, STORE_WIN>::storeSparse(const CardsInfo& cards) {
    std::vector<uint64_t> result;
	BoardIndex bi;
	int numThreads = std::clamp<int>(std::thread::hardware_concurrency(), 1, 1024);

	for (U64 i = CARDSMULT; i--> 0; ) {
		U8 cardI = UNLOAD_ORDER[0][i];
		auto& row = refTable[cardI];
        if (!row.isDecompressed) {
            row.initiateDecompress(*this);
            for (int j = 0; j < numThreads; j++) // TODO: thread
                row.partialDecompress(j);
            row.finishDecompress(*this, false);
        }

        U64 cnt = 0;
		for (auto& entry : row.mem)
			cnt += countOfInterestBits<STORE_WIN>(entry);
        result.push_back(cnt);

        for (U64 i = 0; i < row.mem.size(); i++) {
		    auto entry = getOfInterestBits<STORE_WIN>(row.mem[i]);
            while (entry) {
                int bitIndex = getFirstResolvedIndex<STORE_WIN>(entry);
                result.push_back(NUM_BOARDS_PER_ENTRY<STORE_WIN> * i + bitIndex); // TODO: thats a bug?? fuck
                entry &= entry - 1;
            }
        }
    }

    return result;
}