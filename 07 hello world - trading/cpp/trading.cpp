#include "tse_helpers.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

	double const brokerFee {0.5};
	std::int64_t const brokerLatencyNanoseconds {1000000LL};

	// The broker is one of the two ends this example replaces. It keeps the last price it was told about and
	// fills in full whatever the robot sends it, a millisecond later, at that price - charging its fee.
	struct Broker final {
		double       lastPrice;
		std::int64_t lastTsNanoseconds;
		std::size_t  orderCount;
		std::size_t  fillCount;
	};

	void transmit
	(
		Broker& broker,
		tse::Execution const& execution,
		tse::Order const& order
	)
	{
		++broker.orderCount;
		if (order.quantity <= 0.0) {
			return;
		}
		execution.applyFill
		(
			order.clientOrderId,
			broker.lastPrice,
			order.quantity,
			brokerFee,
			broker.lastTsNanoseconds + brokerLatencyNanoseconds
		);
		++broker.fillCount;
	}

	// The client is the other such end. Live work has no file to walk over: it has a subscription that hands
	// over one bar at a time, and the same bar tells the broker where the market is. The recorded bars of
	// example 02 stand here for that subscription.
	struct MarketDataClient final {

		tse::Market const& market;
		std::string const  symbol;
		Broker&            broker;

		std::size_t replay(std::vector<tse::OhlcvTick> const& subscription) const &
		{
			for (tse::OhlcvTick const& bar : subscription) {
				broker.lastPrice = bar.close;
				broker.lastTsNanoseconds = bar.tsNanoseconds;
				market.pushOhlcv(symbol, bar);
			}
			return subscription.size();
		}
	};

}

int main()
{
	std::vector<tse::OhlcvTick> const rows {helpers::loadAapl()};
	Broker broker {0.0, 0, 0u, 0u};

	tse::Account account {"AAPL", tse::StorageRegime::mem};
	tse::setLogLevel(tse::LogLevel::none);
	tse::Market const market {account.createMarket("MD", tse::MdType::ohlcv)};
	// Example 02 runs this very strategy against the built-in Simulator. Going live means the broker, not the
	// engine, decides how an order fills, so the Simulator gives way to a custom execution - and that is the
	// only change on this end.
	tse::Execution const brokerExecution
	{
		account.createCustom
		(
			"BROKER",
			[&broker](tse::Execution const& execution, tse::Order const& order)
			{
				transmit(broker, execution, order);
			},
			8, -1
		)
	};
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
	// A custom execution is started and stopped by whoever created it; the engine never does it for you.
	brokerExecution.start();

	MarketDataClient const client {market, "AAPL", broker};
	std::size_t const bars {client.replay(rows)};

	brokerExecution.stop();

	tse::Summary const summary {account.getSummary()};
	tse::PositionState const position {account.getPositionState("AAPL")};
	std::printf
	(
		"go live: bars=%zu orders=%zu fills=%zu executed=%d netProfit=%.4f trades=%lld finalQuantity=%.1f\n",
		bars,
		broker.orderCount,
		broker.fillCount,
		brokerExecution.getCount(),
		summary.totalNetProfit,
		static_cast<long long>(summary.totalNumberOfTrades),
		position.quantity
	);
	return 0;
}
