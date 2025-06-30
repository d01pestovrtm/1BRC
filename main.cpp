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
1) I am using C++23 standard without any side libraries like robin hood hashing
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
#include <vector>
#include <format>
#include <algorithm>
#include <fstream>
#include <string_view>
#include <set>


namespace oneBRC {
using FloatType = float; 
struct Stat {
	float min;
	float max;
	float sum;
	int nRecords;
};
//using Stat = std::vector<FloatType>;


using Records = std::unordered_map <std::string, Stat>;
//TODO to const char* conversion
using Record = std::pair <std::string, FloatType>;


Record parseLine(const std::string& line) {
	auto pos = line.find(';');
	return { line.substr(0, pos), std::atof(line.substr(pos + 1).c_str()) };
}

Records processRecords(std::istream& input) {
	Records records;

	std::string line;
	while(std::getline(input, line)) {
		auto[place, temp] = parseLine(line);
		float min, max, sum;
		min = max = sum = temp;
		int n = 0;

		auto it = records.find(place);
		if (it != records.end()){
			auto& record = it->second;
			min = std::min(record.min, min);
			max = std::max(record.max, max);
			sum += record.sum;
			n = record.nRecords;
		}
		records[place] = Stat{min, max, sum, ++n};
	}
	return records;
}

void writeOutput( Records& records) {

	auto printStat = [](Records& r, const std::string& key, std::string_view format) {
		const auto& stat = r[key];
		std::cout << std::vformat(format, std::make_format_args(key, stat.min, stat.sum / stat.nRecords,  stat.max));
	};
	
	// we have to sort strings. I am not sure this is the best way
	std::set<std::string> v;
	for (auto& [key, _] : records) {
		v.insert(key);
	}
	
	//print stat
	constexpr std::string_view first_fmt = "{}={:.1f}/{:.1f}/{:.1f}, ";
	constexpr std::string_view last_fmt = "{}={:.1f}/{:.1f}/{:.1f}";
	std::cout << '{';
	//note: preincrement doesn't copy iterator
	auto begin = v.begin();
	for (auto end = std::prev(v.end()) ; begin != end; ++begin) {
		printStat(records, *begin, first_fmt);
	}
	printStat(records, *begin, last_fmt);
	std::cout << '}';
}

}// namespace oneBRC

int main() {
	static_assert (alignof(float) == alignof(int));
	std::ifstream ifile("measurements_10000.txt", ifile.in); 
	auto records = oneBRC::processRecords(ifile);
	oneBRC::writeOutput(records);
	return 0;
}

