#pragma once

#include "tse.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#ifndef TSE_DATA_DIR
	#define TSE_DATA_DIR "."
#endif

namespace helpers {

	inline std::string dataPath(std::string const& name)
	{
		return std::string(TSE_DATA_DIR) + "/" + name;
	}

	inline void cleanup(std::string const& dbPath)
	{
		std::remove(dbPath.c_str());
		std::remove((dbPath + "-wal").c_str());
		std::remove((dbPath + "-shm").c_str());
	}

	inline tse::SimulatorConfig simulatorConfig()
	{
		return tse::SimulatorConfig
		{
			100, 100,
			tse::OhlcvField::close, tse::OhlcvField::open,
			tse::BidAskField::bid, tse::BidAskField::ask
		};
	}

	inline tse::RuleParams entryParams(double quantity)
	{
		return tse::RuleParams
		{
			tse::QuantityMode::fixed, quantity,
			tse::PriceType::market, 0.0, 0.0, 0.0,
			tse::Side::long_, tse::Side::neutral, tse::Tif::day, 10
		};
	}

	inline tse::RuleParams exitParams()
	{
		return tse::RuleParams
		{
			tse::QuantityMode::all, 0.0,
			tse::PriceType::market, 0.0, 0.0, 0.0,
			tse::Side::short_, tse::Side::long_, tse::Tif::day, 10
		};
	}

	inline std::function<double(double)> makeRollingMean(int period)
	{
		return [period, window = std::vector<double>(period, 0.0), count = 0, rollingSum = 0.0]
		(double value) mutable -> double
		{
			int const idx {count % period};
			if (count >= period) {
				rollingSum += value - window[idx];
			} else {
				rollingSum += value;
			}
			window[idx] = value;
			count += 1;
			return rollingSum / static_cast<double>(period);
		};
	}

	inline tse::InputProcessor makeSma(int period)
	{
		return [period, mean = makeRollingMean(period)]
		(
			tse::Storage const& storage,
			std::string const&,
			tse::OhlcvTick const& tick
		) mutable -> bool
		{
			storage.push(tick.tsNanoseconds, mean(tick.close));
			return storage.size() >= static_cast<std::size_t>(period);
		};
	}

	inline tse::BidAskInputProcessor makeBidAskSma(int period)
	{
		return [period, mean = makeRollingMean(period)]
		(
			tse::Storage const& storage,
			std::string const&,
			tse::BidAskTick const& tick
		) mutable -> bool
		{
			storage.push(tick.tsNanoseconds, mean((tick.bid + tick.ask) / 2.0));
			return storage.size() >= static_cast<std::size_t>(period);
		};
	}

	inline std::vector<tse::OhlcvTick> loadAapl()
	{
		std::ifstream file {dataPath("aapl.csv")};
		std::vector<tse::OhlcvTick> out;
		std::string line;
		std::getline(file, line);
		while (std::getline(file, line)) {
			int y {0}, mo {0}, d {0};
			double open {0.0}, high {0.0}, low {0.0}, close {0.0}, volume {0.0};
			if (std::sscanf(line.c_str(), "%d-%d-%d;%lf;%lf;%lf;%lf;%lf", &y, &mo, &d, &open, &high, &low, &close, &volume) != 8) {
				continue;
			}
			std::chrono::sys_days const date {std::chrono::year {y} / std::chrono::month {static_cast<unsigned>(mo)} / std::chrono::day {static_cast<unsigned>(d)}};
			tse::OhlcvTick tick;
			tick.tsNanoseconds = std::chrono::nanoseconds {date.time_since_epoch()}.count();
			tick.open = open;
			tick.high = high;
			tick.low = low;
			tick.close = close;
			tick.volume = volume;
			out.push_back(tick);
		}
		return out;
	}

	inline std::vector<std::vector<double>> loadCsv(std::string const& name)
	{
		std::ifstream file {dataPath(name)};
		std::vector<std::vector<double>> out;
		std::string line;
		std::getline(file, line);
		while (std::getline(file, line)) {
			if (line.empty()) {
				continue;
			}
			std::vector<double> row;
			std::size_t start {0};
			while (start <= line.size()) {
				std::size_t const sep {line.find(';', start)};
				std::string const cell {line.substr(start, sep == std::string::npos ? std::string::npos : sep - start)};
				row.push_back(std::stod(cell));
				if (sep == std::string::npos) {
					break;
				}
				start = sep + 1;
			}
			out.push_back(row);
		}
		return out;
	}

	inline std::vector<tse::BidAskTick> loadBidask(std::string const& name)
	{
		std::vector<tse::BidAskTick> out;
		for (std::vector<double> const& row : loadCsv(name)) {
			tse::BidAskTick tick;
			tick.tsNanoseconds = static_cast<std::int64_t>(row[0]);
			tick.bid       = row[1];
			tick.bidVolume = row[2];
			tick.ask       = row[3];
			tick.askVolume = row[4];
			tick.last      = row[5];
			out.push_back(tick);
		}
		return out;
	}

	inline std::vector<tse::TradeTick> loadTrades(std::string const& name, tse::Side side)
	{
		std::vector<tse::TradeTick> out;
		for (std::vector<double> const& row : loadCsv(name)) {
			tse::TradeTick tick;
			tick.tsNanoseconds = static_cast<std::int64_t>(row[0]);
			tick.price  = row[1];
			tick.volume = row[2];
			tick.side   = side;
			out.push_back(tick);
		}
		return out;
	}

}
