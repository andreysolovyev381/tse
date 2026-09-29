import math
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H
import tse_selection as S

import xgboost as xgb

tse = H.tse


def main():
    ticks = H.load_aapl()

    # The five candidates of example 23, alike but for how far back the fast average looks, and the
    # same model choosing among them by their ex_post scores.
    params = [10.0, 20.0, 50.0, 100.0, 150.0]
    extracted = {}

    def builder(account, param_value):
        account.set_log_level(tse.LogLevel.Off)
        short_period = int(param_value)
        S.build_candidate(account, short_period, ticks)
        # The finished account is scored twice: once on a single thread and once on every core
        # of the machine. Every row of the two score matrices is compared bit for bit, except the
        # thompson_sampling column, which is random by design, so only where it is NaN must agree.
        one_thread = account.create_ex_post(tse.Duration.Days, -1, 5, 1)
        all_cores = account.create_ex_post(tse.Duration.Days, -1, 5, H.hardware_threads())
        try:
            width = all_cores.param_count()
            single_ts, single_values = one_thread.feature_momentum(0)
            momentum_ts, momentum_values = all_cores.feature_momentum(0)
            same = single_ts == momentum_ts and same_features(
                single_values, momentum_values, width, thompson_column(all_cores)
            )
            # The model reads the scores of the call on every core: the last day bucket says what
            # shape the candidate was in when the backtest ended.
            extracted[short_period] = {
                "buckets": all_cores.bucket_count(0),
                "finite": S.count_finite(momentum_values),
                "features": momentum_values[-width:],
                "same": same,
            }
        finally:
            all_cores.close()
            one_thread.close()

    results = tse.run_grid("AAPLSelect", tse.StorageRegime.Mem, params, builder, tse.Currency.Usd, lib_path=H.LIB_PATH)

    features = []
    labels = []
    for r in results:
        period = int(r.param_value)
        record = extracted[period]
        features.append(record["features"])
        labels.append(r.summary.totalNetProfit)
        print(
            "SMA({:>3d}): buckets={} finiteScores={} netProfit={:.4f} sameAsOneThread={}".format(
                period, record["buckets"], record["finite"], r.summary.totalNetProfit, "yes" if record["same"] else "no"
            )
        )

    # As in example 23, the model learns net profit on every candidate but the last one and then
    # ranks all five: the pick is the highest predicted profit.
    booster = S.train_model(features, labels)
    best_index = S.select_best(booster, features)

    print(
        "candidate selection: candidates={} scoreParams={} model picks SMA({})".format(
            len(params), len(features[0]), int(params[best_index])
        )
    )
    return 0


def thompson_column(ex_post):
    for p in range(ex_post.param_count()):
        if ex_post.param_name(p) == "thompson_sampling":
            return p
    return None


def same_features(one_thread, all_cores, width, thompson):
    if len(one_thread) != len(all_cores):
        return False
    for k, (single, many) in enumerate(zip(one_thread, all_cores)):
        if k % width == thompson:
            if math.isnan(single) != math.isnan(many):
                return False
        elif struct.pack("<f", single) != struct.pack("<f", many):
            return False
    return True


if __name__ == "__main__":
    sys.exit(main())
