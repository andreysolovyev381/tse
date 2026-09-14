#include "tse_helpers.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

	struct BookFeed final {
		std::vector<tse::BookMessage> book;
		std::vector<tse::TradeTick> price;
	};

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

	tse::TradeTick tradeTick(std::int64_t tsNanoseconds, double price)
	{
		return tse::TradeTick {tsNanoseconds, price, 1.0, tse::Side::trade};
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

	// A laboratory feed rather than a recording: every step adds exactly the size that drags the
	// book onto the next target imbalance, so the sequence of signals is known before the run.
	BookFeed makeImbalanceFeed
	(
		std::vector<double> const& targets,
		std::vector<double> const& prices,
		double bidPrice,
		double askPrice,
		double initialBidQuantity
	)
	{
		std::int64_t constexpr
		base {1000000000LL},
		half {500000000LL};
		BookFeed feed;
		double
		sb {initialBidQuantity},
		sa {0.0};
		std::int64_t step {0};

		feed.price.push_back(tradeTick(base - half, prices[0]));
		feed.book.push_back(newOrder(base * (2 * step + 1), tse::Side::long_, bidPrice, initialBidQuantity));
		feed.price.push_back(tradeTick(base * (2 * step + 1) + half, prices[0]));
		++step;
		for (std::size_t i {0}; i < targets.size(); ++i) {
			double const
			t {targets[i]},
			curr {(sb - sa) / (sb + sa)};
			bool const addBid {t > curr};
			double const delta
			{
				addBid
					? (sa * (1.0 + t) / (1.0 - t)) - sb
					: (sb * (1.0 - t) / (1.0 + t)) - sa
			};
			if (addBid) {
				sb += delta;
			}
			else {
				sa += delta;
			}
			feed.book.push_back(newOrder(base * (2 * step + 1), addBid ? tse::Side::long_ : tse::Side::short_, addBid ? bidPrice : askPrice, delta));
			feed.price.push_back(tradeTick(base * (2 * step + 1) + half, prices[i + 1]));
			++step;
		}
		return feed;
	}

	tse::Summary runScenario
	(
		std::string const& level,
		tse::BookLevelKind kind,
		BookFeed const& feed
	)
	{
		std::string const
		symbol {"BOOK" + level},
		longLabel {level + "Long"},
		shortLabel {level + "Short"};

		tse::Account account {"Ofi" + level, tse::StorageRegime::mem};
		tse::Market const bookMkt {account.createMarket("book", tse::MdType::book)};
		tse::Market const priceMkt {account.createMarket("price", tse::MdType::trade)};
		account.createSimulator("sim", helpers::simulatorConfig(), 64, -1);
		account.addContract(symbol, 1, tse::Instrument::equity, tse::Underlying::equity, tse::Venue::undefined, 50000);
		tse::Book const book {account.createBook(symbol, kind)};

		// Resting-size imbalance over the whole book, with a fifth of extra size on one side
		// taken as pressure worth joining; the opposite reading closes the position. The 4 is the
		// length of the input's own storage, not a book depth.
		account.addInputBookImbalance(level, 4, tse::Duration::nanoseconds, book, bookMkt, {symbol});
		account.addPatternThreshold(longLabel, tse::Duration::nanoseconds, {level}, tse::Cmp::ge, 0.2);
		account.addPatternThreshold(shortLabel, tse::Duration::nanoseconds, {level}, tse::Cmp::le, -0.2);

		account.addRuleMarket("EnterLong", tse::RuleType::entry, marketParams(tse::Side::long_, tse::Side::neutral), longLabel, symbol);
		account.addRuleMarket("ExitLong", tse::RuleType::exit, marketParams(tse::Side::short_, tse::Side::long_), shortLabel, symbol);
		account.addRuleMarket("EnterShort", tse::RuleType::entry, marketParams(tse::Side::short_, tse::Side::neutral), shortLabel, symbol);
		account.addRuleMarket("ExitShort", tse::RuleType::exit, marketParams(tse::Side::long_, tse::Side::short_), longLabel, symbol);
		account.addRobot("Robot", {"EnterLong", "ExitLong", "EnterShort", "ExitShort"});
		account.portfolioSubscribe(priceMkt, symbol);
		account.start("Robot");

		priceMkt.pushTrade(symbol, feed.price[0]);
		for (std::size_t i {0}; i < feed.book.size(); ++i) {
			bookMkt.pushBook(symbol, feed.book[i]);
			priceMkt.pushTrade(symbol, feed.price[i + 1]);
		}

		return account.getSummary();
	}

}

int main()
{
	tse::setLogLevel(tse::LogLevel::none);

	std::vector<double> const targets
	{
		-0.4, -0.5, 0.4, 0.5, -0.4, -0.5, 0.4, 0.5, -0.4, -0.5, 0.4
	};
	std::vector<double> const prices
	{
		100, 102, 102, 100, 100, 102, 102, 100, 100, 102, 102, 100
	};
	BookFeed const feed {makeImbalanceFeed(targets, prices, 99.95, 100.05, 12.0)};

	std::vector<std::string> const levels {"L1", "L2", "L3"};
	std::vector<tse::BookLevelKind> const kinds
	{
		tse::BookLevelKind::l1, tse::BookLevelKind::l2, tse::BookLevelKind::l3
	};

	// The same strategy over the same feed, on the three book depths a venue may publish. The
	// feed touches one price per side, so the depth changes what is stored, never the verdict.
	std::vector<tse::Summary> results;
	for (std::size_t i {0}; i < levels.size(); ++i) {
		results.push_back(runScenario(levels[i], kinds[i], feed));
	}

	std::printf
	(
		"netProfit L1=%.4f L2=%.4f L3=%.4f\n",
		results[0].totalNetProfit,
		results[1].totalNetProfit,
		results[2].totalNetProfit
	);
	return 0;
}
