/*
MIT License

Copyright (c) 2025 RNDr. Dmitrij Pesztov

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <iostream>
#include <unordered_map>
#include <string>
#include <spanstream>
#include <ranges>
#include <vector>
#include <format>
#include <algorithm>
#include <string_view>
#include <set>
#include <utility>
#include <type_traits>
#include <filesystem>
#include <charconv>
#include <sys/stat.h>
#include <sys/mman.h> 
#include <fcntl.h>
#include <unistd.h>
#include <functional>

#include <mutex>
#include <future>
#include <thread>

//Is golang style defer macro here applicable?
// https://habr.com/ru/articles/916760/
template <typename T, T empty = T{}>
struct MoveOnly {
	MoveOnly() : value_(empty) {}
	MoveOnly(T value) : value_(value) {}
	
	//For POD doesn't matter, but whatever
	MoveOnly(MoveOnly&& other) noexcept(std::is_nothrow_move_constructible_v<T>) :
		value_(std::exchange(other.value_, empty)) {}
	MoveOnly& operator=(MoveOnly&& other) noexcept(std::is_nothrow_move_constructible_v<T>) {
		value_ = std::exchange(other.value_, empty);
		return *this;
	}
	
	/* The copy constructor/assignment are by default deleted
	MoveOnly(const MoveOnly& other) = delete;
	MoveOnly& operator=(const MoveOnly& other) = delete;
	*/
	
	//operator T for comparison with integers for example
	operator T() const { return value_; }
	
	T get() const { return value_; }
private:
	T value_;
};


//RAII wrapper for handling file open/close
//we use posix otherwise we cannot memory map the file
//TODO add version for windows via #ifdef _WIN32
struct FileHandler {
	explicit FileHandler(const std::filesystem::path &file_path) :
		fdescr_(open(file_path.c_str(), O_RDONLY))
	{
		if (fdescr_ == -1)
			throw std::system_error(errno, std::system_category(), "Cannot open file");
	}
	
	~FileHandler() {
		if (fdescr_ >= 0)
			close(fdescr_);
	}
	
	int get () const { return fdescr_.get(); }
private:
	MoveOnly<int, -1> fdescr_;
};


//struct for mapping file and giving buffer
struct MemoryMap {
	explicit MemoryMap(const std::filesystem::path &file_path) :
		fhandler_(file_path)
	{
		struct stat file_stat;
		//here was probably a typo by Mr. Tóth 
		//https://pubs.opengroup.org/onlinepubs/009696699/functions/fstat.html
		if (fstat(fhandler_.get(), &file_stat) == -1)
			throw std::system_error(errno, std::system_category(), 
				"Cannot obtain information about the file");
		
		size_ = file_stat.st_size;
		begin_ = static_cast <char *>(mmap(NULL, size_, PROT_READ, MAP_PRIVATE, fhandler_.get(), 0));
		if (begin_ == MAP_FAILED)
			throw std::system_error(errno, std::system_category(), 
				"Cannot establish a mapping between a process' address space and a file");
		current_ = begin_;
	}
	
	//return memory chunk <= bufferSize such that all measurements are consistent
	//by implementation chunk will have at least one measurement - 100 bytes station name + maximum 6 bytes temperature < 128
	//TODO test if string_view is better here
	std::span <const char> getChunk(size_t bufferSize = 128) {		
	
		std::lock_guard<std::mutex> lk(mut_);
		if (bufferSize >= size_) {
			auto result =  std::span <const char>(current_, begin_ + size_);
			current_ = begin_ + size_;
			return result;
		}
		if (current_ == begin_ + size_ )
			return {};


		const char * end = current_ + bufferSize;
		if (end > begin_ + size_)
			end = begin_ + size_;
		
		//Here is a possible overflow, so I need an extra check
		/*
		The reason is: imagine we have 2 lines:
		test\n
		test\n
		and size of buffer is 9. so end will point to the second \n
		without this extra check end++ will be triggered after while loop 
		and cause buffer overflow
		*/
		if ((end + 1) == begin_ + size_)
			end--;
		
		//Normally I have to check that end >= current_, 
		//but we know that buffer is able to contain at least one measurement ending with '\n'
		while (*end != '\n')
			end --;
		end++;

		
		auto result = std::span <const char>(current_, end);
		current_ =  end;
		return result;
	}
	
	~MemoryMap() {
		if (begin_ != nullptr) 
			munmap(begin_, size_);
	}
private:
	FileHandler fhandler_;
	MoveOnly<char *> begin_;
	MoveOnly<size_t> size_;
	const char * current_;
	std::mutex mut_;
};


struct Stat {
	float min;
	float max;
	float sum;
	int nRecords;
};

struct string_hash {
	using is_transparent = void;
	[[nodiscard]] size_t operator()(const char *txt) const {
		return std::hash<std::string_view>{}(txt);
	}
	[[nodiscard]] size_t operator()(std::string_view txt) const {
		return std::hash<std::string_view>{}(txt);
	}
	[[nodiscard]] size_t operator()(const std::string &txt) const {
		return std::hash<std::string>{}(txt);
	}
};

