import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H

tse = H.tse


def main():
    base = os.path.join(tempfile.gettempdir(), "tse_example_initial_params_py")
    shutil.rmtree(base, ignore_errors=True)
    data_a = os.path.join(base, "data_a")
    data_b = os.path.join(base, "data_b")
    logs = os.path.join(base, "logs", "run", "day_01")
    os.makedirs(data_a)

    # An existing folder is taken as it stands, a missing one is created for you, the whole path at once.
    tse.set_initial_params(data_folder=data_a, log_folder=logs, log_level=tse.LogLevel.Off, lib_path=H.LIB_PATH)

    # An account captures the data folder at construction, so a later switch never moves
    # the blotter of an account that is already trading: only the next account sees data_b.
    # In the db regime that blotter is a file of the configured folder, named after the account.
    captured_a = tse.Account("CapturedA", tse.StorageRegime.Db, lib_path=H.LIB_PATH)
    tse.set_initial_params(data_folder=data_b, log_folder="", log_level=None, lib_path=H.LIB_PATH)
    captured_b = tse.Account("CapturedB", tse.StorageRegime.Db, lib_path=H.LIB_PATH)
    captured_a.close()
    captured_b.close()

    blotter_in_data_a = os.path.isfile(os.path.join(data_a, "CapturedA_Blotter_retained_trades.sqlite3.db"))
    blotter_in_data_b = os.path.isfile(os.path.join(data_b, "CapturedB_Blotter_retained_trades.sqlite3.db"))
    nested_log_folder = os.path.isdir(logs)

    # A file standing where a folder is expected is refused, and the reason waits in the init error channel.
    a_file = os.path.join(base, "not_a_dir.txt")
    with open(a_file, "w") as handle:
        handle.write("x")
    try:
        tse.set_initial_params(data_folder=a_file, log_folder="", log_level=None, lib_path=H.LIB_PATH)
    except tse.TseError as error:
        print("data folder refused: {}".format(error))

    shutil.rmtree(base, ignore_errors=True)
    print("blotterInDataA={} blotterInDataB={} nestedLogFolder={}".format(
        int(blotter_in_data_a), int(blotter_in_data_b), int(nested_log_folder)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
