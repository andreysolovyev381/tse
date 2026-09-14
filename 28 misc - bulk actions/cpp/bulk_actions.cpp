#include "tse_helpers.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace {

	std::string const symbol {"BULK"};

	std::int64_t constexpr base {1000000000LL};

	double constexpr marketPrice {100.0};
	double constexpr restingLimit {150.0};
	double constexpr probePrice {155.0};
	double constexpr tradedQuantity {40.0};

	std::uint64_t messageCounter {0};

	void pushSignal
	(
		tse::Market const& market,
		std::int64_t const tsNanoseconds,
		tse::Side const side
	)
	{
		++messageCounter;
		market.pushBook(symbol, tse::BookMessage {tse::BookMessageKind::new_, tsNanoseconds, messageCounter, messageCounter, marketPrice, tradedQuantity, side});
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

	struct Rig final {
		tse::Account account;
		tse::Market signals;
		tse::Market prices;
	};

	Rig makeRig(std::string label)
	{
		tse::Account account {std::move(label), tse::StorageRegime::mem, tse::Currency::usd, -1};
		tse::Market const signals {account.createMarket("Bulk signals", tse::MdType::book)};
		tse::Market const prices {account.createMarket("Bulk prices", tse::MdType::trade)};
		account.createSimulator("sim", helpers::simulatorConfig(), 64, -1);
		account.addContract(symbol, 1, tse::Instrument::equity, tse::Underlying::equity, tse::Venue::undefined, 50000);
		account.addInputBook
		(
			"BulkOpenSignal", 4, tse::Duration::nanoseconds,
			[](tse::Storage const& storage, std::string const&, tse::BookMessage const& message) -> bool
			{
				if (message.kind != tse::BookMessageKind::new_) { return false; }
				if (message.txnSide != tse::Side::long_) { return false; }
				storage.push(message.tsNanoseconds, message.quantity);
				return true;
			},
			signals, {symbol}, -1
		);
		account.addInputBook
		(
			"BulkExitSignal", 4, tse::Duration::nanoseconds,
			[](tse::Storage const& storage, std::string const&, tse::BookMessage const& message) -> bool
			{
				if (message.kind != tse::BookMessageKind::new_) { return false; }
				if (message.txnSide != tse::Side::short_) { return false; }
				storage.push(message.tsNanoseconds, message.quantity);
				return true;
			},
			signals, {symbol}, -1
		);
		account.addPatternFormula
		(
			"BulkOpenPattern", tse::Duration::nanoseconds, {"BulkOpenSignal"},
			[](std::string const&, std::int64_t, double) -> bool
			{
				return true;
			},
			-1
		);
		account.addPatternFormula
		(
			"BulkExitPattern", tse::Duration::nanoseconds, {"BulkExitSignal"},
			[](std::string const&, std::int64_t, double) -> bool
			{
				return true;
			},
			-1
		);
		account.addRuleMarket("BulkEntryRule", tse::RuleType::entry, helpers::entryParams(tradedQuantity), "BulkOpenPattern", symbol);
		account.addRuleMarket
		(
			"BulkRestingExit", tse::RuleType::exit,
			tse::RuleParams
			{
				tse::QuantityMode::fixed, tradedQuantity,
				tse::PriceType::limit, restingLimit, 0.0, 0.0,
				tse::Side::short_, tse::Side::long_, tse::Tif::day, 10
			},
			"BulkExitPattern", symbol
		);
		account.addRobot("BulkRobot", {"BulkEntryRule", "BulkRestingExit"});
		account.portfolioSubscribe(prices, symbol);
		account.start("BulkRobot");
		return Rig {std::move(account), signals, prices};
	}

	std::int64_t runCancelSaleScenario()
	{
		Rig rig {makeRig("BulkCancelSale")};

		// The exit rests as a limit at 150 while the market prints 100, so it waits on the book unfilled.
		// cancelAll pulls resting orders, saleAll flattens the position; neither stops the robot trading.
		pushSignal(rig.signals, base, tse::Side::long_);
		pushPrice(rig.prices, base * 2, marketPrice);
		pushSignal(rig.signals, base * 3, tse::Side::short_);
		pushPrice(rig.prices, base * 4, marketPrice);

		rig.account.cancelAll("BulkRobot");
		rig.account.saleAll("BulkRobot");
		pushPrice(rig.prices, base * 5, marketPrice);

		pushSignal(rig.signals, base * 6, tse::Side::long_);
		pushPrice(rig.prices, base * 7, marketPrice);
		pushSignal(rig.signals, base * 8, tse::Side::short_);
		pushPrice(rig.prices, base * 9, marketPrice);

		rig.account.haltAndCancelSaleAll("BulkRobot");
		pushPrice(rig.prices, base * 10, marketPrice);
		pushSignal(rig.signals, base * 11, tse::Side::long_);
		pushPrice(rig.prices, base * 12, marketPrice);

		return rig.account.getSummary().totalNumberOfTrades;
	}

	std::int64_t runHaltCancelScenario()
	{
		Rig rig {makeRig("BulkHaltCancel")};

		// haltAndCancelAll is the panic button that keeps the risk: robot stopped, resting exit withdrawn,
		// position left open. The probe at 155 would have filled that exit, and now nothing happens.
		pushSignal(rig.signals, base, tse::Side::long_);
		pushPrice(rig.prices, base * 2, marketPrice);
		pushSignal(rig.signals, base * 3, tse::Side::short_);
		pushPrice(rig.prices, base * 4, marketPrice);

		rig.account.haltAndCancelAll("BulkRobot");
		pushPrice(rig.prices, base * 5, probePrice);

		pushSignal(rig.signals, base * 6, tse::Side::long_);
		pushPrice(rig.prices, base * 7, marketPrice);

		return rig.account.getSummary().totalNumberOfTrades;
	}

	std::int64_t runHaltSaleScenario()
	{
		Rig rig {makeRig("BulkHaltSale")};

		// haltAndSaleAll is the panic button that leaves no risk behind: robot stopped, position
		// closed at market, so every signal that arrives afterwards reaches a flat desk.
		pushSignal(rig.signals, base, tse::Side::long_);
		pushPrice(rig.prices, base * 2, marketPrice);

		rig.account.haltAndSaleAll("BulkRobot");
		pushPrice(rig.prices, base * 3, marketPrice);

		pushSignal(rig.signals, base * 4, tse::Side::long_);
		pushPrice(rig.prices, base * 5, marketPrice);

		return rig.account.getSummary().totalNumberOfTrades;
	}

}//!namespace

int main()
{
	tse::setLogLevel(tse::LogLevel::none);

	std::int64_t const cancelSaleTrades {runCancelSaleScenario()};
	std::int64_t const haltCancelTrades {runHaltCancelScenario()};
	std::int64_t const haltSaleTrades {runHaltSaleScenario()};

	std::printf
	(
		"bulk_actions: cancelSaleTrades=%lld haltCancelTrades=%lld haltSaleTrades=%lld\n",
		static_cast<long long>(cancelSaleTrades),
		static_cast<long long>(haltCancelTrades),
		static_cast<long long>(haltSaleTrades)
	);
	return 0;
}
