#pragma once

#include <array>
#include <atomic>

class Sync {
	std::array<std::atomic<int>, 2> counters{ 0 };
	bool csel = false;
public:

	bool slaveNotifyWait();
	void masterWait(int numThreads);
	void masterNotify(int numThreads);
};