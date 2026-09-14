#include "tse_helpers.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

	std::string const symbol {"EURSTK"};

	std::int64_t constexpr
		enterCheckPoint {1000000000LL},
		exitCheckPoint {3000000000LL},
		hugeCoolDown {1000000000000000LL},
		drainOffset {100000000LL};

	double constexpr
		entryQuantity {10.0},
		entryPrice {100.0},
		exitPrice {110.0};

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

	void pushPrice
	(
		tse::Market const& market,
		std::int64_t const tsNanoseconds,
		double const price
	)
	{
		market.pushTrade(symbol, tse::TradeTick {tsNanoseconds, price, 1.0, tse::Side::trade});
	}

	tse::Market buildStrategy
	(
		tse::Account& account,
		double const quantity
	)
	{
		tse::Market const market {account.createMarket("price", tse::MdType::trade)};
		account.createSimulator("sim", helpers::simulatorConfig(), 64, -1);
		account.addContract(symbol, 1, tse::Instrument::equity, tse::Underlying::undefined, tse::Venue::undefined, 100000);
		account.addInputTrade("Px", 2, tse::Duration::nanoseconds, priceProcessor(), market, {symbol});
		// Two fixed moments in time carry the whole example: the first opens the position, the second closes it, giving one clean round trip.
		account.addPatternTimestamp("EnterAt", tse::Duration::nanoseconds, {"Px"}, enterCheckPoint, hugeCoolDown);
		account.addPatternTimestamp("ExitAt", tse::Duration::nanoseconds, {"Px"}, exitCheckPoint, hugeCoolDown);
		account.addRuleMarket("Enter", tse::RuleType::entry, helpers::entryParams(quantity), "EnterAt", symbol);
		account.addRuleMarket("Exit", tse::RuleType::exit, helpers::exitParams(), "ExitAt", symbol);
		account.addRobot("EurRobot", {"Enter", "Exit"});
		account.portfolioSubscribe(market, symbol);
		account.start("EurRobot");
		return market;
	}

	double runEurBacktest()
	{
		// The euro account below exercises an interface that ships ahead of its cross-currency arithmetic: today the engine is validated on a single currency, US dollar.
		// The account is denominated in euro, ISO 4217 code 978, so every price, cash figure and profit below is read in euro.
		tse::Account account {"MulticurrencyEur", tse::StorageRegime::mem, tse::Currency::eur, -1};
		tse::Market const market {buildStrategy(account, entryQuantity)};

		// The repeat push a fraction of a second later gives the simulator its chance to fill the order the first one triggered.
		pushPrice(market, enterCheckPoint, entryPrice);
		pushPrice(market, enterCheckPoint + drainOffset, entryPrice);

		pushPrice(market, exitCheckPoint, exitPrice);
		pushPrice(market, exitCheckPoint + drainOffset, exitPrice);

		return account.getSummary().totalNetProfit;
	}

	std::vector<tse::GridResult> runEurGrid()
	{
		std::vector<double> const params {2.0, 5.0};
		return tse::runGrid
		(
			"EurGrid",
			tse::StorageRegime::mem,
			params,
			[](tse::Account& account, double paramValue)
			{
				tse::Market const market {buildStrategy(account, paramValue)};
				pushPrice(market, enterCheckPoint, entryPrice);
				pushPrice(market, enterCheckPoint + drainOffset, entryPrice);
				pushPrice(market, exitCheckPoint, exitPrice);
				pushPrice(market, exitCheckPoint + drainOffset, exitPrice);
			},
			tse::Currency::eur
		);
	}

}//!namespace

int main()
{
	tse::setLogLevel(tse::LogLevel::none);

	double const netProfit {runEurBacktest()};
	std::vector<tse::GridResult> const results {runEurGrid()};

	std::printf
	(
		"multicurrency: eurNetProfit=%.2f gridNetProfit[%.0f]=%.2f gridNetProfit[%.0f]=%.2f\n",
		netProfit,
		results[0].paramValue, results[0].summary.totalNetProfit,
		results[1].paramValue, results[1].summary.totalNetProfit
	);
	return 0;
}
