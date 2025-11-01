#ifndef KEYHUNT_WORKQUEUE_H
#define KEYHUNT_WORKQUEUE_H

#include <cstddef>
#include <cstdint>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>

template <typename IntType>
class WorkQueue {
public:
	WorkQueue()
		: configured_(false),
		  finished_(false),
		  running_(false),
		  rangeStart_(nullptr),
		  rangeEnd_(nullptr),
		  chunkSize_(0),
		  prefetch_(0) {}

	~WorkQueue() {
		shutdown();
	}

	void configure(IntType *rangeStart, IntType *rangeEnd, uint64_t chunkSize, size_t prefetch) {
		std::lock_guard<std::mutex> lock(mutex_);
		rangeStart_ = rangeStart;
		rangeEnd_ = rangeEnd;
		chunkSize_ = chunkSize;
		prefetch_ = prefetch;
		configured_ = (rangeStart_ != nullptr && rangeEnd_ != nullptr && chunkSize_ > 0 && prefetch_ > 0);
	}

	void start() {
		std::lock_guard<std::mutex> lock(mutex_);
		if (!configured_ || running_) {
			return;
		}
		finished_ = false;
		running_ = true;
		producer_ = std::thread(&WorkQueue::producer_loop, this);
	}

	bool enabled() const {
		std::lock_guard<std::mutex> lock(mutex_);
		return running_;
	}

	bool pop(IntType &out) {
		std::unique_lock<std::mutex> lock(mutex_);
		if (!running_) {
			return false;
		}
		cv_.wait(lock, [this]() { return !queue_.empty() || finished_; });
		if (queue_.empty()) {
			return false;
		}
		out = queue_.front();
		queue_.pop();
		space_.notify_one();
		return true;
	}

	void shutdown() {
		{
			std::lock_guard<std::mutex> lock(mutex_);
			finished_ = true;
		}
		cv_.notify_all();
		space_.notify_all();
		if (running_) {
			if (producer_.joinable()) {
				producer_.join();
			}
			running_ = false;
		}
		std::lock_guard<std::mutex> lock(mutex_);
		std::queue<IntType> empty;
		std::swap(queue_, empty);
		configured_ = false;
	}

private:
	void producer_loop() {
		for (;;) {
			IntType nextKey;
			{
				std::unique_lock<std::mutex> lock(mutex_);
				if (!configured_) {
					finished_ = true;
					cv_.notify_all();
					return;
				}
				while (!finished_ && queue_.size() >= prefetch_) {
					space_.wait(lock);
				}
				if (finished_) {
					cv_.notify_all();
					return;
				}
				if (!rangeStart_->IsLower(rangeEnd_)) {
					finished_ = true;
					cv_.notify_all();
					return;
				}
				nextKey.Set(rangeStart_);
				rangeStart_->Add(chunkSize_);
				queue_.push(nextKey);
			}
			cv_.notify_one();
		}
	}

	std::mutex mutable mutex_;
	std::condition_variable cv_;
	std::condition_variable space_;
	std::queue<IntType> queue_;
	bool configured_;
	bool finished_;
	bool running_;
	IntType *rangeStart_;
	IntType *rangeEnd_;
	uint64_t chunkSize_;
	size_t prefetch_;
	std::thread producer_;
};

#endif // KEYHUNT_WORKQUEUE_H
