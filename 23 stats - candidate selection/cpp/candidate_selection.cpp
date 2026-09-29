#include "tse_helpers.hpp"
#include "tse_selection.hpp"

#include <xgboost/c_api.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

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
			helpers::buildCandidate(account, shortPeriod, ticks);
			tse::ExPost const exPost {account.createExPost(tse::Duration::days, -1, 5, helpers::hardwareThreads())};
			std::size_t const buckets {exPost.bucketCount(0)};
			bucketCounts[idx] = buckets;
			std::pair<std::vector<std::int64_t>, std::vector<float>> const momentum {exPost.featureMomentum(0)};
			finiteCounts[idx] = helpers::countFinite(momentum.second);
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
	BoosterHandle const booster {helpers::trainModel(featureMatrix, labels, candidateCount - 1u, paramCount)};
	std::size_t const bestIndex {helpers::selectBest(booster, featureMatrix, candidateCount, paramCount)};
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
