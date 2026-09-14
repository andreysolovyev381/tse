#include "tse_helpers.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

	std::string const symbol {"MMFLOW"};

	std::int64_t constexpr
		quietGapNanoseconds {10000000000LL},
		printLagNanoseconds {500000000LL};

	std::size_t constexpr sameSideRunLength {3u};

	double constexpr sweepMultiple {3.0};

	tse::Side constexpr quoteSide {tse::Side::long_};

	struct ClientTrade final {
		std::int64_t tsNanoseconds;
		tse::Side    side;
		double       price;
		double       quantity;
	};

	struct FlowState final {
		double       quantitySum;
		std::size_t  tradeCount;
		std::size_t  runLength;
		std::int64_t previousTsNanoseconds;
		tse::Side    previousSide;
		tse::Side    hitSide;
		bool         sweepFired;
		bool         runFired;
		bool         gapFired;
	};

	struct QuoteState final {
		std::uint64_t restingClientOrderId;
		std::uint64_t nextClientOrderId;
		std::size_t   cancelCount;
		std::size_t   replaceCount;
		std::size_t   modifyCount;
	};

	std::vector<ClientTrade> loadClientFlow()
	{
		std::ifstream file {helpers::dataPath("mm_client_flow.csv")};
		std::vector<ClientTrade> out;
		std::string line;
		std::getline(file, line);
		while (std::getline(file, line)) {
			long long ts {0};
			char sideText[8] {};
			double
				price {0.0},
				quantity {0.0};
			if (std::sscanf(line.c_str(), "%lld,%*d,%*[^,],%7[^,],%*d,%*d,%lf,%lf", &ts, sideText, &price, &quantity) != 4) {
				continue;
			}
			out.push_back(ClientTrade
			{
				static_cast<std::int64_t>(ts),
				std::strcmp(sideText, "bid") == 0 ? tse::Side::long_ : tse::Side::short_,
				price,
				quantity
			});
		}
		return out;
	}

	tse::BookMessage executedMessage
	(
		ClientTrade const& trade,
		std::uint64_t messageId
	)
	{
		tse::BookMessage message;
		message.kind = tse::BookMessageKind::executed;
		message.tsNanoseconds = trade.tsNanoseconds;
		message.messageId = messageId;
		message.orderId = messageId;
		message.price = trade.price;
		message.quantity = trade.quantity;
		message.txnSide = trade.side;
		return message;
	}

}

