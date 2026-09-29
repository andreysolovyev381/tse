import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H
import tse_selection as S

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
        S.build_candidate(account, short_period, ticks)
        ex_post = account.create_ex_post(tse.Duration.Days, -1, 5, H.hardware_threads())
        try:
            width = ex_post.param_count()
            _, values = ex_post.feature_momentum(0)
            # Scores of the last day bucket say what shape the candidate was in when the backtest
            # ended, and that single row is everything the model gets to see about it.
            extracted[short_period] = {
                "buckets": ex_post.bucket_count(0),
                "finite": S.count_finite(values),
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
    booster = S.train_model(features, labels)
    best_index = S.select_best(booster, features)

    print(
        "candidate selection: candidates={} scoreParams={} model picks SMA({})".format(
            len(params), len(features[0]), int(params[best_index])
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