using Records = std::unordered_map <std::string, Stat, string_hash, std::equal_to<>>;


//SFINAE to ensure that the lookup in hash_map is heterogenous
template <typename T, typename = void>
struct is_heterogenous_lookup : std::false_type {};

template <typename T>
struct is_heterogenous_lookup<T, std::void_t<typename T::is_transparent>> : std::true_type {};



void updateRecords(std::span <const char> sp, Records& records) {
	static_assert(is_heterogenous_lookup<Records::key_equal>::value, 
		      "Comparator must support heterogenous lookup, i.e., *::is_transparent = void");
	
	auto nextRecord = [](std::span <const char> sp) {
		auto begin = sp.begin();
		auto end = std::find(begin, sp.end(), '\n');
		return std::string_view(begin, end + 1);
	};
		      
	size_t begin = 0;
	const size_t end = sp.size();
	while (begin < end) {
		auto record = nextRecord(sp.subspan(begin));
		begin += record.size();		
		
		auto sep = record.find_first_of(';');
		
		std::string_view place_sv = record.substr(0, sep);
		std::string_view temp_sv = record.substr(sep + 1);
		
		float temp;
		if (std::from_chars(temp_sv.data(), temp_sv.data() + temp_sv.size(),  temp).ec 
			!= std::errc{})
			continue; //This case is unlikely
		
		
		auto recordFound = records.find(place_sv);
		if (recordFound == records.end()) {
			//create std::string only first time we have this place
			records.emplace(std::string(place_sv), Stat{temp, temp, temp, 1});
		} else {
			auto& stat = recordFound->second;
			stat.min = std::min(stat.min, temp);
			stat.sum += temp;
			stat.max = std::max(stat.max, temp);
			stat.nRecords ++;
		}	
	}
}


std::vector<std::string> sortStations(const Records& r){
	//C++23 style
	//unfortunately, my g++ doesn't support std::ranges::to
	//TODO try to implement ranges::to on my own. should be an interesting task
	std::vector<std::string> result(r.size());
	auto key_view = r | std::views::keys;

#ifdef __cpp_lib_ranges_to_container
	result = std::ranges::to < std::vector > (key_view);
#else
	std::ranges::copy(key_view, result.begin());
#endif

	std::ranges::sort(result);
	
	return result;
}

void printRecords(const Records& r) {
	auto sortedStations = sortStations(r);
	
	auto printStat = [](const Records& r, const std::string& key, std::string_view format) {
		const auto& stat = r.at(key);
		std::cout << std::vformat(format, 
			std::make_format_args(key, stat.min, stat.sum / stat.nRecords,  stat.max));
	};

	constexpr std::string_view first_fmt = "{}={:.1f}/{:.1f}/{:.1f}, ";
	constexpr std::string_view last_fmt = "{}={:.1f}/{:.1f}/{:.1f}";
	
	std::cout << '{';
	auto begin = sortedStations.begin();
	for (auto end = std::prev(sortedStations.end()) ; begin != end; ++begin) {
		printStat(r, *begin, first_fmt);
	}
	printStat(r, *begin, last_fmt);
	std::cout << '}';
}


Records createRecords(const std::filesystem::path &file_path, const size_t chunkSize, const int numWorkers) {
	//I could use packaged_task as well, but I want to try with promises, because in the real live 
	//I might need them to set exceptions

	auto m = MemoryMap(file_path);
	std::vector < std::future < Records > > futures;
	std::vector < std::thread > workers;
	std::vector < Records > records_v;
	Records result;

	for (int i = 0; i < numWorkers; i++) {
		std::promise <Records> p;
		futures.push_back(p.get_future());
		//maybe use std::ref to m
		workers.emplace_back([&m, p = std::move(p), chunkSize = chunkSize]() mutable {
				Records local;
				auto sp = m.getChunk(chunkSize);
				while (!sp.empty()) {
					updateRecords(sp, local);
					sp = m.getChunk(chunkSize);
				}
				p.set_value(std::move(local));
			}
		);
	}
	for (auto& w : workers) {
		if (w.joinable()) w.join();
	}
	for (auto& f : futures) {
        	records_v.push_back(f.get());
	}
	

	for (const auto& map : records_v) {
		for (const auto&[key, value] : map) {
			auto it = result.find(key);
			if (it == result.end())
				result.emplace(key, value);
			else {
				auto& stat = it->second;
				stat.min = std::min(stat.min, value.min);
				stat.sum += value.sum;
				stat.max = std::max(stat.max, value.max);
				stat.nRecords += value.nRecords;
			}
		}
	}
	return result;
}

int main(int argc, char* argv[]) {
	const size_t chunkSize = 128 * 1024 * 1024;
	const int numWorkers = 8;
	auto records = createRecords("../measurements.txt", chunkSize, numWorkers);
	printRecords(records);
}
