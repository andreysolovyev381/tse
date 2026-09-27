#include "tse_helpers.hpp"

#include <cstddef>
#include <cstdio>
#include <vector>

namespace {

	struct Combination final {
		int shortPeriod;
		int longPeriod;
	};

}

int main()
{
	std::vector<tse::OhlcvTick> const ticks {helpers::loadAapl()};
	tse::setLogLevel(tse::LogLevel::none);

	// Every short lookback is tried against every long one; the grid is the list of their combinations.
	std::vector<int> const
		shortPeriods {10, 20, 50},
		longPeriods {100, 200};

	std::vector<Combination> combinations;
	std::vector<double> indices;
	for (int const shortPeriod : shortPeriods) {
		for (int const longPeriod : longPeriods) {
			indices.push_back(static_cast<double>(combinations.size()));
			combinations.push_back(Combination {shortPeriod, longPeriod});
		}
	}

	// run_grid hands the builder one number per backtest, so the number is the index of a combination and the builder looks the combination up.
	std::vector<tse::GridResult> const results
	{
		tse::runGrid
		(
			"AAPLGrid",
			tse::StorageRegime::mem,
			indices,
			[&ticks, &combinations](tse::Account& account, double index)
			{
				Combination const& combination {combinations[static_cast<std::size_t>(index)]};
				tse::Market const market {account.createMarket("MD", tse::MdType::ohlcv)};
				account.createSimulator("Sim", helpers::simulatorConfig());
				account.addContract("AAPL", 1, tse::Instrument::equity, tse::Underlying::undefined, tse::Venue::undefined, 100000);
				account.addInputOhlcv("SMAShort", combination.shortPeriod, tse::Duration::days, helpers::makeSma(combination.shortPeriod), market, {"AAPL"});
				account.addInputOhlcv("SMALong", combination.longPeriod, tse::Duration::days, helpers::makeSma(combination.longPeriod), market, {"AAPL"});
				account.addPatternCrossover("ToLong", tse::Duration::days, {"SMAShort", "SMALong"}, tse::Cmp::ge);
				account.addPatternCrossover("ToShort", tse::Duration::days, {"SMAShort", "SMALong"}, tse::Cmp::lt);
				account.addRuleMarket("Entry", tse::RuleType::entry, helpers::entryParams(100.0), "ToLong", "AAPL");
				account.addRuleMarket("Exit", tse::RuleType::exit, helpers::exitParams(), "ToShort", "AAPL");
				account.addRobot("Strat", {"Entry", "Exit"});
				account.start("Strat");
				for (tse::OhlcvTick const& tick : ticks) {
					market.pushOhlcv("AAPL", tick);
				}
			},
			tse::Currency::usd
		)
	};

	tse::GridResult const* best {&results.front()};
	for (tse::GridResult const& result : results) {
		Combination const& combination {combinations[static_cast<std::size_t>(result.paramValue)]};
		std::printf
		(
			"  SMA(%d) x SMA(%d): netProfit=%.4f trades=%lld\n",
			combination.shortPeriod,
			combination.longPeriod,
			result.summary.totalNetProfit,
			static_cast<long long>(result.summary.totalNumberOfTrades)
		);
		if (result.summary.totalNetProfit > best->summary.totalNetProfit) {
			best = &result;
		}
	}

	Combination const& winner {combinations[static_cast<std::size_t>(best->paramValue)]};
	std::printf
	(
		"best=SMA(%d) x SMA(%d) netProfit=%.4f\n",
		winner.shortPeriod,
		winner.longPeriod,
		best->summary.totalNetProfit
	);
	return 0;
}
