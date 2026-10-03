import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H

tse = H.tse

BROKER_FEE = 0.5
BROKER_LATENCY_NANOSECONDS = 1000000


# The broker is one of the two ends this example replaces. It keeps the last price it was told about and
# fills in full whatever the robot sends it, a millisecond later, at that price - charging its fee.
class Broker:
    def __init__(self):
        self.last_price = 0.0
        self.last_ts_nanoseconds = 0
        self.order_count = 0
        self.fill_count = 0

    def transmit(self, execution, order):
        self.order_count += 1
        if order.quantity <= 0.0:
            return
        execution.apply_fill(
            order.clientOrderId.decode(),
            self.last_price,
            order.quantity,
            BROKER_FEE,
            self.last_ts_nanoseconds + BROKER_LATENCY_NANOSECONDS,
        )
        self.fill_count += 1


# The client is the other such end. Live work has no file to walk over: it has a subscription that hands
# over one bar at a time, and the same bar tells the broker where the market is. The recorded bars of
# example 02 stand here for that subscription.
class MarketDataClient:
    def __init__(self, market, symbol, broker):
        self.market = market
        self.symbol = symbol
        self.broker = broker

    def replay(self, subscription):
        for bar in subscription:
            self.broker.last_price = bar.close
            self.broker.last_ts_nanoseconds = bar.tsNanoseconds
            self.market.push_ohlcv_by_name(self.symbol, bar)
        return len(subscription)


def main():
    rows = H.load_aapl()
    broker = Broker()

    account = tse.Account("AAPL", tse.StorageRegime.Mem, lib_path=H.LIB_PATH)
    account.set_log_level(tse.LogLevel.Off)
    market = account.create_market("MD", tse.MdType.Ohlcv)
    # Example 02 runs this very strategy against the built-in Simulator. Going live means the broker, not the
    # engine, decides how an order fills, so the Simulator gives way to a custom execution - and that is the
    # only change on this end.
    broker_execution = account.create_custom("BROKER", broker.transmit, 8, -1)
    account.add_contract("AAPL", 1, tse.Instrument.Equity, tse.Underlying.Undefined, tse.Venue.Undefined, 100000)

    # MACD is the distance between a fast and a slow average of the close: it is positive while the recent
    # days are stronger than the older ones, and negative when they fade. The averages are meaningless until
    # the slow one has seen enough days, so the indicator declares itself ready only then and no rule fires before that.
    fast, slow = 12, 26
    alpha_fast, alpha_slow = 2.0 / (fast + 1), 2.0 / (slow + 1)
    ema = {"fast": None, "slow": None}

    def macd(storage, contract_id, tick):
        ema["fast"] = tick.close if ema["fast"] is None else ema["fast"] + alpha_fast * (tick.close - ema["fast"])
        ema["slow"] = tick.close if ema["slow"] is None else ema["slow"] + alpha_slow * (tick.close - ema["slow"])
        storage.push(tick.tsNanoseconds, ema["fast"] - ema["slow"])
        return storage.size() >= slow

    account.add_input_ohlcv("MACD", slow, tse.Duration.Days, macd, market, ["AAPL"])
    # Buy 100 shares once the momentum turns positive and sell the whole position when it turns negative.
    account.add_pattern_threshold("ToLong", tse.Duration.Days, ["MACD"], tse.Cmp.Ge, 0.0)
    account.add_pattern_threshold("ToShort", tse.Duration.Days, ["MACD"], tse.Cmp.Lt, 0.0)
    account.add_rule_market("Entry", tse.RuleType.Entry, H.entry_params(100.0), "ToLong", "AAPL")
    account.add_rule_market("Exit", tse.RuleType.Exit, H.exit_params(), "ToShort", "AAPL")
    account.add_robot("Strat", ["Entry", "Exit"])
    account.start("Strat")
    # A custom execution is started and stopped by whoever created it; the engine never does it for you.
    broker_execution.start()

    client = MarketDataClient(market, "AAPL", broker)
    bars = client.replay(rows)

    broker_execution.stop()

    summary = account.get_summary()
    executed = broker_execution.get_count()
    final_quantity = account.get_position_state("AAPL").quantity
    account.close()

    print("go live: bars={} orders={} fills={} executed={} netProfit={:.4f} trades={} finalQuantity={:.1f}".format(
        bars, broker.order_count, broker.fill_count, executed,
        summary.totalNetProfit, summary.totalNumberOfTrades, final_quantity))
    return 0


if __name__ == "__main__":
    sys.exit(main())
