#include "tse_helpers.hpp"

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

int main()
{
	std::vector<tse::OhlcvTick> const ticks {tse::Account::loadOhlcvCsv(helpers::dataPath("WTI_OHLCVminute_sept2016.csv"))};
	tse::setLogLevel(tse::LogLevel::none);
	double const startingEquity {100000.0};

	tse::Account account {"StatsThreads", tse::StorageRegime::mem};
	tse::Market const market {account.createMarket("OHLCV", tse::MdType::ohlcv)};
	account.createSimulator("Sim", helpers::simulatorConfig());

	// Eight robots trade the crossover of example 22 and differ only in how far back the slow average
	// looks. Each robot trades a contract of its own, and every contract is fed the very same WTI bars:
	// the profit of a fill is booked against the exposure of its contract, so robots sharing one
	// contract would not be separate strategies.
	std::vector<int> const slowPeriods {10, 15, 20, 25, 30, 35, 40, 45};
	std::vector<std::string>
		contracts,
		robots;
	for (int const slow : slowPeriods) {
		std::string const
			period {std::to_string(slow)},
			contract {"WTI_" + period},
			fastInput {"Fast " + contract},
			slowInput {"Slow " + contract},
			toLong {"ToLong " + contract},
			toShort {"ToShort " + contract},
			entry {"Entry " + contract},
			exit {"Exit " + contract},
			robot {"SMA 3/" + period};
		account.addContract(contract, 1, tse::Instrument::future, tse::Underlying::commodity, tse::Venue::undefined, 100000);
		account.addInputOhlcv(fastInput, 3, tse::Duration::days, helpers::makeSma(3), market, {contract});
		account.addInputOhlcv(slowInput, slow, tse::Duration::days, helpers::makeSma(slow), market, {contract});
		account.addPatternCrossover(toLong, tse::Duration::days, {fastInput, slowInput}, tse::Cmp::ge);
		account.addPatternCrossover(toShort, tse::Duration::days, {fastInput, slowInput}, tse::Cmp::lt);
		account.addRuleMarket(entry, tse::RuleType::entry, helpers::entryParams(580.0), toLong, contract);
		account.addRuleMarket(exit, tse::RuleType::exit, helpers::exitParams(), toShort, contract);
		account.addRobot(robot, {entry, exit});
		contracts.push_back(contract);
		robots.push_back(robot);
	}
	account.setAccountEquity(startingEquity, 0.0);
	for (std::string const& robot : robots) {
		account.start(robot);
	}

	for (tse::OhlcvTick const& tick : ticks) {
		for (std::string const& contract : contracts) {
			market.pushOhlcv(contract, tick);
		}
	}

	// ex_post gets every core of the machine. The day buckets of different robots are built in
	// parallel, and at each day step the robots whose scoring window is complete are scored in
	// parallel. The scores do not depend on the number of threads: they are the same, bit for bit,
	// as with one thread, except thompson_sampling, which is random by design.
	tse::ExPost const exPost {account.createExPost(tse::Duration::days, -1, 5, helpers::hardwareThreads())};

	// The scoring runs once, when the object is created. Saving writes the buckets and the scores
	// the object already holds, and loading the database back shows the round trip.
	std::filesystem::path const dbPath {std::filesystem::temp_directory_path() / "tse_stats_threads_cpp.sqlite3.db"};
	helpers::cleanup(dbPath.string());
	exPost.save(dbPath.string());
	std::size_t const savedRobots {account.exPostLoad(dbPath.string())};
	helpers::cleanup(dbPath.string());

	for (std::size_t robot {0}; robot != exPost.robotCount(); ++robot) {
		std::string const label {exPost.robotLabel(robot)};
		tse::Summary const summary {account.getRobotSummary(label)};
		std::printf
		(
			"%s: trades=%lld netProfit=%.4f buckets=%zu\n",
			label.c_str(),
			static_cast<long long>(summary.totalNumberOfTrades),
			summary.totalNetProfit,
			exPost.bucketCount(robot)
		);
	}

	tse::Summary const summary {account.getSummary()};
	std::printf
	(
		"netProfit=%.4f trades=%lld robots=%zu params=%zu savedRobots=%zu\n",
		summary.totalNetProfit,
		static_cast<long long>(summary.totalNumberOfTrades),
		exPost.robotCount(),
		tse::ExPost::paramCount(),
		savedRobots
	);
	return 0;
}
