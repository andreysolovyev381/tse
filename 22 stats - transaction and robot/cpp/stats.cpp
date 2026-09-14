#include "tse_helpers.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

	char const* sideName(tse::Side side) noexcept
	{
		switch (side) {
			case tse::Side::neutral: { return "Neutral"; }
			case tse::Side::long_:   { return "Long"; }
			case tse::Side::short_:  { return "Short"; }
			default:                 { return "Undefined"; }
		}
	}

}

int main()
{
	std::vector<tse::OhlcvTick> const ticks {tse::Account::loadOhlcvCsv(helpers::dataPath("WTI_OHLCVminute_sept2016.csv"))};
	tse::setLogLevel(tse::LogLevel::none);
	double const startingEquity {100000.0};

	tse::Account account {"Stats", tse::StorageRegime::mem};
	tse::Market const market {account.createMarket("OHLCV", tse::MdType::ohlcv)};
	tse::Execution const execution {account.createSimulator("Sim", helpers::simulatorConfig())};

	account.addContract("WTI", 1, tse::Instrument::future, tse::Underlying::commodity, tse::Venue::undefined, 100000);
	account.addInputOhlcv("SMA3", 3, tse::Duration::days, helpers::makeSma(3), market, {"WTI"});
	account.addInputOhlcv("SMA10", 10, tse::Duration::days, helpers::makeSma(10), market, {"WTI"});
	// Hold WTI while the three day average stays above the ten day one, stand aside when it drops back under.
	account.addPatternCrossover("ToLong", tse::Duration::days, {"SMA3", "SMA10"}, tse::Cmp::ge);
	account.addPatternCrossover("ToShort", tse::Duration::days, {"SMA3", "SMA10"}, tse::Cmp::lt);
	account.addRuleMarket("Entry", tse::RuleType::entry, helpers::entryParams(580.0), "ToLong", "WTI");
	account.addRuleMarket("Exit", tse::RuleType::exit, helpers::exitParams(), "ToShort", "WTI");
	account.addRobot("Strat", {"Entry", "Exit"});
	account.setAccountEquity(startingEquity, 0.0);
	account.start("Strat");

	for (tse::OhlcvTick const& tick : ticks) {
		market.pushOhlcv("WTI", tick);
	}

	// The first kind of statistics: the robot level totals for the whole run, and next to them the
	// transaction level records the blotter kept, one row per fill.
	tse::Summary const summary {account.getSummary()};
	std::vector<tse::Trade> const trades {account.getTrades()};
	std::printf("retained transactions: %zu execution count: %d\n", trades.size(), execution.getCount());
	for (std::size_t i {0}; i < trades.size() and i < 5; ++i) {
		tse::Trade const& trade {trades[i]};
		std::printf
		(
			"  %s price=%.4f quantity=%.0f bookedPL=%.4f side=%s\n",
			trade.symbol.c_str(),
			trade.price,
			trade.quantity,
			trade.bookedPL,
			sideName(trade.txnSide)
		);
	}

	// The second kind: ex_post cuts the very same run into day buckets and scores each bucket, so a
	// model can study how the robot behaved through time instead of one number at the end.
	tse::ExPost const exPost {account.createExPost(tse::Duration::days, -1, 5)};
	std::size_t const
	paramCount {tse::ExPost::paramCount()},
	buckets {exPost.bucketCount(0)};

	std::filesystem::path const dbPath {std::filesystem::temp_directory_path() / "tse_ex06_cpp.sqlite3.db"};
	helpers::cleanup(dbPath.string());
	account.exPostSave(dbPath.string(), tse::Duration::days, -1, 5);
	std::size_t const savedRobots {account.exPostLoad(dbPath.string())};
	helpers::cleanup(dbPath.string());

	std::printf
	(
		"netProfit=%.4f trades=%lld retained=%zu buckets=%zu params=%zu savedRobots=%zu\n",
		summary.totalNetProfit,
		static_cast<long long>(summary.totalNumberOfTrades),
		trades.size(),
		buckets,
		paramCount,
		savedRobots
	);
	return 0;
}
