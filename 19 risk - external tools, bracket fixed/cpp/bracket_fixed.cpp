#include "tse_helpers.hpp"

#include <cstdio>
#include <string>
#include <vector>

int main()
{
	tse::setLogLevel(tse::LogLevel::none);

	tse::Account account {"RiskManagementExternalTools BracketFixed", tse::StorageRegime::mem};
	std::vector<tse::OhlcvTick> const ticks
		{tse::Account::loadOhlcvCsv(helpers::dataPath("WTI_OHLCVminute_sept2016.csv"))};

	tse::Market const market {account.createMarket("OHLCV", tse::MdType::ohlcv)};
	account.createSimulator("Sim", helpers::simulatorConfig(), 10, -1);
	account.addContract("WTI", 1, tse::Instrument::future, tse::Underlying::commodity, tse::Venue::undefined, 100000);

	account.addInputOhlcv("WTI SMA fast", 3, tse::Duration::days, helpers::makeSma(3), market, {"WTI"});
	account.addInputOhlcv("WTI SMA slow", 10, tse::Duration::days, helpers::makeSma(10), market, {"WTI"});
	account.addPatternCrossover("PatternToLong", tse::Duration::days, {"WTI SMA fast", "WTI SMA slow"}, tse::Cmp::ge);

	// Buy 580 lots of crude as soon as the three-day average climbs above the ten-day one.
	tse::LegDescriptor const entry
	{
		"WTI",
		tse::QuantityMode::fixed,
		580.0,
		tse::PriceType::market,
		45.0,
		0.0,
		0.0,
		tse::TxnType::enter,
		tse::Side::long_,
		tse::Side::neutral,
		tse::Tif::day,
		tse::Priority::replaceable
	};
	// Here the risk is not recomputed on every tick. The stop 2% below the fill and the target 3%
	// above it go out with the entry and rest at the venue, so they guard the position even if
	// this program stops running.
	tse::VenueRiskSpec const risk
	{
		tse::VenueRiskLeg {true, 0, 0.02},
		tse::VenueRiskLeg {true, 0, 0.03},
		tse::Tif::gtc
	};
	account.addRuleBracket("Bracket", entry, risk, "PatternToLong");
	account.addRobot("Bracket robot", {"Bracket"});
	account.start("Bracket robot");

	for (tse::OhlcvTick const& tick : ticks) {
		market.pushOhlcv("WTI", tick);
	}

	tse::Summary const summary {account.getSummary()};
	std::printf
	(
		"bracket fixed: trades=%lld netProfit=%.4f grossProfit=%.4f grossLoss=%.4f profitFactor=%.6f maxDrawdown=%.4f\n",
		static_cast<long long>(summary.totalNumberOfTrades),
		summary.totalNetProfit,
		summary.grossProfit,
		summary.grossLoss,
		summary.profitFactor,
		summary.maxDrawdown
	);
	return 0;
}
