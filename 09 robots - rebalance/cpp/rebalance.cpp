#include "tse_helpers.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

	std::string const symbol {"RBL"};

	std::int64_t constexpr enterCheckPoint {1000000000LL};
	std::int64_t constexpr rebalanceCheckPoint {2000000000LL};
	std::int64_t constexpr rebalanceCoolDown {1000000000LL};
	std::int64_t constexpr hugeCoolDown {1000000000000000LL};
	std::int64_t constexpr drainOffset {100000000LL};

	double constexpr entryQuantity {4.0};
	double constexpr rebalanceQuantity {2.0};
	double constexpr entryPrice {100.0};
	double constexpr firstRebalancePrice {103.0};
	double constexpr secondRebalancePrice {108.0};
	double constexpr rebalanceLimitPrice {110.0};

	tse::TradeInputProcessor priceProcessor()
	{
		return []
		(
			tse::Storage const& storage,
			std::string const&,
			tse::TradeTick const& tick
		) -> bool
		{
			storage.push(tick.tsNanoseconds, tick.price);
			return true;
		};
	}

	tse::RuleParams makeRuleParams
	(
		double const quantity,
		double const limitPrice,
		tse::Side const txnSide,
		tse::Side const posSide
	)
	{
		return tse::RuleParams
		{
			tse::QuantityMode::fixed, quantity,
			tse::PriceType::limit, limitPrice, 0.0, 0.0,
			txnSide, posSide, tse::Tif::day, 10
		};
	}

	void pushPrice
	(
		tse::Market const& market,
		std::int64_t const tsNanoseconds,
		double const price
	)
	{
		market.pushTrade(symbol, tse::TradeTick {tsNanoseconds, price, 1.0, tse::Side::trade});
	}

}//!namespace

int main()
{
	tse::setLogLevel(tse::LogLevel::none);

	tse::Account account {"RebalanceExample", tse::StorageRegime::mem};
	tse::Market const market {account.createMarket("price", tse::MdType::trade)};
	account.createSimulator("sim", helpers::simulatorConfig(), 64, -1);
	account.addContract(symbol, 1, tse::Instrument::future, tse::Underlying::commodity, tse::Venue::undefined, 100000);
	account.addInputTrade("Px", 2, tse::Duration::nanoseconds, priceProcessor(), market, {symbol});
	// The entry pattern carries a cool down it can never reach, so the position is opened once;
	// the rebalance pattern rearms one second after it fires and can therefore fire again.
	account.addPatternTimestamp("EnterAt", tse::Duration::nanoseconds, {"Px"}, enterCheckPoint, hugeCoolDown);
	account.addPatternTimestamp("RebalanceAt", tse::Duration::nanoseconds, {"Px"}, rebalanceCheckPoint, rebalanceCoolDown);
	account.addRuleMarket
	(
		"RuleEntry", tse::RuleType::entry,
		makeRuleParams(entryQuantity, entryPrice, tse::Side::long_, tse::Side::neutral),
		"EnterAt", symbol
	);
	// Every firing of a rebalance adds two more units on top of the position already held:
	// it is an additive chaining delta, not an instruction to bring the position to two.
	account.addRuleMarket
	(
		"RuleRebalance", tse::RuleType::rebalance,
		makeRuleParams(rebalanceQuantity, rebalanceLimitPrice, tse::Side::long_, tse::Side::long_),
		"RebalanceAt", symbol
	);
	account.addRobot("RebalanceRobot", {"RuleEntry", "RuleRebalance"});
	account.portfolioSubscribe(market, symbol);
	account.start("RebalanceRobot");

	pushPrice(market, enterCheckPoint, entryPrice);
	pushPrice(market, enterCheckPoint + drainOffset, entryPrice);

	pushPrice(market, rebalanceCheckPoint, firstRebalancePrice);
	pushPrice(market, rebalanceCheckPoint + drainOffset, firstRebalancePrice);

	pushPrice(market, rebalanceCheckPoint + rebalanceCoolDown, secondRebalancePrice);
	pushPrice(market, rebalanceCheckPoint + rebalanceCoolDown + drainOffset, secondRebalancePrice);

	// Four units at 100, two more at 103, two more at 108: the position grows to eight and the
	// acquisition price is re-averaged over every fill.
	tse::Summary const summary {account.getSummary()};
	tse::PositionState const finalState {account.getPositionState(symbol)};
	std::printf
	(
		"rebalance: position=%.0f acquisitionPrice=%.4f trades=%lld\n",
		finalState.quantity,
		finalState.acquisitionPrice,
		static_cast<long long>(summary.totalNumberOfTrades)
	);
	return 0;
}
