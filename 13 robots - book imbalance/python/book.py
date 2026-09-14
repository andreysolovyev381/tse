import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H

tse = H.tse

_message_counter = {"value": 0}


def new_order(ts_nanoseconds, side, price, quantity):
    _message_counter["value"] += 1
    ident = _message_counter["value"]
    return tse.make_book_message(tse.BookMessageKind.New, ts_nanoseconds, ident, ident, price, quantity, side)


def cancel_order(ts_nanoseconds, resting_id):
    return tse.make_book_message(tse.BookMessageKind.Cancel, ts_nanoseconds, resting_id, resting_id, 0.0, 0.0, tse.Side.Neutral)


def market_params(txn_side, pos_side):
    return tse.make_rule_params(
        tse.Quantity.Fixed, 100.0,
        tse.Price.Market, 0.0, 0.0, 0.0,
        txn_side, pos_side, tse.Tif.Day, 10,
    )


def main():
    ticks = H.load_bidask("btc_orderflow.csv")

    account = H.account("BookImbalance", tse.StorageRegime.Mem, tse.LogLevel.Off)
    book_mkt = account.create_market("book", tse.MdType.Book)
    price_mkt = account.create_market("price", tse.MdType.Trade)
    account.create_simulator("Sim", H.simulator_options(), 64, -1)
    account.add_contract("BOOKA", 1, tse.Instrument.Equity, tse.Underlying.Equity, tse.Venue.Undefined, 10000)
    book = account.create_book("BOOKA", tse.BookLevelKind.L3)

    # Imbalance over the whole book answers one question: whose resting size is bigger, the
    # buyers' or the sellers'. It reads +1 when only bids rest, -1 when only offers do. The 4
    # is the length of the input's own storage, not a book depth.
    account.add_input_book_imbalance("Imbalance", 4, tse.Duration.Nanoseconds, book, book_mkt, ["BOOKA"])

    # A fifth more size on one side is treated as pressure worth joining, and the opposite
    # reading is what takes the position off again.
    account.add_pattern_threshold("ToLong", tse.Duration.Nanoseconds, ["Imbalance"], tse.Cmp.Ge, 0.2)
    account.add_pattern_threshold("ToShort", tse.Duration.Nanoseconds, ["Imbalance"], tse.Cmp.Le, -0.2)

    account.add_rule_market("EnterLong", tse.RuleType.Entry, market_params(tse.Side.Long, tse.Side.Neutral), "ToLong", "BOOKA")
    account.add_rule_market("ExitLong", tse.RuleType.Exit, market_params(tse.Side.Short, tse.Side.Long), "ToShort", "BOOKA")
    account.add_rule_market("EnterShort", tse.RuleType.Entry, market_params(tse.Side.Short, tse.Side.Neutral), "ToShort", "BOOKA")
    account.add_rule_market("ExitShort", tse.RuleType.Exit, market_params(tse.Side.Long, tse.Side.Short), "ToLong", "BOOKA")
    account.add_robot("Robot", ["EnterLong", "ExitLong", "EnterShort", "ExitShort"])
    account.portfolio_subscribe(price_mkt, "BOOKA")
    account.start("Robot")

    # Each recorded snapshot is replayed as the two orders it implies, one per side, and the
    # pair the previous snapshot left resting is pulled right after, so the book always holds
    # the current quote alone instead of a pile of every quote ever seen. Then comes the print
    # that the market actually paid between the snapshots.
    resting_bid = 0
    resting_ask = 0
    for tick in ticks:
        bid = new_order(tick.tsNanoseconds, tse.Side.Long, tick.bid, tick.bidVolume)
        ask = new_order(tick.tsNanoseconds + 1, tse.Side.Short, tick.ask, tick.askVolume)
        book_mkt.push_book_by_name("BOOKA", bid)
        book_mkt.push_book_by_name("BOOKA", ask)
        if resting_bid != 0:
            book_mkt.push_book_by_name("BOOKA", cancel_order(tick.tsNanoseconds + 2, resting_bid))
            book_mkt.push_book_by_name("BOOKA", cancel_order(tick.tsNanoseconds + 3, resting_ask))
        resting_bid = bid.messageId
        resting_ask = ask.messageId
        price_mkt.push_trade_by_name("BOOKA", tse.TseTickTrade(tick.tsNanoseconds + 4, tick.last, 1.0, int(tse.Side.Trade)))

    summary = account.get_summary()
    account.close()

    print("netProfit={:.4f} trades={}".format(summary.totalNetProfit, summary.totalNumberOfTrades))
    return 0


if __name__ == "__main__":
    sys.exit(main())
