import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H

tse = H.tse

SYMBOL = "BULK"
BASE = 1000000000

MARKET_PRICE = 100.0
RESTING_LIMIT = 150.0
PROBE_PRICE = 155.0
TRADED_QUANTITY = 40.0

message_counter = {"value": 0}


def open_signal_processor(storage, contract_id, message):
    if message.kind != int(tse.BookMessageKind.New):
        return False
    if message.txnSide != int(tse.Side.Long):
        return False
    storage.push(message.tsNanoseconds, message.quantity)
    return True


def exit_signal_processor(storage, contract_id, message):
    if message.kind != int(tse.BookMessageKind.New):
        return False
    if message.txnSide != int(tse.Side.Short):
        return False
    storage.push(message.tsNanoseconds, message.quantity)
    return True


def always_fire(input_label, ts_nanoseconds, value):
    return True


def resting_exit_params():
    return tse.make_rule_params(
        tse.Quantity.Fixed, TRADED_QUANTITY,
        tse.Price.Limit, RESTING_LIMIT, 0.0, 0.0,
        tse.Side.Short, tse.Side.Long, tse.Tif.Day, 10,
    )


def make_rig(label):
    account = H.account(label, tse.StorageRegime.Mem, tse.LogLevel.Off)
    signals = account.create_market("Bulk signals", tse.MdType.Book)
    prices = account.create_market("Bulk prices", tse.MdType.Trade)
    account.create_simulator("sim", H.simulator_options(), 64, -1)
    account.add_contract(SYMBOL, 1, tse.Instrument.Equity, tse.Underlying.Equity, tse.Venue.Undefined, 50000)
    account.add_input_book("BulkOpenSignal", 4, tse.Duration.Nanoseconds, open_signal_processor, signals, [SYMBOL])
    account.add_input_book("BulkExitSignal", 4, tse.Duration.Nanoseconds, exit_signal_processor, signals, [SYMBOL])
    account.add_pattern_formula("BulkOpenPattern", tse.Duration.Nanoseconds, ["BulkOpenSignal"], always_fire)
    account.add_pattern_formula("BulkExitPattern", tse.Duration.Nanoseconds, ["BulkExitSignal"], always_fire)
    account.add_rule_market("BulkEntryRule", tse.RuleType.Entry, H.entry_params(TRADED_QUANTITY), "BulkOpenPattern", SYMBOL)
    account.add_rule_market("BulkRestingExit", tse.RuleType.Exit, resting_exit_params(), "BulkExitPattern", SYMBOL)
    account.add_robot("BulkRobot", ["BulkEntryRule", "BulkRestingExit"])
    account.portfolio_subscribe(prices, SYMBOL)
    account.start("BulkRobot")
    return account, signals, prices


def push_signal(signals, ts_nanoseconds, side):
    message_counter["value"] += 1
    message = tse.make_book_message(
        tse.BookMessageKind.New, ts_nanoseconds,
        message_counter["value"], message_counter["value"],
        MARKET_PRICE, TRADED_QUANTITY, side,
    )
    signals.push_book_by_name(SYMBOL, message)


def push_price(prices, ts_nanoseconds, price):
    prices.push_trade_by_name(SYMBOL, tse.TseTickTrade(ts_nanoseconds, price, 1.0, int(tse.Side.Trade)))


def run_cancel_sale_scenario():
    account, signals, prices = make_rig("BulkCancelSale")

    # The exit rests as a limit at 150 while the market prints 100, so it waits on the book unfilled.
    # cancel_all pulls resting orders, sale_all flattens the position; neither stops the robot trading.
    push_signal(signals, BASE, tse.Side.Long)
    push_price(prices, BASE * 2, MARKET_PRICE)
    push_signal(signals, BASE * 3, tse.Side.Short)
    push_price(prices, BASE * 4, MARKET_PRICE)

    account.cancel_all("BulkRobot")
    account.sale_all("BulkRobot")
    push_price(prices, BASE * 5, MARKET_PRICE)

    push_signal(signals, BASE * 6, tse.Side.Long)
    push_price(prices, BASE * 7, MARKET_PRICE)
    push_signal(signals, BASE * 8, tse.Side.Short)
    push_price(prices, BASE * 9, MARKET_PRICE)

    account.halt_and_cancel_sale_all("BulkRobot")
    push_price(prices, BASE * 10, MARKET_PRICE)
    push_signal(signals, BASE * 11, tse.Side.Long)
    push_price(prices, BASE * 12, MARKET_PRICE)

    total = account.get_summary().totalNumberOfTrades
    account.close()
    return total


def run_halt_cancel_scenario():
    account, signals, prices = make_rig("BulkHaltCancel")

    # halt_and_cancel_all is the panic button that keeps the risk: robot stopped, resting exit withdrawn,
    # position left open. The probe at 155 would have filled that exit, and now nothing happens.
    push_signal(signals, BASE, tse.Side.Long)
    push_price(prices, BASE * 2, MARKET_PRICE)
    push_signal(signals, BASE * 3, tse.Side.Short)
    push_price(prices, BASE * 4, MARKET_PRICE)

    account.halt_and_cancel_all("BulkRobot")
    push_price(prices, BASE * 5, PROBE_PRICE)

    push_signal(signals, BASE * 6, tse.Side.Long)
    push_price(prices, BASE * 7, MARKET_PRICE)

    total = account.get_summary().totalNumberOfTrades
    account.close()
    return total


def run_halt_sale_scenario():
    account, signals, prices = make_rig("BulkHaltSale")

    # halt_and_sale_all is the panic button that leaves no risk behind: robot stopped, position
    # closed at market, so every signal that arrives afterwards reaches a flat desk.
    push_signal(signals, BASE, tse.Side.Long)
    push_price(prices, BASE * 2, MARKET_PRICE)

    account.halt_and_sale_all("BulkRobot")
    push_price(prices, BASE * 3, MARKET_PRICE)

    push_signal(signals, BASE * 4, tse.Side.Long)
    push_price(prices, BASE * 5, MARKET_PRICE)

    total = account.get_summary().totalNumberOfTrades
    account.close()
    return total


def main():
    cancel_sale_trades = run_cancel_sale_scenario()
    halt_cancel_trades = run_halt_cancel_scenario()
    halt_sale_trades = run_halt_sale_scenario()

    print(
        "bulk_actions: cancelSaleTrades={} haltCancelTrades={} haltSaleTrades={}".format(
            cancel_sale_trades, halt_cancel_trades, halt_sale_trades
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
