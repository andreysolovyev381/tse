#include "tse_helpers.hpp"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

	std::uint64_t messageCounter {0};

	struct MmState final {
		std::size_t orderCount;
		bool unloading;
	};

	// Quoting against only every second client order is the naive defence against adverse selection:
	// the maker refuses to keep accumulating against a flow that may be informed.
	bool isEverySecondClientOrder(MmState const& state)
	{
		return state.orderCount % 2u == 0u;
	}

	tse::BookMessage newOrder
	(
		std::int64_t tsNanoseconds,
		tse::Side side,
		double price,
		double quantity
	)
	{
		tse::BookMessage message;
		message.kind = tse::BookMessageKind::new_;
		message.tsNanoseconds = tsNanoseconds;
		message.messageId = ++messageCounter;
		message.orderId = message.messageId;
		message.price = price;
		message.quantity = quantity;
		message.txnSide = side;
		return message;
	}

	tse::TradeTick tradeTick
	(
		std::int64_t tsNanoseconds,
		double price
	)
	{
		return tse::TradeTick {tsNanoseconds, price, 1.0, tse::Side::trade};
	}

	tse::RuleParams fromSignalParams
	(
		tse::Side txnSide,
		tse::Side posSide
	)
	{
		return tse::RuleParams
		{
			tse::QuantityMode::from_signal, 0.0,
			tse::PriceType::market, 0.0, 0.0, 0.0,
			txnSide, posSide, tse::Tif::day, 10
		};
	}

}

