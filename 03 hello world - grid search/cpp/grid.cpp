#include "tse_helpers.hpp"

#include <cstdio>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

int main()
{
	std::vector<tse::OhlcvTick> const ticks {helpers::loadAapl()};
	tse::setLogLevel(tse::LogLevel::none);

	// How many days the fast average looks back. Too short and every wiggle flips the position,
	// too long and the trend is half over before the strategy joins it: let the data pick.
	std::vector<double> const params {10.0, 20.0, 50.0, 100.0};

	std::mutex threadsMutex;
	std::set<std::thread::id> workerThreads;

	// Grid points are independent backtests, so the engine runs them side by side on a pool of
	// worker threads; every run notes down the thread it happened to land on.
	std::vector<tse::GridResult> const results {tse::runGrid
	(
		"AAPLGrid",
		tse::StorageRegime::mem,
		params,
		[&ticks, &threadsMutex, &workerThreads](tse::Account& account, double paramValue)
		{
			{
				std::lock_guard<std::mutex> const lock {threadsMutex};
				workerThreads.insert(std::this_thread::get_id());
			}
			int const shortPeriod {static_cast<int>(paramValue)};
			tse::Market const market {account.createMarket("MD", tse::MdType::ohlcv)};
			account.createSimulator("Sim", helpers::simulatorConfig());
			account.addContract("AAPL", 1, tse::Instrument::equity, tse::Underlying::undefined, tse::Venue::undefined, 100000);
			account.addInputOhlcv("SMAShort", shortPeriod, tse::Duration::days, helpers::makeSma(shortPeriod), market, {"AAPL"});
			account.addInputOhlcv("SMA200", 200, tse::Duration::days, helpers::makeSma(200), market, {"AAPL"});
			// Hold the stock while the fast average stays above the 200 day trend, stand aside once it drops back under.
			account.addPatternCrossover("ToLong", tse::Duration::days, {"SMAShort", "SMA200"}, tse::Cmp::ge);
			account.addPatternCrossover("ToShort", tse::Duration::days, {"SMAShort", "SMA200"}, tse::Cmp::lt);
			account.addRuleMarket("Entry", tse::RuleType::entry, helpers::entryParams(100.0), "ToLong", "AAPL");
			account.addRuleMarket("Exit", tse::RuleType::exit, helpers::exitParams(), "ToShort", "AAPL");
			account.addRobot("Strat", {"Entry", "Exit"});
			account.start("Strat");
			for (tse::OhlcvTick const& tick : ticks) {
				market.pushOhlcv("AAPL", tick);
			}
		},
		tse::Currency::usd
	)};

	tse::GridResult const* best {&results.front()};
	for (tse::GridResult const& result : results) {
		std::printf
		(
			"  SMA(%d) x SMA(200): netProfit=%.4f trades=%lld\n",
			static_cast<int>(result.paramValue),
			result.summary.totalNetProfit,
			static_cast<long long>(result.summary.totalNumberOfTrades)
		);
		if (result.summary.totalNetProfit > best->summary.totalNetProfit) {
			best = &result;
		}
	}

	std::printf
	(
		"best=SMA(%d) netProfit=%.4f workerThreads=%zu\n",
		static_cast<int>(best->paramValue),
		best->summary.totalNetProfit,
		workerThreads.size()
	);
	return 0;
}
