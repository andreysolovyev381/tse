import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H

tse = H.tse

DB_PATH = os.path.join(tempfile.gettempdir(), "tse_docs_ex06.db")


def build_savable(account, market):
    account.register_input_processor("sma3", H.make_sma(3))
    account.register_input_processor("sma10", H.make_sma(10))
    account.add_contract("WTI", 1, tse.Instrument.Future, tse.Underlying.Commodity, tse.Venue.Undefined, 100000)
    # fast-over-slow crossover on WTI minute bars: long while the 3-bar average leads the 10-bar one
    account.add_input_ohlcv_by_key("WTI SMA3", 3, tse.Duration.Days, "sma3", market, ["WTI"])
    account.add_input_ohlcv_by_key("WTI SMA10", 10, tse.Duration.Days, "sma10", market, ["WTI"])
    account.add_pattern_crossover("PatternOpenLong", tse.Duration.Days, ["WTI SMA3", "WTI SMA10"], tse.Cmp.Ge)
    account.add_pattern_crossover("PatternCloseLong", tse.Duration.Days, ["WTI SMA3", "WTI SMA10"], tse.Cmp.Lt)
    # every entry buys a fixed 580 barrels, and the exit always closes the whole position
    account.add_rule_market("RuleEntry", tse.RuleType.Entry, H.entry_params(580.0), "PatternOpenLong", "WTI")
    account.add_rule_market("RuleExit", tse.RuleType.Exit, H.exit_params(), "PatternCloseLong", "WTI")
    account.add_robot("Strat", ["RuleEntry", "RuleExit"])


def research():
    account = H.account("WTI-Research", tse.StorageRegime.Mem, tse.LogLevel.Off)
    market = account.create_market("MD", tse.MdType.Ohlcv)
    account.create_simulator("Sim", H.simulator_options())
    build_savable(account, market)
    account.start("Strat")
    ticks = account.load_ohlcv_csv(H.data_path("WTI_OHLCVminute_sept2016.csv"))
    for tick in ticks:
        market.push_ohlcv_by_name("WTI", tick)
    summary = account.get_summary()
    account.close()
    return summary


def save_recipe():
    account = H.account("WTI-Saver", tse.StorageRegime.Mem, tse.LogLevel.Off)
    market = account.create_market("MD", tse.MdType.Ohlcv)
    build_savable(account, market)
    # only the recipe travels: contracts, inputs, patterns and rules, never the market data or the results
    account.save("Strat", DB_PATH)
    account.close()


def main():
    H.cleanup(DB_PATH)
    summary = research()
    save_recipe()
    print('research netProfit={:.4f} trades={}; saved "Strat" to {} (schema v3)'.format(
        summary.totalNetProfit, summary.totalNumberOfTrades, DB_PATH))
    return 0


if __name__ == "__main__":
    sys.exit(main())
