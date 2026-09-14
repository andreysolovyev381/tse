import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H

tse = H.tse

WINDOW_FROM_NS = 1_262_304_000_000_000_000
WINDOW_TO_NS = 1_293_840_000_000_000_000


def main():
    reader = tse.CsvReader(date_format="%Y-%m-%d", separator=";", has_header=True, lib_path=H.LIB_PATH)
    reader.read_file(H.data_path("aapl.csv"))
    stamps = reader.read_index_by_name("Index")
    closes = reader.read_doubles_by_name("AAPL.Close")
    reader.close()

    # Cut the series down to calendar year 2010: the bounds are midnight UTC on 1 January 2010
    # and on 1 January 2011, so the year is taken whole and neither neighbour leaks in.
    window_stamps = []
    window_closes = []
    for ts, close in zip(stamps, closes):
        if WINDOW_FROM_NS <= ts < WINDOW_TO_NS:
            window_stamps.append(ts)
            window_closes.append(close)

    writer = tse.CsvWriter(separator=";", format_timestamps=True, date_format="%Y-%m-%d", lib_path=H.LIB_PATH)
    filtered_path = os.path.join(tempfile.gettempdir(), "tse_example_timeserie_filtered_py.csv")
    writer.write_scalar(filtered_path, window_stamps, window_closes, headers=["Index", "AAPL.Close"])
    writer.close()

    # What the writer put on disk is a series in its own right, so the reader takes it back.
    rereader = tse.CsvReader(date_format="%Y-%m-%d", separator=";", has_header=True, lib_path=H.LIB_PATH)
    rereader.read_file(filtered_path)
    filtered_stamps = rereader.read_index_by_name("Index")
    filtered_closes = rereader.read_doubles_by_name("AAPL.Close")
    rereader.close()

    # head and tail hand back a (begin, length) window into the series, never a copy of it.
    head_bounds = tse.head(filtered_stamps, filtered_closes, n=5, lib_path=H.LIB_PATH)
    tail_bounds = tse.tail(filtered_stamps, filtered_closes, n=5, lib_path=H.LIB_PATH)

    os.remove(filtered_path)

    print("rows={} window2010={} head=({},{}) tail=({},{})".format(
        len(stamps), len(window_stamps),
        head_bounds[0], head_bounds[1],
        tail_bounds[0], tail_bounds[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
