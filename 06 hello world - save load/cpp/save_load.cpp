#include "tse_helpers.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

int main()
{
	tse::setLogLevel(tse::LogLevel::none);
	// the python leg of this example saved the recipe here; this leg only reads it back and trades it
	std::string const dbPath {(std::filesystem::temp_directory_path() / "tse_docs_ex06.db").string()};

	tse::Account account {"WTI-Loader", tse::StorageRegime::mem};
	// a recipe remembers the name of every processor but never its code, so the callbacks are supplied again
	account.registerInputProcessor("sma3", helpers::makeSma(3));
	account.registerInputProcessor("sma10", helpers::makeSma(10));
	account.load("Strat", dbPath);

	tse::Market const market {account.createMarket("MD", tse::MdType::ohlcv)};
	account.createSimulator("Sim", helpers::simulatorConfig());
	// market data is never part of a recipe: today's adapter is attached to the restored inputs
	account.bindInput("WTI SMA3", market);
	account.bindInput("WTI SMA10", market);
	account.start("Strat");

	std::vector<tse::OhlcvTick> const ticks
		{tse::Account::loadOhlcvCsv(helpers::dataPath("WTI_OHLCVminute_sept2016.csv"))};
	for (tse::OhlcvTick const& tick : ticks) {
		market.pushOhlcv("WTI", tick);
	}

	tse::Summary const summary {account.getSummary()};

	std::printf
	(
		"loaded \"Strat\" from %s (schema v3): netProfit=%.4f trades=%lld\n",
		dbPath.c_str(),
		summary.totalNetProfit,
		static_cast<long long>(summary.totalNumberOfTrades)
	);
	return 0;
}
