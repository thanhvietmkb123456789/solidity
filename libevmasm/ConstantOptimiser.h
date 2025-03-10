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
/** @file ConstantOptimiser.cpp
 * @author Christian <c@ethdev.com>
 * @date 2015
 */

#pragma once

#include <libevmasm/Exceptions.h>

#include <liblangutil/EVMVersion.h>

#include <libsolutil/Numeric.h>
#include <libsolutil/Assertions.h>

#include <vector>

namespace solidity::evmasm
{

class AssemblyItem;
using AssemblyItems = std::vector<AssemblyItem>;
class Assembly;

/**
 * Abstract base class for one way to change how constants are represented in the code.
 */
class ConstantOptimisationMethod
{
public:
	/// Tries to optimised how constants are represented in the source code and modifies
	/// @a _assembly.
	/// @returns zero if no optimisations could be performed.
	static unsigned optimiseConstants(
		bool _isCreation,
		size_t _runs,
		langutil::EVMVersion _evmVersion,
		Assembly& _assembly
	);

protected:
	/// This is the public API for the optimiser methods, but it doesn't need to be exposed to the caller.

	struct Params
	{
		bool isCreation; ///< Whether this is called during contract creation or runtime.
		size_t runs; ///< Estimated number of calls per opcode oven the lifetime of the contract.
		size_t multiplicity; ///< Number of times the constant appears in the code.
		langutil::EVMVersion evmVersion; ///< Version of the EVM
	};

	explicit ConstantOptimisationMethod(Params const& _params, u256 const& _value):
		m_params(_params), m_value(_value) {}
	virtual ~ConstantOptimisationMethod() = default;
	virtual bigint gasNeeded() const = 0;
	/// Executes the method, potentially appending to the assembly and returns a vector of
	/// assembly items the constant should be replaced with in one sweep.
	/// If the vector is empty, the constants will not be deleted.
	virtual AssemblyItems execute(Assembly& _assembly) const = 0;

protected:
	/// @returns the run gas for the given items ignoring special gas costs
	static bigint simpleRunGas(AssemblyItems const& _items, langutil::EVMVersion _evmVersion);
	/// @returns the gas needed to store the given data literally
	bigint dataGas(bytes const& _data) const;
	static size_t bytesRequired(AssemblyItems const& _items, langutil::EVMVersion _evmVersion);
	/// @returns the combined estimated gas usage taking @a m_params into account.
	bigint combineGas(
		bigint const& _runGas,
		bigint const& _repeatedDataGas,
		bigint const& _uniqueDataGas
	) const
	{
		// _runGas is not multiplied by _multiplicity because the runs are "per opcode"
		return m_params.runs * _runGas + m_params.multiplicity * _repeatedDataGas + _uniqueDataGas;
	}

	/// Replaces all constants i by the code given in @a _replacement[i].
	static void replaceConstants(AssemblyItems& _items, std::map<u256, AssemblyItems> const& _replacements);

	Params m_params;
	u256 const& m_value;
};

/**
 * Optimisation method that pushes the constant to the stack literally. This is the default method,
 * i.e. executing it does not alter the Assembly.
 */
class LiteralMethod: public ConstantOptimisationMethod
{
public:
	explicit LiteralMethod(Params const& _params, u256 const& _value):
		ConstantOptimisationMethod(_params, _value) {}
	bigint gasNeeded() const override;
	AssemblyItems execute(Assembly&) const override;
};

/**
 * Method that stores the data in the .data section of the code and copies it to the stack.
 */
class CodeCopyMethod: public ConstantOptimisationMethod
{
public:
	explicit CodeCopyMethod(Params const& _params, u256 const& _value):
		ConstantOptimisationMethod(_params, _value) {}
	bigint gasNeeded() const override;
	AssemblyItems execute(Assembly& _assembly) const override;

protected:
	AssemblyItems copyRoutine(AssemblyItem* _pushData = nullptr) const;
};

/**
 * Method that tries to compute the constant.
 */
class ComputeMethod: public ConstantOptimisationMethod
{
public:
	ComputeMethod(Params const& _params, u256 const& _value);
	~ComputeMethod() override;

	bigint gasNeeded() const override { return gasNeeded(m_routine); }
	AssemblyItems execute(Assembly&) const override;

protected:
	/// Tries to recursively find a way to compute @a _value.
	AssemblyItems findRepresentation(u256 const& _value);
	/// Recomputes the value from the calculated representation and checks for correctness.
	bool checkRepresentation(u256 const& _value, AssemblyItems const& _routine) const;
	bigint gasNeeded(AssemblyItems const& _routine) const;

	/// Counter for the complexity of optimization, will stop when it reaches zero.
	size_t m_maxSteps = 10000;
	AssemblyItems m_routine;
};

}
