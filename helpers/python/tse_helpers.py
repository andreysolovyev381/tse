import os
import sys

_HELPERS_DIR = os.path.dirname(os.path.abspath(__file__))
EXAMPLES_ROOT = os.path.abspath(os.path.join(_HELPERS_DIR, "..", ".."))
_ENGINE_ROOT = os.path.abspath(os.path.join(EXAMPLES_ROOT, "..", "..", ".."))

SDK_PYTHON_DIR = os.environ.get(
    "TSE_SDK_PYTHON",
    os.path.join(_ENGINE_ROOT, "src", "library", "export", "python"),
)
LIB_PATH = os.environ.get(
    "TSE_EXPORT_LIB",
    os.path.join(_ENGINE_ROOT, "deployment", "dist", "libtse_export.dylib"),
)
DATA_DIR = os.environ.get("TSE_DATA_DIR", os.path.join(EXAMPLES_ROOT, "data"))

if SDK_PYTHON_DIR not in sys.path:
    sys.path.insert(0, SDK_PYTHON_DIR)
import tse


def data_path(name):
    return os.path.join(DATA_DIR, name)


def cleanup(db_path):
    for suffix in ("", "-wal", "-shm"):
        try:
            os.remove(db_path + suffix)
        except OSError:
            pass


def account(label, regime, log):
    acc = tse.Account(label, regime, lib_path=LIB_PATH)
    acc.set_log_level(log)
    return acc


def simulator_options():
    return tse.make_simulator_config(
        100, 100,
        tse.OhlcvField.Close, tse.OhlcvField.Open,
        tse.BidAskField.Bid, tse.BidAskField.Ask,
    )


def entry_params(quantity):
    return tse.make_rule_params(
        tse.Quantity.Fixed, quantity,
        tse.Price.Market, 0.0, 0.0, 0.0,
        tse.Side.Long, tse.Side.Neutral, tse.Tif.Day, 10,
    )


def exit_params():
    return tse.make_rule_params(
        tse.Quantity.All, 0.0,
        tse.Price.Market, 0.0, 0.0, 0.0,
        tse.Side.Short, tse.Side.Long, tse.Tif.Day, 10,
    )


def make_rolling_mean(period):
    state = {"window": [0.0] * period, "rolling_sum": 0.0, "count": 0}

    def mean(value):
        idx = state["count"] % period
        if state["count"] >= period:
            state["rolling_sum"] += value - state["window"][idx]
        else:
            state["rolling_sum"] += value
        state["window"][idx] = value
        state["count"] += 1
        return state["rolling_sum"] / period

    return mean


def make_sma(period):
    mean = make_rolling_mean(period)

    def processor(storage, contract_id, tick):
        storage.push(tick.tsNanoseconds, mean(tick.close))
        return storage.size() >= period

    return processor


def make_bidask_sma(period):
    mean = make_rolling_mean(period)

    def processor(storage, contract_id, tick):
        storage.push(tick.tsNanoseconds, mean((tick.bid + tick.ask) / 2.0))
        return storage.size() >= period

    return processor


def load_aapl():
    import calendar
    import datetime

    rows = []
    with open(data_path("aapl.csv")) as handle:
        header = handle.readline().strip().split(";")
        col = {name: i for i, name in enumerate(header)}
        for line in handle:
            parts = line.strip().split(";")
            if len(parts) < len(header):
                continue
            date = datetime.datetime.strptime(parts[col["Index"]], "%Y-%m-%d")
            ts_ns = int(calendar.timegm(date.timetuple())) * 1_000_000_000
            rows.append(
                tse.TseTickOHLCV(
                    ts_ns,
                    float(parts[col["AAPL.Open"]]),
                    float(parts[col["AAPL.High"]]),
                    float(parts[col["AAPL.Low"]]),
                    float(parts[col["AAPL.Close"]]),
                    float(parts[col["AAPL.Volume"]]),
                )
            )
    return rows


def load_csv(name):
    rows = []
    with open(data_path(name)) as handle:
        handle.readline()
        for line in handle:
            line = line.strip()
            if not line:
                continue
            rows.append([float(x) for x in line.split(";")])
    return rows


def load_bidask(name):
    return [
        tse.TseTickBidAsk(int(r[0]), r[1], r[2], r[3], r[4], r[5])
        for r in load_csv(name)
    ]


def load_trades(name, side):
    return [
        tse.TseTickTrade(int(r[0]), r[1], r[2], int(side))
        for r in load_csv(name)
    ]
