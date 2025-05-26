#include <iostream>
#include <unordered_map>
#include <string>
#include <vector>
#include <format>
#include <algorithm>
#include <fstream>
#include <string_view>
#include <set>
#include "thread_safe.h"

namespace oneBRC {
/* std::vector is probably the most optimized container, so I want to spare time on 
 * min_temp = std::min(min_temp, other_temp) and use push_backs instead 
 * It is also interesting to test if my idea with SIMD (single instuction many additions) will work
 * using FloatType as I want to write my custom float
 */
using FloatType = float; 
using Stat = std::vector<FloatType>;


using Records = std::unordered_map <std::string, Stat>;
using Record = std::pair <std::string, FloatType>;


Record parseLine(const std::string& line) {
	auto pos = line.find(';');
	return { line.substr(0, pos), std::atof(line.substr(pos + 1).c_str()) };
}

Records processRecords(std::istream& input) {
	Records records;
	/* std::string is okay I think, because most of the time the short string optimization should be invoked
	 */
	std::string line;
	while(std::getline(input, line)) {
		//RVO optimization, so copy is okay
		auto[place, temp] = parseLine(line);
		records[place].push_back(temp);
	}
	return records;
}

void writeOutput( Records& records) {

	auto printStat = [](Records& r, const std::string& key, std::string_view format) {
		const auto& stat = r[key];
		const auto [min, max] = std::minmax_element(stat.begin(), stat.end());
		auto avg = [](const std::vector <FloatType>& stat) -> FloatType 
		{
			FloatType sum = 0.f;
			for (auto s : stat) {
				sum += s;
			}
			return sum / stat.size();
		}(stat);
		std::cout << std::vformat(format, std::make_format_args(key, *min, avg,  *max));
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
	std::ifstream ifile("measurements_10000.txt", ifile.in); 
	auto records = oneBRC::processRecords(ifile);
	oneBRC::writeOutput(records);
	return 0;
}

