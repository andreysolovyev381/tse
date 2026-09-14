import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H

tse = H.tse


def main():
    account = H.account("ManualBooking", tse.StorageRegime.Mem, tse.LogLevel.Off)
    account.add_contract("TSLA", 1, tse.Instrument.Equity, tse.Underlying.Equity, tse.Venue.NASDAQ, 100000)
    account.portfolio_add_contract("TSLA")

    # Fills the engine never placed - a phone order, another desk - are booked by hand:
    # the booking moves the position and returns the P&L the closing part realised.
    account.book_trade(make_manual_trade("manual-1", 100.0, 5.0, tse.Side.Long, 1_000_000_000))
    position_moving_booked_pl = account.book_trade(make_manual_trade("manual-2", 110.0, 2.0, tse.Side.Short, 2_000_000_000))
    account.book_trade(make_manual_trade("manual-3", 110.0, 3.0, tse.Side.Short, 3_000_000_000))

    # The second mode prices a fill against exposure snapshots the caller supplies, which is how
    # a what-if is costed: it answers with the P&L and leaves the live position exactly as it was.
    exposure_trade = make_manual_trade("manual-4", 120.0, 4.0, tse.Side.Short, 4_000_000_000)
    exposure_trade.posSide = int(tse.Side.Long)
    contract_exposure = tse.make_contract_exposure(10.0, 0.0, 100.0, 1_000_000_000, 110.0, 3_000_000_000, tse.Side.Long)
    portfolio_exposure = tse.make_portfolio_exposure(1000.0, 1100.0, 100.0, tse.Side.Long)
    exposure_booked_pl = account.book_trade(exposure_trade, contract_exposure, portfolio_exposure)

    final_state = account.get_position_state("TSLA")
    account.close()

    print("positionMovingBookedPL={:.4f} exposureBookedPL={:.4f} finalQuantity={:.1f}".format(
        position_moving_booked_pl, exposure_booked_pl, final_state.quantity))
    return 0


def make_manual_trade(client_order_id, price, quantity, txn_side, ts):
    return tse.make_retained(
        ts, "TSLA", 0,
        client_order_id, client_order_id,
        "Manual", "ManualBot",
        ts, ts,
        price, quantity, 0.0, 0.0,
        txn_side, tse.Side.Neutral,
        1, 2,
        tse.Price.Market, tse.Quantity.Fixed, tse.Tif.Day, tse.Priority.Replaceable,
    )


if __name__ == "__main__":
    sys.exit(main())
