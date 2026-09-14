#include "tse_helpers.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

int main()
{
	std::error_code ec {};
	std::filesystem::path const base {std::filesystem::temp_directory_path() / "tse_example_initial_params_cpp"};
	std::filesystem::remove_all(base, ec);
	std::filesystem::path const
		dataA {base / "data_a"},
		dataB {base / "data_b"},
		logs {base / "logs" / "run" / "day_01"};
	std::filesystem::create_directories(dataA, ec);

	// An existing folder is taken as it stands, a missing one is created for you, the whole path at once.
	tse::setInitialParams(tse::InitialParams {dataA.string(), logs.string(), tse::LogLevel::none, true});

	{
		// An account captures the data folder at construction, so a later switch never moves
		// the blotter of an account that is already trading: only the next account sees dataB.
		// In the db regime that blotter is a file of the configured folder, named after the account.
		tse::Account const capturedA {"CapturedA", tse::StorageRegime::db};
		tse::setInitialParams(tse::InitialParams {dataB.string(), "", tse::LogLevel::none, false});
		tse::Account const capturedB {"CapturedB", tse::StorageRegime::db};
	}

	bool const
		blotterInDataA {std::filesystem::exists(dataA / "CapturedA_Blotter_retained_trades.sqlite3.db", ec)},
		blotterInDataB {std::filesystem::exists(dataB / "CapturedB_Blotter_retained_trades.sqlite3.db", ec)},
		nestedLogFolder {std::filesystem::is_directory(logs, ec)};

	// A file standing where a folder is expected is refused, and the reason waits in the init error channel.
	std::filesystem::path const aFile {base / "not_a_dir.txt"};
	std::ofstream {aFile} << "x";
	tse::setInitialParams(tse::InitialParams {aFile.string(), "", tse::LogLevel::none, false});
	std::printf("data folder refused: %s\n", tse::lastInitError().c_str());

	std::filesystem::remove_all(base, ec);
	std::printf("blotterInDataA=%d blotterInDataB=%d nestedLogFolder=%d\n", blotterInDataA, blotterInDataB, nestedLogFolder);
	return 0;
}
