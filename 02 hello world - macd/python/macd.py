import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H

tse = H.tse


def main():
    rows = H.load_aapl()

    account = tse.Account("AAPL", tse.StorageRegime.Mem, lib_path=H.LIB_PATH)
    account.set_log_level(tse.LogLevel.Off)
    market = account.create_market("MD", tse.MdType.Ohlcv)
    account.create_simulator("Sim", H.simulator_options())
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

    for tick in rows:
        market.push_ohlcv_by_name("AAPL", tick)

    summary = account.get_summary()
    account.close()
    print("netProfit={:.4f} trades={}".format(summary.totalNetProfit, summary.totalNumberOfTrades))
    return 0


if __name__ == "__main__":
    sys.exit(main())
