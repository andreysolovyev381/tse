#include "tse_helpers.hpp"

#include <xgboost/c_api.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

	BoosterHandle trainModel();
	double predictOne(BoosterHandle booster, double mid, double spread, double imbalance);
	tse::BidAskInputProcessor useGbm(std::shared_ptr<void> model);

}

int main()
{
	std::vector<tse::BidAskTick> const ticks {helpers::loadBidask("btc_bidask.csv")};
	tse::setLogLevel(tse::LogLevel::none);

	// The gradient boosted model is trained here and then lives inside the data processor:
	// the engine never inspects it, it only reads the number the processor pushes.
	std::shared_ptr<void> const model {trainModel(), [](void* handle) { XGBoosterFree(handle); }};

	tse::Account account {"BTC/USDT", tse::StorageRegime::mem};
	tse::Market const market {account.createMarket("MD", tse::MdType::bidask)};
	account.createSimulator("Sim", helpers::simulatorConfig());
	account.addContract("BTC", 1, tse::Instrument::future, tse::Underlying::crypto, tse::Venue::undefined, 10000);
	account.addInputBidAsk("GBM", 1, tse::Duration::minutes, useGbm(model), market, {"BTC"});
	// A positive forecast means the model expects the price to rise: the robot stays long
	// while the forecast is positive and closes the position once it turns negative.
	account.addPatternThreshold("ToLong", tse::Duration::minutes, {"GBM"}, tse::Cmp::ge, 0.0);
	account.addPatternThreshold("ToShort", tse::Duration::minutes, {"GBM"}, tse::Cmp::lt, 0.0);
	account.addRuleMarket("Entry", tse::RuleType::entry, helpers::entryParams(1.0), "ToLong", "BTC");
	account.addRuleMarket("Exit", tse::RuleType::exit, helpers::exitParams(), "ToShort", "BTC");
	account.addRobot("Strat", {"Entry", "Exit"});
	account.start("Strat");

	for (tse::BidAskTick const& tick : ticks) {
		market.pushBidAsk("BTC", tick);
	}

	tse::Summary const summary {account.getSummary()};
	long long const trades {static_cast<long long>(summary.totalNumberOfTrades)};
	std::printf("netProfit=%.4f trades=%lld\n", summary.totalNetProfit, trades);
	return 0;
}

namespace {

	BoosterHandle trainModel()
	{
		std::vector<float>
			features(256 * 3, 0.0f),
			labels(256, 0.0f);
		for (std::size_t i {0}; i < 256; ++i) {
			float const
				mid       {44000.0f + static_cast<float>(i % 40) * 50.0f},
				spread    {4.0f + static_cast<float>(i % 15)},
				imbalance {-1.0f + 2.0f * static_cast<float>(i % 21) / 20.0f};
			features[i * 3 + 0] = mid;
			features[i * 3 + 1] = spread;
			features[i * 3 + 2] = imbalance;
			labels[i] = imbalance;
		}
		DMatrixHandle dtrain {nullptr};
		XGDMatrixCreateFromMat(features.data(), 256, 3, std::nanf(""), &dtrain);
		XGDMatrixSetFloatInfo(dtrain, "label", labels.data(), 256);

		BoosterHandle booster {nullptr};
		XGBoosterCreate(&dtrain, 1, &booster);
		XGBoosterSetParam(booster, "objective", "reg:squarederror");
		XGBoosterSetParam(booster, "max_depth", "3");
		XGBoosterSetParam(booster, "eta", "0.3");
		for (int round {0}; round < 20; ++round) {
			XGBoosterUpdateOneIter(booster, round, dtrain);
		}
		XGDMatrixFree(dtrain);
		return booster;
	}

	double predictOne
	(
		BoosterHandle booster,
		double mid,
		double spread,
		double imbalance
	)
	{
		float const features[3] {static_cast<float>(mid), static_cast<float>(spread), static_cast<float>(imbalance)};
		DMatrixHandle dmatrix {nullptr};
		XGDMatrixCreateFromMat(features, 1, 3, std::nanf(""), &dmatrix);
		bst_ulong outLen {0};
		float const* outResult {nullptr};
		XGBoosterPredict(booster, dmatrix, 0, 0, 0, &outLen, &outResult);
		double const prediction {outLen > 0 ? static_cast<double>(outResult[0]) : 0.0};
		XGDMatrixFree(dmatrix);
		return prediction;
	}

	tse::BidAskInputProcessor useGbm(std::shared_ptr<void> model)
	{
		return [model]
		(
			tse::Storage const& storage,
			std::string const&,
			tse::BidAskTick const& tick
		) -> bool
		{
			// Book imbalance carries the signal the model was trained on: more volume resting
			// on the bid than on the ask is buying pressure.
			double const
				mid {(tick.bid + tick.ask) / 2.0},
				spread {tick.ask - tick.bid},
				denom {tick.bidVolume + tick.askVolume},
				imbalance {denom != 0.0 ? (tick.bidVolume - tick.askVolume) / denom : 0.0};
			storage.push(tick.tsNanoseconds, predictOne(model.get(), mid, spread, imbalance));
			return storage.size() >= 1;
		};
	}

}
