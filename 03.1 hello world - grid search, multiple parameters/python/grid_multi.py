import itertools
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H

tse = H.tse


def main():
    ticks = H.load_aapl()

    # Every short lookback is tried against every long one; the grid is the list of their combinations.
    short_periods = [10, 20, 50]
    long_periods = [100, 200]
    combinations = list(itertools.product(short_periods, long_periods))
    indices = [float(index) for index in range(len(combinations))]

    def builder(account, index):
        account.set_log_level(tse.LogLevel.Off)
        short_period, long_period = combinations[int(index)]
        market = account.create_market("MD", tse.MdType.Ohlcv)
        account.create_simulator("Sim", H.simulator_options())
        account.add_contract("AAPL", 1, tse.Instrument.Equity, tse.Underlying.Undefined, tse.Venue.Undefined, 100000)
        account.add_input_ohlcv("SMAShort", short_period, tse.Duration.Days, H.make_sma(short_period), market, ["AAPL"])
        account.add_input_ohlcv("SMALong", long_period, tse.Duration.Days, H.make_sma(long_period), market, ["AAPL"])
        account.add_pattern_crossover("ToLong", tse.Duration.Days, ["SMAShort", "SMALong"], tse.Cmp.Ge)
        account.add_pattern_crossover("ToShort", tse.Duration.Days, ["SMAShort", "SMALong"], tse.Cmp.Lt)
        account.add_rule_market("Entry", tse.RuleType.Entry, H.entry_params(100.0), "ToLong", "AAPL")
        account.add_rule_market("Exit", tse.RuleType.Exit, H.exit_params(), "ToShort", "AAPL")
        account.add_robot("Strat", ["Entry", "Exit"])
        account.start("Strat")
        for tick in ticks:
            market.push_ohlcv_by_name("AAPL", tick)

    # run_grid hands the builder one number per backtest, so the number is the index of a combination and the builder looks the combination up.
    results = tse.run_grid("AAPLGrid", tse.StorageRegime.Mem, indices, builder, tse.Currency.Usd, lib_path=H.LIB_PATH)

    best = results[0]
    for result in results:
        short_period, long_period = combinations[int(result.param_value)]
        print("  SMA({}) x SMA({}): netProfit={:.4f} trades={}".format(
            short_period, long_period, result.summary.totalNetProfit, result.summary.totalNumberOfTrades))
        if result.summary.totalNetProfit > best.summary.totalNetProfit:
            best = result

    short_period, long_period = combinations[int(best.param_value)]
    print("best=SMA({}) x SMA({}) netProfit={:.4f}".format(short_period, long_period, best.summary.totalNetProfit))
    return 0


if __name__ == "__main__":
    sys.exit(main())
