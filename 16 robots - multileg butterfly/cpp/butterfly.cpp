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

	// A call butterfly buys one wing, sells two at the body and buys the far wing:
	// it pays while the underlying stays near the body strike, and the wings cap the loss.
	std::vector<LegSpec> legs
	{
		LegSpec {"SPY   260116C00440000", 1.0, tse::Side::long_, 0.0, 0.0},
		LegSpec {"SPY   260116C00450000", 2.0, tse::Side::short_, 0.0, 0.0},
		LegSpec {"SPY   260116C00460000", 1.0, tse::Side::long_, 0.0, 0.0}
	};
	loadLegPrices("multileg_spy_butterfly.csv", legs);

	tse::Account account {"SpyButterfly", tse::StorageRegime::mem};
	tse::Market const market {account.createMarket("price", tse::MdType::trade)};
	account.createSimulator("sim", helpers::simulatorConfig(), 64, -1);
	// One option contract carries a hundred shares, so a dollar on a leg is a hundred dollars of P&L.
	for (LegSpec const& leg : legs) {
		account.addContract(leg.symbol, 100, tse::Instrument::option, tse::Underlying::equity, tse::Venue::CBOE, 100000);
	}
	account.addInputTrade("Px", 2, tse::Duration::nanoseconds, priceProcessor(), market, {legs[0].symbol});
	account.addPatternTimestamp("EnterAt", tse::Duration::nanoseconds, {"Px"}, enterCheckPoint, hugeCoolDown);
	account.addPatternTimestamp("ExitAt", tse::Duration::nanoseconds, {"Px"}, exitCheckPoint, hugeCoolDown);
	// A multileg transaction is all or nothing: the three legs fill at one instant against
	// a single net price, or none of them fills.
	account.addRuleMultileg("Butterfly enter", entryLegs(legs), "EnterAt");
	account.addRuleMultileg("Butterfly exit", exitLegs(legs), "ExitAt");
	account.addRobot("SPY_BUTTERFLY", {"Butterfly enter", "Butterfly exit"});
	for (LegSpec const& leg : legs) {
		account.portfolioSubscribe(market, leg.symbol);
	}
	account.start("SPY_BUTTERFLY");

	pushPrice(market, legs[0].symbol, enterCheckPoint, legs[0].entryPrice);
	pushPrice(market, legs[0].symbol, 1000000110LL, legs[0].entryPrice);
	pushPrice(market, legs[1].symbol, 1000000120LL, legs[1].entryPrice);
	pushPrice(market, legs[2].symbol, 1000000130LL, legs[2].entryPrice);

	pushPrice(market, legs[0].symbol, exitCheckPoint, legs[0].exitPrice);
	pushPrice(market, legs[2].symbol, 2000000200LL, legs[2].exitPrice);
	pushPrice(market, legs[1].symbol, 2000000200LL, legs[1].exitPrice);
	pushPrice(market, legs[0].symbol, 2000000200LL, legs[0].exitPrice);

	tse::Summary const summary {account.getSummary()};

	std::printf("butterfly: netProfit=%.2f\n", summary.totalNetProfit);
	return 0;
}
