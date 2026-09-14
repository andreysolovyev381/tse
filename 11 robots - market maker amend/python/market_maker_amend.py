import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H

tse = H.tse

SYMBOL = "MMFLOW"
QUIET_GAP_NANOSECONDS = 10_000_000_000
PRINT_LAG_NANOSECONDS = 500_000_000
SAME_SIDE_RUN_LENGTH = 3
SWEEP_MULTIPLE = 3.0
QUOTE_SIDE = int(tse.Side.Long)


def load_client_flow():
    rows = []
    with open(H.data_path("mm_client_flow.csv")) as handle:
        handle.readline()
        for line in handle:
            parts = line.strip().split(",")
            if len(parts) < 8:
                continue
            side = tse.Side.Long if parts[3] == "bid" else tse.Side.Short
            rows.append((int(parts[0]), int(side), float(parts[6]), float(parts[7])))
    return rows


def main():
    quote_price = 43.22
    step_away_price = 43.21
    full_size = 4.0
    defensive_size = 2.0
    inventory_cap = 12.0

    account = H.account("MarketMakerAmendExample", tse.StorageRegime.Mem, tse.LogLevel.Off)
    flow_mkt = account.create_market("client flow", tse.MdType.Book)
    print_mkt = account.create_market("prints", tse.MdType.Trade)
    account.create_simulator("sim", H.simulator_options(), 64, -1)
    account.add_contract(SYMBOL, 1, tse.Instrument.Equity, tse.Underlying.Equity, tse.Venue.Undefined, 10000)

    flow = {
        "quantity_sum": 0.0,
        "trade_count": 0,
        "run_length": 0,
        "previous_ts": 0,
        "previous_side": int(tse.Side.Undefined),
        "hit_side": int(tse.Side.Undefined),
        "sweep_fired": False,
        "run_fired": False,
        "gap_fired": False,
    }
    quote = {
        "resting_client_order_id": 0,
        "next_client_order_id": 1,
        "cancel_count": 0,
        "replace_count": 0,
        "modify_count": 0,
    }

    # Every executed client trade is measured once, here: an outsized print, a third
    # trade in a row on one side and a long silence are the three things the maker reacts to.
    def client_flow(storage, contract_id, message):
        if message.kind != tse.BookMessageKind.Executed:
            return False
        average = 0.0 if flow["trade_count"] == 0 else flow["quantity_sum"] / flow["trade_count"]
        flow["sweep_fired"] = flow["trade_count"] != 0 and message.quantity > SWEEP_MULTIPLE * average
        flow["gap_fired"] = (
            flow["trade_count"] != 0
            and message.tsNanoseconds - flow["previous_ts"] > QUIET_GAP_NANOSECONDS
        )
        flow["run_length"] = flow["run_length"] + 1 if message.txnSide == flow["previous_side"] else 1
        flow["run_fired"] = flow["run_length"] == SAME_SIDE_RUN_LENGTH
        if flow["run_fired"]:
            flow["run_length"] = 0
        flow["hit_side"] = message.txnSide
        flow["previous_side"] = message.txnSide
        flow["previous_ts"] = message.tsNanoseconds
        flow["quantity_sum"] += message.quantity
        flow["trade_count"] += 1
        storage.push(message.tsNanoseconds, message.quantity)
        return True

    account.add_input_book("ClientFlow", 4, tse.Duration.Nanoseconds, client_flow, flow_mkt, [SYMBOL])

    # One quote lives at a time and the maker knows it by its client order id: a cancel drops
    # the id, a replace mints a fresh one, a modify keeps it. Zero means nothing rests.
    def quote_is_gone(input_label, ts_nanoseconds, value):
        if quote["resting_client_order_id"] != 0:
            return False
        quote["resting_client_order_id"] = quote["next_client_order_id"]
        quote["next_client_order_id"] += 1
        return True

    def sweep_hit(input_label, ts_nanoseconds, value):
        if (
            not flow["sweep_fired"]
            or flow["hit_side"] != QUOTE_SIDE
            or quote["resting_client_order_id"] == 0
        ):
            return False
        quote["resting_client_order_id"] = 0
        quote["cancel_count"] += 1
        return True

    def same_side_run(input_label, ts_nanoseconds, value):
        if not flow["run_fired"] or quote["resting_client_order_id"] == 0:
            return False
        quote["resting_client_order_id"] = quote["next_client_order_id"]
        quote["next_client_order_id"] += 1
        quote["replace_count"] += 1
        return True

    def quiet_gap(input_label, ts_nanoseconds, value):
        if not flow["gap_fired"] or quote["resting_client_order_id"] == 0:
            return False
        quote["modify_count"] += 1
        return True

    def inventory_full(input_label, ts_nanoseconds, value):
        state = account.get_position_state(SYMBOL)
        return state.side == int(tse.Side.Long) and state.quantity >= inventory_cap

    account.add_pattern_formula("QuoteIsGone", tse.Duration.Nanoseconds, ["ClientFlow"], quote_is_gone)
    account.add_pattern_formula("SweepHit", tse.Duration.Nanoseconds, ["ClientFlow"], sweep_hit)
    account.add_pattern_formula("SameSideRun", tse.Duration.Nanoseconds, ["ClientFlow"], same_side_run)
    account.add_pattern_formula("QuietGap", tse.Duration.Nanoseconds, ["ClientFlow"], quiet_gap)
    account.add_pattern_formula("InventoryFull", tse.Duration.Nanoseconds, ["ClientFlow"], inventory_full)

    # The three amend builders share one argument list but read different parts of it: cancel ignores
    # quantity and price, modify reads quantity only, replace reads both. Pull the quote when a sweep
    # hits its side, step it away and shrink it against a one-sided run, restore its size when quiet.
    account.add_rule_market(
        "RestQuote", tse.RuleType.Entry,
        tse.make_rule_params(
            tse.Quantity.Fixed, full_size,
            tse.Price.Limit, quote_price, 0.0, 0.0,
            QUOTE_SIDE, tse.Side.Neutral, tse.Tif.Day, 10,
        ),
        "QuoteIsGone", SYMBOL,
    )
    account.add_rule_cancel("PullQuote", SYMBOL, 0.0, 0.0, "SweepHit")
    account.add_rule_replace("StepAway", SYMBOL, defensive_size, step_away_price, "SameSideRun")
    account.add_rule_modify("BackToFullSize", SYMBOL, full_size, 0.0, "QuietGap")
    account.add_rule_market(
        "Unwind", tse.RuleType.Exit,
        tse.make_rule_params(
            tse.Quantity.All, 0.0,
            tse.Price.Market, 0.0, 0.0, 0.0,
            tse.Side.Short, tse.Side.Long, tse.Tif.Day, 10,
        ),
        "InventoryFull", SYMBOL,
    )

    account.add_robot("AmendingMaker", ["RestQuote", "PullQuote", "StepAway", "BackToFullSize", "Unwind"])
    account.portfolio_subscribe(print_mkt, SYMBOL)
    account.start("AmendingMaker")

    message_id = 0
    for ts_nanoseconds, side, price, quantity in load_client_flow():
        message_id += 1
        flow_mkt.push_book_by_name(
            SYMBOL,
            tse.make_book_message(
                tse.BookMessageKind.Executed, ts_nanoseconds,
                message_id, message_id,
                price, quantity, side,
            ),
        )
        print_mkt.push_trade_by_name(
            SYMBOL,
            tse.TseTickTrade(ts_nanoseconds + PRINT_LAG_NANOSECONDS, price, quantity, int(tse.Side.Trade)),
        )

    summary = account.get_summary()
    account.close()

    print("market maker amend: cancels={} replaces={} modifies={} netProfit={:.4f} trades={}".format(
        quote["cancel_count"], quote["replace_count"], quote["modify_count"],
        summary.totalNetProfit, summary.totalNumberOfTrades))
    return 0


if __name__ == "__main__":
    sys.exit(main())
