import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H

import numpy as np
import xgboost as xgb

tse = H.tse


def main():
    ticks = H.load_bidask("btc_bidask.csv")

    # The gradient boosted model is trained here and then lives inside the data processor:
    # the engine never inspects it, it only reads the number the processor pushes.
    model = train_model()

    account = H.account("BTC/USDT", tse.StorageRegime.Mem, tse.LogLevel.Off)
    market = account.create_market("MD", tse.MdType.Bidask)
    account.create_simulator("Sim", H.simulator_options())
    account.add_contract("BTC", 1, tse.Instrument.Future, tse.Underlying.Crypto, tse.Venue.Undefined, 10000)
    account.add_input_bidask("GBM", 1, tse.Duration.Minutes, use_gbm(model), market, ["BTC"])
    # A positive forecast means the model expects the price to rise: the robot stays long
    # while the forecast is positive and closes the position once it turns negative.
    account.add_pattern_threshold("ToLong", tse.Duration.Minutes, ["GBM"], tse.Cmp.Ge, 0.0)
    account.add_pattern_threshold("ToShort", tse.Duration.Minutes, ["GBM"], tse.Cmp.Lt, 0.0)
    account.add_rule_market("Entry", tse.RuleType.Entry, H.entry_params(1.0), "ToLong", "BTC")
    account.add_rule_market("Exit", tse.RuleType.Exit, H.exit_params(), "ToShort", "BTC")
    account.add_robot("Strat", ["Entry", "Exit"])
    account.start("Strat")

    for tick in ticks:
        market.push_bidask_by_name("BTC", tick)

    summary = account.get_summary()
    account.close()

    trades = summary.totalNumberOfTrades
    print("netProfit={:.4f} trades={}".format(summary.totalNetProfit, trades))
    return 0


def train_model():
    index = np.arange(256)
    mid = 44000.0 + (index % 40) * 50.0
    spread = 4.0 + (index % 15)
    imbalance = -1.0 + 2.0 * (index % 21) / 20.0
    features = np.column_stack([mid, spread, imbalance]).astype(np.float32)
    labels = imbalance
    dtrain = xgb.DMatrix(features, label=labels)
    params = {"objective": "reg:squarederror", "max_depth": 3, "eta": 0.3}
    return xgb.train(params, dtrain, num_boost_round=20)


def use_gbm(model):
    def processor(storage, contract_id, tick):
        # Book imbalance carries the signal the model was trained on: more volume resting
        # on the bid than on the ask is buying pressure.
        mid = (tick.bid + tick.ask) / 2.0
        spread = tick.ask - tick.bid
        denom = tick.bidVolume + tick.askVolume
        imbalance = (tick.bidVolume - tick.askVolume) / denom if denom != 0.0 else 0.0
        features = np.array([[mid, spread, imbalance]], dtype=np.float32)
        prediction = float(model.predict(xgb.DMatrix(features))[0])
        storage.push(tick.tsNanoseconds, prediction)
        return storage.size() >= 1

    return processor


if __name__ == "__main__":
    sys.exit(main())
