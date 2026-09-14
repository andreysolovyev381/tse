import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H

tse = H.tse


def run_wti(label, regime):
    account = H.account(label, regime, tse.LogLevel.Off)
    market = account.create_market("OHLCV", tse.MdType.Ohlcv)
    account.create_simulator("Sim", H.simulator_options())
    account.add_contract("WTI", 1, tse.Instrument.Future, tse.Underlying.Commodity, tse.Venue.Undefined, 100000)
    # A short-horizon trend follower on oil futures: three days of price against ten, long above and flat below.
    account.add_input_ohlcv("SMA3", 3, tse.Duration.Days, H.make_sma(3), market, ["WTI"])
    account.add_input_ohlcv("SMA10", 10, tse.Duration.Days, H.make_sma(10), market, ["WTI"])
    account.add_pattern_crossover("ToLong", tse.Duration.Days, ["SMA3", "SMA10"], tse.Cmp.Ge)
    account.add_pattern_crossover("ToShort", tse.Duration.Days, ["SMA3", "SMA10"], tse.Cmp.Lt)
    account.add_rule_market("Entry", tse.RuleType.Entry, H.entry_params(580.0), "ToLong", "WTI")
    account.add_rule_market("Exit", tse.RuleType.Exit, H.exit_params(), "ToShort", "WTI")
    account.add_robot("Strat", ["Entry", "Exit"])
    account.start("Strat")
    ticks = account.load_ohlcv_csv(H.data_path("WTI_OHLCVminute_sept2016.csv"))
    for tick in ticks:
        market.push_ohlcv_by_name("WTI", tick)
    summary = account.get_summary()
    account.close()
    return summary


def main():
    data_folder = os.path.join(tempfile.gettempdir(), "tse_example_storage_regimes_py")
    shutil.rmtree(data_folder, ignore_errors=True)
    os.makedirs(data_folder)
    tse.set_initial_params(data_folder=data_folder, log_folder="", log_level=None, lib_path=H.LIB_PATH)

    # The regime says only where the blotter lives, on disk in the folder above or in memory, and never changes what the strategy does.
    mem_summary = run_wti("RegimesMem", tse.StorageRegime.Mem)
    db_summary = run_wti("RegimesDb", tse.StorageRegime.Db)

    shutil.rmtree(data_folder, ignore_errors=True)

    print("mem={:.4f}/{} db={:.4f}/{}".format(
        mem_summary.totalNetProfit, mem_summary.totalNumberOfTrades,
        db_summary.totalNetProfit, db_summary.totalNumberOfTrades))
    return 0


if __name__ == "__main__":
    sys.exit(main())
