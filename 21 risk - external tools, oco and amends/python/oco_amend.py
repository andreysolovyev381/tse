import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H

tse = H.tse

SYMBOL = "Symbol_1_Close"

# The two moments of the story: at the first one the robot puts an order on the book,
# at the second one it changes its mind about that order.
ENTER_CHECKPOINT = 1000000000
AMEND_CHECKPOINT = 2000000000
HUGE_COOLDOWN = 1000000000000000


def price_processor(storage, contract_id, tick):
    storage.push(tick.tsNanoseconds, tick.price)
    return True


def make_rig(label, fire_label):
    account = H.account(label, tse.StorageRegime.Mem, tse.LogLevel.Off)
    market = account.create_market("price", tse.MdType.Trade)
    account.create_simulator("sim", H.simulator_options(), 64, -1)
    account.add_contract(SYMBOL, 1, tse.Instrument.Equity, tse.Underlying.Undefined, tse.Venue.Undefined, 100000)
    account.add_input_trade("Px", 2, tse.Duration.Nanoseconds, price_processor, market, [SYMBOL])
    account.add_pattern_timestamp("EnterAt", tse.Duration.Nanoseconds, ["Px"], ENTER_CHECKPOINT, HUGE_COOLDOWN)
    account.add_pattern_timestamp(fire_label, tse.Duration.Nanoseconds, ["Px"], AMEND_CHECKPOINT, HUGE_COOLDOWN)
    return account, market


def push_price(market, ts_nanoseconds, price):
    market.push_trade_by_name(SYMBOL, tse.TseTickTrade(ts_nanoseconds, price, 1.0, int(tse.Side.Trade)))


def run_amend_scenario(label, kind):
    account, market = make_rig(label, "AmendAt")
    account.add_rule_market(
        "Enter", tse.RuleType.Entry,
        tse.make_rule_params(
            tse.Quantity.Fixed, 10.0,
            tse.Price.Limit, 99.0, 0.0, 0.0,
            tse.Side.Long, tse.Side.Neutral, tse.Tif.Day, 10,
        ),
        "EnterAt", SYMBOL,
    )
    # Three ways to change your mind about an order already resting at the venue: withdraw it,
    # shrink it, or pull it and post a fresh one at a different price.
    if kind == "cancel":
        account.add_rule_cancel("Amend", SYMBOL, 10.0, 10.0, "AmendAt")
    if kind == "modify":
        account.add_rule_modify("Amend", SYMBOL, 4.0, 99.0, "AmendAt")
    if kind == "replace":
        account.add_rule_replace("Amend", SYMBOL, 4.0, 98.0, "AmendAt")
    account.add_robot("AmendRobot", ["Enter", "Amend"])
    account.start("AmendRobot")

    push_price(market, ENTER_CHECKPOINT, 100.0)
    push_price(market, AMEND_CHECKPOINT, 100.0)
    after = account.get_position_state(SYMBOL).quantity
    account.close()
    return after


def run_oco_scenario():
    account, market = make_rig("RiskManagementExternalTools OcoPair", "OcoAt")
    account.add_rule_market("Enter", tse.RuleType.Entry, H.entry_params(100.0), "EnterAt", SYMBOL)
    # Stop and target left resting at the venue as one pair: whichever is hit first cancels
    # the other, so the position can never be closed twice.
    account.add_rule_oco(
        "OCO", SYMBOL,
        tse.make_venue_risk_spec(
            tse.make_venue_risk_leg(True, 0, 0.02),
            tse.make_venue_risk_leg(True, 0, 0.03),
            tse.Tif.Gtc,
        ),
        "OcoAt",
    )
    account.add_robot("OcoRobot", ["Enter", "OCO"])
    account.start("OcoRobot")

    push_price(market, ENTER_CHECKPOINT, 100.0)
    push_price(market, 1200000000, 100.0)
    push_price(market, AMEND_CHECKPOINT, 100.0)
    push_price(market, 3000000000, 97.0)
    push_price(market, 4000000000, 97.0)

    summary = account.get_summary()
    account.close()
    return summary


def main():
    after_cancel = run_amend_scenario("RiskManagementExternalTools AmendCancel", "cancel")
    after_modify = run_amend_scenario("RiskManagementExternalTools AmendModify", "modify")
    after_replace = run_amend_scenario("RiskManagementExternalTools AmendReplace", "replace")
    oco = run_oco_scenario()

    print(
        "oco_amend: afterCancel={:.1f} afterModify={:.1f} afterReplace={:.1f} ocoNetProfit={:.2f} ocoTrades={}".format(
            after_cancel, after_modify, after_replace, oco.totalNetProfit, oco.totalNumberOfTrades
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
