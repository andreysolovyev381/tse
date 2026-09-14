import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H

tse = H.tse


def main():
    aapl = H.load_aapl()
    btc = H.load_bidask("btc_bidask.csv")
    wti = H.load_trades("wti_trades.csv", tse.Side.Neutral)

    account = H.account("Multi", tse.StorageRegime.Mem, tse.LogLevel.Off)
    aapl_md = account.create_market("AAPL_OHLCV", tse.MdType.Ohlcv)
    btc_md = account.create_market("BTC_BIDASK", tse.MdType.Bidask)
    wti_md = account.create_market("WTI_TRADE", tse.MdType.Trade)
    account.create_simulator("Sim", H.simulator_options())

    account.add_contract("AAPL", 1, tse.Instrument.Equity, tse.Underlying.Undefined, tse.Venue.Undefined, 100000)
    account.add_contract("BTC/USDT", 1, tse.Instrument.Currency, tse.Underlying.Crypto, tse.Venue.Undefined, 100000)
    account.add_contract("WTI", 1, tse.Instrument.Future, tse.Underlying.Commodity, tse.Venue.Undefined, 100000)

    account.add_input_ohlcv("AaplSma", 50, tse.Duration.Days, H.make_sma(50), aapl_md, ["AAPL"])
    account.add_input_bidask("BtcSma", 20, tse.Duration.Minutes, H.make_bidask_sma(20), btc_md, ["BTC/USDT"])

    account.add_pattern_threshold("ToLong", tse.Duration.Days, ["AaplSma"], tse.Cmp.Ge, 50.0)
    account.add_pattern_peak("ToShort", tse.Duration.Minutes, ["BtcSma"])

    # The signal and the trade need not share a contract: a rich Apple says risk is on,
    # a peak in the Bitcoin mid price says the party is over, and the position is taken in oil.
    long_params = tse.make_rule_params(
        tse.Quantity.Fixed, 10.0, tse.Price.Market, 0.0, 0.0, 0.0,
        tse.Side.Long, tse.Side.Neutral, tse.Tif.Day, 10,
    )
    short_params = tse.make_rule_params(
        tse.Quantity.Fixed, 10.0, tse.Price.Limit, 80.0, 0.0, 0.0,
        tse.Side.Short, tse.Side.Long, tse.Tif.Day, 10,
    )
    account.add_rule_market("Entry", tse.RuleType.Entry, long_params, "ToLong", "WTI")
    account.add_rule_market("Exit", tse.RuleType.Exit, short_params, "ToShort", "WTI")
    account.add_robot("Strat", ["Entry", "Exit"])

    # An open position is worth what the market says right now, so the portfolio is marked off the oil feed.
    account.portfolio_subscribe(wti_md, "WTI")
    account.start("Strat")

    # Three feeds, three tick shapes, one clock: always push whichever feed holds the oldest
    # unsent tick, so the strategy lives through the history in the order it really happened.
    far_future = float("inf")
    aapl_at, btc_at, wti_at = 0, 0, 0
    while aapl_at < len(aapl) or btc_at < len(btc) or wti_at < len(wti):
        aapl_ts = aapl[aapl_at].tsNanoseconds if aapl_at < len(aapl) else far_future
        btc_ts = btc[btc_at].tsNanoseconds if btc_at < len(btc) else far_future
        wti_ts = wti[wti_at].tsNanoseconds if wti_at < len(wti) else far_future
        if aapl_ts <= btc_ts and aapl_ts <= wti_ts:
            aapl_md.push_ohlcv_by_name("AAPL", aapl[aapl_at])
            aapl_at += 1
        elif btc_ts <= wti_ts:
            btc_md.push_bidask_by_name("BTC/USDT", btc[btc_at])
            btc_at += 1
        else:
            wti_md.push_trade_by_name("WTI", wti[wti_at])
            wti_at += 1

    summary = account.get_summary()
    trades = account.get_trades()
    wti_pos = account.get_position_state("WTI")
    account.close()

    print("netProfit={:.4f} trades={} retained={} wtiMark={:.4f}".format(
        summary.totalNetProfit, summary.totalNumberOfTrades, len(trades), wti_pos.marketPrice))
    return 0


if __name__ == "__main__":
    sys.exit(main())
