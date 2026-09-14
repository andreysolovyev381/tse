import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H

tse = H.tse


def run_aapl(label, layout, rows):
    account = tse.Account(label, tse.StorageRegime.Mem, tse.Currency.Usd, layout["account"], lib_path=H.LIB_PATH)
    account.set_log_level(tse.LogLevel.Off)
    market = account.create_market("OHLCV", tse.MdType.Ohlcv)
    account.create_simulator("Sim", H.simulator_options(), 3, layout["simulator"])
    account.add_contract("AAPL", 1, tse.Instrument.Equity, tse.Underlying.Undefined, tse.Venue.Undefined, 100000)
    # A classic trend follower: hold long while the fifty-day average stands above the two-hundred-day one, step aside when it falls back.
    account.add_input_ohlcv("SMA50", 50, tse.Duration.Days, H.make_sma(50), market, ["AAPL"], layout["sma50"])
    account.add_input_ohlcv("SMA200", 200, tse.Duration.Days, H.make_sma(200), market, ["AAPL"], layout["sma200"])
    account.add_pattern_crossover("ToLong", tse.Duration.Days, ["SMA50", "SMA200"], tse.Cmp.Ge, layout["to_long"])
    account.add_pattern_crossover("ToShort", tse.Duration.Days, ["SMA50", "SMA200"], tse.Cmp.Lt, layout["to_short"])
    account.add_rule_market("Entry", tse.RuleType.Entry, H.entry_params(100.0), "ToLong", "AAPL")
    account.add_rule_market("Exit", tse.RuleType.Exit, H.exit_params(), "ToShort", "AAPL")
    account.add_robot("Strat", ["Entry", "Exit"])
    account.start("Strat")
    for tick in rows:
        market.push_ohlcv_by_name("AAPL", tick)
    summary = account.get_summary()
    account.close()
    return summary


def main():
    rows = H.load_aapl()

    # The trailing number is the CPU core that piece of the strategy is nailed to; -1 leaves the choice to the operating system.
    unpinned_layout = {"account": -1, "simulator": -1, "sma50": -1, "sma200": -1, "to_long": -1, "to_short": -1}
    pinned_layout = {"account": -1, "simulator": -1, "sma50": 0, "sma200": 1, "to_long": 0, "to_short": 1}

    # Affinity buys latency, never a different trade: the same data through both layouts must give the same profit and the same trade count.
    unpinned = run_aapl("AffinityUnpinned", unpinned_layout, rows)
    pinned = run_aapl("AffinityPinned", pinned_layout, rows)

    print("unpinned={:.4f}/{} pinned={:.4f}/{}".format(
        unpinned.totalNetProfit, unpinned.totalNumberOfTrades,
        pinned.totalNetProfit, pinned.totalNumberOfTrades))
    return 0


if __name__ == "__main__":
    sys.exit(main())