int main()
{
	tse::setLogLevel(tse::LogLevel::none);

	double const
		orderPrice {100.0},
		orderQuantity {5.0},
		upperThreshold {15.0},
		lowerThreshold {0.0};

	tse::Account account {"MarketMakerExample", tse::StorageRegime::mem};
	tse::Market const bookMkt {account.createMarket("MM client orders", tse::MdType::book)};
	tse::Market const priceMkt {account.createMarket("MM prices", tse::MdType::trade)};
	tse::Execution const exec {account.createSimulator("sim", helpers::simulatorConfig(), 64, -1)};
	account.addContract("MMTEST", 1, tse::Instrument::equity, tse::Underlying::equity, tse::Venue::undefined, 50000);

	MmState state {0u, false};

	account.addInputBook
	(
		"ClientSells", 4, tse::Duration::nanoseconds,
		[&state](tse::Storage const& storage, std::string const&, tse::BookMessage const& message) -> bool
		{
			if (message.kind != tse::BookMessageKind::new_) { return false; }
			if (message.txnSide != tse::Side::short_) { return false; }
			++state.orderCount;
			storage.push(message.tsNanoseconds, message.quantity);
			return true;
		},
		bookMkt, {"MMTEST"}
	);
	account.addInputBook
	(
		"ClientBuys", 4, tse::Duration::nanoseconds,
		[&state](tse::Storage const& storage, std::string const&, tse::BookMessage const& message) -> bool
		{
			if (message.kind != tse::BookMessageKind::new_) { return false; }
			if (message.txnSide != tse::Side::long_) { return false; }
			++state.orderCount;
			storage.push(message.tsNanoseconds, message.quantity);
			return true;
		},
		bookMkt, {"MMTEST"}
	);

	// Inventory management: the maker accumulates up to the upper band and then unwinds
	// all the way back to flat before it quotes that side again.
	std::function<void()> const refreshMode
	{
		[&state, &account, upperThreshold, lowerThreshold]() -> void
		{
			double const quantity {account.getPositionState("MMTEST").quantity};
			if (not state.unloading and quantity >= upperThreshold) { state.unloading = true; }
			if (state.unloading and quantity <= lowerThreshold) { state.unloading = false; }
		}
	};

	account.addPatternFormula
	(
		"MmOpenLong", tse::Duration::nanoseconds, {"ClientSells"},
		[&state, &account, &refreshMode](std::string const&, std::int64_t, double) -> bool
		{
			refreshMode();
			if (state.unloading) { return false; }
			if (not isEverySecondClientOrder(state)) { return false; }
			return account.getPositionState("MMTEST").side != tse::Side::short_;
		}
	);
	account.addPatternFormula
	(
		"MmCloseShort", tse::Duration::nanoseconds, {"ClientSells"},
		[&state, &account, &refreshMode](std::string const&, std::int64_t, double) -> bool
		{
			refreshMode();
			if (not state.unloading) { return false; }
			return account.getPositionState("MMTEST").side == tse::Side::short_;
		}
	);
	account.addPatternFormula
	(
		"MmOpenShort", tse::Duration::nanoseconds, {"ClientBuys"},
		[&state, &account, &refreshMode](std::string const&, std::int64_t, double) -> bool
		{
			refreshMode();
			if (state.unloading) { return false; }
			if (not isEverySecondClientOrder(state)) { return false; }
			return account.getPositionState("MMTEST").side != tse::Side::long_;
		}
	);
	account.addPatternFormula
	(
		"MmCloseLong", tse::Duration::nanoseconds, {"ClientBuys"},
		[&state, &account, &refreshMode](std::string const&, std::int64_t, double) -> bool
		{
			refreshMode();
			if (not state.unloading) { return false; }
			return account.getPositionState("MMTEST").side == tse::Side::long_;
		}
	);

	account.addRuleMarket("MmBuyOpen", tse::RuleType::entry, fromSignalParams(tse::Side::long_, tse::Side::neutral), "MmOpenLong", "MMTEST");
	account.addRuleMarket("MmSellClose", tse::RuleType::exit, fromSignalParams(tse::Side::short_, tse::Side::long_), "MmCloseLong", "MMTEST");
	account.addRuleMarket("MmSellOpen", tse::RuleType::entry, fromSignalParams(tse::Side::short_, tse::Side::neutral), "MmOpenShort", "MMTEST");
	account.addRuleMarket("MmBuyClose", tse::RuleType::exit, fromSignalParams(tse::Side::long_, tse::Side::short_), "MmCloseShort", "MMTEST");

	account.addRobot("NaiveMarketMaker", {"MmBuyOpen", "MmSellClose", "MmSellOpen", "MmBuyClose"});
	account.portfolioSubscribe(priceMkt, "MMTEST");
	account.start("NaiveMarketMaker");

	std::vector<tse::Side> const clientSides
	{
		tse::Side::short_, tse::Side::short_, tse::Side::short_, tse::Side::short_,
		tse::Side::short_, tse::Side::short_, tse::Side::short_, tse::Side::short_,
		tse::Side::long_, tse::Side::long_, tse::Side::long_, tse::Side::long_,
		tse::Side::long_, tse::Side::long_, tse::Side::long_, tse::Side::long_,
		tse::Side::short_, tse::Side::short_, tse::Side::short_
	};
	std::int64_t const
		base {1000000000LL},
		half {500000000LL};

	for (std::size_t i {0}; i < clientSides.size(); ++i) {
		std::int64_t const step {static_cast<std::int64_t>(i)};
		bookMkt.pushBook("MMTEST", newOrder(base * (2 * step + 1), clientSides[i], orderPrice, orderQuantity));
		priceMkt.pushTrade("MMTEST", tradeTick(base * (2 * step + 1) + half, orderPrice));
	}

	double const finalQuantity {account.getPositionState("MMTEST").quantity};
	std::int32_t const executedCount {exec.getCount()};
	tse::Summary const summary {account.getSummary()};

	std::printf
	(
		"market maker: steps=19 executions=%d trades=%lld finalInventory=%.1f netProfit=%.4f\n",
		executedCount,
		static_cast<long long>(summary.totalNumberOfTrades),
		finalQuantity,
		summary.totalNetProfit
	);
	return 0;
}
