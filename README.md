This is my attempt for 1 billion row challenge.
Below is the description from https://github.com/gunnarmorling/1brc

The text file "measurements.txt" (15.9 Gb) contains temperature values for a range of weather stations. 
Each row is one measurement in the format <string: station name>;<double: measurement>, 
with the measurement value having exactly one fractional digit. The following shows ten rows as an example:\
Hamburg;12.0\
Bulawayo;8.9\
Palembang;38.8\
St. John's;15.2\
Cracow;12.6\
Bridgetown;26.9\
Istanbul;6.2\
Roseau;34.4\
Conakry;31.2\
Istanbul;23.0

The task is to write a program which reads the file, calculates the min, mean, and max temperature value per weather station, 
and emits the results on stdout like this 
(i.e. sorted alphabetically by station name, and the result values per station in the format \<min\>/\<mean\>/\<max\>, rounded to one fractional digit):

{Abha=-23.0/18.0/59.2, Abidjan=-16.2/26.0/67.3, Abéché=-10.0/29.4/69.0, Accra=-10.1/26.4/66.4, Addis Ababa=-23.7/16.0/67.0, Adelaide=-27.8/17.3/58.5, ...}

Rules and limits applied to C++ version:
* I am using C++23 standard compiled with g++ without any side libraries like robin hood hashing
  * compilation via g++-13 -O3 -std=c++23 main.cpp
* Implementations must be provided as a single source file
* The computation must happen at application runtime, i.e. you cannot process the measurements file at compile time
* Input value ranges are as follows:
  * Station name: non null UTF-8 string of min length 1 character and max length 100 bytes, containing neither ; nor \n characters. (i.e. this could be 100 one-byte characters, or 50 two-byte characters, etc.)
  * Temperature value: non null double between -99.9 (inclusive) and 99.9 (inclusive), always with one fractional digit
  * There is a maximum of 10,000 unique station names
  * Line endings in the file are \n characters on all platforms
  * Implementations must not rely on specifics of a given data set, e.g. any valid station name as per the constraints above and any data distribution (number of measurements per station) must be supported
  * The rounding of output values must be done using the semantics of IEEE 754 rounding-direction "roundTowardPositive"
 
---

My implementation is particullary based on the article of @HappyCerberus - [link](https://simontoth.substack.com/p/daily-bite-of-c-optimizing-code-to)

I found the idea of Mr. Tóth really great. It is the first time I use RAII on low-level objects without smart pointers. `std::lock_guard<std::mutex>` doesn't count.
Below is my possibly wrong explanation of MoveOnly struct in his example:
```
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
```
While it might look dummy to create such struct for POD types, but in this approach we don't forget to exchange values properly
There is also a problem that move semantic for POD is copy.
This might be critical by using `open` function which returns file descriptor as integer. 
And close 2 times the same descriptor is UB(imagine if we accidentaly copied the same descriptor(int))
This helper struct prevents such errors. As we use it for `char*` or `int` empty will be aggregate constructed. 


I am also thankful to Mr. Tóth for showing other 2 important concepts: heterogenous lookup and memeory mapping.

---

I want to implement in the future following optimizations:
* threads with local hash_map and then merging
* integers instead of floats for faster parsing
* custom hash_map instead of slow `std::unordered_map`. In my case I don't need iterators and other things STL supports
* Design concurrent hash map either with bucket mutex or some how lock free 
  * I think about RCU. As it should not be that often we have 2 threads updating the same value. Maybe I can make records counter atomic which I use later for mean value. Thus, I can deal with race condition by checking if the counter was updated since last time.
