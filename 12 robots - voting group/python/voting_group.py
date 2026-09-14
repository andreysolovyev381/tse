import os
import random
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H

tse = H.tse

CONTRACT = "WTI"


def close_params(quantity):
    return tse.make_rule_params(
        tse.Quantity.Fixed, quantity,
        tse.Price.Market, 0.0, 0.0, 0.0,
        tse.Side.Short, tse.Side.Long, tse.Tif.Day, 10,
    )


def coin_flip(generator, chance):
    def vote(input_label, ts_nanoseconds, value):
        return generator.random() < chance

    return vote


def to_retained(trade):
    return tse.make_retained(
        trade.tsExecutionNanoseconds,
        trade.symbol.decode(),
        0,
        trade.clientOrderId.decode(),
        trade.brokerOrderId.decode(),
        trade.ruleLabel.decode(),
        trade.robotLabel.decode(),
        trade.tsMktEventNanoseconds,
        trade.tsExecutionNanoseconds,
        trade.price,
        trade.quantity,
        trade.fee,
        trade.bookedPL,
        trade.txnSide,
        trade.posSide,
        trade.bookMode,
        trade.execution,
        tse.Price.Market,
        tse.Quantity.Fixed,
        tse.Tif.Day,
        tse.Priority.NonReplaceable,
    )


def main():
    generator = random.Random(42)

    trader_quantity = 10.0
    voter_quantity = 30.0
    vote_chance = 0.0001
    voter_label = "Voting robot"
    trader_labels = ["Trader one", "Trader two", "Trader three"]

    account = H.account("VotingGroup", tse.StorageRegime.Mem, tse.LogLevel.Off)
    ticks = account.load_ohlcv_csv(H.data_path("WTI_OHLCVminute_sept2016.csv"))

    prices = account.create_market("WTI prices", tse.MdType.Ohlcv)
    executed = account.create_market("Group fills", tse.MdType.Executed)
    execution = account.create_simulator("Sim", H.simulator_options())
    account.add_contract(CONTRACT, 1, tse.Instrument.Future, tse.Underlying.Commodity, tse.Venue.Undefined, 100000)

    def price_heartbeat(storage, contract_id, tick):
        storage.push(tick.tsNanoseconds, tick.close)
        return True

    account.add_input_ohlcv("WTI price", 1, tse.Duration.Minutes, price_heartbeat, prices, [CONTRACT])

    # Every trader ignores the price and votes by a coin flip: the price feed is only the
    # heartbeat that makes all three of them decide once per bar.
    for trader in trader_labels:
        long_pattern = trader + " goes long"
        flat_pattern = trader + " goes flat"
        enter_rule = trader + " enter"
        exit_rule = trader + " exit"
        account.add_pattern_formula(long_pattern, tse.Duration.Minutes, ["WTI price"], coin_flip(generator, vote_chance))
        account.add_pattern_formula(flat_pattern, tse.Duration.Minutes, ["WTI price"], coin_flip(generator, vote_chance))
        account.add_rule_market(enter_rule, tse.RuleType.Entry, H.entry_params(trader_quantity), long_pattern, CONTRACT)
        account.add_rule_market(exit_rule, tse.RuleType.Exit, close_params(trader_quantity), flat_pattern, CONTRACT)
        account.add_robot(trader, [enter_rule, exit_rule])

    # The group is assembled right here: the three traders' fills are replayed into an
    # executed-trade adapter, the fourth robot's input over that channel keeps their signed
    # sum - a long fill adds, a short fill subtracts - and the two thresholds over that net
    # put the fourth robot long while the group is net long and flat when the net is back to zero.
    group = {"net": 0.0}

    def group_net(storage, contract_id, trade):
        if trade.txnSide == int(tse.Side.Long):
            group["net"] += trade.quantity
        else:
            group["net"] -= trade.quantity
        storage.push(trade.tsNanoseconds, group["net"])
        return True

    account.add_input_executed("Group net", 1, tse.Duration.Minutes, group_net, executed, [CONTRACT])
    account.add_pattern_threshold("Group is long", tse.Duration.Minutes, ["Group net"], tse.Cmp.Gt, 0.0)
    account.add_pattern_threshold("Group is flat", tse.Duration.Minutes, ["Group net"], tse.Cmp.Le, 0.0)
    account.add_rule_market("Voter enter", tse.RuleType.Entry, H.entry_params(voter_quantity), "Group is long", CONTRACT)
    account.add_rule_market("Voter exit", tse.RuleType.Exit, close_params(voter_quantity), "Group is flat", CONTRACT)
    account.add_robot(voter_label, ["Voter enter", "Voter exit"])

    for trader in trader_labels:
        account.start(trader)
    account.start(voter_label)

    seen_trades = 0
    voter_key = voter_label.encode()
    for tick in ticks:
        prices.push_ohlcv_by_name(CONTRACT, tick)
        if execution.get_count() == seen_trades:
            continue
        trades = account.get_trades(0, 0, None)
        for i in range(seen_trades, len(trades)):
            if trades[i].robotLabel == voter_key:
                continue
            executed.push_executed_by_name(trades[i].symbol.decode(), to_retained(trades[i]))
        seen_trades = len(trades)

    first = account.get_robot_summary(trader_labels[0])
    second = account.get_robot_summary(trader_labels[1])
    third = account.get_robot_summary(trader_labels[2])
    voter = account.get_robot_summary(voter_label)
    account.close()

    print("voting group: trader trades={}/{}/{} voter trades={} voter netProfit={:.4f}".format(
        first.totalNumberOfTrades, second.totalNumberOfTrades, third.totalNumberOfTrades,
        voter.totalNumberOfTrades, voter.totalNetProfit))
    return 0


if __name__ == "__main__":
    sys.exit(main())
