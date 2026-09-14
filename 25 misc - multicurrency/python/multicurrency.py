import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "helpers", "python"))
import tse_helpers as H

tse = H.tse

SYMBOL = "EURSTK"
ENTER_CHECKPOINT = 1000000000
EXIT_CHECKPOINT = 3000000000
HUGE_COOLDOWN = 1000000000000000
DRAIN_OFFSET = 100000000

ENTRY_QUANTITY = 10.0
ENTRY_PRICE = 100.0
EXIT_PRICE = 110.0


def price_processor(storage, contract_id, tick):
    storage.push(tick.tsNanoseconds, tick.price)
    return True


def push_price(market, ts_nanoseconds, price):
    market.push_trade_by_name(SYMBOL, tse.TseTickTrade(ts_nanoseconds, price, 1.0, int(tse.Side.Trade)))


def build_strategy(account, quantity):
    market = account.create_market("price", tse.MdType.Trade)
    account.create_simulator("sim", H.simulator_options(), 64, -1)
    account.add_contract(SYMBOL, 1, tse.Instrument.Equity, tse.Underlying.Undefined, tse.Venue.Undefined, 100000)
    account.add_input_trade("Px", 2, tse.Duration.Nanoseconds, price_processor, market, [SYMBOL])
    # Two fixed moments in time carry the whole example: the first opens the position, the second closes it, giving one clean round trip.
    account.add_pattern_timestamp("EnterAt", tse.Duration.Nanoseconds, ["Px"], ENTER_CHECKPOINT, HUGE_COOLDOWN)
    account.add_pattern_timestamp("ExitAt", tse.Duration.Nanoseconds, ["Px"], EXIT_CHECKPOINT, HUGE_COOLDOWN)
    account.add_rule_market("Enter", tse.RuleType.Entry, H.entry_params(quantity), "EnterAt", SYMBOL)
    account.add_rule_market("Exit", tse.RuleType.Exit, H.exit_params(), "ExitAt", SYMBOL)
    account.add_robot("EurRobot", ["Enter", "Exit"])
    account.portfolio_subscribe(market, SYMBOL)
    account.start("EurRobot")
    return market


def run_eur_backtest():
    # The euro account below exercises an interface that ships ahead of its cross-currency arithmetic: today the engine is validated on a single currency, US dollar.
    # The account is denominated in euro, ISO 4217 code 978, so every price, cash figure and profit below is read in euro.
    account = tse.Account("MulticurrencyEur", tse.StorageRegime.Mem, tse.Currency.Eur, -1, lib_path=H.LIB_PATH)
    account.set_log_level(tse.LogLevel.Off)
    market = build_strategy(account, ENTRY_QUANTITY)

    # The repeat push a fraction of a second later gives the simulator its chance to fill the order the first one triggered.
    push_price(market, ENTER_CHECKPOINT, ENTRY_PRICE)
    push_price(market, ENTER_CHECKPOINT + DRAIN_OFFSET, ENTRY_PRICE)

    push_price(market, EXIT_CHECKPOINT, EXIT_PRICE)
    push_price(market, EXIT_CHECKPOINT + DRAIN_OFFSET, EXIT_PRICE)

    net_profit = account.get_summary().totalNetProfit
    account.close()
    return net_profit


def run_eur_grid():
    params = [2.0, 5.0]

    def builder(account, param_value):
        market = build_strategy(account, param_value)
        push_price(market, ENTER_CHECKPOINT, ENTRY_PRICE)
        push_price(market, ENTER_CHECKPOINT + DRAIN_OFFSET, ENTRY_PRICE)
        push_price(market, EXIT_CHECKPOINT, EXIT_PRICE)
        push_price(market, EXIT_CHECKPOINT + DRAIN_OFFSET, EXIT_PRICE)

    return tse.run_grid("EurGrid", tse.StorageRegime.Mem, params, builder, tse.Currency.Eur, lib_path=H.LIB_PATH)


def main():
    net_profit = run_eur_backtest()
    results = run_eur_grid()

    print(
        "multicurrency: eurNetProfit={:.2f} gridNetProfit[{:.0f}]={:.2f} gridNetProfit[{:.0f}]={:.2f}".format(
            net_profit,
            results[0].param_value, results[0].summary.totalNetProfit,
            results[1].param_value, results[1].summary.totalNetProfit,
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
