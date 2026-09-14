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
    # The future carries the directional view, the two puts underneath insure it against a fall
    # and the far call buys back the upside: four legs, one hedged position.
    legs = [
        {"symbol": "CLF26", "instrument": tse.Instrument.Future, "quantity": 1.0, "entry_side": tse.Side.Long, "entry_price": 0.0, "exit_price": 0.0},
        {"symbol": "LO F26 P00045000", "instrument": tse.Instrument.Option, "quantity": 1.0, "entry_side": tse.Side.Long, "entry_price": 0.0, "exit_price": 0.0},
        {"symbol": "LO F26 P00044000", "instrument": tse.Instrument.Option, "quantity": 1.0, "entry_side": tse.Side.Long, "entry_price": 0.0, "exit_price": 0.0},
        {"symbol": "LO F26 C00060000", "instrument": tse.Instrument.Option, "quantity": 1.0, "entry_side": tse.Side.Long, "entry_price": 0.0, "exit_price": 0.0},
    ]
    load_leg_prices("multileg_wti_strangle.csv", legs)

    account = H.account("WtiStrangle", tse.StorageRegime.Mem, tse.LogLevel.Off)
    market = account.create_market("price", tse.MdType.Trade)
    account.create_simulator("sim", H.simulator_options(), 64, -1)
    # A WTI contract is a thousand barrels, so a dollar on the future is a thousand dollars of P&L.
    for leg in legs:
        account.add_contract(leg["symbol"], 1000, leg["instrument"], tse.Underlying.Commodity, tse.Venue.CME, 100000)
    account.add_input_trade("Px", 2, tse.Duration.Nanoseconds, price_processor, market, [legs[0]["symbol"]])
    account.add_pattern_timestamp("EnterAt", tse.Duration.Nanoseconds, ["Px"], ENTER_CHECKPOINT, HUGE_COOLDOWN)
    account.add_pattern_timestamp("ExitAt", tse.Duration.Nanoseconds, ["Px"], EXIT_CHECKPOINT, HUGE_COOLDOWN)
    # A multileg transaction is all or nothing: the four legs fill at one instant against
    # a single net price, or none of them fills, so the hedge can never go on half-built.
    account.add_rule_multileg("Strangle enter", entry_legs(legs), "EnterAt")
    account.add_rule_multileg("Strangle exit", exit_legs(legs), "ExitAt")
    account.add_robot("WTI_STRANGLE", ["Strangle enter", "Strangle exit"])
    for leg in legs:
        account.portfolio_subscribe(market, leg["symbol"])
    account.start("WTI_STRANGLE")

    push_price(market, legs[0]["symbol"], ENTER_CHECKPOINT, legs[0]["entry_price"])
    push_price(market, legs[0]["symbol"], 1000000110, legs[0]["entry_price"])
    push_price(market, legs[1]["symbol"], 1000000120, legs[1]["entry_price"])
    push_price(market, legs[2]["symbol"], 1000000130, legs[2]["entry_price"])
    push_price(market, legs[3]["symbol"], 1000000140, legs[3]["entry_price"])

    push_price(market, legs[0]["symbol"], EXIT_CHECKPOINT, legs[0]["exit_price"])
    push_price(market, legs[0]["symbol"], 2000000200, legs[0]["exit_price"])
    push_price(market, legs[1]["symbol"], 2000000200, legs[1]["exit_price"])
    push_price(market, legs[2]["symbol"], 2000000200, legs[2]["exit_price"])
    push_price(market, legs[3]["symbol"], 2000000200, legs[3]["exit_price"])

    summary = account.get_summary()
    account.close()

    print("strangle_hedge: netProfit={:.2f}".format(summary.totalNetProfit))
    return 0


if __name__ == "__main__":
    sys.exit(main())
