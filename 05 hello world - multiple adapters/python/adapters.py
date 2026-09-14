import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H

tse = H.tse


def main():
    rows = H.load_aapl()

    account = H.account("MultiAdapter", tse.StorageRegime.Mem, tse.LogLevel.Off)
    # two adapters feed one account: daily bars for the equity, a quote stream for the crypto pair
    prices = account.create_market("EQUITY_OHLCV", tse.MdType.Ohlcv)
    book = account.create_market("CRYPTO_BIDASK", tse.MdType.Bidask)
    account.create_simulator("Sim", H.simulator_options())

    account.add_contract("AAPL", 1, tse.Instrument.Equity, tse.Underlying.Undefined, tse.Venue.Undefined, 100000)
    account.add_contract("BTC/USDT", 1, tse.Instrument.Currency, tse.Underlying.Crypto, tse.Venue.Undefined, 100000)

    # the golden cross: hold AAPL while the 50-day average leads the 200-day one, stand aside when it falls back
    account.add_input_ohlcv("SMA50", 50, tse.Duration.Days, H.make_sma(50), prices, ["AAPL"])
    account.add_input_ohlcv("SMA200", 200, tse.Duration.Days, H.make_sma(200), prices, ["AAPL"])
    account.add_pattern_crossover("ToLong", tse.Duration.Days, ["SMA50", "SMA200"], tse.Cmp.Ge)
    account.add_pattern_crossover("ToShort", tse.Duration.Days, ["SMA50", "SMA200"], tse.Cmp.Lt)
    account.add_rule_market("Entry", tse.RuleType.Entry, H.entry_params(100.0), "ToLong", "AAPL")
    account.add_rule_market("Exit", tse.RuleType.Exit, H.exit_params(), "ToShort", "AAPL")
    account.add_robot("Strat", ["Entry", "Exit"])

    # BTC/USDT carries no rules: it is only subscribed, so the portfolio keeps marking it at the latest quote
    account.portfolio_subscribe(book, "BTC/USDT")
    account.start("Strat")

    ts = 1000000000
    for px in (15.0, 20.0, 30.0, 36.0, 42.0):
        book.push_bidask_by_name("BTC/USDT", tse.TseTickBidAsk(ts, px - 0.05, 1.0, px + 0.05, 1.0, px))
        ts += 1000000000
    for tick in rows:
        prices.push_ohlcv_by_name("AAPL", tick)

    summary = account.get_summary()
    btc = account.get_position_state("BTC/USDT")
    btc_market_price = btc.marketPrice
    btc_quantity = btc.quantity
    account.close()

    print("multi adapter: AAPL netProfit={:.4f} trades={}; BTC/USDT marked at {:.2f} quantity={:.1f}".format(
        summary.totalNetProfit, summary.totalNumberOfTrades, btc_market_price, btc_quantity))
    return 0


if __name__ == "__main__":
    sys.exit(main())
