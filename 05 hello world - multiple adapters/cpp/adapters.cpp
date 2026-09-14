#include "tse_helpers.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

int main()
{
	std::vector<tse::OhlcvTick> const rows {helpers::loadAapl()};
	tse::setLogLevel(tse::LogLevel::none);

	tse::Account account {"MultiAdapter", tse::StorageRegime::mem};
	// two adapters feed one account: daily bars for the equity, a quote stream for the crypto pair
	tse::Market const prices {account.createMarket("EQUITY_OHLCV", tse::MdType::ohlcv)};
	tse::Market const book {account.createMarket("CRYPTO_BIDASK", tse::MdType::bidask)};
	account.createSimulator("Sim", helpers::simulatorConfig());

	account.addContract("AAPL", 1, tse::Instrument::equity, tse::Underlying::undefined, tse::Venue::undefined, 100000);
	account.addContract("BTC/USDT", 1, tse::Instrument::currency, tse::Underlying::crypto, tse::Venue::undefined, 100000);

	// the golden cross: hold AAPL while the 50-day average leads the 200-day one, stand aside when it falls back
	account.addInputOhlcv("SMA50", 50, tse::Duration::days, helpers::makeSma(50), prices, {"AAPL"});
	account.addInputOhlcv("SMA200", 200, tse::Duration::days, helpers::makeSma(200), prices, {"AAPL"});
	account.addPatternCrossover("ToLong", tse::Duration::days, {"SMA50", "SMA200"}, tse::Cmp::ge);
	account.addPatternCrossover("ToShort", tse::Duration::days, {"SMA50", "SMA200"}, tse::Cmp::lt);
	account.addRuleMarket("Entry", tse::RuleType::entry, helpers::entryParams(100.0), "ToLong", "AAPL");
	account.addRuleMarket("Exit", tse::RuleType::exit, helpers::exitParams(), "ToShort", "AAPL");
	account.addRobot("Strat", {"Entry", "Exit"});

	// BTC/USDT carries no rules: it is only subscribed, so the portfolio keeps marking it at the latest quote
	account.portfolioSubscribe(book, "BTC/USDT");
	account.start("Strat");

	std::int64_t ts {1000000000};
	std::vector<double> const btcMarks {15.0, 20.0, 30.0, 36.0, 42.0};
	for (double const px : btcMarks) {
		book.pushBidAsk("BTC/USDT", tse::BidAskTick {ts, px - 0.05, 1.0, px + 0.05, 1.0, px});
		ts += 1000000000;
	}
	for (tse::OhlcvTick const& tick : rows) {
		prices.pushOhlcv("AAPL", tick);
	}

	tse::Summary const summary {account.getSummary()};
	tse::PositionState const btc {account.getPositionState("BTC/USDT")};

	std::printf
	(
		"multi adapter: AAPL netProfit=%.4f trades=%lld; BTC/USDT marked at %.2f quantity=%.1f\n",
		summary.totalNetProfit,
		static_cast<long long>(summary.totalNumberOfTrades),
		btc.marketPrice,
		btc.quantity
	);
	return 0;
}
