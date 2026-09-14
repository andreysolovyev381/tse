import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H

tse = H.tse

_message_counter = {"value": 0}


# Quoting against only every second client order is the naive defence against adverse selection:
# the maker refuses to keep accumulating against a flow that may be informed.
def is_every_second_client_order(state):
    return state["order_count"] % 2 == 0


def new_order(ts_nanoseconds, side, price, quantity):
    _message_counter["value"] += 1
    ident = _message_counter["value"]
    return tse.make_book_message(tse.BookMessageKind.New, ts_nanoseconds, ident, ident, price, quantity, side)


def trade_tick(ts_nanoseconds, price):
    return tse.TseTickTrade(ts_nanoseconds, price, 1.0, int(tse.Side.Trade))


def from_signal_params(txn_side, pos_side):
    return tse.make_rule_params(
        tse.Quantity.FromSignal, 0.0,
        tse.Price.Market, 0.0, 0.0, 0.0,
        txn_side, pos_side, tse.Tif.Day, 10,
    )


def main():
    order_price = 100.0
    order_quantity = 5.0
    upper_threshold = 15.0
    lower_threshold = 0.0

    account = H.account("MarketMakerExample", tse.StorageRegime.Mem, tse.LogLevel.Off)
    book_mkt = account.create_market("MM client orders", tse.MdType.Book)
    price_mkt = account.create_market("MM prices", tse.MdType.Trade)
    execution = account.create_simulator("sim", H.simulator_options(), 64, -1)
    account.add_contract("MMTEST", 1, tse.Instrument.Equity, tse.Underlying.Equity, tse.Venue.Undefined, 50000)

    state = {"order_count": 0, "unloading": False}

    def client_sells(storage, contract_id, message):
        if message.kind != tse.BookMessageKind.New:
            return False
        if message.txnSide != tse.Side.Short:
            return False
        state["order_count"] += 1
        storage.push(message.tsNanoseconds, message.quantity)
        return True

    def client_buys(storage, contract_id, message):
        if message.kind != tse.BookMessageKind.New:
            return False
        if message.txnSide != tse.Side.Long:
            return False
        state["order_count"] += 1
        storage.push(message.tsNanoseconds, message.quantity)
        return True

    account.add_input_book("ClientSells", 4, tse.Duration.Nanoseconds, client_sells, book_mkt, ["MMTEST"])
    account.add_input_book("ClientBuys", 4, tse.Duration.Nanoseconds, client_buys, book_mkt, ["MMTEST"])

    # Inventory management: the maker accumulates up to the upper band and then unwinds
    # all the way back to flat before it quotes that side again.
    def refresh_mode():
        quantity = account.get_position_state("MMTEST").quantity
        if not state["unloading"] and quantity >= upper_threshold:
            state["unloading"] = True
        if state["unloading"] and quantity <= lower_threshold:
            state["unloading"] = False

    def open_long(input_label, ts_nanoseconds, value):
        refresh_mode()
        if state["unloading"]:
            return False
        if not is_every_second_client_order(state):
            return False
        return account.get_position_state("MMTEST").side != tse.Side.Short

    def close_short(input_label, ts_nanoseconds, value):
        refresh_mode()
        if not state["unloading"]:
            return False
        return account.get_position_state("MMTEST").side == tse.Side.Short

    def open_short(input_label, ts_nanoseconds, value):
        refresh_mode()
        if state["unloading"]:
            return False
        if not is_every_second_client_order(state):
            return False
        return account.get_position_state("MMTEST").side != tse.Side.Long

    def close_long(input_label, ts_nanoseconds, value):
        refresh_mode()
        if not state["unloading"]:
            return False
        return account.get_position_state("MMTEST").side == tse.Side.Long

    account.add_pattern_formula("MmOpenLong", tse.Duration.Nanoseconds, ["ClientSells"], open_long)
    account.add_pattern_formula("MmCloseShort", tse.Duration.Nanoseconds, ["ClientSells"], close_short)
    account.add_pattern_formula("MmOpenShort", tse.Duration.Nanoseconds, ["ClientBuys"], open_short)
    account.add_pattern_formula("MmCloseLong", tse.Duration.Nanoseconds, ["ClientBuys"], close_long)

    account.add_rule_market("MmBuyOpen", tse.RuleType.Entry, from_signal_params(tse.Side.Long, tse.Side.Neutral), "MmOpenLong", "MMTEST")
    account.add_rule_market("MmSellClose", tse.RuleType.Exit, from_signal_params(tse.Side.Short, tse.Side.Long), "MmCloseLong", "MMTEST")
    account.add_rule_market("MmSellOpen", tse.RuleType.Entry, from_signal_params(tse.Side.Short, tse.Side.Neutral), "MmOpenShort", "MMTEST")
    account.add_rule_market("MmBuyClose", tse.RuleType.Exit, from_signal_params(tse.Side.Long, tse.Side.Short), "MmCloseShort", "MMTEST")

    account.add_robot("NaiveMarketMaker", ["MmBuyOpen", "MmSellClose", "MmSellOpen", "MmBuyClose"])
    account.portfolio_subscribe(price_mkt, "MMTEST")
    account.start("NaiveMarketMaker")

    client_sides = [tse.Side.Short] * 8 + [tse.Side.Long] * 8 + [tse.Side.Short] * 3
    base = 1000000000
    half = 500000000

    for i in range(len(client_sides)):
        ts = base * (2 * i + 1)
        book_mkt.push_book_by_name("MMTEST", new_order(ts, client_sides[i], order_price, order_quantity))
        price_mkt.push_trade_by_name("MMTEST", trade_tick(ts + half, order_price))

    final_quantity = account.get_position_state("MMTEST").quantity
    executed_count = execution.get_count()
    summary = account.get_summary()
    account.close()

    print("market maker: steps=19 executions={} trades={} finalInventory={:.1f} netProfit={:.4f}".format(
        executed_count, summary.totalNumberOfTrades, final_quantity, summary.totalNetProfit))
    return 0


if __name__ == "__main__":
    sys.exit(main())
