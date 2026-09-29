#include "tse_helpers.hpp"
#include "tse_selection.hpp"

#include <xgboost/c_api.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace {

	std::size_t thompsonColumn();

	bool sameFeatures
	(
		std::vector<float> const& oneThread,
		std::vector<float> const& allCores,
		std::size_t width,
		std::size_t thompson
	);

}//!namespace

int main()
{
	std::vector<tse::OhlcvTick> const ticks {helpers::loadAapl()};
	tse::setLogLevel(tse::LogLevel::none);

	// The five candidates of example 23, alike but for how far back the fast average looks, and the
	// same model choosing among them by their ex_post scores.
	std::vector<double> const params {10.0, 20.0, 50.0, 100.0, 150.0};
	std::size_t const
		candidateCount {params.size()},
		paramCount {tse::ExPost::paramCount()},
		thompson {thompsonColumn()};

	std::vector<std::size_t> bucketCounts(candidateCount, std::size_t {0});
	std::vector<std::size_t> finiteCounts(candidateCount, std::size_t {0});
	std::vector<char> sameAsOneThread(candidateCount, char {0});
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
			// The finished account is scored twice: once on a single thread and once on every core
			// of the machine. Every row of the two score matrices is compared bit for bit, except the
			// thompson_sampling column, which is random by design, so only where it is NaN must agree.
			tse::ExPost const
				oneThread {account.createExPost(tse::Duration::days, -1, 5, 1u)},
				allCores {account.createExPost(tse::Duration::days, -1, 5, helpers::hardwareThreads())};
			std::pair<std::vector<std::int64_t>, std::vector<float>> const
				single {oneThread.featureMomentum(0)},
				momentum {allCores.featureMomentum(0)};
			sameAsOneThread[idx] = single.first == momentum.first && sameFeatures(single.second, momentum.second, paramCount, thompson) ? char {1} : char {0};
			std::size_t const buckets {allCores.bucketCount(0)};
			bucketCounts[idx] = buckets;
			finiteCounts[idx] = helpers::countFinite(momentum.second);
			// The model reads the scores of the call on every core: the last day bucket says what shape
			// the candidate was in when the backtest ended.
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
			"SMA(%3d): buckets=%zu finiteScores=%zu netProfit=%.4f sameAsOneThread=%s\n",
			static_cast<int>(result.paramValue),
			bucketCounts[idx],
			finiteCounts[idx],
			result.summary.totalNetProfit,
			sameAsOneThread[idx] != char {0} ? "yes" : "no"
		);
	}

	// As in example 23, the model learns net profit on every candidate but the last one and then
	// ranks all five: the pick is the highest predicted profit.
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

namespace {

	std::size_t thompsonColumn()
	{
		std::size_t const paramCount {tse::ExPost::paramCount()};
		for (std::size_t p {0}; p != paramCount; ++p) {
			if (tse::ExPost::paramName(p) == "thompson_sampling") {
				return p;
			}
		}
		return paramCount;
	}

	bool sameFeatures
	(
		std::vector<float> const& oneThread,
		std::vector<float> const& allCores,
		std::size_t width,
		std::size_t thompson
	)
	{
		if (oneThread.size() != allCores.size()) {
			return false;
		}
		for (std::size_t k {0}; k != oneThread.size(); ++k) {
			bool const same
			{
				k % width == thompson ?
				std::isnan(oneThread[k]) == std::isnan(allCores[k]) :
				std::bit_cast<std::uint32_t>(oneThread[k]) == std::bit_cast<std::uint32_t>(allCores[k])
			};
			if (not same) {
				return false;
			}
		}
		return true;
	}

}//!namespace
