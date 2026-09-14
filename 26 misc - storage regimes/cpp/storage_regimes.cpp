#include "tse_helpers.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {

	tse::Summary runWti
	(
		std::string const& label,
		tse::StorageRegime regime
	)
	{
		tse::Account account {label, regime};
		tse::Market const market {account.createMarket("OHLCV", tse::MdType::ohlcv)};
		account.createSimulator("Sim", helpers::simulatorConfig());
		account.addContract("WTI", 1, tse::Instrument::future, tse::Underlying::commodity, tse::Venue::undefined, 100000);
		// A short-horizon trend follower on oil futures: three days of price against ten, long above and flat below.
		account.addInputOhlcv("SMA3", 3, tse::Duration::days, helpers::makeSma(3), market, {"WTI"});
		account.addInputOhlcv("SMA10", 10, tse::Duration::days, helpers::makeSma(10), market, {"WTI"});
		account.addPatternCrossover("ToLong", tse::Duration::days, {"SMA3", "SMA10"}, tse::Cmp::ge);
		account.addPatternCrossover("ToShort", tse::Duration::days, {"SMA3", "SMA10"}, tse::Cmp::lt);
		account.addRuleMarket("Entry", tse::RuleType::entry, helpers::entryParams(580.0), "ToLong", "WTI");
		account.addRuleMarket("Exit", tse::RuleType::exit, helpers::exitParams(), "ToShort", "WTI");
		account.addRobot("Strat", {"Entry", "Exit"});
		account.start("Strat");
		std::vector<tse::OhlcvTick> const ticks {tse::Account::loadOhlcvCsv(helpers::dataPath("WTI_OHLCVminute_sept2016.csv"))};
		for (tse::OhlcvTick const& tick : ticks) {
			market.pushOhlcv("WTI", tick);
		}
		return account.getSummary();
	}

}

int main()
{
	std::error_code ec {};
	std::filesystem::path const dataFolder {std::filesystem::temp_directory_path() / "tse_example_storage_regimes"};
	std::filesystem::remove_all(dataFolder, ec);
	std::filesystem::create_directories(dataFolder, ec);

	tse::setLogLevel(tse::LogLevel::none);
	tse::setInitialParams(tse::InitialParams {dataFolder.string(), "", tse::LogLevel::none, false});

	// The regime says only where the blotter lives, on disk in the folder above or in memory, and never changes what the strategy does.
	tse::Summary const
		memSummary {runWti("RegimesMem", tse::StorageRegime::mem)},
		dbSummary {runWti("RegimesDb", tse::StorageRegime::db)};

	std::filesystem::remove_all(dataFolder, ec);

	std::printf
	(
		"mem=%.4f/%lld db=%.4f/%lld\n",
		memSummary.totalNetProfit,
		static_cast<long long>(memSummary.totalNumberOfTrades),
		dbSummary.totalNetProfit,
		static_cast<long long>(dbSummary.totalNumberOfTrades)
	);
	return 0;
}
