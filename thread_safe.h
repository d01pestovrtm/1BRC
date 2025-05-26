#pragma once
#include <queue>
#include <mutex>
#include <optional>


namespace oneBRC {

// A threadsafe-queue. Implementation from https://codetrips.com/2020/07/26/modern-c-writing-a-thread-safe-queue/
template<typename T>
class ThreadsafeQueue {
	std::queue<T> queue_;
	mutable std::mutex mutex_;
 
	// Moved out of public interface to prevent races between this
	// and pop().
	bool empty() const {
		return queue_.empty();
	}
 
public:
	ThreadsafeQueue() = default;
	ThreadsafeQueue(const ThreadsafeQueue<T> &) = delete ;
	ThreadsafeQueue& operator=(const ThreadsafeQueue<T> &) = delete ;

	ThreadsafeQueue(ThreadsafeQueue<T>&& other) noexcept {
		std::lock_guard<std::mutex> lock(mutex_);
		queue_ = std::move(other.queue_);
	}
  
	//TODO because of Rule of 5 I must implement move assignement. I will do this later
	ThreadsafeQueue& operator=(ThreadsafeQueue<T> &&) noexcept = delete ;
  	virtual ~ThreadsafeQueue() {}
 
	size_t size() const {
		std::lock_guard<std::mutex> lock(mutex_);
		return queue_.size();
	}
 
 	std::optional<T> pop() {
		std::lock_guard<std::mutex> lock(mutex_);
		if (queue_.empty()) {
      			return {};
      		}
    		T tmp = queue_.front();
    		queue_.pop();
    		return tmp;
  	}
 
	void push(const T &item) {
		std::lock_guard<std::mutex> lock(mutex_);
		queue_.push(item);
	}
};

}
