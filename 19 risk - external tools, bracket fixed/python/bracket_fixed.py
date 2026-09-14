import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H

tse = H.tse


def main():
    account = H.account("RiskManagementExternalTools BracketFixed", tse.StorageRegime.Mem, tse.LogLevel.Off)
    ticks = account.load_ohlcv_csv(H.data_path("WTI_OHLCVminute_sept2016.csv"))

    market = account.create_market("OHLCV", tse.MdType.Ohlcv)
    account.create_simulator("Sim", H.simulator_options(), 10, -1)
    account.add_contract("WTI", 1, tse.Instrument.Future, tse.Underlying.Commodity, tse.Venue.Undefined, 100000)

    account.add_input_ohlcv("WTI SMA fast", 3, tse.Duration.Days, H.make_sma(3), market, ["WTI"])
    account.add_input_ohlcv("WTI SMA slow", 10, tse.Duration.Days, H.make_sma(10), market, ["WTI"])
    account.add_pattern_crossover("PatternToLong", tse.Duration.Days, ["WTI SMA fast", "WTI SMA slow"], tse.Cmp.Ge)

    # Buy 580 lots of crude as soon as the three-day average climbs above the ten-day one.
    entry = tse.make_leg_descriptor(
        "WTI",
        tse.Quantity.Fixed,
        580.0,
        tse.Price.Market,
        45.0,
        0.0,
        0.0,
        tse.TxnType.Enter,
        tse.Side.Long,
        tse.Side.Neutral,
        tse.Tif.Day,
        tse.Priority.Replaceable,
    )
    # Here the risk is not recomputed on every tick. The stop 2% below the fill and the target 3%
    # above it go out with the entry and rest at the venue, so they guard the position even if
    # this program stops running.
    risk = tse.make_venue_risk_spec(
        tse.make_venue_risk_leg(True, 0, 0.02),
        tse.make_venue_risk_leg(True, 0, 0.03),
        tse.Tif.Gtc,
    )
    account.add_rule_bracket("Bracket", entry, risk, "PatternToLong")
    account.add_robot("Bracket robot", ["Bracket"])
    account.start("Bracket robot")

    for tick in ticks:
        market.push_ohlcv_by_name("WTI", tick)

    summary = account.get_summary()
    account.close()

    print("bracket fixed: trades={} netProfit={:.4f} grossProfit={:.4f} grossLoss={:.4f} profitFactor={:.6f} maxDrawdown={:.4f}".format(
        summary.totalNumberOfTrades, summary.totalNetProfit, summary.grossProfit,
        summary.grossLoss, summary.profitFactor, summary.maxDrawdown))
    return 0


if __name__ == "__main__":
    sys.exit(main())
