#include "tse_helpers.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {

	struct CoreLayout {
		int accountCore;
		int simulatorCore;
		int sma50Core;
		int sma200Core;
		int toLongCore;
		int toShortCore;
	};

	tse::Summary runAapl
	(
		std::string const& label,
		CoreLayout const& layout,
		std::vector<tse::OhlcvTick> const& rows
	)
	{
		tse::Account account {label, tse::StorageRegime::mem, tse::Currency::usd, layout.accountCore};
		tse::Market const market {account.createMarket("OHLCV", tse::MdType::ohlcv)};
		account.createSimulator("Sim", helpers::simulatorConfig(), 3, layout.simulatorCore);
		account.addContract("AAPL", 1, tse::Instrument::equity, tse::Underlying::undefined, tse::Venue::undefined, 100000);
		// A classic trend follower: hold long while the fifty-day average stands above the two-hundred-day one, step aside when it falls back.
		account.addInputOhlcv("SMA50", 50, tse::Duration::days, helpers::makeSma(50), market, {"AAPL"}, layout.sma50Core);
		account.addInputOhlcv("SMA200", 200, tse::Duration::days, helpers::makeSma(200), market, {"AAPL"}, layout.sma200Core);
		account.addPatternCrossover("ToLong", tse::Duration::days, {"SMA50", "SMA200"}, tse::Cmp::ge, layout.toLongCore);
		account.addPatternCrossover("ToShort", tse::Duration::days, {"SMA50", "SMA200"}, tse::Cmp::lt, layout.toShortCore);
		account.addRuleMarket("Entry", tse::RuleType::entry, helpers::entryParams(100.0), "ToLong", "AAPL");
		account.addRuleMarket("Exit", tse::RuleType::exit, helpers::exitParams(), "ToShort", "AAPL");
		account.addRobot("Strat", {"Entry", "Exit"});
		account.start("Strat");
		for (tse::OhlcvTick const& tick : rows) {
			market.pushOhlcv("AAPL", tick);
		}
		return account.getSummary();
	}

}

int main()
{
	tse::setLogLevel(tse::LogLevel::none);
	std::vector<tse::OhlcvTick> const rows {helpers::loadAapl()};

	// The trailing number is the CPU core that piece of the strategy is nailed to; -1 leaves the choice to the operating system.
	CoreLayout const
		unpinnedLayout {-1, -1, -1, -1, -1, -1},
		pinnedLayout {-1, -1, 0, 1, 0, 1};

	// Affinity buys latency, never a different trade: the same data through both layouts must give the same profit and the same trade count.
	tse::Summary const
		unpinned {runAapl("AffinityUnpinned", unpinnedLayout, rows)},
		pinned {runAapl("AffinityPinned", pinnedLayout, rows)};

	std::printf
	(
		"unpinned=%.4f/%lld pinned=%.4f/%lld\n",
		unpinned.totalNetProfit,
		static_cast<long long>(unpinned.totalNumberOfTrades),
		pinned.totalNetProfit,
		static_cast<long long>(pinned.totalNumberOfTrades)
	);
	return 0;
}
