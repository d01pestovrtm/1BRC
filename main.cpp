/*
This is my attempt for 1 billion row challenge.
Below is the description from https://github.com/gunnarmorling/1brc

The text file "measurements.txt" (15.9 Gb) contains temperature values for a range of weather stations. 
Each row is one measurement in the format <string: station name>;<double: measurement>, 
with the measurement value having exactly one fractional digit. The following shows ten rows as an example:
Hamburg;12.0
Bulawayo;8.9
Palembang;38.8
St. John's;15.2
Cracow;12.6
Bridgetown;26.9
Istanbul;6.2
Roseau;34.4
Conakry;31.2
Istanbul;23.0

The task is to write a program which reads the file, calculates the min, mean, and max temperature value per weather station, 
and emits the results on stdout like this 
(i.e. sorted alphabetically by station name, and the result values per station in the format <min>/<mean>/<max>, rounded to one fractional digit):
{Abha=-23.0/18.0/59.2, Abidjan=-16.2/26.0/67.3, Abéché=-10.0/29.4/69.0, Accra=-10.1/26.4/66.4, Addis Ababa=-23.7/16.0/67.0, Adelaide=-27.8/17.3/58.5, ...}

Rules and limits applied to C++ version:
1) I am using C++23 standard compiled with g++ without any side libraries like robin hood hashing
2) Implementations must be provided as a single source file
3) The computation must happen at application runtime, i.e. you cannot process the measurements file at compile time
4) Input value ranges are as follows:
-- Station name: non null UTF-8 string of min length 1 character and max length 100 bytes, containing neither ; nor \n characters. (i.e. this could be 100 one-byte characters, or 50 two-byte characters, etc.)
-- Temperature value: non null double between -99.9 (inclusive) and 99.9 (inclusive), always with one fractional digit
-- There is a maximum of 10,000 unique station names
-- Line endings in the file are \n characters on all platforms
-- Implementations must not rely on specifics of a given data set, e.g. any valid station name as per the constraints above and any data distribution (number of measurements per station) must be supported
-- The rounding of output values must be done using the semantics of IEEE 754 rounding-direction "roundTowardPositive"

*/



#include <iostream>
#include <unordered_map>
#include <string>
//for open
#include <sys/stat.h>
#include <sys/mman.h> 
#include <fcntl.h>
//for close
#include <unistd.h>

#include <spanstream>

#include <vector>
#include <format>
#include <algorithm>
#include <fstream>
#include <string_view>
#include <set>
#include <utility>
#include <type_traits>
#include <format>
#include <filesystem>
#include <concepts>
#include <type_traits>
#include <functional>
#include <charconv>
/*I found this idea of @simontoth really great. It is the first time I use RAII on low-level objects without smart pointers.
While it might look dummy to create such struct for POD types, but in this approach we don't forget to exchange values properly
There is also a problem that move semantic for POD is copy.
This might be critical by using open function which returns file descriptor as integer. 
And close 2 times the same descriptor is UB(imagine if we accidentaly copied the same descriptor(int))
This helper struct prevents such errors
*/
//TODO golang style defer macro?
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


//struct for handling file open/close
//we use posix otherwise we cannot memory map the file
//TODO #ifdef _WIN32
struct FileHandler {
	explicit FileHandler(const std::filesystem::path &file_path) :
		fdescr_(open(file_path.c_str(), O_RDONLY))
	{
		//if error
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
			throw std::system_error(errno, std::system_category(), "Cannot obtain information about the file");
		
		size_ = file_stat.st_size;
		begin_ = static_cast <char *>(mmap(NULL, size_, PROT_READ, MAP_PRIVATE, fhandler_.get(), 0));
		if (begin_ == MAP_FAILED)
			throw std::system_error(errno, std::system_category(), "Cannot establish a mapping between a process' address space and a file");
		current_ = begin_;
	}
	
