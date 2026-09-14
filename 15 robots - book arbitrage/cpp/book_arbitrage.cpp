#include "tse_helpers.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

	struct SpreadState final {
		double lastA;
		double lastB;
		bool haveA;
		bool haveB;
	};

	struct BookSideState final {
		double sb;
		double sa;
	};

	tse::BookMessage newOrder(std::int64_t tsNanoseconds, tse::Side side, double price, double quantity);
	tse::TradeTick tradeTick(std::int64_t tsNanoseconds, double price);
	tse::FormulaProcessor spreadFormula(int mode);
	tse::BookMessage bookStep
	(
		BookSideState& state,
		double target,
		double bidPrice,
		double askPrice,
		std::int64_t tsNanoseconds
	);

}

int main()
{
	tse::setLogLevel(tse::LogLevel::none);

	tse::Account account {"BookArbitrage", tse::StorageRegime::mem};
	tse::Market const
	bookMktA {account.createMarket("book A", tse::MdType::book)},
	bookMktB {account.createMarket("book B", tse::MdType::book)},
	priceMkt {account.createMarket("price C", tse::MdType::trade)};
	account.createSimulator("sim", helpers::simulatorConfig(), 64, -1);
	account.addContract("BOOKA", 1, tse::Instrument::equity, tse::Underlying::equity, tse::Venue::undefined, 50000);
	account.addContract("BOOKB", 1, tse::Instrument::equity, tse::Underlying::equity, tse::Venue::undefined, 50000);
	account.addContract("TRADEC", 1, tse::Instrument::equity, tse::Underlying::equity, tse::Venue::undefined, 50000);
	tse::Book const
	bookA {account.createBook("BOOKA", tse::BookLevelKind::l2)},
	bookB {account.createBook("BOOKB", tse::BookLevelKind::l3)};

	bookA.apply(newOrder(1000000LL, tse::Side::long_, 99.95, 6.0));
	bookA.apply(newOrder(2000000LL, tse::Side::short_, 100.05, 6.0));
	bookB.apply(newOrder(3000000LL, tse::Side::long_, 99.95, 6.0));
	bookB.apply(newOrder(4000000LL, tse::Side::short_, 100.05, 6.0));

	account.addInputBookImbalance("ImbA", 4, tse::Duration::nanoseconds, bookA, bookMktA, {"BOOKA"});
	account.addInputBookImbalance("ImbB", 4, tse::Duration::nanoseconds, bookB, bookMktB, {"BOOKB"});

	// The same asset is quoted on two venues. Neither book's imbalance is a signal on its own:
	// the strategy waits for the two to disagree and bets that the disagreement closes.
	account.addPatternFormula("SpreadEnterShort", tse::Duration::nanoseconds, {"ImbA", "ImbB"}, spreadFormula(0));
	account.addPatternFormula("SpreadEnterLong", tse::Duration::nanoseconds, {"ImbA", "ImbB"}, spreadFormula(1));
	account.addPatternFormula("SpreadExit", tse::Duration::nanoseconds, {"ImbA", "ImbB"}, spreadFormula(2));

	tse::RuleParams const
	enterShortParams
	{
		tse::QuantityMode::fixed, 100.0,
		tse::PriceType::market, 0.0, 0.0, 0.0,
		tse::Side::short_, tse::Side::neutral, tse::Tif::day, 10
	},
	exitShortParams
	{
		tse::QuantityMode::fixed, 100.0,
		tse::PriceType::market, 0.0, 0.0, 0.0,
		tse::Side::long_, tse::Side::short_, tse::Tif::day, 10
	},
	enterLongParams
	{
		tse::QuantityMode::fixed, 100.0,
		tse::PriceType::market, 0.0, 0.0, 0.0,
		tse::Side::long_, tse::Side::neutral, tse::Tif::day, 10
	},
	exitLongParams
	{
		tse::QuantityMode::fixed, 100.0,
		tse::PriceType::market, 0.0, 0.0, 0.0,
		tse::Side::short_, tse::Side::long_, tse::Tif::day, 10
	};

	// The signal is read off the two books, but the position is taken in a third, liquid contract.
	account.addRuleMarket("EnterShortC", tse::RuleType::entry, enterShortParams, "SpreadEnterShort", "TRADEC");
	account.addRuleMarket("ExitShortC", tse::RuleType::exit, exitShortParams, "SpreadExit", "TRADEC");
	account.addRuleMarket("EnterLongC", tse::RuleType::entry, enterLongParams, "SpreadEnterLong", "TRADEC");
	account.addRuleMarket("ExitLongC", tse::RuleType::exit, exitLongParams, "SpreadExit", "TRADEC");

	account.addRobot("Robot", {"EnterShortC", "ExitShortC", "EnterLongC", "ExitLongC"});
	account.portfolioSubscribe(priceMkt, "TRADEC");
	account.start("Robot");

	std::vector<double> const
	targetsA {0.125, 0.3, 0.0, -0.3, 0.0, 0.3, 0.0, -0.3, 0.0, 0.3, 0.0, -0.3, 0.0},
	targetsB {-0.125, -0.3, 0.0, 0.3, 0.0, -0.3, 0.0, 0.3, 0.0, -0.3, 0.0, 0.3, 0.0},
	pricesC {100, 102, 100, 98, 100, 102, 100, 98, 100, 102, 100, 98, 100};
	std::int64_t constexpr
	base {1000000000LL},
	qOff {250000000LL},
	half {500000000LL};
	BookSideState
	stateA {6.0, 6.0},
	stateB {6.0, 6.0};

	for (std::size_t i {0}; i < targetsA.size(); ++i) {
		std::int64_t const step {static_cast<std::int64_t>(i) + 1};
		bookMktA.pushBook("BOOKA", bookStep(stateA, targetsA[i], 99.95, 100.05, base * (2 * step + 1)));
		bookMktB.pushBook("BOOKB", bookStep(stateB, targetsB[i], 99.95, 100.05, base * (2 * step + 1) + qOff));
		priceMkt.pushTrade("TRADEC", tradeTick(base * (2 * step + 1) + half, pricesC[i]));
	}

	tse::Summary const summary {account.getSummary()};
	long long const trades {static_cast<long long>(summary.totalNumberOfTrades)};

	std::printf("netProfit=%.4f trades=%lld\n", summary.totalNetProfit, trades);
	return 0;
}

