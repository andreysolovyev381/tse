import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H

tse = H.tse

SIDE_NAME = {
    int(tse.Side.Undefined): "Undefined",
    int(tse.Side.Neutral): "Neutral",
    int(tse.Side.Long): "Long",
    int(tse.Side.Short): "Short",
}


def main():
    starting_equity = 100000.0

    account = H.account("Stats", tse.StorageRegime.Mem, tse.LogLevel.Off)
    market = account.create_market("OHLCV", tse.MdType.Ohlcv)
    execution = account.create_simulator("Sim", H.simulator_options())

    account.add_contract("WTI", 1, tse.Instrument.Future, tse.Underlying.Commodity, tse.Venue.Undefined, 100000)
    account.add_input_ohlcv("SMA3", 3, tse.Duration.Days, H.make_sma(3), market, ["WTI"])
    account.add_input_ohlcv("SMA10", 10, tse.Duration.Days, H.make_sma(10), market, ["WTI"])
    # Hold WTI while the three day average stays above the ten day one, stand aside when it drops back under.
    account.add_pattern_crossover("ToLong", tse.Duration.Days, ["SMA3", "SMA10"], tse.Cmp.Ge)
    account.add_pattern_crossover("ToShort", tse.Duration.Days, ["SMA3", "SMA10"], tse.Cmp.Lt)
    account.add_rule_market("Entry", tse.RuleType.Entry, H.entry_params(580.0), "ToLong", "WTI")
    account.add_rule_market("Exit", tse.RuleType.Exit, H.exit_params(), "ToShort", "WTI")
    account.add_robot("Strat", ["Entry", "Exit"])
    account.set_account_equity(starting_equity, 0.0)
    account.start("Strat")

    ticks = account.load_ohlcv_csv(H.data_path("WTI_OHLCVminute_sept2016.csv"))
    for tick in ticks:
        market.push_ohlcv_by_name("WTI", tick)

    # The first kind of statistics: the robot level totals for the whole run, and next to them the
    # transaction level records the blotter kept, one row per fill.
    summary = account.get_summary()
    trades = account.get_trades()
    print("retained transactions: {} execution count: {}".format(len(trades), execution.get_count()))
    for trade in trades[:5]:
        print("  {} price={:.4f} quantity={:.0f} bookedPL={:.4f} side={}".format(
            trade.symbol.decode(), trade.price, trade.quantity, trade.bookedPL, SIDE_NAME[trade.txnSide]))

    # The second kind: ex_post cuts the very same run into day buckets and scores each bucket, so a
    # model can study how the robot behaved through time instead of one number at the end.
    ex_post = account.create_ex_post(tse.Duration.Days, -1, 5)
    param_count = ex_post.param_count()
    buckets = ex_post.bucket_count(0)

    db_path = os.path.join(tempfile.gettempdir(), "tse_ex06_py.sqlite3.db")
    H.cleanup(db_path)
    account.ex_post_save(db_path, tse.Duration.Days, -1, 5)
    saved_robots = account.ex_post_load(db_path)
    H.cleanup(db_path)

    ex_post.close()
    account.close()

    print("netProfit={:.4f} trades={} retained={} buckets={} params={} savedRobots={}".format(
        summary.totalNetProfit, summary.totalNumberOfTrades, len(trades), buckets, param_count, saved_robots))
    return 0


if __name__ == "__main__":
    sys.exit(main())