	//return memory chunk <= bufferSize such that all measurements are consistent
	//by implementation chunk will have at least one measurement
	//TODO test if string_view is better here
	std::span <const char> getChunk(size_t bufferSize = 128) {
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

std::string_view nextRecord(std::span <const char> sp) {
	auto begin = sp.begin();
	auto end = std::find(begin, sp.end(), '\n');
	return std::string_view(begin, end + 1);
}

Records r;

//SFINAE to ensure that the lookup in hash_map is heterogenous
template <typename T, typename = void>
struct is_heterogenous_lookup : std::false_type {};

template <typename T>
struct is_heterogenous_lookup<T, std::void_t<typename T::is_transparent>> : std::true_type {};



void updateRecords(std::span <const char> sp, Records& records) {
	static_assert(is_heterogenous_lookup<Records::key_equal>::value, 
		      "Comparator must support heterogenous lookup, i.e., *::is_transparent = void");
	size_t begin = 0;
	size_t end = sp.size();
	while (begin < end) {
		auto record = nextRecord(sp.subspan(begin, end));
		begin += record.size();		
		auto sep = record.find_first_of(';');
		auto place_sv = record.substr(0, sep);
		auto temp_sv = record.substr(sep + 1);
		auto found = records.find(place_sv);
		float temp;
		
		if (found == records.end()) {
			//create std::string only first time we have this place
			if (std::from_chars(temp_sv.data(), temp_sv.data() + temp_sv.size(),  temp).ec == std::errc{})
				records.emplace(std::string(place_sv), Stat{temp, temp, temp, 1});
		} else {
			auto& stat = found->second;
			if (std::from_chars(temp_sv.data(), temp_sv.data() + temp_sv.size(),  temp).ec == std::errc{})
			{
				stat.min = std::min(stat.min, temp);
				stat.sum += temp;
				stat.max = std::max(stat.max, temp);
				stat.nRecords ++;
			}	
		}	
	}
}

//maybe std::ref (std::string)?
std::vector<std::string> makeSortedVectorfromMap(const Records& r){
	std::vector<std::string> result;
	result.reserve(r.size());
	std::ranges::transform(r, std::back_inserter(result), 
				[](auto const& it) {return it.first;});
	std::ranges::sort(result);
	return result;
}

void printRecords( Records& r) {
	auto records = makeSortedVectorfromMap(r);
	auto printStat = [](Records& r, const std::string& key, std::string_view format) {
		const auto& stat = r[key];
		std::cout << std::vformat(format, 
			std::make_format_args(key, stat.min, stat.sum / stat.nRecords,  stat.max));
	};
	//print stat
	constexpr std::string_view first_fmt = "{}={:.1f}/{:.1f}/{:.1f}, ";
	constexpr std::string_view last_fmt = "{}={:.1f}/{:.1f}/{:.1f}";
	std::cout << '{';
	//note: preincrement doesn't copy iterator
	auto begin = records.begin();
	for (auto end = std::prev(records.end()) ; begin != end; ++begin) {
		printStat(r, *begin, first_fmt);
	}
	printStat(r, *begin, last_fmt);
	std::cout << '}';
}


std::ostream& operator<<(std::ostream& out, const Records& r) {
	for (auto& [place, temp] : r) {
		out << place << ": " <<
			temp.min << '/' << temp.max << '/' << temp.sum << '/' << temp.nRecords << '\n';
	}
	return out;
} 

std::ostream& operator<<(std::ostream& out, std::span <const char> sp) {
	for (auto c : sp)
		out << c;
	return out;
}

int main(int argc, char* argv[]) {
	auto m =  MemoryMap("test.txt");
	std::size_t chunkSize = 30;
	auto sp = m.getChunk(chunkSize);
	while (!sp.empty()) {
		std::cout << sp;
		updateRecords(sp, r);
		sp = m.getChunk(chunkSize);
	}
	std::cout << r;
	printRecords(r);
}

