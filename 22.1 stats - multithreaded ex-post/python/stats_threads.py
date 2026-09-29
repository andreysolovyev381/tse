import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H

tse = H.tse


def main():
    starting_equity = 100000.0

    account = H.account("StatsThreads", tse.StorageRegime.Mem, tse.LogLevel.Off)
    market = account.create_market("OHLCV", tse.MdType.Ohlcv)
    account.create_simulator("Sim", H.simulator_options())

    # Eight robots trade the crossover of example 22 and differ only in how far back the slow average
    # looks. Each robot trades a contract of its own, and every contract is fed the very same WTI bars:
    # the profit of a fill is booked against the exposure of its contract, so robots sharing one
    # contract would not be separate strategies.
    slow_periods = [10, 15, 20, 25, 30, 35, 40, 45]
    contracts = []
    robots = []
    for slow in slow_periods:
        period = str(slow)
        contract = "WTI_" + period
        fast_input = "Fast " + contract
        slow_input = "Slow " + contract
        to_long = "ToLong " + contract
        to_short = "ToShort " + contract
        entry_rule = "Entry " + contract
        exit_rule = "Exit " + contract
        robot = "SMA 3/" + period
        account.add_contract(contract, 1, tse.Instrument.Future, tse.Underlying.Commodity, tse.Venue.Undefined, 100000)
        account.add_input_ohlcv(fast_input, 3, tse.Duration.Days, H.make_sma(3), market, [contract])
        account.add_input_ohlcv(slow_input, slow, tse.Duration.Days, H.make_sma(slow), market, [contract])
        account.add_pattern_crossover(to_long, tse.Duration.Days, [fast_input, slow_input], tse.Cmp.Ge)
        account.add_pattern_crossover(to_short, tse.Duration.Days, [fast_input, slow_input], tse.Cmp.Lt)
        account.add_rule_market(entry_rule, tse.RuleType.Entry, H.entry_params(580.0), to_long, contract)
        account.add_rule_market(exit_rule, tse.RuleType.Exit, H.exit_params(), to_short, contract)
        account.add_robot(robot, [entry_rule, exit_rule])
        contracts.append(contract)
        robots.append(robot)
    account.set_account_equity(starting_equity, 0.0)
    for robot in robots:
        account.start(robot)

    ticks = account.load_ohlcv_csv(H.data_path("WTI_OHLCVminute_sept2016.csv"))
    for tick in ticks:
        for contract in contracts:
            market.push_ohlcv_by_name(contract, tick)

    # ex_post gets every core of the machine. The day buckets of different robots are built in
    # parallel, and at each day step the robots whose scoring window is complete are scored in
    # parallel. The scores do not depend on the number of threads: they are the same, bit for bit,
    # as with one thread, except thompson_sampling, which is random by design.
    ex_post = account.create_ex_post(tse.Duration.Days, -1, 5, H.hardware_threads())

    # The scoring runs once, when the object is created. Saving writes the buckets and the scores
    # the object already holds, and loading the database back shows the round trip.
    db_path = os.path.join(tempfile.gettempdir(), "tse_stats_threads_py.sqlite3.db")
    H.cleanup(db_path)
    ex_post.save(db_path)
    saved_robots = account.ex_post_load(db_path)
    H.cleanup(db_path)

    for robot in range(ex_post.robot_count()):
        label = ex_post.robot_label(robot)
        summary = account.get_robot_summary(label)
        print("{}: trades={} netProfit={:.4f} buckets={}".format(
            label, summary.totalNumberOfTrades, summary.totalNetProfit, ex_post.bucket_count(robot)))

    summary = account.get_summary()
    print("netProfit={:.4f} trades={} robots={} params={} savedRobots={}".format(
        summary.totalNetProfit, summary.totalNumberOfTrades, ex_post.robot_count(), ex_post.param_count(), saved_robots))

    ex_post.close()
    account.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
