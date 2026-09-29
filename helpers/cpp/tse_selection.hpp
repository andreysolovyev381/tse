#pragma once

#include "tse_helpers.hpp"

#include <xgboost/c_api.h>

#include <cmath>
#include <cstddef>
#include <vector>

namespace helpers {

	inline void buildCandidate
	(
		tse::Account& account,
		int shortPeriod,
		std::vector<tse::OhlcvTick> const& ticks
	)
	{
		tse::Market const market {account.createMarket("MD", tse::MdType::ohlcv)};
		account.createSimulator("Sim", simulatorConfig());
		account.addContract("AAPL", 1, tse::Instrument::equity, tse::Underlying::undefined, tse::Venue::undefined, 100000);
		account.addInputOhlcv("SMAShort", shortPeriod, tse::Duration::days, makeSma(shortPeriod), market, {"AAPL"});
		account.addInputOhlcv("SMA200", 200, tse::Duration::days, makeSma(200), market, {"AAPL"});
		account.addPatternCrossover("ToLong", tse::Duration::days, {"SMAShort", "SMA200"}, tse::Cmp::ge);
		account.addPatternCrossover("ToShort", tse::Duration::days, {"SMAShort", "SMA200"}, tse::Cmp::lt);
		account.addRuleMarket("Entry", tse::RuleType::entry, entryParams(100.0), "ToLong", "AAPL");
		account.addRuleMarket("Exit", tse::RuleType::exit, exitParams(), "ToShort", "AAPL");
		account.addRobot("Strat", {"Entry", "Exit"});
		account.setAccountEquity(100000.0, 0.0);
		account.start("Strat");
		for (tse::OhlcvTick const& tick : ticks) {
			market.pushOhlcv("AAPL", tick);
		}
	}

	inline std::size_t countFinite(std::vector<float> const& values)
	{
		std::size_t finite {0};
		for (float const value : values) {
			if (not std::isnan(static_cast<double>(value))) {
				++finite;
			}
		}
		return finite;
	}

	inline BoosterHandle trainModel
	(
		std::vector<float> const& featureMatrix,
		std::vector<float> const& labels,
		std::size_t rows,
		std::size_t width
	)
	{
		DMatrixHandle dTrain {nullptr};
		XGDMatrixCreateFromMat(featureMatrix.data(), rows, width, NAN, &dTrain);
		XGDMatrixSetFloatInfo(dTrain, "label", labels.data(), rows);

		BoosterHandle booster {nullptr};
		XGBoosterCreate(&dTrain, 1, &booster);
		XGBoosterSetParam(booster, "objective", "reg:squarederror");
		XGBoosterSetParam(booster, "max_depth", "2");
		XGBoosterSetParam(booster, "eta", "0.3");
		for (int round {0}; round != 4; ++round) {
			XGBoosterUpdateOneIter(booster, round, dTrain);
		}

		XGDMatrixFree(dTrain);
		return booster;
	}

	inline std::size_t selectBest
	(
		BoosterHandle booster,
		std::vector<float> const& featureMatrix,
		std::size_t rows,
		std::size_t width
	)
	{
		DMatrixHandle dAll {nullptr};
		XGDMatrixCreateFromMat(featureMatrix.data(), rows, width, NAN, &dAll);
		bst_ulong outLength {0};
		float const* predicted {nullptr};
		XGBoosterPredict(booster, dAll, 0, 0, 0, &outLength, &predicted);
		std::size_t bestIndex {0};
		for (std::size_t i {1}; i != outLength; ++i) {
			if (predicted[i] > predicted[bestIndex]) {
				bestIndex = i;
			}
		}
		XGDMatrixFree(dAll);
		return bestIndex;
	}

}
