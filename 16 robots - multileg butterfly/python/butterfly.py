import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H

tse = H.tse

ENTER_CHECKPOINT = 1000000000
EXIT_CHECKPOINT = 2000000000
HUGE_COOLDOWN = 1000000000000000


def flip_side(side):
    if side == tse.Side.Long:
        return tse.Side.Short
    if side == tse.Side.Short:
        return tse.Side.Long
    return tse.Side.Neutral


def load_price_rows(name):
    rows = []
    with open(H.data_path(name)) as handle:
        handle.readline()
        for line in handle:
            line = line.strip()
            if not line:
                continue
            cells = line.split(",")
            rows.append([float(cell) for cell in cells[1:]])
    return rows


def load_leg_prices(name, legs):
    rows = load_price_rows(name)
    for i, leg in enumerate(legs):
        leg["entry_price"] = rows[0][i]
        leg["exit_price"] = rows[1][i]


def price_processor(storage, contract_id, tick):
    storage.push(tick.tsNanoseconds, tick.price)
    return True


def make_leg(symbol, quantity, limit_price, txn_type, txn_side, pos_side):
    return tse.make_leg_descriptor(
        symbol,
        tse.Quantity.Fixed,
        quantity,
        tse.Price.Limit,
        limit_price,
        0.0,
        0.0,
        txn_type,
        txn_side,
        pos_side,
        tse.Tif.Day,
        tse.Priority.Replaceable,
    )


def entry_legs(legs):
    return [
        make_leg(leg["symbol"], leg["quantity"], leg["entry_price"], tse.TxnType.Enter, leg["entry_side"], tse.Side.Neutral)
        for leg in legs
    ]


def exit_legs(legs):
    return [
        make_leg(leg["symbol"], leg["quantity"], leg["exit_price"], tse.TxnType.Exit, flip_side(leg["entry_side"]), leg["entry_side"])
        for leg in legs
    ]


def push_price(market, symbol, ts_nanoseconds, price):
    market.push_trade_by_name(symbol, tse.TseTickTrade(ts_nanoseconds, price, 1.0, int(tse.Side.Trade)))


def main():
    # A call butterfly buys one wing, sells two at the body and buys the far wing:
    # it pays while the underlying stays near the body strike, and the wings cap the loss.
    legs = [
        {"symbol": "SPY   260116C00440000", "quantity": 1.0, "entry_side": tse.Side.Long, "entry_price": 0.0, "exit_price": 0.0},
        {"symbol": "SPY   260116C00450000", "quantity": 2.0, "entry_side": tse.Side.Short, "entry_price": 0.0, "exit_price": 0.0},
        {"symbol": "SPY   260116C00460000", "quantity": 1.0, "entry_side": tse.Side.Long, "entry_price": 0.0, "exit_price": 0.0},
    ]
    load_leg_prices("multileg_spy_butterfly.csv", legs)

    account = H.account("SpyButterfly", tse.StorageRegime.Mem, tse.LogLevel.Off)
    market = account.create_market("price", tse.MdType.Trade)
    account.create_simulator("sim", H.simulator_options(), 64, -1)
    # One option contract carries a hundred shares, so a dollar on a leg is a hundred dollars of P&L.
    for leg in legs:
        account.add_contract(leg["symbol"], 100, tse.Instrument.Option, tse.Underlying.Equity, tse.Venue.CBOE, 100000)
    account.add_input_trade("Px", 2, tse.Duration.Nanoseconds, price_processor, market, [legs[0]["symbol"]])
    account.add_pattern_timestamp("EnterAt", tse.Duration.Nanoseconds, ["Px"], ENTER_CHECKPOINT, HUGE_COOLDOWN)
    account.add_pattern_timestamp("ExitAt", tse.Duration.Nanoseconds, ["Px"], EXIT_CHECKPOINT, HUGE_COOLDOWN)
    # A multileg transaction is all or nothing: the three legs fill at one instant against
    # a single net price, or none of them fills.
    account.add_rule_multileg("Butterfly enter", entry_legs(legs), "EnterAt")
    account.add_rule_multileg("Butterfly exit", exit_legs(legs), "ExitAt")
    account.add_robot("SPY_BUTTERFLY", ["Butterfly enter", "Butterfly exit"])
    for leg in legs:
        account.portfolio_subscribe(market, leg["symbol"])
    account.start("SPY_BUTTERFLY")

    push_price(market, legs[0]["symbol"], ENTER_CHECKPOINT, legs[0]["entry_price"])
    push_price(market, legs[0]["symbol"], 1000000110, legs[0]["entry_price"])
    push_price(market, legs[1]["symbol"], 1000000120, legs[1]["entry_price"])
    push_price(market, legs[2]["symbol"], 1000000130, legs[2]["entry_price"])

    push_price(market, legs[0]["symbol"], EXIT_CHECKPOINT, legs[0]["exit_price"])
    push_price(market, legs[2]["symbol"], 2000000200, legs[2]["exit_price"])
    push_price(market, legs[1]["symbol"], 2000000200, legs[1]["exit_price"])
    push_price(market, legs[0]["symbol"], 2000000200, legs[0]["exit_price"])

    summary = account.get_summary()
    account.close()

    print("butterfly: netProfit={:.2f}".format(summary.totalNetProfit))
    return 0


if __name__ == "__main__":
    sys.exit(main())
