
#include "Board.h"
#include "Index.hpp"
#include "TableBase.h"
#include "Card.hpp"
#include "Sync.h"

#include <vector>
#include <atomic>
#include <algorithm>
#include <thread>
#include <chrono>
#include <iostream>
#include <cassert>
#include <immintrin.h>
#include <x86intrin.h>

#define NO_PRINTS



struct ThreadObj {
	Sync sync;
	U64 depth = 2;
};


constexpr U64 CHUNK_P0_POSITIONS = 16;




template<U16 TB_MEN, int DEPTH>
void processEntry(const CardsInfo &cards, std::atomic<U32>& cardsEntry, Board& board, U64& newStates) {
	if (!cardsEntry.load(std::memory_order_relaxed))
		return;
	// TODO: backwards movegen on the entry
}

template<U16 TB_MEN, int DEPTH, U32 ROW>
void processRow(const CardsInfo &cards, TableBase<TB_MEN>& table, U64& chunk, U64& rowStartChunk, std::atomic<U64> &chunkCounter, U64& newStates) {
	auto& row = std::get<ROW>(table.tb);
	constexpr U64 P0_CHUNKS = (row.size() - 1) / CHUNK_P0_POSITIONS + 1;

	for (; chunk < rowStartChunk + P0_CHUNKS; chunk = chunkCounter++) {
		const U32 begin = (chunk - rowStartChunk) * CHUNK_P0_POSITIONS;
		const U32 end = std::min<U32>(begin + CHUNK_P0_POSITIONS, row.size());
		Board board;
		for (U32 ip0 = begin; ip0 < end; ip0++) {
			auto& rowP0 = row[ip0];
			board.bbp[0] = PAWNTABLE_P0<TB_MEN, ROW>[ip0];
			for (U32 ip1 = 0; ip1 < rowP0.size(); ip1++) {
				auto& rowP1 = rowP0[ip1];
				board.bbp[1] = _pdep_u32(PAWNTABLE_P1<TB_MEN, ROW>[ip1], ~board.bbp[0]);
				for (U32 ik0 = 0; ik0 < rowP1.size(); ik0++) {
					auto& rowK0 = rowP1[ik0];
					board.bbk[0] = _pdep_u32(1U << ik0, board.bbp[0]);
					for (U32 ik1 = 0; ik1 < rowK0.size(); ik1++) {
						auto& cardsEntry = rowK0[ik1];
						board.bbk[1] = _pdep_u32(1U << ik1, board.bbp[1]);
						processEntry<TB_MEN, DEPTH>(cards, cardsEntry, board, newStates);
					}
				}
			}
		}
	}
	rowStartChunk += P0_CHUNKS;
}

template<U16 TB_MEN, int DEPTH>
void singleDepthPass(const CardsInfo &cards, TableBase<TB_MEN>& table, std::atomic<U64> &chunkCounter, std::atomic<U64>& stateCounter) {
	U64 newStates = 0;
	U64 chunk = chunkCounter++;
	U64 rowStartChunk = 0;

	[&]<U32... ROW>(std::integer_sequence<U32, ROW...>) {
		(processRow<TB_MEN, DEPTH, ROW>(cards, table, chunk, rowStartChunk, chunkCounter, newStates), ...);
	}(std::make_integer_sequence<U32, std::tuple_size_v<typename TableBase<TB_MEN>::TableBaseStorage>>{});

	stateCounter += newStates;
}

template<U16 TB_MEN>
void singleThread(const CardsInfo& cards, TableBase<TB_MEN>& tb, std::atomic<U64>& chunkCounter, std::atomic<U64>& stateCounter, ThreadObj& comm) {
	while (true) {
		comm.sync.slaveNotifyWait();
		if (comm.depth == 0)
			break;

		if (comm.depth == 2)
			singleDepthPass<TB_MEN, 2>(cards, tb, chunkCounter, stateCounter);
		else
			singleDepthPass<TB_MEN, 3>(cards, tb, chunkCounter, stateCounter);
	}
}

template <U16 TB_MEN>
TableBase<TB_MEN>::TableBase(const CardsInfo& cards) {
	std::atomic<U64> chunkCounter;
	std::atomic<U64> stateCounter = 0;
	ThreadObj comm;
	int numThreads = std::clamp<int>(std::thread::hardware_concurrency(), 1, 1024);
	std::vector<std::thread> threads(numThreads);
	for (int i = 0; i < numThreads; i++)
		threads[i] = std::thread(&singleThread<TB_MEN>, std::cref(cards), std::ref(*this), std::ref(chunkCounter), std::ref(stateCounter), std::ref(comm));
	comm.sync.masterWait(numThreads);

	U64 lastStateCounter = 1;
	while (stateCounter != lastStateCounter) {
		chunkCounter = 0;
		lastStateCounter = stateCounter;
		comm.sync.masterNotify(numThreads);
		comm.sync.masterWait(numThreads);
	}

	comm.depth = 0; // signal to stop threads
	comm.sync.masterNotify(numThreads);
	for (auto& thread : threads)
		thread.join();
}
