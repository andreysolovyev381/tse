#include "tse_helpers.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

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

	tse::BookMessage cancelOrder
	(
		std::int64_t tsNanoseconds,
		std::uint64_t restingId
	)
	{
		tse::BookMessage message;
		message.kind = tse::BookMessageKind::cancel;
		message.tsNanoseconds = tsNanoseconds;
		message.messageId = restingId;
		message.orderId = restingId;
		message.price = 0.0;
		message.quantity = 0.0;
		message.txnSide = tse::Side::neutral;
		return message;
	}

	tse::RuleParams marketParams
	(
		tse::Side txnSide,
		tse::Side posSide
	)
	{
		return tse::RuleParams
		{
			tse::QuantityMode::fixed, 100.0,
			tse::PriceType::market, 0.0, 0.0, 0.0,
			txnSide, posSide, tse::Tif::day, 10
		};
	}

}

int main()
{
	tse::setLogLevel(tse::LogLevel::none);

	std::vector<tse::BidAskTick> const ticks {helpers::loadBidask("btc_orderflow.csv")};

	tse::Account account {"BookImbalance", tse::StorageRegime::mem};
	tse::Market const bookMkt {account.createMarket("book", tse::MdType::book)};
	tse::Market const priceMkt {account.createMarket("price", tse::MdType::trade)};
	account.createSimulator("Sim", helpers::simulatorConfig(), 64, -1);
	account.addContract("BOOKA", 1, tse::Instrument::equity, tse::Underlying::equity, tse::Venue::undefined, 10000);
	tse::Book const book {account.createBook("BOOKA", tse::BookLevelKind::l3)};

	// Imbalance over the whole book answers one question: whose resting size is bigger, the
	// buyers' or the sellers'. It reads +1 when only bids rest, -1 when only offers do. The 4
	// is the length of the input's own storage, not a book depth.
	account.addInputBookImbalance("Imbalance", 4, tse::Duration::nanoseconds, book, bookMkt, {"BOOKA"});

	// A fifth more size on one side is treated as pressure worth joining, and the opposite
	// reading is what takes the position off again.
	account.addPatternThreshold("ToLong", tse::Duration::nanoseconds, {"Imbalance"}, tse::Cmp::ge, 0.2);
	account.addPatternThreshold("ToShort", tse::Duration::nanoseconds, {"Imbalance"}, tse::Cmp::le, -0.2);

	account.addRuleMarket("EnterLong", tse::RuleType::entry, marketParams(tse::Side::long_, tse::Side::neutral), "ToLong", "BOOKA");
	account.addRuleMarket("ExitLong", tse::RuleType::exit, marketParams(tse::Side::short_, tse::Side::long_), "ToShort", "BOOKA");
	account.addRuleMarket("EnterShort", tse::RuleType::entry, marketParams(tse::Side::short_, tse::Side::neutral), "ToShort", "BOOKA");
	account.addRuleMarket("ExitShort", tse::RuleType::exit, marketParams(tse::Side::long_, tse::Side::short_), "ToLong", "BOOKA");
	account.addRobot("Robot", {"EnterLong", "ExitLong", "EnterShort", "ExitShort"});
	account.portfolioSubscribe(priceMkt, "BOOKA");
	account.start("Robot");

	// Each recorded snapshot is replayed as the two orders it implies, one per side, and the
	// pair the previous snapshot left resting is pulled right after, so the book always holds
	// the current quote alone instead of a pile of every quote ever seen. Then comes the print
	// that the market actually paid between the snapshots.
	std::uint64_t
	restingBid {0},
	restingAsk {0};
	for (tse::BidAskTick const& tick : ticks) {
		tse::BookMessage const
		bid {newOrder(tick.tsNanoseconds, tse::Side::long_, tick.bid, tick.bidVolume)},
		ask {newOrder(tick.tsNanoseconds + 1, tse::Side::short_, tick.ask, tick.askVolume)};
		bookMkt.pushBook("BOOKA", bid);
		bookMkt.pushBook("BOOKA", ask);
		if (restingBid != 0) {
			bookMkt.pushBook("BOOKA", cancelOrder(tick.tsNanoseconds + 2, restingBid));
			bookMkt.pushBook("BOOKA", cancelOrder(tick.tsNanoseconds + 3, restingAsk));
		}
		restingBid = bid.messageId;
		restingAsk = ask.messageId;
		priceMkt.pushTrade("BOOKA", tse::TradeTick {tick.tsNanoseconds + 4, tick.last, 1.0, tse::Side::trade});
	}

	tse::Summary const summary {account.getSummary()};

	std::printf("netProfit=%.4f trades=%lld\n", summary.totalNetProfit, static_cast<long long>(summary.totalNumberOfTrades));
	return 0;
}
