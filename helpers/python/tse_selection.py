import math

import tse_helpers as H
import xgboost as xgb

tse = H.tse


def build_candidate(account, short_period, ticks):
    market = account.create_market("MD", tse.MdType.Ohlcv)
    account.create_simulator("Sim", H.simulator_options())
    account.add_contract("AAPL", 1, tse.Instrument.Equity, tse.Underlying.Undefined, tse.Venue.Undefined, 100000)
    account.add_input_ohlcv("SMAShort", short_period, tse.Duration.Days, H.make_sma(short_period), market, ["AAPL"])
    account.add_input_ohlcv("SMA200", 200, tse.Duration.Days, H.make_sma(200), market, ["AAPL"])
    account.add_pattern_crossover("ToLong", tse.Duration.Days, ["SMAShort", "SMA200"], tse.Cmp.Ge)
    account.add_pattern_crossover("ToShort", tse.Duration.Days, ["SMAShort", "SMA200"], tse.Cmp.Lt)
    account.add_rule_market("Entry", tse.RuleType.Entry, H.entry_params(100.0), "ToLong", "AAPL")
    account.add_rule_market("Exit", tse.RuleType.Exit, H.exit_params(), "ToShort", "AAPL")
    account.add_robot("Strat", ["Entry", "Exit"])
    account.set_account_equity(100000.0, 0.0)
    account.start("Strat")
    for tick in ticks:
        market.push_ohlcv_by_name("AAPL", tick)


def count_finite(values):
    return sum(1 for v in values if not math.isnan(v))


def train_model(features, labels):
    d_train = xgb.DMatrix(features[:-1], label=labels[:-1])
    booster_params = {"objective": "reg:squarederror", "max_depth": 2, "eta": 0.3, "verbosity": 0}
    return xgb.train(booster_params, d_train, num_boost_round=4)


def select_best(booster, features):
    predicted = booster.predict(xgb.DMatrix(features))
    return max(range(len(predicted)), key=lambda i: predicted[i])
