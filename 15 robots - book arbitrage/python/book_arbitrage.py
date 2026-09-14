import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H

tse = H.tse

_message_counter = {"value": 0}


def main():
    account = H.account("BookArbitrage", tse.StorageRegime.Mem, tse.LogLevel.Off)
    book_mkt_a = account.create_market("book A", tse.MdType.Book)
    book_mkt_b = account.create_market("book B", tse.MdType.Book)
    price_mkt = account.create_market("price C", tse.MdType.Trade)
    account.create_simulator("sim", H.simulator_options(), 64, -1)
    account.add_contract("BOOKA", 1, tse.Instrument.Equity, tse.Underlying.Equity, tse.Venue.Undefined, 50000)
    account.add_contract("BOOKB", 1, tse.Instrument.Equity, tse.Underlying.Equity, tse.Venue.Undefined, 50000)
    account.add_contract("TRADEC", 1, tse.Instrument.Equity, tse.Underlying.Equity, tse.Venue.Undefined, 50000)
    book_a = account.create_book("BOOKA", tse.BookLevelKind.L2)
    book_b = account.create_book("BOOKB", tse.BookLevelKind.L3)

    book_a.apply(new_order(1000000, tse.Side.Long, 99.95, 6.0))
    book_a.apply(new_order(2000000, tse.Side.Short, 100.05, 6.0))
    book_b.apply(new_order(3000000, tse.Side.Long, 99.95, 6.0))
    book_b.apply(new_order(4000000, tse.Side.Short, 100.05, 6.0))

    account.add_input_book_imbalance("ImbA", 4, tse.Duration.Nanoseconds, book_a, book_mkt_a, ["BOOKA"])
    account.add_input_book_imbalance("ImbB", 4, tse.Duration.Nanoseconds, book_b, book_mkt_b, ["BOOKB"])

    # The same asset is quoted on two venues. Neither book's imbalance is a signal on its own:
    # the strategy waits for the two to disagree and bets that the disagreement closes.
    account.add_pattern_formula("SpreadEnterShort", tse.Duration.Nanoseconds, ["ImbA", "ImbB"], spread_formula(0))
    account.add_pattern_formula("SpreadEnterLong", tse.Duration.Nanoseconds, ["ImbA", "ImbB"], spread_formula(1))
    account.add_pattern_formula("SpreadExit", tse.Duration.Nanoseconds, ["ImbA", "ImbB"], spread_formula(2))

    enter_short_params = tse.make_rule_params(
        tse.Quantity.Fixed, 100.0,
        tse.Price.Market, 0.0, 0.0, 0.0,
        tse.Side.Short, tse.Side.Neutral, tse.Tif.Day, 10,
    )
    exit_short_params = tse.make_rule_params(
        tse.Quantity.Fixed, 100.0,
        tse.Price.Market, 0.0, 0.0, 0.0,
        tse.Side.Long, tse.Side.Short, tse.Tif.Day, 10,
    )
    enter_long_params = tse.make_rule_params(
        tse.Quantity.Fixed, 100.0,
        tse.Price.Market, 0.0, 0.0, 0.0,
        tse.Side.Long, tse.Side.Neutral, tse.Tif.Day, 10,
    )
    exit_long_params = tse.make_rule_params(
        tse.Quantity.Fixed, 100.0,
        tse.Price.Market, 0.0, 0.0, 0.0,
        tse.Side.Short, tse.Side.Long, tse.Tif.Day, 10,
    )

    # The signal is read off the two books, but the position is taken in a third, liquid contract.
    account.add_rule_market("EnterShortC", tse.RuleType.Entry, enter_short_params, "SpreadEnterShort", "TRADEC")
    account.add_rule_market("ExitShortC", tse.RuleType.Exit, exit_short_params, "SpreadExit", "TRADEC")
    account.add_rule_market("EnterLongC", tse.RuleType.Entry, enter_long_params, "SpreadEnterLong", "TRADEC")
    account.add_rule_market("ExitLongC", tse.RuleType.Exit, exit_long_params, "SpreadExit", "TRADEC")

    account.add_robot("Robot", ["EnterShortC", "ExitShortC", "EnterLongC", "ExitLongC"])
    account.portfolio_subscribe(price_mkt, "TRADEC")
    account.start("Robot")

    targets_a = [0.125, 0.3, 0.0, -0.3, 0.0, 0.3, 0.0, -0.3, 0.0, 0.3, 0.0, -0.3, 0.0]
    targets_b = [-0.125, -0.3, 0.0, 0.3, 0.0, -0.3, 0.0, 0.3, 0.0, -0.3, 0.0, 0.3, 0.0]
    prices_c = [100, 102, 100, 98, 100, 102, 100, 98, 100, 102, 100, 98, 100]
    base = 1000000000
    q_off = 250000000
    half = 500000000
    state_a = {"sb": 6.0, "sa": 6.0}
    state_b = {"sb": 6.0, "sa": 6.0}

    for i in range(len(targets_a)):
        step = i + 1
        book_mkt_a.push_book_by_name(
            "BOOKA", book_step(state_a, targets_a[i], 99.95, 100.05, base * (2 * step + 1)))
        book_mkt_b.push_book_by_name(
            "BOOKB", book_step(state_b, targets_b[i], 99.95, 100.05, base * (2 * step + 1) + q_off))
        price_mkt.push_trade_by_name("TRADEC", trade_tick(base * (2 * step + 1) + half, prices_c[i]))

    summary = account.get_summary()
    account.close()
    trades = summary.totalNumberOfTrades

    print("netProfit={:.4f} trades={}".format(summary.totalNetProfit, trades))
    return 0


def new_order(ts_nanoseconds, side, price, quantity):
    _message_counter["value"] += 1
    ident = _message_counter["value"]
    return tse.make_book_message(tse.BookMessageKind.New, ts_nanoseconds, ident, ident, price, quantity, side)


def trade_tick(ts_nanoseconds, price):
    return tse.TseTickTrade(ts_nanoseconds, price, 1.0, int(tse.Side.Trade))


def spread_formula(mode):
    state = {"last_a": 0.0, "last_b": 0.0, "have_a": False, "have_b": False}

    def processor(input_label, ts_nanoseconds, value):
        if input_label == "ImbA":
            state["last_a"] = value
            state["have_a"] = True
        elif input_label == "ImbB":
            state["last_b"] = value
            state["have_b"] = True
        if not (state["have_a"] and state["have_b"]):
            return False
        spread = state["last_a"] - state["last_b"]
        # Enter when the two books disagree by more than 0.3, and leave once the gap
        # has collapsed back into a narrow band around zero.
        if mode == 0:
            return spread > 0.3
        if mode == 1:
            return spread < -0.3
        return spread < 0.2 and spread > -0.2

    return processor


def book_step(state, target, bid_price, ask_price, ts_nanoseconds):
    curr = (state["sb"] - state["sa"]) / (state["sb"] + state["sa"])
    if target > curr:
        delta = (state["sa"] * (1.0 + target) / (1.0 - target)) - state["sb"]
        state["sb"] += delta
        return new_order(ts_nanoseconds, tse.Side.Long, bid_price, delta)
    delta = (state["sb"] * (1.0 - target) / (1.0 + target)) - state["sa"]
    state["sa"] += delta
    return new_order(ts_nanoseconds, tse.Side.Short, ask_price, delta)


if __name__ == "__main__":
    sys.exit(main())
