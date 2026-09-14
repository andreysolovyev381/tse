import os
import sys
import threading

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H

tse = H.tse


def main():
    ticks = H.load_aapl()

    # How many days the fast average looks back. Too short and every wiggle flips the position,
    # too long and the trend is half over before the strategy joins it: let the data pick.
    params = [10.0, 20.0, 50.0, 100.0]

    worker_threads = set()

    def builder(account, param_value):
        account.set_log_level(tse.LogLevel.Off)
        worker_threads.add(threading.get_ident())
        short_period = int(param_value)
        market = account.create_market("MD", tse.MdType.Ohlcv)
        account.create_simulator("Sim", H.simulator_options())
        account.add_contract("AAPL", 1, tse.Instrument.Equity, tse.Underlying.Undefined, tse.Venue.Undefined, 100000)
        account.add_input_ohlcv("SMAShort", short_period, tse.Duration.Days, H.make_sma(short_period), market, ["AAPL"])
        account.add_input_ohlcv("SMA200", 200, tse.Duration.Days, H.make_sma(200), market, ["AAPL"])
        # Hold the stock while the fast average stays above the 200 day trend, stand aside once it drops back under.
        account.add_pattern_crossover("ToLong", tse.Duration.Days, ["SMAShort", "SMA200"], tse.Cmp.Ge)
        account.add_pattern_crossover("ToShort", tse.Duration.Days, ["SMAShort", "SMA200"], tse.Cmp.Lt)
        account.add_rule_market("Entry", tse.RuleType.Entry, H.entry_params(100.0), "ToLong", "AAPL")
        account.add_rule_market("Exit", tse.RuleType.Exit, H.exit_params(), "ToShort", "AAPL")
        account.add_robot("Strat", ["Entry", "Exit"])
        account.start("Strat")
        for tick in ticks:
            market.push_ohlcv_by_name("AAPL", tick)

    # Grid points are independent backtests, so the engine runs them side by side on a pool of
    # worker threads; every run notes down the thread it happened to land on.
    results = tse.run_grid("AAPLGrid", tse.StorageRegime.Mem, params, builder, tse.Currency.Usd, lib_path=H.LIB_PATH)

    best = results[0]
    for result in results:
        print("  SMA({}) x SMA(200): netProfit={:.4f} trades={}".format(
            int(result.param_value), result.summary.totalNetProfit, result.summary.totalNumberOfTrades))
        if result.summary.totalNetProfit > best.summary.totalNetProfit:
            best = result

    print("best=SMA({}) netProfit={:.4f} workerThreads={}".format(
        int(best.param_value), best.summary.totalNetProfit, len(worker_threads)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
