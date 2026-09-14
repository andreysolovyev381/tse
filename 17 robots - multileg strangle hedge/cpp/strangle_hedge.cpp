#include "tse_helpers.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

	std::int64_t constexpr enterCheckPoint {1000000000LL};
	std::int64_t constexpr exitCheckPoint {2000000000LL};
	std::int64_t constexpr hugeCoolDown {1000000000000000LL};

	struct LegSpec final {
		std::string symbol;
		tse::Instrument instrument;
		double quantity;
		tse::Side entrySide;
		double entryPrice;
		double exitPrice;
	};

	tse::Side flipSide(tse::Side const side)
	{
		if (side == tse::Side::long_) {
			return tse::Side::short_;
		}
		if (side == tse::Side::short_) {
			return tse::Side::long_;
		}
		return tse::Side::neutral;
	}

	std::vector<std::vector<double>> loadPriceRows(std::string const& name)
	{
		std::ifstream file {helpers::dataPath(name)};
		std::vector<std::vector<double>> rows;
		std::string line;
		std::getline(file, line);
		while (std::getline(file, line)) {
			if (line.empty()) {
				continue;
			}
			std::vector<double> row;
			std::size_t
				start {0},
				column {0};
			while (start <= line.size()) {
				std::size_t const sep {line.find(',', start)};
				std::string const cell {line.substr(start, sep == std::string::npos ? std::string::npos : sep - start)};
				if (column > 0) {
					row.push_back(std::stod(cell));
				}
				++column;
				if (sep == std::string::npos) {
					break;
				}
				start = sep + 1;
			}
			rows.push_back(row);
		}
		return rows;
	}

	void loadLegPrices
	(
		std::string const& name,
		std::vector<LegSpec>& legs
	)
	{
		std::vector<std::vector<double>> const rows {loadPriceRows(name)};
		for (std::size_t i {0}; i < legs.size(); ++i) {
			legs[i].entryPrice = rows[0][i];
			legs[i].exitPrice = rows[1][i];
		}
	}

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

	tse::LegDescriptor makeLeg
	(
		std::string symbol,
		double const quantity,
		double const limitPrice,
		tse::TxnType const txnType,
		tse::Side const txnSide,
		tse::Side const posSide
	)
	{
		return tse::LegDescriptor
		{
			std::move(symbol),
			tse::QuantityMode::fixed,
			quantity,
			tse::PriceType::limit,
			limitPrice,
			0.0,
			0.0,
			txnType,
			txnSide,
			posSide,
			tse::Tif::day,
			tse::Priority::replaceable
		};
	}

	std::vector<tse::LegDescriptor> entryLegs(std::vector<LegSpec> const& legs)
	{
		std::vector<tse::LegDescriptor> out;
		out.reserve(legs.size());
		for (LegSpec const& leg : legs) {
			out.push_back(makeLeg(leg.symbol, leg.quantity, leg.entryPrice, tse::TxnType::enter, leg.entrySide, tse::Side::neutral));
		}
		return out;
	}

	std::vector<tse::LegDescriptor> exitLegs(std::vector<LegSpec> const& legs)
	{
		std::vector<tse::LegDescriptor> out;
		out.reserve(legs.size());
		for (LegSpec const& leg : legs) {
			out.push_back(makeLeg(leg.symbol, leg.quantity, leg.exitPrice, tse::TxnType::exit, flipSide(leg.entrySide), leg.entrySide));
		}
		return out;
	}

	void pushPrice
	(
		tse::Market const& market,
		std::string const& symbol,
		std::int64_t const tsNanoseconds,
		double const price
	)
	{
		market.pushTrade(symbol, tse::TradeTick {tsNanoseconds, price, 1.0, tse::Side::trade});
	}

}

int main()
{
	tse::setLogLevel(tse::LogLevel::none);

	// The future carries the directional view, the two puts underneath insure it against a fall
	// and the far call buys back the upside: four legs, one hedged position.
	std::vector<LegSpec> legs
	{
		LegSpec {"CLF26", tse::Instrument::future, 1.0, tse::Side::long_, 0.0, 0.0},
		LegSpec {"LO F26 P00045000", tse::Instrument::option, 1.0, tse::Side::long_, 0.0, 0.0},
		LegSpec {"LO F26 P00044000", tse::Instrument::option, 1.0, tse::Side::long_, 0.0, 0.0},
		LegSpec {"LO F26 C00060000", tse::Instrument::option, 1.0, tse::Side::long_, 0.0, 0.0}
	};
	loadLegPrices("multileg_wti_strangle.csv", legs);

	tse::Account account {"WtiStrangle", tse::StorageRegime::mem};
	tse::Market const market {account.createMarket("price", tse::MdType::trade)};
	account.createSimulator("sim", helpers::simulatorConfig(), 64, -1);
	// A WTI contract is a thousand barrels, so a dollar on the future is a thousand dollars of P&L.
	for (LegSpec const& leg : legs) {
		account.addContract(leg.symbol, 1000, leg.instrument, tse::Underlying::commodity, tse::Venue::CME, 100000);
	}
	account.addInputTrade("Px", 2, tse::Duration::nanoseconds, priceProcessor(), market, {legs[0].symbol});
	account.addPatternTimestamp("EnterAt", tse::Duration::nanoseconds, {"Px"}, enterCheckPoint, hugeCoolDown);
	account.addPatternTimestamp("ExitAt", tse::Duration::nanoseconds, {"Px"}, exitCheckPoint, hugeCoolDown);
	// A multileg transaction is all or nothing: the four legs fill at one instant against
	// a single net price, or none of them fills, so the hedge can never go on half-built.
	account.addRuleMultileg("Strangle enter", entryLegs(legs), "EnterAt");
	account.addRuleMultileg("Strangle exit", exitLegs(legs), "ExitAt");
	account.addRobot("WTI_STRANGLE", {"Strangle enter", "Strangle exit"});
	for (LegSpec const& leg : legs) {
		account.portfolioSubscribe(market, leg.symbol);
	}
	account.start("WTI_STRANGLE");

	pushPrice(market, legs[0].symbol, enterCheckPoint, legs[0].entryPrice);
	pushPrice(market, legs[0].symbol, 1000000110LL, legs[0].entryPrice);
	pushPrice(market, legs[1].symbol, 1000000120LL, legs[1].entryPrice);
	pushPrice(market, legs[2].symbol, 1000000130LL, legs[2].entryPrice);
	pushPrice(market, legs[3].symbol, 1000000140LL, legs[3].entryPrice);

	pushPrice(market, legs[0].symbol, exitCheckPoint, legs[0].exitPrice);
	pushPrice(market, legs[0].symbol, 2000000200LL, legs[0].exitPrice);
	pushPrice(market, legs[1].symbol, 2000000200LL, legs[1].exitPrice);
	pushPrice(market, legs[2].symbol, 2000000200LL, legs[2].exitPrice);
	pushPrice(market, legs[3].symbol, 2000000200LL, legs[3].exitPrice);

	tse::Summary const summary {account.getSummary()};

	std::printf("strangle_hedge: netProfit=%.2f\n", summary.totalNetProfit);
	return 0;
}
