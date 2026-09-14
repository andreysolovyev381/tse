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


def trade_tick(ts_nanoseconds, price):
    return tse.TseTickTrade(ts_nanoseconds, price, 1.0, int(tse.Side.Trade))


def market_params(txn_side, pos_side):
    return tse.make_rule_params(
        tse.Quantity.Fixed, 100.0,
        tse.Price.Market, 0.0, 0.0, 0.0,
        txn_side, pos_side, tse.Tif.Day, 10,
    )


# A laboratory feed rather than a recording: every step adds exactly the size that drags the
# book onto the next target imbalance, so the sequence of signals is known before the run.
def make_imbalance_feed(targets, prices, bid_price, ask_price, initial_bid_quantity):
    base = 1000000000
    half = 500000000
    book_feed = []
    price_feed = []
    sb = initial_bid_quantity
    sa = 0.0
    step = 0

    price_feed.append(trade_tick(base - half, prices[0]))
    book_feed.append(new_order(base * (2 * step + 1), tse.Side.Long, bid_price, initial_bid_quantity))
    price_feed.append(trade_tick(base * (2 * step + 1) + half, prices[0]))
    step += 1
    for i, t in enumerate(targets):
        curr = (sb - sa) / (sb + sa)
        add_bid = t > curr
        if add_bid:
            delta = (sa * (1.0 + t) / (1.0 - t)) - sb
            sb += delta
            side = tse.Side.Long
            level_price = bid_price
        else:
            delta = (sb * (1.0 - t) / (1.0 + t)) - sa
            sa += delta
            side = tse.Side.Short
            level_price = ask_price
        book_feed.append(new_order(base * (2 * step + 1), side, level_price, delta))
        price_feed.append(trade_tick(base * (2 * step + 1) + half, prices[i + 1]))
        step += 1
    return book_feed, price_feed


def run_scenario(level, kind, book_feed, price_feed):
    symbol = "BOOK" + level
    long_label = level + "Long"
    short_label = level + "Short"

    account = H.account("Ofi" + level, tse.StorageRegime.Mem, tse.LogLevel.Off)
    book_mkt = account.create_market("book", tse.MdType.Book)
    price_mkt = account.create_market("price", tse.MdType.Trade)
    account.create_simulator("sim", H.simulator_options(), 64, -1)
    account.add_contract(symbol, 1, tse.Instrument.Equity, tse.Underlying.Equity, tse.Venue.Undefined, 50000)
    book = account.create_book(symbol, kind)

    # Resting-size imbalance over the whole book, with a fifth of extra size on one side
    # taken as pressure worth joining; the opposite reading closes the position. The 4 is the
    # length of the input's own storage, not a book depth.
    account.add_input_book_imbalance(level, 4, tse.Duration.Nanoseconds, book, book_mkt, [symbol])
    account.add_pattern_threshold(long_label, tse.Duration.Nanoseconds, [level], tse.Cmp.Ge, 0.2)
    account.add_pattern_threshold(short_label, tse.Duration.Nanoseconds, [level], tse.Cmp.Le, -0.2)

    account.add_rule_market("EnterLong", tse.RuleType.Entry, market_params(tse.Side.Long, tse.Side.Neutral), long_label, symbol)
    account.add_rule_market("ExitLong", tse.RuleType.Exit, market_params(tse.Side.Short, tse.Side.Long), short_label, symbol)
    account.add_rule_market("EnterShort", tse.RuleType.Entry, market_params(tse.Side.Short, tse.Side.Neutral), short_label, symbol)
    account.add_rule_market("ExitShort", tse.RuleType.Exit, market_params(tse.Side.Long, tse.Side.Short), long_label, symbol)
    account.add_robot("Robot", ["EnterLong", "ExitLong", "EnterShort", "ExitShort"])
    account.portfolio_subscribe(price_mkt, symbol)
    account.start("Robot")

    price_mkt.push_trade_by_name(symbol, price_feed[0])
    for i, message in enumerate(book_feed):
        book_mkt.push_book_by_name(symbol, message)
        price_mkt.push_trade_by_name(symbol, price_feed[i + 1])

    summary = account.get_summary()
    account.close()
    return summary


def main():
    targets = [-0.4, -0.5, 0.4, 0.5, -0.4, -0.5, 0.4, 0.5, -0.4, -0.5, 0.4]
    prices = [100, 102, 102, 100, 100, 102, 102, 100, 100, 102, 102, 100]
    book_feed, price_feed = make_imbalance_feed(targets, prices, 99.95, 100.05, 12.0)

    levels = ["L1", "L2", "L3"]
    kinds = [tse.BookLevelKind.L1, tse.BookLevelKind.L2, tse.BookLevelKind.L3]

    # The same strategy over the same feed, on the three book depths a venue may publish. The
    # feed touches one price per side, so the depth changes what is stored, never the verdict.
    results = []
    for i in range(len(levels)):
        results.append(run_scenario(levels[i], kinds[i], book_feed, price_feed))

    print("netProfit L1={:.4f} L2={:.4f} L3={:.4f}".format(
        results[0].totalNetProfit, results[1].totalNetProfit, results[2].totalNetProfit))
    return 0


if __name__ == "__main__":
    sys.exit(main())
