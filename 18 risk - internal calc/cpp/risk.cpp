#include "tse_helpers.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

	std::int64_t constexpr dayNs {86400000000000LL};

	void buildAaplRobot(tse::Account& account, tse::Market const& market)
	{
		account.addContract("AAPL", 1, tse::Instrument::equity, tse::Underlying::undefined, tse::Venue::undefined, 100000);
		account.addInputOhlcv("SMA50", 50, tse::Duration::days, helpers::makeSma(50), market, {"AAPL"});
		account.addInputOhlcv("SMA200", 200, tse::Duration::days, helpers::makeSma(200), market, {"AAPL"});
		account.addPatternCrossover("ToLong", tse::Duration::days, {"SMA50", "SMA200"}, tse::Cmp::ge);
		account.addPatternCrossover("ToShort", tse::Duration::days, {"SMA50", "SMA200"}, tse::Cmp::lt);
		account.addRuleMarket("Entry", tse::RuleType::entry, helpers::entryParams(100.0), "ToLong", "AAPL");
		account.addRuleMarket("Exit", tse::RuleType::exit, helpers::exitParams(), "ToShort", "AAPL");
	}

	// Ceilings the engine tests inside itself, against the position the order would leave us
	// holding, before it lets that order out: five thousand dollars of market value across the
	// book, and a hundred shares of any one name. The hundred AAPL shares the robot buys are
	// worth more than five thousand dollars once the share price passes fifty, so every entry
	// from that point on is refused and this run parts company with the baseline.
	void addPolicies(tse::Account& account)
	{
		account.addRiskPolicy("MaxValue", tse::RiskPolicy::value, 5000.0, tse::Cmp::le, std::string {});
		account.addRiskPolicy("MaxQty", tse::RiskPolicy::quantity, 100.0, tse::Cmp::le, "AAPL");
	}

	// Exits the engine works out itself, tick by tick, from the open position: the fixed pair
	// measures from the entry price, the trailing pair from the best price seen since entry.
	void addRiskRules(tse::Account& account)
	{
		tse::RuleParams const flat {helpers::exitParams()};
		account.addRuleRisk("StopLoss", tse::RuleType::stop_loss, flat, 0.10, "AAPL");
		account.addRuleRisk("TakeProfit", tse::RuleType::take_profit, flat, 0.20, "AAPL");
		account.addRuleRisk("StopLossTrail", tse::RuleType::stop_loss_trailing, flat, 0.12, "AAPL");
		account.addRuleRisk("TakeProfitTrail", tse::RuleType::take_profit_trailing, flat, 0.25, "AAPL");
	}

	tse::Summary runAapl
	(
		std::string const& label,
		bool const withPolicies,
		bool const withRiskRules,
		std::vector<tse::OhlcvTick> const& aaplTicks
	)
	{
		tse::Account account {label, tse::StorageRegime::mem};
		tse::Market const market {account.createMarket("OHLCV", tse::MdType::ohlcv)};
		account.createSimulator("Sim", helpers::simulatorConfig());
		buildAaplRobot(account, market);
		if (withPolicies) {
			addPolicies(account);
		}
		std::vector<std::string> ruleLabels {"Entry", "Exit"};
		if (withRiskRules) {
			addRiskRules(account);
			ruleLabels = {"Entry", "StopLoss", "TakeProfit", "StopLossTrail", "TakeProfitTrail", "Exit"};
		}
		account.addRobot("Strat", ruleLabels);
		account.start("Strat");
		for (tse::OhlcvTick const& tick : aaplTicks) {
			market.pushOhlcv("AAPL", tick);
		}
		return account.getSummary();
	}

	tse::Summary runWtiWindow
	(
		std::string const& label,
		bool const alwaysOpen
	)
	{
		tse::Account account {label, tse::StorageRegime::mem};
		tse::Market const market {account.createMarket("OHLCV", tse::MdType::ohlcv)};
		account.createSimulator("Sim", helpers::simulatorConfig());
		account.addContract("WTI", 1, tse::Instrument::future, tse::Underlying::commodity, tse::Venue::undefined, 100000);
		account.addInputOhlcv("SMA3", 3, tse::Duration::days, helpers::makeSma(3), market, {"WTI"});
		account.addInputOhlcv("SMA10", 10, tse::Duration::days, helpers::makeSma(10), market, {"WTI"});
		account.addPatternCrossover("ToLong", tse::Duration::days, {"SMA3", "SMA10"}, tse::Cmp::ge);
		account.addPatternCrossover("ToShort", tse::Duration::days, {"SMA3", "SMA10"}, tse::Cmp::lt);
		account.addRuleMarket("Entry", tse::RuleType::entry, helpers::entryParams(580.0), "ToLong", "WTI");
		account.addRuleMarket("Exit", tse::RuleType::exit, helpers::exitParams(), "ToShort", "WTI");
		account.addRobot("Strat", {"Entry", "Exit"});
		// Trading hours the engine enforces before every order. The second window is one
		// nanosecond a day wide, which is a plain way of saying "do not trade at all".
		if (alwaysOpen) {
			account.addRiskPolicyTimePeriod("Window", std::string {}, dayNs, 0, dayNs, "UTC");
		} else {
			account.addRiskPolicyTimePeriod("Window", std::string {}, dayNs, 1, 1, "UTC");
		}
		account.setAccountEquity(100000.0, 0.0);
		account.start("Strat");
		std::vector<tse::OhlcvTick> const wtiTicks {tse::Account::loadOhlcvCsv(helpers::dataPath("WTI_OHLCVminute_sept2016.csv"))};
		for (tse::OhlcvTick const& tick : wtiTicks) {
			market.pushOhlcv("WTI", tick);
		}
		return account.getSummary();
	}

}

int main()
{
	std::vector<tse::OhlcvTick> const aaplTicks {helpers::loadAapl()};
	tse::setLogLevel(tse::LogLevel::none);

	tse::Summary const baseline {runAapl("RiskManagementInternalCalc Baseline", false, false, aaplTicks)};
	tse::Summary const withPolicies {runAapl("RiskManagementInternalCalc Policies", true, false, aaplTicks)};
	tse::Summary const withRules {runAapl("RiskManagementInternalCalc Rules", false, true, aaplTicks)};
	tse::Summary const windowAlways {runWtiWindow("RiskManagementInternalCalc TimeWindowAlways", true)};
	tse::Summary const windowNever {runWtiWindow("RiskManagementInternalCalc TimeWindowNever", false)};

	std::printf
	(
		"baseline=%.4f/%lld policies=%.4f/%lld riskRules=%.4f/%lld windowAlways=%.4f/%lld windowNever=%lld\n",
		baseline.totalNetProfit,
		static_cast<long long>(baseline.totalNumberOfTrades),
		withPolicies.totalNetProfit,
		static_cast<long long>(withPolicies.totalNumberOfTrades),
		withRules.totalNetProfit,
		static_cast<long long>(withRules.totalNumberOfTrades),
		windowAlways.totalNetProfit,
		static_cast<long long>(windowAlways.totalNumberOfTrades),
		static_cast<long long>(windowNever.totalNumberOfTrades)
	);
	return 0;
}
