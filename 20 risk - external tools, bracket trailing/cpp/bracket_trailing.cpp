#include "tse_helpers.hpp"

#include <cstdio>
#include <string>
#include <vector>

int main()
{
	tse::setLogLevel(tse::LogLevel::none);

	tse::Account account {"RiskManagementExternalTools BracketTrailing", tse::StorageRegime::mem};
	std::vector<tse::OhlcvTick> const ticks {helpers::loadAapl()};

	tse::Market const market {account.createMarket("OHLCV", tse::MdType::ohlcv)};
	account.createSimulator("Sim", helpers::simulatorConfig(), 200, -1);
	account.addContract("AAPL", 1, tse::Instrument::equity, tse::Underlying::undefined, tse::Venue::undefined, 100000);

	account.addInputOhlcv("AAPL SMA fast", 50, tse::Duration::days, helpers::makeSma(50), market, {"AAPL"});
	account.addInputOhlcv("AAPL SMA slow", 200, tse::Duration::days, helpers::makeSma(200), market, {"AAPL"});
	account.addPatternCrossover("PatternToLong", tse::Duration::days, {"AAPL SMA fast", "AAPL SMA slow"}, tse::Cmp::ge);

	// Buy 100 shares when the fifty-day average crosses above the two-hundred-day one.
	tse::LegDescriptor const entry
	{
		"AAPL",
		tse::QuantityMode::fixed,
		100.0,
		tse::PriceType::market,
		150.0,
		0.0,
		0.0,
		tse::TxnType::enter,
		tse::Side::long_,
		tse::Side::neutral,
		tse::Tif::day,
		tse::Priority::replaceable
	};
	// Both legs rest at the venue again, but the stop now trails: it follows the highest price the
	// trade has reached, five percent behind, so gains already made are not handed back. The ten
	// percent target stays where the fill put it.
	tse::VenueRiskSpec const risk
	{
		tse::VenueRiskLeg {true, 1, 0.05},
		tse::VenueRiskLeg {true, 0, 0.10},
		tse::Tif::gtc
	};
	account.addRuleBracket("Bracket", entry, risk, "PatternToLong");
	account.addRobot("Bracket robot", {"Bracket"});
	account.start("Bracket robot");

	for (tse::OhlcvTick const& tick : ticks) {
		market.pushOhlcv("AAPL", tick);
	}

	tse::Summary const summary {account.getSummary()};
	std::printf
	(
		"bracket trailing: trades=%lld netProfit=%.4f grossProfit=%.4f grossLoss=%.4f profitFactor=%.6f maxDrawdown=%.4f\n",
		static_cast<long long>(summary.totalNumberOfTrades),
		summary.totalNetProfit,
		summary.grossProfit,
		summary.grossLoss,
		summary.profitFactor,
		summary.maxDrawdown
	);
	return 0;
}