namespace {

	std::uint64_t messageCounter {0};

	tse::BookMessage newOrder
	(
		std::int64_t tsNanoseconds,
		tse::Side side,
		double price,
		double quantity
	)
	{
		++messageCounter;
		tse::BookMessage message;
		message.kind = tse::BookMessageKind::new_;
		message.tsNanoseconds = tsNanoseconds;
		message.messageId = messageCounter;
		message.orderId = messageCounter;
		message.price = price;
		message.quantity = quantity;
		message.txnSide = side;
		return message;
	}

	tse::TradeTick tradeTick(std::int64_t tsNanoseconds, double price)
	{
		return tse::TradeTick {tsNanoseconds, price, 1.0, tse::Side::trade};
	}

	tse::FormulaProcessor spreadFormula(int mode)
	{
		std::shared_ptr<SpreadState> state {new SpreadState {0.0, 0.0, false, false}};
		return [state, mode](std::string const& inputLabel, std::int64_t, double value) -> bool
		{
			if (inputLabel == "ImbA") {
				state->lastA = value;
				state->haveA = true;
			}
			else if (inputLabel == "ImbB") {
				state->lastB = value;
				state->haveB = true;
			}
			if (not (state->haveA and state->haveB)) {
				return false;
			}
			double const spread {state->lastA - state->lastB};
			// Enter when the two books disagree by more than 0.3, and leave once the gap
			// has collapsed back into a narrow band around zero.
			if (mode == 0) {
				return spread > 0.3;
			}
			if (mode == 1) {
				return spread < -0.3;
			}
			return spread < 0.2 and spread > -0.2;
		};
	}

	tse::BookMessage bookStep
	(
		BookSideState& state,
		double target,
		double bidPrice,
		double askPrice,
		std::int64_t tsNanoseconds
	)
	{
		double const curr {(state.sb - state.sa) / (state.sb + state.sa)};
		if (target > curr) {
			double const delta {(state.sa * (1.0 + target) / (1.0 - target)) - state.sb};
			state.sb += delta;
			return newOrder(tsNanoseconds, tse::Side::long_, bidPrice, delta);
		}
		double const delta {(state.sb * (1.0 - target) / (1.0 + target)) - state.sa};
		state.sa += delta;
		return newOrder(tsNanoseconds, tse::Side::short_, askPrice, delta);
	}

}
