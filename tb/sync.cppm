export module tb:sync;
import std;

export class Sync {
	std::array<std::atomic<int>, 2> counters{0};
	bool csel = false;

public:
	bool slaveNotifyWait();
	void masterWait(int numThreads);
	void masterNotify(int numThreads);
};

bool Sync::slaveNotifyWait() {
	auto& counter = counters[csel];
	counter++;
	counter.notify_all();
	while (true) {
		int counterRead = counter;
		if (counterRead == 0)
			break;
		counter.wait(counterRead);
	}
	return false;
}

void Sync::masterWait(int numThreads) {
	auto& counter = counters[csel];
	while (true) {
		int counterRead = counter;
		if (counterRead == numThreads)
			break;
		counter.wait(counterRead);
	}
}

void Sync::masterNotify(int numThreads) {
	auto& counter = counters[csel];
	counter -= numThreads;
	csel = !csel;
	counter.notify_all();
}
