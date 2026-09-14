import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H

tse = H.tse

BROKER_FEE = 0.5
BROKER_LATENCY_NANOSECONDS = 1000000


def flow(storage, contract_id, tick):
    if tick.volume <= 0.0:
        return False
    storage.push(tick.tsNanoseconds, tick.volume)
    return True


def enter_signal(input_label, ts_nanoseconds, value):
    return value > 0.0


def main():
    ticks = H.load_trades("wti_trades.csv", tse.Side.Neutral)
    state = {"market_price": 0.0, "market_ts_nanoseconds": 0, "order_count": 0, "fill_count": 0}

    def report_fill(execution, order, quantity, delay_nanoseconds):
        execution.apply_fill(order.clientOrderId.decode(), state["market_price"], quantity, BROKER_FEE, state["market_ts_nanoseconds"] + delay_nanoseconds)
        state["fill_count"] += 1

    def transmit(execution, order):
        state["order_count"] += 1
        if order.quantity <= 0.0:
            return
        # a real broker rarely fills a whole order at once, so here every second order comes back as two trades
        if state["order_count"] % 2 == 0:
            half = order.quantity / 2.0
            report_fill(execution, order, half, BROKER_LATENCY_NANOSECONDS)
            report_fill(execution, order, order.quantity - half, 2 * BROKER_LATENCY_NANOSECONDS)
        else:
            report_fill(execution, order, order.quantity, BROKER_LATENCY_NANOSECONDS)

    account = H.account("GoLiveExample", tse.StorageRegime.Mem, tse.LogLevel.Off)
    real_market_data = account.create_market("REAL_MARKET_DATA", tse.MdType.Trade)
    # going live means the broker, not the engine, decides how an order fills
    broker_execution = account.create_custom("BROKER_EXECUTION", transmit, 8, -1)

    account.add_contract("WTI", 1, tse.Instrument.Future, tse.Underlying.Commodity, tse.Venue.Undefined, 100000)
    account.add_input_trade("Flow", 4, tse.Duration.Nanoseconds, flow, real_market_data, ["WTI"])
    # no edge here: any trade that carries volume is a buy signal, and the order size is that same volume
    account.add_pattern_formula("EnterSignal", tse.Duration.Nanoseconds, ["Flow"], enter_signal)
    account.add_rule_market(
        "Enter", tse.RuleType.Entry,
        tse.make_rule_params(
            tse.Quantity.FromSignal, 0.0,
            tse.Price.Market, 0.0, 0.0, 0.0,
            tse.Side.Long, tse.Side.Neutral, tse.Tif.Day, 10,
        ),
        "EnterSignal", "WTI")
    account.add_robot("GoLive", ["Enter"])
    account.portfolio_subscribe(real_market_data, "WTI")
    account.start("GoLive")

    for tick in ticks:
        state["market_price"] = tick.price
        state["market_ts_nanoseconds"] = tick.tsNanoseconds
        real_market_data.push_trade_by_name("WTI", tick)

    executed = broker_execution.get_count()
    final_quantity = account.get_position_state("WTI").quantity
    account.close()

    print("go live: replayed={} orders={} fills={} executed={} finalQuantity={:.1f}".format(
        len(ticks), state["order_count"], state["fill_count"], executed, final_quantity))
    return 0


if __name__ == "__main__":
    sys.exit(main())
