#include "tse_helpers.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

	double const brokerFee {0.5};
	std::int64_t const brokerLatencyNanoseconds {1000000LL};

	struct BrokerState final {
		double       marketPrice;
		std::int64_t marketTsNanoseconds;
		std::size_t  orderCount;
		std::size_t  fillCount;
	};

	void reportFill
	(
		BrokerState& state,
		tse::Execution const& execution,
		tse::Order const& order,
		double const quantity,
		std::int64_t const delayNanoseconds
	)
	{
		execution.applyFill(order.clientOrderId, state.marketPrice, quantity, brokerFee, state.marketTsNanoseconds + delayNanoseconds);
		++state.fillCount;
	}

	void transmit
	(
		BrokerState& state,
		tse::Execution const& execution,
		tse::Order const& order
	)
	{
		++state.orderCount;
		if (order.quantity <= 0.0) {
			return;
		}
		// a real broker rarely fills a whole order at once, so here every second order comes back as two trades
		if (state.orderCount % 2u == 0u) {
			double const half {order.quantity / 2.0};
			reportFill(state, execution, order, half, brokerLatencyNanoseconds);
			reportFill(state, execution, order, order.quantity - half, 2 * brokerLatencyNanoseconds);
		}
		else {
			reportFill(state, execution, order, order.quantity, brokerLatencyNanoseconds);
		}
	}

}

int main()
{
	tse::setLogLevel(tse::LogLevel::none);
	std::vector<tse::TradeTick> const ticks {helpers::loadTrades("wti_trades.csv", tse::Side::neutral)};
	BrokerState state {0.0, 0, 0u, 0u};

	tse::Account account {"GoLiveExample", tse::StorageRegime::mem};
	tse::Market const realMarketData {account.createMarket("REAL_MARKET_DATA", tse::MdType::trade)};
	// going live means the broker, not the engine, decides how an order fills
	tse::Execution const brokerExecution
	{
		account.createCustom
		(
			"BROKER_EXECUTION",
			[&state](tse::Execution const& execution, tse::Order const& order)
			{
				transmit(state, execution, order);
			},
			8, -1
		)
	};

	account.addContract("WTI", 1, tse::Instrument::future, tse::Underlying::commodity, tse::Venue::undefined, 100000);
	account.addInputTrade
	(
		"Flow", 4, tse::Duration::nanoseconds,
		[](tse::Storage const& storage, std::string const&, tse::TradeTick const& tick) -> bool
		{
			if (tick.volume <= 0.0) {
				return false;
			}
			storage.push(tick.tsNanoseconds, tick.volume);
			return true;
		},
		realMarketData, {"WTI"}
	);
	// no edge here: any trade that carries volume is a buy signal, and the order size is that same volume
	account.addPatternFormula
	(
		"EnterSignal", tse::Duration::nanoseconds, {"Flow"},
		[](std::string const&, std::int64_t, double value) -> bool
		{
			return value > 0.0;
		}
	);
	account.addRuleMarket
	(
		"Enter", tse::RuleType::entry,
		tse::RuleParams
		{
			tse::QuantityMode::from_signal, 0.0,
			tse::PriceType::market, 0.0, 0.0, 0.0,
			tse::Side::long_, tse::Side::neutral, tse::Tif::day, 10
		},
		"EnterSignal", "WTI"
	);
	account.addRobot("GoLive", {"Enter"});
	account.portfolioSubscribe(realMarketData, "WTI");
	account.start("GoLive");

	for (tse::TradeTick const& tick : ticks) {
		state.marketPrice = tick.price;
		state.marketTsNanoseconds = tick.tsNanoseconds;
		realMarketData.pushTrade("WTI", tick);
	}

	tse::PositionState const position {account.getPositionState("WTI")};
	std::printf
	(
		"go live: replayed=%zu orders=%zu fills=%zu executed=%d finalQuantity=%.1f\n",
		ticks.size(),
		state.orderCount,
		state.fillCount,
		brokerExecution.getCount(),
		position.quantity
	);
	return 0;
}
