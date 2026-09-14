import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H

tse = H.tse


def main():
    account = H.account("RiskManagementExternalTools BracketTrailing", tse.StorageRegime.Mem, tse.LogLevel.Off)
    ticks = H.load_aapl()

    market = account.create_market("OHLCV", tse.MdType.Ohlcv)
    account.create_simulator("Sim", H.simulator_options(), 200, -1)
    account.add_contract("AAPL", 1, tse.Instrument.Equity, tse.Underlying.Undefined, tse.Venue.Undefined, 100000)

    account.add_input_ohlcv("AAPL SMA fast", 50, tse.Duration.Days, H.make_sma(50), market, ["AAPL"])
    account.add_input_ohlcv("AAPL SMA slow", 200, tse.Duration.Days, H.make_sma(200), market, ["AAPL"])
    account.add_pattern_crossover("PatternToLong", tse.Duration.Days, ["AAPL SMA fast", "AAPL SMA slow"], tse.Cmp.Ge)

    # Buy 100 shares when the fifty-day average crosses above the two-hundred-day one.
    entry = tse.make_leg_descriptor(
        "AAPL",
        tse.Quantity.Fixed,
        100.0,
        tse.Price.Market,
        150.0,
        0.0,
        0.0,
        tse.TxnType.Enter,
        tse.Side.Long,
        tse.Side.Neutral,
        tse.Tif.Day,
        tse.Priority.Replaceable,
    )
    # Both legs rest at the venue again, but the stop now trails: it follows the highest price the
    # trade has reached, five percent behind, so gains already made are not handed back. The ten
    # percent target stays where the fill put it.
    risk = tse.make_venue_risk_spec(
        tse.make_venue_risk_leg(True, 1, 0.05),
        tse.make_venue_risk_leg(True, 0, 0.10),
        tse.Tif.Gtc,
    )
    account.add_rule_bracket("Bracket", entry, risk, "PatternToLong")
    account.add_robot("Bracket robot", ["Bracket"])
    account.start("Bracket robot")

    for tick in ticks:
        market.push_ohlcv_by_name("AAPL", tick)

    summary = account.get_summary()
    account.close()

    print("bracket trailing: trades={} netProfit={:.4f} grossProfit={:.4f} grossLoss={:.4f} profitFactor={:.6f} maxDrawdown={:.4f}".format(
        summary.totalNumberOfTrades, summary.totalNetProfit, summary.grossProfit,
        summary.grossLoss, summary.profitFactor, summary.maxDrawdown))
    return 0


if __name__ == "__main__":
    sys.exit(main())
