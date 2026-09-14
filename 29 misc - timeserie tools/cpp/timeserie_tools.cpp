#include "tse_helpers.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

	std::int64_t constexpr
		windowFromNs {1262304000000000000LL},
		windowToNs {1293840000000000000LL};

}

int main()
{
	tse::setLogLevel(tse::LogLevel::none);

	tse::CsvOptions readOptions {};
	readOptions.dateFormat = "%Y-%m-%d";
	readOptions.separator = ';';
	readOptions.hasHeader = true;

	tse::CsvReader reader {readOptions};
	reader.readFile(helpers::dataPath("aapl.csv"));
	std::vector<std::int64_t> const stamps {reader.readIndex(std::string {"Index"})};
	std::vector<double> const closes {reader.readDoubles(std::string {"AAPL.Close"})};

	// Cut the series down to calendar year 2010: the bounds are midnight UTC on 1 January 2010
	// and on 1 January 2011, so the year is taken whole and neither neighbour leaks in.
	std::vector<std::int64_t> windowStamps;
	std::vector<double> windowCloses;
	for (std::size_t i {0}; i != stamps.size(); ++i) {
		if (stamps[i] >= windowFromNs && stamps[i] < windowToNs) {
			windowStamps.push_back(stamps[i]);
			windowCloses.push_back(closes[i]);
		}
	}

	tse::CsvWriterOptions writeOptions {};
	writeOptions.separator = ';';
	writeOptions.formatTimestamps = true;
	writeOptions.dateFormat = "%Y-%m-%d";
	tse::CsvWriter const writer {writeOptions};
	std::filesystem::path const filteredPath {std::filesystem::temp_directory_path() / "tse_example_timeserie_filtered.csv"};
	writer.writeScalar(filteredPath.string(), windowStamps, windowCloses, {"Index", "AAPL.Close"});

	// What the writer put on disk is a series in its own right, so the reader takes it back.
	tse::CsvReader rereader {readOptions};
	rereader.readFile(filteredPath.string());
	std::vector<std::int64_t> const filteredStamps {rereader.readIndex(std::string {"Index"})};
	std::vector<double> const filteredCloses {rereader.readDoubles(std::string {"AAPL.Close"})};

	// head and tail hand back a (begin, length) window into the series, never a copy of it.
	std::pair<std::size_t, std::size_t> const headBounds {tse::head(filteredStamps, filteredCloses, 5)};
	std::pair<std::size_t, std::size_t> const tailBounds {tse::tail(filteredStamps, filteredCloses, 5)};

	std::error_code ec {};
	std::filesystem::remove(filteredPath, ec);

	std::printf
	(
		"rows=%zu window2010=%zu head=(%zu,%zu) tail=(%zu,%zu)\n",
		stamps.size(),
		windowStamps.size(),
		headBounds.first, headBounds.second,
		tailBounds.first, tailBounds.second
	);
	return 0;
}
