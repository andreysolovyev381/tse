import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H

tse = H.tse

SYMBOL = "RBL"

ENTER_CHECKPOINT = 1000000000
REBALANCE_CHECKPOINT = 2000000000
REBALANCE_COOLDOWN = 1000000000
HUGE_COOLDOWN = 1000000000000000
DRAIN_OFFSET = 100000000

ENTRY_QUANTITY = 4.0
REBALANCE_QUANTITY = 2.0
ENTRY_PRICE = 100.0
FIRST_REBALANCE_PRICE = 103.0
SECOND_REBALANCE_PRICE = 108.0
REBALANCE_LIMIT_PRICE = 110.0


def price_processor(storage, contract_id, tick):
    storage.push(tick.tsNanoseconds, tick.price)
    return True


def make_rule_params(quantity, limit_price, txn_side, pos_side):
    return tse.make_rule_params(
        tse.Quantity.Fixed, quantity,
        tse.Price.Limit, limit_price, 0.0, 0.0,
        txn_side, pos_side, tse.Tif.Day, 10,
    )


def push_price(market, ts_nanoseconds, price):
    market.push_trade_by_name(SYMBOL, tse.TseTickTrade(ts_nanoseconds, price, 1.0, int(tse.Side.Trade)))


def main():
    account = H.account("RebalanceExample", tse.StorageRegime.Mem, tse.LogLevel.Off)
    market = account.create_market("price", tse.MdType.Trade)
    account.create_simulator("sim", H.simulator_options(), 64, -1)
    account.add_contract(SYMBOL, 1, tse.Instrument.Future, tse.Underlying.Commodity, tse.Venue.Undefined, 100000)
    account.add_input_trade("Px", 2, tse.Duration.Nanoseconds, price_processor, market, [SYMBOL])
    # The entry pattern carries a cool down it can never reach, so the position is opened once;
    # the rebalance pattern rearms one second after it fires and can therefore fire again.
    account.add_pattern_timestamp("EnterAt", tse.Duration.Nanoseconds, ["Px"], ENTER_CHECKPOINT, HUGE_COOLDOWN)
    account.add_pattern_timestamp("RebalanceAt", tse.Duration.Nanoseconds, ["Px"], REBALANCE_CHECKPOINT, REBALANCE_COOLDOWN)
    account.add_rule_market(
        "RuleEntry", tse.RuleType.Entry,
        make_rule_params(ENTRY_QUANTITY, ENTRY_PRICE, tse.Side.Long, tse.Side.Neutral),
        "EnterAt", SYMBOL,
    )
    # Every firing of a rebalance adds two more units on top of the position already held:
    # it is an additive chaining delta, not an instruction to bring the position to two.
    account.add_rule_market(
        "RuleRebalance", tse.RuleType.Rebalance,
        make_rule_params(REBALANCE_QUANTITY, REBALANCE_LIMIT_PRICE, tse.Side.Long, tse.Side.Long),
        "RebalanceAt", SYMBOL,
    )
    account.add_robot("RebalanceRobot", ["RuleEntry", "RuleRebalance"])
    account.portfolio_subscribe(market, SYMBOL)
    account.start("RebalanceRobot")

    push_price(market, ENTER_CHECKPOINT, ENTRY_PRICE)
    push_price(market, ENTER_CHECKPOINT + DRAIN_OFFSET, ENTRY_PRICE)

    push_price(market, REBALANCE_CHECKPOINT, FIRST_REBALANCE_PRICE)
    push_price(market, REBALANCE_CHECKPOINT + DRAIN_OFFSET, FIRST_REBALANCE_PRICE)

    push_price(market, REBALANCE_CHECKPOINT + REBALANCE_COOLDOWN, SECOND_REBALANCE_PRICE)
    push_price(market, REBALANCE_CHECKPOINT + REBALANCE_COOLDOWN + DRAIN_OFFSET, SECOND_REBALANCE_PRICE)

    # Four units at 100, two more at 103, two more at 108: the position grows to eight and the
    # acquisition price is re-averaged over every fill.
    summary = account.get_summary()
    final_state = account.get_position_state(SYMBOL)
    position = final_state.quantity
    acquisition_price = final_state.acquisitionPrice
    trades = summary.totalNumberOfTrades
    account.close()

    print(
        "rebalance: position={:.0f} acquisitionPrice={:.4f} trades={}".format(
            position, acquisition_price, trades
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