int main()
{
	tse::setLogLevel(tse::LogLevel::none);

	double const
		quotePrice {43.22},
		stepAwayPrice {43.21},
		fullSize {4.0},
		defensiveSize {2.0},
		inventoryCap {12.0};

	tse::Account account {"MarketMakerAmendExample", tse::StorageRegime::mem};
	tse::Market const
		flowMkt {account.createMarket("client flow", tse::MdType::book)},
		printMkt {account.createMarket("prints", tse::MdType::trade)};
	account.createSimulator("sim", helpers::simulatorConfig(), 64, -1);
	account.addContract(symbol, 1, tse::Instrument::equity, tse::Underlying::equity, tse::Venue::undefined, 10000);

	FlowState flow {0.0, 0u, 0u, 0, tse::Side::undefined, tse::Side::undefined, false, false, false};
	QuoteState quote {0u, 1u, 0u, 0u, 0u};

	// Every executed client trade is measured once, here: an outsized print, a third
	// trade in a row on one side and a long silence are the three things the maker reacts to.
	account.addInputBook
	(
		"ClientFlow", 4, tse::Duration::nanoseconds,
		[&flow](tse::Storage const& storage, std::string const&, tse::BookMessage const& message) -> bool
		{
			if (message.kind != tse::BookMessageKind::executed) { return false; }
			double const average
				{flow.tradeCount == 0u ? 0.0 : flow.quantitySum / static_cast<double>(flow.tradeCount)};
			flow.sweepFired = flow.tradeCount != 0u and message.quantity > sweepMultiple * average;
			flow.gapFired = flow.tradeCount != 0u
				and message.tsNanoseconds - flow.previousTsNanoseconds > quietGapNanoseconds;
			flow.runLength = message.txnSide == flow.previousSide ? flow.runLength + 1u : 1u;
			flow.runFired = flow.runLength == sameSideRunLength;
			if (flow.runFired) { flow.runLength = 0u; }
			flow.hitSide = message.txnSide;
			flow.previousSide = message.txnSide;
			flow.previousTsNanoseconds = message.tsNanoseconds;
			flow.quantitySum += message.quantity;
			++flow.tradeCount;
			storage.push(message.tsNanoseconds, message.quantity);
			return true;
		},
		flowMkt, {symbol}
	);

	// One quote lives at a time and the maker knows it by its client order id: a cancel drops
	// the id, a replace mints a fresh one, a modify keeps it. Zero means nothing rests.
	account.addPatternFormula
	(
		"QuoteIsGone", tse::Duration::nanoseconds, {"ClientFlow"},
		[&quote](std::string const&, std::int64_t, double) -> bool
		{
			if (quote.restingClientOrderId != 0u) { return false; }
			quote.restingClientOrderId = quote.nextClientOrderId++;
			return true;
		}
	);
	account.addPatternFormula
	(
		"SweepHit", tse::Duration::nanoseconds, {"ClientFlow"},
		[&flow, &quote](std::string const&, std::int64_t, double) -> bool
		{
			if (not flow.sweepFired or flow.hitSide != quoteSide or quote.restingClientOrderId == 0u) { return false; }
			quote.restingClientOrderId = 0u;
			++quote.cancelCount;
			return true;
		}
	);
	account.addPatternFormula
	(
		"SameSideRun", tse::Duration::nanoseconds, {"ClientFlow"},
		[&flow, &quote](std::string const&, std::int64_t, double) -> bool
		{
			if (not flow.runFired or quote.restingClientOrderId == 0u) { return false; }
			quote.restingClientOrderId = quote.nextClientOrderId++;
			++quote.replaceCount;
			return true;
		}
	);
	account.addPatternFormula
	(
		"QuietGap", tse::Duration::nanoseconds, {"ClientFlow"},
		[&flow, &quote](std::string const&, std::int64_t, double) -> bool
		{
			if (not flow.gapFired or quote.restingClientOrderId == 0u) { return false; }
			++quote.modifyCount;
			return true;
		}
	);
	account.addPatternFormula
	(
		"InventoryFull", tse::Duration::nanoseconds, {"ClientFlow"},
		[&account, inventoryCap](std::string const&, std::int64_t, double) -> bool
		{
			tse::PositionState const state {account.getPositionState(symbol)};
			return state.side == tse::Side::long_ and state.quantity >= inventoryCap;
		}
	);

	// The three amend builders share one argument list but read different parts of it: cancel ignores
	// quantity and price, modify reads quantity only, replace reads both. Pull the quote when a sweep
	// hits its side, step it away and shrink it against a one-sided run, restore its size when quiet.
	account.addRuleMarket
	(
		"RestQuote", tse::RuleType::entry,
		tse::RuleParams
		{
			tse::QuantityMode::fixed, fullSize,
			tse::PriceType::limit, quotePrice, 0.0, 0.0,
			quoteSide, tse::Side::neutral, tse::Tif::day, 10
		},
		"QuoteIsGone", symbol
	);
	account.addRuleCancel("PullQuote", symbol, 0.0, 0.0, "SweepHit");
	account.addRuleReplace("StepAway", symbol, defensiveSize, stepAwayPrice, "SameSideRun");
	account.addRuleModify("BackToFullSize", symbol, fullSize, 0.0, "QuietGap");
	account.addRuleMarket
	(
		"Unwind", tse::RuleType::exit,
		tse::RuleParams
		{
			tse::QuantityMode::all, 0.0,
			tse::PriceType::market, 0.0, 0.0, 0.0,
			tse::Side::short_, tse::Side::long_, tse::Tif::day, 10
		},
		"InventoryFull", symbol
	);

	account.addRobot("AmendingMaker", {"RestQuote", "PullQuote", "StepAway", "BackToFullSize", "Unwind"});
	account.portfolioSubscribe(printMkt, symbol);
	account.start("AmendingMaker");

	std::vector<ClientTrade> const clientFlow {loadClientFlow()};
	std::uint64_t messageId {0};
	for (ClientTrade const& trade : clientFlow) {
		++messageId;
		flowMkt.pushBook(symbol, executedMessage(trade, messageId));
		printMkt.pushTrade
		(
			symbol,
			tse::TradeTick {trade.tsNanoseconds + printLagNanoseconds, trade.price, trade.quantity, tse::Side::trade}
		);
	}

	tse::Summary const summary {account.getSummary()};

	std::printf
	(
		"market maker amend: cancels=%zu replaces=%zu modifies=%zu netProfit=%.4f trades=%lld\n",
		quote.cancelCount,
		quote.replaceCount,
		quote.modifyCount,
		summary.totalNetProfit,
		static_cast<long long>(summary.totalNumberOfTrades)
	);
	return 0;
}
