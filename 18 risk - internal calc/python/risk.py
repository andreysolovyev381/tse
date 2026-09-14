import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H

tse = H.tse

DAY_NS = 86_400_000_000_000


def build_aapl_robot(account, market):
    account.add_contract("AAPL", 1, tse.Instrument.Equity, tse.Underlying.Undefined, tse.Venue.Undefined, 100000)
    account.add_input_ohlcv("SMA50", 50, tse.Duration.Days, H.make_sma(50), market, ["AAPL"])
    account.add_input_ohlcv("SMA200", 200, tse.Duration.Days, H.make_sma(200), market, ["AAPL"])
    account.add_pattern_crossover("ToLong", tse.Duration.Days, ["SMA50", "SMA200"], tse.Cmp.Ge)
    account.add_pattern_crossover("ToShort", tse.Duration.Days, ["SMA50", "SMA200"], tse.Cmp.Lt)
    account.add_rule_market("Entry", tse.RuleType.Entry, H.entry_params(100.0), "ToLong", "AAPL")
    account.add_rule_market("Exit", tse.RuleType.Exit, H.exit_params(), "ToShort", "AAPL")


# Ceilings the engine tests inside itself, against the position the order would leave us
# holding, before it lets that order out: five thousand dollars of market value across the
# book, and a hundred shares of any one name. The hundred AAPL shares the robot buys are
# worth more than five thousand dollars once the share price passes fifty, so every entry
# from that point on is refused and this run parts company with the baseline.
def add_policies(account):
    account.add_risk_policy("MaxValue", tse.RiskPolicy.Value, 5000.0, tse.Cmp.Le, "")
    account.add_risk_policy("MaxQty", tse.RiskPolicy.Quantity, 100.0, tse.Cmp.Le, "AAPL")


# Exits the engine works out itself, tick by tick, from the open position: the fixed pair
# measures from the entry price, the trailing pair from the best price seen since entry.
def add_risk_rules(account):
    flat = H.exit_params()
    account.add_rule_risk("StopLoss", tse.RuleType.StopLoss, flat, -0.10, "AAPL")
    account.add_rule_risk("TakeProfit", tse.RuleType.TakeProfit, flat, 0.20, "AAPL")
    account.add_rule_risk("StopLossTrail", tse.RuleType.StopLossTrailing, flat, -0.12, "AAPL")
    account.add_rule_risk("TakeProfitTrail", tse.RuleType.TakeProfitTrailing, flat, 0.25, "AAPL")


def run_aapl(label, with_policies, with_risk_rules, aapl_ticks):
    account = H.account(label, tse.StorageRegime.Mem, tse.LogLevel.Off)
    market = account.create_market("OHLCV", tse.MdType.Ohlcv)
    account.create_simulator("Sim", H.simulator_options())
    build_aapl_robot(account, market)
    if with_policies:
        add_policies(account)
    rule_labels = ["Entry", "Exit"]
    if with_risk_rules:
        add_risk_rules(account)
        rule_labels = ["Entry", "StopLoss", "TakeProfit", "StopLossTrail", "TakeProfitTrail", "Exit"]
    account.add_robot("Strat", rule_labels)
    account.start("Strat")
    for tick in aapl_ticks:
        market.push_ohlcv_by_name("AAPL", tick)
    summary = account.get_summary()
    account.close()
    return summary


def run_wti_window(label, always_open):
    account = H.account(label, tse.StorageRegime.Mem, tse.LogLevel.Off)
    market = account.create_market("OHLCV", tse.MdType.Ohlcv)
    account.create_simulator("Sim", H.simulator_options())
    account.add_contract("WTI", 1, tse.Instrument.Future, tse.Underlying.Commodity, tse.Venue.Undefined, 100000)
    account.add_input_ohlcv("SMA3", 3, tse.Duration.Days, H.make_sma(3), market, ["WTI"])
    account.add_input_ohlcv("SMA10", 10, tse.Duration.Days, H.make_sma(10), market, ["WTI"])
    account.add_pattern_crossover("ToLong", tse.Duration.Days, ["SMA3", "SMA10"], tse.Cmp.Ge)
    account.add_pattern_crossover("ToShort", tse.Duration.Days, ["SMA3", "SMA10"], tse.Cmp.Lt)
    account.add_rule_market("Entry", tse.RuleType.Entry, H.entry_params(580.0), "ToLong", "WTI")
    account.add_rule_market("Exit", tse.RuleType.Exit, H.exit_params(), "ToShort", "WTI")
    account.add_robot("Strat", ["Entry", "Exit"])
    # Trading hours the engine enforces before every order. The second window is one
    # nanosecond a day wide, which is a plain way of saying "do not trade at all".
    if always_open:
        account.add_risk_policy_time_period("Window", "", DAY_NS, 0, DAY_NS, "UTC")
    else:
        account.add_risk_policy_time_period("Window", "", DAY_NS, 1, 1, "UTC")
    account.set_account_equity(100000.0, 0.0)
    account.start("Strat")
    wti_ticks = account.load_ohlcv_csv(H.data_path("WTI_OHLCVminute_sept2016.csv"))
    for tick in wti_ticks:
        market.push_ohlcv_by_name("WTI", tick)
    summary = account.get_summary()
    account.close()
    return summary


def main():
    aapl_ticks = H.load_aapl()

    baseline = run_aapl("RiskManagementInternalCalc Baseline", False, False, aapl_ticks)
    with_policies = run_aapl("RiskManagementInternalCalc Policies", True, False, aapl_ticks)
    with_rules = run_aapl("RiskManagementInternalCalc Rules", False, True, aapl_ticks)
    window_always = run_wti_window("RiskManagementInternalCalc TimeWindowAlways", True)
    window_never = run_wti_window("RiskManagementInternalCalc TimeWindowNever", False)

    print("baseline={:.4f}/{} policies={:.4f}/{} riskRules={:.4f}/{} windowAlways={:.4f}/{} windowNever={}".format(
        baseline.totalNetProfit, baseline.totalNumberOfTrades,
        with_policies.totalNetProfit, with_policies.totalNumberOfTrades,
        with_rules.totalNetProfit, with_rules.totalNumberOfTrades,
        window_always.totalNetProfit, window_always.totalNumberOfTrades,
        window_never.totalNumberOfTrades))
    return 0


if __name__ == "__main__":
    sys.exit(main())
