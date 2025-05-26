#include <iostream>
#include <mutex>
#include <thread>
#include <format>
#include <optional>
#include <vector>
#include "thread_safe.h"

//toy example to check if thread safe queue works
namespace oneBRC {
	//test idea of go like worker pattern
	void reader(ThreadsafeQueue< int >& q) {
		for (int i = 0; i < 100; i++)
			q.push(i);
	}
	
	void worker(ThreadsafeQueue< int >& q, int id) {
		//according cppreference we can use std::optional as condition in while
		std::optional < int > value;
		while (value = q.pop()) {
			std::cout << std::format ("Worker with id {} - {}\n", id, value.value_or(-1));
			std::this_thread::sleep_for(std::chrono::seconds(1));
		}
		
	}
}

int main(void) {
	oneBRC::ThreadsafeQueue < int > q;
	constexpr size_t num_workers = 4;
	
	std::thread reader(oneBRC::reader, std::ref(q));
	std::vector <std::thread > workers;
	for (int i = 0; i < num_workers; ++i) {
		workers.emplace_back(oneBRC::worker, std::ref(q), i);
	} 
	reader.join();
	for (auto& w : workers) 
		w.join();
	std::cout << "finish!\n";
	return 0;
}
