#include "tse_helpers.hpp"

#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

int main()
{
	std::vector<tse::OhlcvTick> const aapl {helpers::loadAapl()};
	std::vector<tse::BidAskTick> const btc {helpers::loadBidask("btc_bidask.csv")};
	std::vector<tse::TradeTick> const wti {helpers::loadTrades("wti_trades.csv", tse::Side::neutral)};
	tse::setLogLevel(tse::LogLevel::none);

	tse::Account account {"Multi", tse::StorageRegime::mem};

	tse::Market const aaplMd {account.createMarket("AAPL_OHLCV", tse::MdType::ohlcv)};
	tse::Market const btcMd  {account.createMarket("BTC_BIDASK", tse::MdType::bidask)};
	tse::Market const wtiMd  {account.createMarket("WTI_TRADE",  tse::MdType::trade)};
	account.createSimulator("Sim", helpers::simulatorConfig());

	account.addContract("AAPL", 1, tse::Instrument::equity, tse::Underlying::undefined, tse::Venue::undefined, 100000);
	account.addContract("BTC/USDT", 1, tse::Instrument::currency, tse::Underlying::crypto, tse::Venue::undefined, 100000);
	account.addContract("WTI", 1, tse::Instrument::future, tse::Underlying::commodity, tse::Venue::undefined, 100000);

	account.addInputOhlcv("AaplSma", 50, tse::Duration::days, helpers::makeSma(50), aaplMd, {"AAPL"});
	account.addInputBidAsk("BtcSma", 20, tse::Duration::minutes, helpers::makeBidAskSma(20), btcMd, {"BTC/USDT"});

	account.addPatternThreshold("ToLong", tse::Duration::days, {"AaplSma"}, tse::Cmp::ge, 50.0);
	account.addPatternPeak("ToShort", tse::Duration::minutes, {"BtcSma"});

	// The signal and the trade need not share a contract: a rich Apple says risk is on,
	// a peak in the Bitcoin mid price says the party is over, and the position is taken in oil.
	tse::RuleParams const longParams
	{
		tse::QuantityMode::fixed, 10.0,
		tse::PriceType::market, 0.0, 0.0, 0.0,
		tse::Side::long_, tse::Side::neutral, tse::Tif::day, 10
	};
	tse::RuleParams const shortParams
	{
		tse::QuantityMode::fixed, 10.0,
		tse::PriceType::limit, 80.0, 0.0, 0.0,
		tse::Side::short_, tse::Side::long_, tse::Tif::day, 10
	};
	account.addRuleMarket("Entry", tse::RuleType::entry, longParams, "ToLong", "WTI");
	account.addRuleMarket("Exit", tse::RuleType::exit, shortParams, "ToShort", "WTI");
	account.addRobot("Strat", {"Entry", "Exit"});

	// An open position is worth what the market says right now, so the portfolio is marked off the oil feed.
	account.portfolioSubscribe(wtiMd, "WTI");
	account.start("Strat");

	// Three feeds, three tick shapes, one clock: always push whichever feed holds the oldest
	// unsent tick, so the strategy lives through the history in the order it really happened.
	std::int64_t const farFuture {std::numeric_limits<std::int64_t>::max()};
	std::size_t
		aaplAt {0},
		btcAt {0},
		wtiAt {0};
	while (aaplAt < aapl.size() or btcAt < btc.size() or wtiAt < wti.size()) {
		std::int64_t const
			aaplTs {aaplAt < aapl.size() ? aapl[aaplAt].tsNanoseconds : farFuture},
			btcTs  {btcAt  < btc.size()  ? btc[btcAt].tsNanoseconds   : farFuture},
			wtiTs  {wtiAt  < wti.size()  ? wti[wtiAt].tsNanoseconds   : farFuture};
		if (aaplTs <= btcTs and aaplTs <= wtiTs) {
			aaplMd.pushOhlcv("AAPL", aapl[aaplAt]);
			aaplAt += 1;
		} else if (btcTs <= wtiTs) {
			btcMd.pushBidAsk("BTC/USDT", btc[btcAt]);
			btcAt += 1;
		} else {
			wtiMd.pushTrade("WTI", wti[wtiAt]);
			wtiAt += 1;
		}
	}

	tse::Summary const summary {account.getSummary()};
	std::vector<tse::Trade> const trades {account.getTrades()};
	tse::PositionState const wtiPos {account.getPositionState("WTI")};

	std::printf
	(
		"netProfit=%.4f trades=%lld retained=%zu wtiMark=%.4f\n",
		summary.totalNetProfit,
		static_cast<long long>(summary.totalNumberOfTrades),
		trades.size(),
		wtiPos.marketPrice
	);
	return 0;
}
