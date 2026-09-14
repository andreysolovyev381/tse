#include "tse_helpers.hpp"

#include <cstdio>
#include <string>

namespace {

	tse::Retained makeManualTrade
	(
		std::string clientOrderId,
		double price,
		double quantity,
		tse::Side txnSide,
		std::int64_t ts
	);

}

int main()
{
	tse::setLogLevel(tse::LogLevel::none);

	tse::Account account {"ManualBooking", tse::StorageRegime::mem};
	account.addContract("TSLA", 1, tse::Instrument::equity, tse::Underlying::equity, tse::Venue::NASDAQ, 100000);
	account.portfolioAddContract("TSLA");

	// Fills the engine never placed - a phone order, another desk - are booked by hand:
	// the booking moves the position and returns the P&L the closing part realised.
	account.bookTrade(makeManualTrade("manual-1", 100.0, 5.0, tse::Side::long_, 1000000000LL));
	double const positionMovingBookedPL {account.bookTrade(makeManualTrade("manual-2", 110.0, 2.0, tse::Side::short_, 2000000000LL))};
	account.bookTrade(makeManualTrade("manual-3", 110.0, 3.0, tse::Side::short_, 3000000000LL));

	// The second mode prices a fill against exposure snapshots the caller supplies, which is how
	// a what-if is costed: it answers with the P&L and leaves the live position exactly as it was.
	tse::Retained exposureTrade {makeManualTrade("manual-4", 120.0, 4.0, tse::Side::short_, 4000000000LL)};
	exposureTrade.posSide = tse::Side::long_;
	tse::ContractExposure const contractExposure
	{
		10.0, 0.0, 100.0, 1000000000LL, 110.0, 3000000000LL, tse::Side::long_
	};
	tse::PortfolioExposure const portfolioExposure
	{
		1000.0, 1100.0, 100.0, tse::Side::long_
	};
	double const exposureBookedPL {account.bookTradeWithExposure(exposureTrade, contractExposure, portfolioExposure)};

	tse::PositionState const finalState {account.getPositionState("TSLA")};
	std::printf("positionMovingBookedPL=%.4f exposureBookedPL=%.4f finalQuantity=%.1f\n", positionMovingBookedPL, exposureBookedPL, finalState.quantity);
	return 0;
}

namespace {

	tse::Retained makeManualTrade
	(
		std::string clientOrderId,
		double price,
		double quantity,
		tse::Side txnSide,
		std::int64_t ts
	)
	{
		tse::Retained trade;
		trade.tsNanoseconds = ts;
		trade.symbol = "TSLA";
		trade.contractId = 0;
		trade.clientOrderId = clientOrderId;
		trade.brokerOrderId = std::move(clientOrderId);
		trade.ruleLabel = "Manual";
		trade.robotLabel = "ManualBot";
		trade.tsMktEventNanoseconds = ts;
		trade.tsExecutionNanoseconds = ts;
		trade.price = price;
		trade.quantity = quantity;
		trade.fee = 0.0;
		trade.bookedPL = 0.0;
		trade.txnSide = txnSide;
		trade.posSide = tse::Side::neutral;
		trade.bookMode = 1;
		trade.execution = 2;
		trade.priceType = tse::PriceType::market;
		trade.quantityType = tse::QuantityMode::fixed;
		trade.tif = tse::Tif::day;
		trade.priorityType = tse::Priority::replaceable;
		return trade;
	}

}
