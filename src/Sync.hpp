#pragma once

#include <atomic>
#include <limits>

class Sync {
	std::atomic<int> counter = 0;
public:

	template<bool checkEvent = false>
	bool slaveNotifyWait() {
		counter++;
		counter.notify_all();
		while (true) {
			int counterRead = counter;
			if (counterRead == 0)
				break;
			if (checkEvent && counterRead == std::numeric_limits<int>::min())
				return true;
			counter.wait(counterRead);
		}
		return false;
	}

	void masterNotifyWait(int numThreads) {
		masterNotifyDuringWait(numThreads);
		while (true) {
			int counterRead = counter;
			if (counterRead == 0)
				break;
			counter.wait(counterRead);
		}
	}
	
	void masterWaitBeforeNotify(int numThreads) {
		while (true) {
			int counterRead = counter;
			if (counterRead == numThreads)
				break;
			counter.wait(counterRead);
		}
	}
	void masterNotifyDuringWait(int numThreads) {
		counter -= numThreads;
		counter.notify_all();
	}
	void masterNotifyAfterWait(bool event = false) {
		counter = event ? std::numeric_limits<int>::min() : 0;
		counter.notify_all();
	}
};