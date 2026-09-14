#include "tse_helpers.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace {

	std::string const symbol {"Symbol_1_Close"};

	// The two moments of the story: at the first one the robot puts an order on the book,
	// at the second one it changes its mind about that order.
	std::int64_t constexpr enterCheckPoint {1000000000LL};
	std::int64_t constexpr amendCheckPoint {2000000000LL};
	std::int64_t constexpr hugeCoolDown {1000000000000000LL};

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

	struct Rig final {
		tse::Account account;
		tse::Market market;
	};

	Rig makeRig
	(
		std::string label,
		std::string fireLabel
	)
	{
		tse::Account account {std::move(label), tse::StorageRegime::mem};
		tse::Market const market {account.createMarket("price", tse::MdType::trade)};
		account.createSimulator("sim", helpers::simulatorConfig(), 64, -1);
		account.addContract(symbol, 1, tse::Instrument::equity, tse::Underlying::undefined, tse::Venue::undefined, 100000);
		account.addInputTrade("Px", 2, tse::Duration::nanoseconds, priceProcessor(), market, {symbol});
		account.addPatternTimestamp("EnterAt", tse::Duration::nanoseconds, {"Px"}, enterCheckPoint, hugeCoolDown);
		account.addPatternTimestamp(std::move(fireLabel), tse::Duration::nanoseconds, {"Px"}, amendCheckPoint, hugeCoolDown);
		return Rig {std::move(account), market};
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

	enum class AmendKind {
		cancel,
		modify,
		replace
	};

	double runAmendScenario
	(
		std::string label,
		AmendKind const kind
	)
	{
		Rig rig {makeRig(std::move(label), "AmendAt")};
		rig.account.addRuleMarket
		(
			"Enter", tse::RuleType::entry,
			tse::RuleParams
			{
				tse::QuantityMode::fixed, 10.0,
				tse::PriceType::limit, 99.0, 0.0, 0.0,
				tse::Side::long_, tse::Side::neutral, tse::Tif::day, 10
			},
			"EnterAt", symbol
		);
		// Three ways to change your mind about an order already resting at the venue: withdraw it,
		// shrink it, or pull it and post a fresh one at a different price.
		if (kind == AmendKind::cancel) {
			rig.account.addRuleCancel("Amend", symbol, 10.0, 10.0, "AmendAt");
		}
		if (kind == AmendKind::modify) {
			rig.account.addRuleModify("Amend", symbol, 4.0, 99.0, "AmendAt");
		}
		if (kind == AmendKind::replace) {
			rig.account.addRuleReplace("Amend", symbol, 4.0, 98.0, "AmendAt");
		}
		rig.account.addRobot("AmendRobot", {"Enter", "Amend"});
		rig.account.start("AmendRobot");

		pushPrice(rig.market, enterCheckPoint, 100.0);
		pushPrice(rig.market, amendCheckPoint, 100.0);
		return rig.account.getPositionState(symbol).quantity;
	}

	tse::Summary runOcoScenario()
	{
		Rig rig {makeRig("RiskManagementExternalTools OcoPair", "OcoAt")};
		rig.account.addRuleMarket("Enter", tse::RuleType::entry, helpers::entryParams(100.0), "EnterAt", symbol);
		// Stop and target left resting at the venue as one pair: whichever is hit first cancels
		// the other, so the position can never be closed twice.
		rig.account.addRuleOco
		(
			"OCO", symbol,
			tse::VenueRiskSpec
			{
				tse::VenueRiskLeg {true, 0, 0.02},
				tse::VenueRiskLeg {true, 0, 0.03},
				tse::Tif::gtc
			},
			"OcoAt"
		);
		rig.account.addRobot("OcoRobot", {"Enter", "OCO"});
		rig.account.start("OcoRobot");

		pushPrice(rig.market, enterCheckPoint, 100.0);
		pushPrice(rig.market, 1200000000LL, 100.0);
		pushPrice(rig.market, amendCheckPoint, 100.0);
		pushPrice(rig.market, 3000000000LL, 97.0);
		pushPrice(rig.market, 4000000000LL, 97.0);

		return rig.account.getSummary();
	}

}

int main()
{
	tse::setLogLevel(tse::LogLevel::none);

	double const afterCancel {runAmendScenario("RiskManagementExternalTools AmendCancel", AmendKind::cancel)};
	double const afterModify {runAmendScenario("RiskManagementExternalTools AmendModify", AmendKind::modify)};
	double const afterReplace {runAmendScenario("RiskManagementExternalTools AmendReplace", AmendKind::replace)};
	tse::Summary const oco {runOcoScenario()};

	std::printf
	(
		"oco_amend: afterCancel=%.1f afterModify=%.1f afterReplace=%.1f ocoNetProfit=%.2f ocoTrades=%lld\n",
		afterCancel, afterModify, afterReplace, oco.totalNetProfit, static_cast<long long>(oco.totalNumberOfTrades)
	);
	return 0;
}
