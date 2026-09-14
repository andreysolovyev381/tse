#include "tse_helpers.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace {

	std::string const contractSymbol {"WTI"};

	tse::RuleParams closeParams(double quantity)
	{
		return tse::RuleParams
		{
			tse::QuantityMode::fixed, quantity,
			tse::PriceType::market, 0.0, 0.0, 0.0,
			tse::Side::short_, tse::Side::long_, tse::Tif::day, 10
		};
	}

	tse::FormulaProcessor coinFlip
	(
		std::mt19937& generator,
		std::uniform_real_distribution<double>& draw,
		double chance
	)
	{
		return [&generator, &draw, chance](std::string const&, std::int64_t, double) -> bool
		{
			return draw(generator) < chance;
		};
	}

	tse::Retained toRetained(tse::Trade const& trade)
	{
		tse::Retained retained {};
		retained.tsNanoseconds = trade.tsExecutionNanoseconds;
		retained.symbol = trade.symbol;
		retained.contractId = 0;
		retained.clientOrderId = trade.clientOrderId;
		retained.brokerOrderId = trade.brokerOrderId;
		retained.ruleLabel = trade.ruleLabel;
		retained.robotLabel = trade.robotLabel;
		retained.tsMktEventNanoseconds = trade.tsMktEventNanoseconds;
		retained.tsExecutionNanoseconds = trade.tsExecutionNanoseconds;
		retained.price = trade.price;
		retained.quantity = trade.quantity;
		retained.fee = trade.fee;
		retained.bookedPL = trade.bookedPL;
		retained.txnSide = trade.txnSide;
		retained.posSide = trade.posSide;
		retained.bookMode = trade.bookMode;
		retained.execution = trade.execution;
		retained.priceType = tse::PriceType::market;
		retained.quantityType = tse::QuantityMode::fixed;
		retained.tif = tse::Tif::day;
		retained.priorityType = tse::Priority::non_replaceable;
		return retained;
	}

	void forwardGroupFills
	(
		tse::Account const& account,
		tse::Market const& executed,
		std::string const& voterLabel,
		std::size_t& seenTrades
	)
	{
		std::vector<tse::Trade> const trades {account.getTrades(0, 0, std::string {})};
		for (std::size_t i {seenTrades}; i != trades.size(); ++i) {
			if (trades[i].robotLabel == voterLabel) { continue; }
			executed.pushExecuted(trades[i].symbol, toRetained(trades[i]));
		}
		seenTrades = trades.size();
	}

}

int main()
{
	std::vector<tse::OhlcvTick> const ticks
		{tse::Account::loadOhlcvCsv(helpers::dataPath("WTI_OHLCVminute_sept2016.csv"))};
	tse::setLogLevel(tse::LogLevel::none);

	std::mt19937 generator {42u};
	std::uniform_real_distribution<double> draw {0.0, 1.0};

	double const
		traderQuantity {10.0},
		voterQuantity {30.0},
		voteChance {0.0001};
	std::string const voterLabel {"Voting robot"};
	std::vector<std::string> const traderLabels {"Trader one", "Trader two", "Trader three"};

	tse::Account account {"VotingGroup", tse::StorageRegime::mem};
	tse::Market const
		prices {account.createMarket("WTI prices", tse::MdType::ohlcv)},
		executed {account.createMarket("Group fills", tse::MdType::executed)};
	tse::Execution const execution {account.createSimulator("Sim", helpers::simulatorConfig())};
	account.addContract(contractSymbol, 1, tse::Instrument::future, tse::Underlying::commodity, tse::Venue::undefined, 100000);
	account.addInputOhlcv
	(
		"WTI price", 1, tse::Duration::minutes,
		[](tse::Storage const& storage, std::string const&, tse::OhlcvTick const& tick) -> bool
		{
			storage.push(tick.tsNanoseconds, tick.close);
			return true;
		},
		prices, {contractSymbol}
	);

	// Every trader ignores the price and votes by a coin flip: the price feed is only the
	// heartbeat that makes all three of them decide once per bar.
	for (std::string const& trader : traderLabels) {
		std::string const
			longPattern {trader + " goes long"},
			flatPattern {trader + " goes flat"},
			enterRule {trader + " enter"},
			exitRule {trader + " exit"};
		account.addPatternFormula(longPattern, tse::Duration::minutes, {"WTI price"}, coinFlip(generator, draw, voteChance));
		account.addPatternFormula(flatPattern, tse::Duration::minutes, {"WTI price"}, coinFlip(generator, draw, voteChance));
		account.addRuleMarket(enterRule, tse::RuleType::entry, helpers::entryParams(traderQuantity), longPattern, contractSymbol);
		account.addRuleMarket(exitRule, tse::RuleType::exit, closeParams(traderQuantity), flatPattern, contractSymbol);
		account.addRobot(trader, {enterRule, exitRule});
	}

	// The group is assembled right here: the three traders' fills are replayed into an
	// executed-trade adapter, the fourth robot's input over that channel keeps their signed
	// sum - a long fill adds, a short fill subtracts - and the two thresholds over that net
	// put the fourth robot long while the group is net long and flat when the net is back to zero.
	double groupNet {0.0};
	account.addInputExecuted
	(
		"Group net", 1, tse::Duration::minutes,
		[&groupNet](tse::Storage const& storage, std::string const&, tse::Retained const& trade) -> bool
		{
			if (trade.txnSide == tse::Side::long_) {
				groupNet += trade.quantity;
			} else {
				groupNet -= trade.quantity;
			}
			storage.push(trade.tsNanoseconds, groupNet);
			return true;
		},
		executed, {contractSymbol}
	);
	account.addPatternThreshold("Group is long", tse::Duration::minutes, {"Group net"}, tse::Cmp::gt, 0.0);
	account.addPatternThreshold("Group is flat", tse::Duration::minutes, {"Group net"}, tse::Cmp::le, 0.0);
	account.addRuleMarket("Voter enter", tse::RuleType::entry, helpers::entryParams(voterQuantity), "Group is long", contractSymbol);
	account.addRuleMarket("Voter exit", tse::RuleType::exit, closeParams(voterQuantity), "Group is flat", contractSymbol);
	account.addRobot(voterLabel, {"Voter enter", "Voter exit"});

	for (std::string const& trader : traderLabels) {
		account.start(trader);
	}
	account.start(voterLabel);

	std::size_t seenTrades {0};
	for (tse::OhlcvTick const& tick : ticks) {
		prices.pushOhlcv(contractSymbol, tick);
		if (static_cast<std::size_t>(execution.getCount()) != seenTrades) {
			forwardGroupFills(account, executed, voterLabel, seenTrades);
		}
	}

	tse::Summary const
		first {account.getRobotSummary(traderLabels[0])},
		second {account.getRobotSummary(traderLabels[1])},
		third {account.getRobotSummary(traderLabels[2])},
		voter {account.getRobotSummary(voterLabel)};

	std::printf
	(
		"voting group: trader trades=%lld/%lld/%lld voter trades=%lld voter netProfit=%.4f\n",
		static_cast<long long>(first.totalNumberOfTrades),
		static_cast<long long>(second.totalNumberOfTrades),
		static_cast<long long>(third.totalNumberOfTrades),
		static_cast<long long>(voter.totalNumberOfTrades),
		voter.totalNetProfit
	);
	return 0;
}
