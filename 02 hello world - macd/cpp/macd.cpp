#include "tse_helpers.hpp"

#include <cstdio>
#include <string>
#include <vector>

int main()
{
	std::vector<tse::OhlcvTick> const rows {helpers::loadAapl()};

	tse::Account account {"AAPL", tse::StorageRegime::mem};
	tse::setLogLevel(tse::LogLevel::none);
	tse::Market const market {account.createMarket("MD", tse::MdType::ohlcv)};
	account.createSimulator("Sim", helpers::simulatorConfig());
	account.addContract("AAPL", 1, tse::Instrument::equity, tse::Underlying::undefined, tse::Venue::undefined, 100000);

	// MACD is the distance between a fast and a slow average of the close: it is positive while the recent
	// days are stronger than the older ones, and negative when they fade. The averages are meaningless until
	// the slow one has seen enough days, so the indicator declares itself ready only then and no rule fires before that.
	int const
		fast {12},
		slow {26};
	tse::InputProcessor const macd
	{
		[alphaFast {2.0 / (fast + 1)}, alphaSlow {2.0 / (slow + 1)}, slow, emaFast {0.0}, emaSlow {0.0}, seeded {false}]
		(tse::Storage const& storage, std::string const&, tse::OhlcvTick const& tick) mutable -> bool
		{
			emaFast = seeded ? emaFast + alphaFast * (tick.close - emaFast) : tick.close;
			emaSlow = seeded ? emaSlow + alphaSlow * (tick.close - emaSlow) : tick.close;
			seeded = true;
			storage.push(tick.tsNanoseconds, emaFast - emaSlow);
			return storage.size() >= static_cast<std::size_t>(slow);
		}
	};
	account.addInputOhlcv("MACD", slow, tse::Duration::days, macd, market, {"AAPL"});
	// Buy 100 shares once the momentum turns positive and sell the whole position when it turns negative.
	account.addPatternThreshold("ToLong", tse::Duration::days, {"MACD"}, tse::Cmp::ge, 0.0);
	account.addPatternThreshold("ToShort", tse::Duration::days, {"MACD"}, tse::Cmp::lt, 0.0);
	account.addRuleMarket("Entry", tse::RuleType::entry, helpers::entryParams(100.0), "ToLong", "AAPL");
	account.addRuleMarket("Exit", tse::RuleType::exit, helpers::exitParams(), "ToShort", "AAPL");
	account.addRobot("Strat", {"Entry", "Exit"});
	account.start("Strat");

	for (tse::OhlcvTick const& tick : rows) {
		market.pushOhlcv("AAPL", tick);
	}

	tse::Summary const summary {account.getSummary()};
	std::printf("netProfit=%.4f trades=%lld\n", summary.totalNetProfit, static_cast<long long>(summary.totalNumberOfTrades));
	return 0;
}
