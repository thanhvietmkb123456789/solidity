/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
// SPDX-License-Identifier: GPL-3.0

#include <test/libsolidity/GasTest.h>
#include <test/libsolidity/util/Common.h>
#include <test/Common.h>
#include <libsolutil/CommonIO.h>
#include <libsolutil/JSON.h>
#include <libsolutil/StringUtils.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/throw_exception.hpp>
#include <fstream>
#include <stdexcept>

using namespace solidity::langutil;
using namespace solidity::frontend;
using namespace solidity::frontend::test;
using namespace solidity;
using namespace boost::unit_test;

GasTest::GasTest(std::string const& _filename):
	EVMVersionRestrictedTestCase(_filename)
{
	m_source = m_reader.source();
	m_optimise = m_reader.boolSetting("optimize", false);
	m_optimiseYul = m_reader.boolSetting("optimize-yul", false);
	m_optimiseRuns = m_reader.sizetSetting("optimize-runs", OptimiserSettings{}.expectedExecutionsPerDeployment);
	parseExpectations(m_reader.stream());
}

void GasTest::parseExpectations(std::istream& _stream)
{
	std::map<std::string, std::string>* currentKind = nullptr;
	std::string line;

	while (getline(_stream, line))
		if (!boost::starts_with(line, "// "))
			BOOST_THROW_EXCEPTION(std::runtime_error("Invalid expectation: expected \"// \"."));
		else if (boost::ends_with(line, ":"))
		{
			std::string kind = line.substr(3, line.length() - 4);
			boost::trim(kind);
			currentKind = &m_expectations[std::move(kind)];
		}
		else if (!currentKind)
			BOOST_THROW_EXCEPTION(std::runtime_error("No function kind specified. Expected \"creation:\", \"external:\" or \"internal:\"."));
		else
		{
			auto it = line.begin() + 3;
			skipWhitespace(it, line.end());
			auto functionNameBegin = it;
			while (it != line.end() && *it != ':')
				++it;
			std::string functionName(functionNameBegin, it);
			if (functionName == "fallback")
				functionName.clear();
			expect(it, line.end(), ':');
			skipWhitespace(it, line.end());
			if (it == line.end())
				BOOST_THROW_EXCEPTION(std::runtime_error("Invalid expectation: expected gas cost."));
			(*currentKind)[functionName] = std::string(it, line.end());
		}
}

void GasTest::printUpdatedExpectations(std::ostream& _stream, std::string const& _linePrefix) const
{
	Json estimates = compiler().gasEstimates(compiler().lastContractName());
	for (auto& [key, group] : estimates.items())
	{
		_stream << _linePrefix << key << ":" << std::endl;
		for (auto& [elementKey, value] : group.items())
		{
			_stream << _linePrefix << "  ";
			if (elementKey.empty())
				_stream << "fallback";
			else
				_stream << elementKey;
			_stream << ": " << value.get<std::string>() << std::endl;
		}
	}
}

void GasTest::setupCompiler(CompilerStack& _compiler)
{
	AnalysisFramework::setupCompiler(_compiler);

	// Prerelease CBOR metadata varies in size due to changing version numbers and build dates.
	// This leads to volatile creation cost estimates. Therefore we force the compiler to
	// release mode for testing gas estimates.
	_compiler.setMetadataFormat(CompilerStack::MetadataFormat::NoMetadata);
	OptimiserSettings settings = m_optimise ? OptimiserSettings::standard() : OptimiserSettings::minimal();
	if (m_optimiseYul)
	{
		settings.runYulOptimiser = m_optimise;
		settings.optimizeStackAllocation = m_optimise;
	}
	settings.expectedExecutionsPerDeployment = m_optimiseRuns;
	_compiler.setOptimiserSettings(settings);
}

TestCase::TestResult GasTest::run(std::ostream& _stream, std::string const& _linePrefix, bool _formatted)
{
	if (!runFramework(withPreamble(m_source), PipelineStage::Compilation))
	{
		util::printPrefixed(_stream, formatErrors(filteredErrors(), _formatted), _linePrefix);
		return TestResult::FatalError;
	}

	Json estimateGroups = compiler().gasEstimates(compiler().lastContractName());
	if (
		m_expectations.size() == estimateGroups.size() &&
		boost::all(m_expectations, [&](auto const& expectations) {
		auto const& estimates = estimateGroups[expectations.first];
		return estimates.size() == expectations.second.size() &&
			boost::all(expectations.second, [&](auto const& entry) {
				return entry.second == estimates[entry.first].template get<std::string>();
			});
		})
	)
		return TestResult::Success;
	else
	{
		_stream << _linePrefix << "Expected:" << std::endl;
		for (auto const& expectations: m_expectations)
		{
			_stream << _linePrefix << "  " << expectations.first << ":" << std::endl;
			for (auto const& entry: expectations.second)
				_stream << _linePrefix
					<< "    "
					<< (entry.first.empty() ? "fallback" : entry.first)
					<< ": "
					<< entry.second
					<< std::endl;
		}
		_stream << _linePrefix << "Obtained:" << std::endl;
		printUpdatedExpectations(_stream, _linePrefix + "  ");
		return TestResult::Failure;
	}
}
