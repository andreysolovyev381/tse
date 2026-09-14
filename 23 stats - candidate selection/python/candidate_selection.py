import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H

import xgboost as xgb

tse = H.tse


def main():
    ticks = H.load_aapl()

    # Five candidate strategies, alike but for how far back the fast average looks. Which one to
    # trade next is decided by a model reading their ex_post scores, not by whoever earned most.
    params = [10.0, 20.0, 50.0, 100.0, 150.0]
    extracted = {}

    def builder(account, param_value):
        account.set_log_level(tse.LogLevel.Off)
        short_period = int(param_value)
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
        ex_post = account.create_ex_post(tse.Duration.Days, -1, 5)
        try:
            width = ex_post.param_count()
            _, values = ex_post.feature_momentum(0)
            # Scores of the last day bucket say what shape the candidate was in when the backtest
            # ended, and that single row is everything the model gets to see about it.
            extracted[short_period] = {
                "buckets": ex_post.bucket_count(0),
                "finite": sum(1 for v in values if not math.isnan(v)),
                "features": values[-width:],
            }
        finally:
            ex_post.close()

    results = tse.run_grid("AAPLSelect", tse.StorageRegime.Mem, params, builder, tse.Currency.Usd, lib_path=H.LIB_PATH)

    features = []
    labels = []
    for r in results:
        period = int(r.param_value)
        record = extracted[period]
        features.append(record["features"])
        labels.append(r.summary.totalNetProfit)
        print(
            "SMA({:>3d}): buckets={} finiteScores={} netProfit={:.4f}".format(
                period, record["buckets"], record["finite"], r.summary.totalNetProfit
            )
        )

    # The model learns net profit from those scores on every candidate but the last one, which is
    # held back, and then ranks all five: the pick is the highest predicted profit.
    booster = train_model(features, labels)
    best_index = select_best(booster, features)

    print(
        "candidate selection: candidates={} scoreParams={} model picks SMA({})".format(
            len(params), len(features[0]), int(params[best_index])
        )
    )
    return 0


def train_model(features, labels):
    d_train = xgb.DMatrix(features[:-1], label=labels[:-1])
    booster_params = {"objective": "reg:squarederror", "max_depth": 2, "eta": 0.3, "verbosity": 0}
    return xgb.train(booster_params, d_train, num_boost_round=4)


def select_best(booster, features):
    predicted = booster.predict(xgb.DMatrix(features))
    return max(range(len(predicted)), key=lambda i: predicted[i])


if __name__ == "__main__":
    sys.exit(main())
