#include "tse_helpers.hpp"

#include <xgboost/c_api.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

namespace {

	std::size_t countFinite(std::vector<float> const& values);

	BoosterHandle trainModel
	(
		std::vector<float> const& featureMatrix,
		std::vector<float> const& labels,
		std::size_t rows,
		std::size_t width
	);

	std::size_t selectBest
	(
		BoosterHandle booster,
		std::vector<float> const& featureMatrix,
		std::size_t rows,
		std::size_t width
	);

}//!namespace

int main()
{
	std::vector<tse::OhlcvTick> const ticks {helpers::loadAapl()};
	tse::setLogLevel(tse::LogLevel::none);

	// Five candidate strategies, alike but for how far back the fast average looks. Which one to
	// trade next is decided by a model reading their ex_post scores, not by whoever earned most.
	std::vector<double> const params {10.0, 20.0, 50.0, 100.0, 150.0};
	std::size_t const
	candidateCount {params.size()},
	paramCount {tse::ExPost::paramCount()};

	std::vector<std::size_t> bucketCounts(candidateCount, std::size_t {0});
	std::vector<std::size_t> finiteCounts(candidateCount, std::size_t {0});
	std::vector<float> featureMatrix(candidateCount * paramCount, 0.0f);

	std::vector<tse::GridResult> const results {tse::runGrid
	(
		"AAPLSelect",
		tse::StorageRegime::mem,
		params,
		[&](tse::Account& account, double paramValue)
		{
			int const shortPeriod {static_cast<int>(paramValue)};
			std::size_t const idx {static_cast<std::size_t>(std::find(params.begin(), params.end(), paramValue) - params.begin())};
			tse::Market const market {account.createMarket("MD", tse::MdType::ohlcv)};
			account.createSimulator("Sim", helpers::simulatorConfig());
			account.addContract("AAPL", 1, tse::Instrument::equity, tse::Underlying::undefined, tse::Venue::undefined, 100000);
			account.addInputOhlcv("SMAShort", shortPeriod, tse::Duration::days, helpers::makeSma(shortPeriod), market, {"AAPL"});
			account.addInputOhlcv("SMA200", 200, tse::Duration::days, helpers::makeSma(200), market, {"AAPL"});
			account.addPatternCrossover("ToLong", tse::Duration::days, {"SMAShort", "SMA200"}, tse::Cmp::ge);
			account.addPatternCrossover("ToShort", tse::Duration::days, {"SMAShort", "SMA200"}, tse::Cmp::lt);
			account.addRuleMarket("Entry", tse::RuleType::entry, helpers::entryParams(100.0), "ToLong", "AAPL");
			account.addRuleMarket("Exit", tse::RuleType::exit, helpers::exitParams(), "ToShort", "AAPL");
			account.addRobot("Strat", {"Entry", "Exit"});
			account.setAccountEquity(100000.0, 0.0);
			account.start("Strat");
			for (tse::OhlcvTick const& tick : ticks) {
				market.pushOhlcv("AAPL", tick);
			}
			tse::ExPost const exPost {account.createExPost(tse::Duration::days, -1, 5)};
			std::size_t const buckets {exPost.bucketCount(0)};
			bucketCounts[idx] = buckets;
			std::pair<std::vector<std::int64_t>, std::vector<float>> const momentum {exPost.featureMomentum(0)};
			finiteCounts[idx] = countFinite(momentum.second);
			// Scores of the last day bucket say what shape the candidate was in when the backtest
			// ended, and that single row is everything the model gets to see about it.
			for (std::size_t p {0}; p != paramCount; ++p) {
				featureMatrix[idx * paramCount + p] = momentum.second[(buckets - 1u) * paramCount + p];
			}
		},
		tse::Currency::usd
	)};

	std::vector<float> labels(candidateCount, 0.0f);
	for (tse::GridResult const& result : results) {
		std::size_t const idx {static_cast<std::size_t>(std::find(params.begin(), params.end(), result.paramValue) - params.begin())};
		labels[idx] = static_cast<float>(result.summary.totalNetProfit);
		std::printf
		(
			"SMA(%3d): buckets=%zu finiteScores=%zu netProfit=%.4f\n",
			static_cast<int>(result.paramValue),
			bucketCounts[idx],
			finiteCounts[idx],
			result.summary.totalNetProfit
		);
	}

	// The model learns net profit from those scores on every candidate but the last one, which is
	// held back, and then ranks all five: the pick is the highest predicted profit.
	BoosterHandle const booster {trainModel(featureMatrix, labels, candidateCount - 1u, paramCount)};
	std::size_t const bestIndex {selectBest(booster, featureMatrix, candidateCount, paramCount)};
	XGBoosterFree(booster);

	std::printf
	(
		"candidate selection: candidates=%zu scoreParams=%zu model picks SMA(%d)\n",
		candidateCount,
		paramCount,
		static_cast<int>(params[bestIndex])
	);
	return 0;
}

namespace {

	std::size_t countFinite(std::vector<float> const& values)
	{
		std::size_t finite {0};
		for (float const value : values) {
			if (not std::isnan(static_cast<double>(value))) {
				++finite;
			}
		}
		return finite;
	}

	BoosterHandle trainModel
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

	std::size_t selectBest
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

}//!namespace
