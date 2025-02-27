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
/**
 * @file PeepholeOptimiser.cpp
 * Performs local optimising code changes to assembly.
 */

#include <libevmasm/PeepholeOptimiser.h>

#include <libevmasm/AssemblyItem.h>
#include <libevmasm/SemanticInformation.h>

using namespace solidity;
using namespace solidity::evmasm;

// TODO: Extend this to use the tools from ExpressionClasses.cpp

namespace
{

struct OptimiserState
{
	AssemblyItems const& items;
	size_t i;
	std::back_insert_iterator<AssemblyItems> out;
	langutil::EVMVersion evmVersion = langutil::EVMVersion();
};

template<typename FunctionType>
struct FunctionParameterCount;
template<typename R, typename... Args>
struct FunctionParameterCount<R(Args...)>
{
	static constexpr auto value = sizeof...(Args);
};

template <class Method>
struct SimplePeepholeOptimizerMethod
{
	template <size_t... Indices>
	static bool applyRule(
		AssemblyItems::const_iterator _in,
		std::back_insert_iterator<AssemblyItems> _out,
		std::index_sequence<Indices...>
	)
	{
		return Method::applySimple(_in[Indices]..., _out);
	}
	static bool apply(OptimiserState& _state)
	{
		static constexpr size_t WindowSize = FunctionParameterCount<decltype(Method::applySimple)>::value - 1;
		if (
			_state.i + WindowSize <= _state.items.size() &&
			applyRule(_state.items.begin() + static_cast<ptrdiff_t>(_state.i), _state.out, std::make_index_sequence<WindowSize>{})
		)
		{
			_state.i += WindowSize;
			return true;
		}
		else
			return false;
	}
};

struct Identity: SimplePeepholeOptimizerMethod<Identity>
{
	static bool applySimple(
		AssemblyItem const& _item,
		std::back_insert_iterator<AssemblyItems> _out
	)
	{
		*_out = _item;
		return true;
	}
};

struct PushPop: SimplePeepholeOptimizerMethod<PushPop>
{
	static bool applySimple(
		AssemblyItem const& _push,
		AssemblyItem const& _pop,
		std::back_insert_iterator<AssemblyItems>
	)
	{
		auto t = _push.type();
		return _pop == Instruction::POP && (
			SemanticInformation::isDupInstruction(_push) ||
			t == Push || t == PushTag || t == PushSub ||
			t == PushSubSize || t == PushProgramSize || t == PushData || t == PushLibraryAddress
		);
	}
};

struct OpPop: SimplePeepholeOptimizerMethod<OpPop>
{
	static bool applySimple(
		AssemblyItem const& _op,
		AssemblyItem const& _pop,
		std::back_insert_iterator<AssemblyItems> _out
	)
	{
		if (_pop == Instruction::POP && _op.type() == Operation)
		{
			Instruction instr = _op.instruction();
			if (instructionInfo(instr, langutil::EVMVersion()).ret == 1 && !instructionInfo(instr, langutil::EVMVersion()).sideEffects)
			{
				for (int j = 0; j < instructionInfo(instr, langutil::EVMVersion()).args; j++)
					*_out = {Instruction::POP, _op.debugData()};
				return true;
			}
		}
		return false;
	}
};

struct OpStop: SimplePeepholeOptimizerMethod<OpStop>
{
	static bool applySimple(
		AssemblyItem const& _op,
		AssemblyItem const& _stop,
		std::back_insert_iterator<AssemblyItems> _out
	)
	{
		if (_stop == Instruction::STOP)
		{
			if (_op.type() == Operation)
			{
				Instruction instr = _op.instruction();
				if (!instructionInfo(instr, langutil::EVMVersion()).sideEffects)
				{
					*_out = {Instruction::STOP, _op.debugData()};
					return true;
				}
			}
			else if (_op.type() == Push)
			{
				*_out = {Instruction::STOP, _op.debugData()};
				return true;
			}
		}
		return false;
	}
};

struct OpReturnRevert: SimplePeepholeOptimizerMethod<OpReturnRevert>
{
	static bool applySimple(
		AssemblyItem const& _op,
		AssemblyItem const& _push,
		AssemblyItem const& _pushOrDup,
		AssemblyItem const& _returnRevert,
		std::back_insert_iterator<AssemblyItems> _out
	)
	{
		if (
			(_returnRevert == Instruction::RETURN || _returnRevert == Instruction::REVERT) &&
			_push.type() == Push &&
			(_pushOrDup.type() == Push || _pushOrDup == dupInstruction(1))
		)
			if (
				(_op.type() == Operation && !instructionInfo(_op.instruction(), langutil::EVMVersion()).sideEffects) ||
				_op.type() == Push
			)
			{
					*_out = _push;
					*_out = _pushOrDup;
					*_out = _returnRevert;
					return true;
			}
		return false;
	}
};

struct DoubleSwap: SimplePeepholeOptimizerMethod<DoubleSwap>
{
	static size_t applySimple(
		AssemblyItem const& _s1,
		AssemblyItem const& _s2,
		std::back_insert_iterator<AssemblyItems>
	)
	{
		return _s1 == _s2 && SemanticInformation::isSwapInstruction(_s1);
	}
};

struct DoublePush
{
	static bool apply(OptimiserState& _state)
	{
		size_t windowSize = 2;
		if (_state.i + windowSize > _state.items.size())
			return false;

		auto push1 = _state.items.begin() + static_cast<ptrdiff_t>(_state.i);
		auto push2 = _state.items.begin() + static_cast<ptrdiff_t>(_state.i + 1);
		assertThrow(push1 != _state.items.end() && push2 != _state.items.end(), OptimizerException, "");

		if (
			push1->type() == Push &&
			push2->type() == Push &&
			push1->data() == push2->data() &&
			(!_state.evmVersion.hasPush0() || push1->data() != 0)
		)
		{
			*_state.out = *push1;
			*_state.out = {Instruction::DUP1, push2->debugData()};
			_state.i += windowSize;
			return true;
		}
		else
			return false;
	}
};

struct CommutativeSwap: SimplePeepholeOptimizerMethod<CommutativeSwap>
{
	static bool applySimple(
		AssemblyItem const& _swap,
		AssemblyItem const& _op,
		std::back_insert_iterator<AssemblyItems> _out
	)
	{
		// Remove SWAP1 if following instruction is commutative
		if (
			_swap == Instruction::SWAP1 &&
			SemanticInformation::isCommutativeOperation(_op)
		)
		{
			*_out = _op;
			return true;
		}
		else
			return false;
	}
};

struct SwapComparison: SimplePeepholeOptimizerMethod<SwapComparison>
{
	static bool applySimple(
		AssemblyItem const& _swap,
		AssemblyItem const& _op,
		std::back_insert_iterator<AssemblyItems> _out
	)
	{
		static std::map<Instruction, Instruction> const swappableOps{
			{ Instruction::LT, Instruction::GT },
			{ Instruction::GT, Instruction::LT },
			{ Instruction::SLT, Instruction::SGT },
			{ Instruction::SGT, Instruction::SLT }
		};

		if (
			_swap == Instruction::SWAP1 &&
			_op.type() == Operation &&
			swappableOps.count(_op.instruction())
		)
		{
			*_out = swappableOps.at(_op.instruction());
			return true;
		}
		else
			return false;
	}
};

/// Remove swapN after dupN
struct DupSwap: SimplePeepholeOptimizerMethod<DupSwap>
{
	static size_t applySimple(
		AssemblyItem const& _dupN,
		AssemblyItem const& _swapN,
		std::back_insert_iterator<AssemblyItems> _out
	)
	{
		if (
			SemanticInformation::isDupInstruction(_dupN) &&
			SemanticInformation::isSwapInstruction(_swapN) &&
			getDupNumber(_dupN.instruction()) == getSwapNumber(_swapN.instruction())
		)
		{
			*_out = _dupN;
			return true;
		}
		else
			return false;
	}
};


struct IsZeroIsZeroJumpI: SimplePeepholeOptimizerMethod<IsZeroIsZeroJumpI>
{
	static size_t applySimple(
		AssemblyItem const& _iszero1,
		AssemblyItem const& _iszero2,
		AssemblyItem const& _pushTag,
		AssemblyItem const& _jumpi,
		std::back_insert_iterator<AssemblyItems> _out
	)
	{
		if (
			_iszero1 == Instruction::ISZERO &&
			_iszero2 == Instruction::ISZERO &&
			_pushTag.type() == PushTag &&
			_jumpi == Instruction::JUMPI
		)
		{
			*_out = _pushTag;
			*_out = _jumpi;
			return true;
		}
		else
			return false;
	}
};

struct IsZeroIsZeroRJumpI: SimplePeepholeOptimizerMethod<IsZeroIsZeroRJumpI>
{
	static size_t applySimple(
		AssemblyItem const& _iszero1,
		AssemblyItem const& _iszero2,
		AssemblyItem const& _rjumpi,
		std::back_insert_iterator<AssemblyItems> _out
	)
	{
		if (
			_iszero1 == Instruction::ISZERO &&
			_iszero2 == Instruction::ISZERO &&
			_rjumpi.type() == ConditionalRelativeJump
		)
		{
			*_out = _rjumpi;
			return true;
		}
		else
			return false;
	}
};

struct EqIsZeroJumpI: SimplePeepholeOptimizerMethod<EqIsZeroJumpI>
{
	static size_t applySimple(
		AssemblyItem const& _eq,
		AssemblyItem const& _iszero,
		AssemblyItem const& _pushTag,
		AssemblyItem const& _jumpi,
		std::back_insert_iterator<AssemblyItems> _out
	)
	{
		if (
			_eq == Instruction::EQ &&
			_iszero == Instruction::ISZERO &&
			_pushTag.type() == PushTag &&
			_jumpi == Instruction::JUMPI
		)
		{
			*_out = AssemblyItem(Instruction::SUB, _eq.debugData());
			*_out = _pushTag;
			*_out = _jumpi;
			return true;
		}
		else
			return false;
	}
};

struct EqIsZeroRJumpI: SimplePeepholeOptimizerMethod<EqIsZeroRJumpI>
{
	static size_t applySimple(
		AssemblyItem const& _eq,
		AssemblyItem const& _iszero,
		AssemblyItem const& _rjumpi,
		std::back_insert_iterator<AssemblyItems> _out
	)
	{
		if (
			_eq == Instruction::EQ &&
			_iszero == Instruction::ISZERO &&
			_rjumpi.type() == ConditionalRelativeJump
		)
		{
			*_out = AssemblyItem(Instruction::SUB, _eq.debugData());
			*_out = _rjumpi;
			return true;
		}
		else
			return false;
	}
};

// push_tag_1 jumpi push_tag_2 jump tag_1: -> iszero push_tag_2 jumpi tag_1:
struct DoubleJump: SimplePeepholeOptimizerMethod<DoubleJump>
{
	static size_t applySimple(
		AssemblyItem const& _pushTag1,
		AssemblyItem const& _jumpi,
		AssemblyItem const& _pushTag2,
		AssemblyItem const& _jump,
		AssemblyItem const& _tag1,
		std::back_insert_iterator<AssemblyItems> _out
	)
	{
		if (
			_pushTag1.type() == PushTag &&
			_jumpi == Instruction::JUMPI &&
			_pushTag2.type() == PushTag &&
			_jump == Instruction::JUMP &&
			_tag1.type() == Tag &&
			_pushTag1.data() == _tag1.data()
		)
		{
			*_out = AssemblyItem(Instruction::ISZERO, _jumpi.debugData());
			*_out = _pushTag2;
			*_out = _jumpi;
			*_out = _tag1;
			return true;
		}
		else
			return false;
	}
};

// rjumpi(tag_1) rjump(tag_2) tag_1: -> iszero rjumpi(tag_2) tag_1:
struct DoubleRJump: SimplePeepholeOptimizerMethod<DoubleRJump>
{
	static size_t applySimple(
		AssemblyItem const& _rjumpi,
		AssemblyItem const& _rjump,
		AssemblyItem const& _tag1,
		std::back_insert_iterator<AssemblyItems> _out
	)
	{
		if (
			_rjumpi.type() == ConditionalRelativeJump &&
			_rjump.type() == RelativeJump &&
			_tag1.type() == Tag &&
			_rjumpi.data() == _tag1.data()
		)
		{
			*_out = AssemblyItem(Instruction::ISZERO, _rjumpi.debugData());
			*_out = AssemblyItem::conditionalRelativeJumpTo(_rjump.tag(), _rjump.debugData());
			*_out = _tag1;
			return true;
		}
		else
			return false;
	}
};

struct JumpToNext: SimplePeepholeOptimizerMethod<JumpToNext>
{
	static size_t applySimple(
		AssemblyItem const& _pushTag,
		AssemblyItem const& _jump,
		AssemblyItem const& _tag,
		std::back_insert_iterator<AssemblyItems> _out
	)
	{
		if (
			_pushTag.type() == PushTag &&
			(_jump == Instruction::JUMP || _jump == Instruction::JUMPI) &&
			_tag.type() == Tag &&
			_pushTag.data() == _tag.data()
		)
		{
			if (_jump == Instruction::JUMPI)
				*_out = AssemblyItem(Instruction::POP, _jump.debugData());
			*_out = _tag;
			return true;
		}
		else
			return false;
	}
};

struct RJumpToNext: SimplePeepholeOptimizerMethod<RJumpToNext>
{
	static size_t applySimple(
		AssemblyItem const& _rjump,
		AssemblyItem const& _tag,
		std::back_insert_iterator<AssemblyItems> _out
	)
	{
		if (
			(_rjump.type() == ConditionalRelativeJump || _rjump.type() == RelativeJump) &&
			_tag.type() == Tag &&
			_rjump.data() == _tag.data()
		)
		{
			if (_rjump.type() == ConditionalRelativeJump)
				*_out = AssemblyItem(Instruction::POP, _rjump.debugData());
			*_out = _tag;
			return true;
		}
		else
			return false;
	}
};

struct TagConjunctions: SimplePeepholeOptimizerMethod<TagConjunctions>
{
	static bool applySimple(
		AssemblyItem const& _pushTag,
		AssemblyItem const& _pushConstant,
		AssemblyItem const& _and,
		std::back_insert_iterator<AssemblyItems> _out
	)
	{
		if (_and != Instruction::AND)
			return false;
		if (
			_pushTag.type() == PushTag &&
			_pushConstant.type() == Push &&
			(_pushConstant.data() & u256(0xFFFFFFFF)) == u256(0xFFFFFFFF)
		)
		{
			*_out = _pushTag;
			return true;
		}
		else if (
			// tag and constant are swapped
			_pushConstant.type() == PushTag &&
			_pushTag.type() == Push &&
			(_pushTag.data() & u256(0xFFFFFFFF)) == u256(0xFFFFFFFF)
		)
		{
			*_out = _pushConstant;
			return true;
		}
		else
			return false;
	}
};

struct TruthyAnd: SimplePeepholeOptimizerMethod<TruthyAnd>
{
	static bool applySimple(
		AssemblyItem const& _push,
		AssemblyItem const& _not,
		AssemblyItem const& _and,
		std::back_insert_iterator<AssemblyItems>
	)
	{
		return (
			_push.type() == Push && _push.data() == 0 &&
			_not == Instruction::NOT &&
			_and == Instruction::AND
		);
	}
};

/// Removes everything after a JUMP (or similar) until the next JUMPDEST.
struct UnreachableCode
{
	static bool apply(OptimiserState& _state)
	{
		auto it = _state.items.begin() + static_cast<ptrdiff_t>(_state.i);
		auto end = _state.items.end();
		if (it == end)
			return false;
		if (
			it[0] != Instruction::JUMP &&
			it[0] != Instruction::RJUMP &&
			it[0] != Instruction::RETURN &&
			it[0] != Instruction::STOP &&
			it[0] != Instruction::INVALID &&
			it[0] != Instruction::SELFDESTRUCT &&
			it[0] != Instruction::REVERT
		)
			return false;

		ptrdiff_t i = 1;
		while (it + i != end && it[i].type() != Tag)
			i++;
		if (i > 1)
		{
			*_state.out = it[0];
			_state.i += static_cast<size_t>(i);
			return true;
		}
		else
			return false;
	}
};

struct DeduplicateNextTagSize3 : SimplePeepholeOptimizerMethod<DeduplicateNextTagSize3>
{
	static bool applySimple(
		AssemblyItem const& _precedingItem,
		AssemblyItem const& _itemA,
		AssemblyItem const& _itemB,
		AssemblyItem const& _breakingItem,
		AssemblyItem const& _tag,
		AssemblyItem const& _itemC,
		AssemblyItem const& _itemD,
		AssemblyItem const& _breakingItem2,
		std::back_insert_iterator<AssemblyItems> _out
	)
	{
		if (
			_precedingItem.type() != Tag &&
			_itemA == _itemC &&
			_itemB == _itemD &&
			_breakingItem == _breakingItem2 &&
			_tag.type() == Tag &&
			SemanticInformation::terminatesControlFlow(_breakingItem)
		)
		{
			*_out = _precedingItem;
			*_out = _tag;
			*_out = _itemC;
			*_out = _itemD;
			*_out = _breakingItem2;
			return true;
		}

		return false;
	}
};

struct DeduplicateNextTagSize2 : SimplePeepholeOptimizerMethod<DeduplicateNextTagSize2>
{
	static bool applySimple(
		AssemblyItem const& _precedingItem,
		AssemblyItem const& _itemA,
		AssemblyItem const& _breakingItem,
		AssemblyItem const& _tag,
		AssemblyItem const& _itemC,
		AssemblyItem const& _breakingItem2,
		std::back_insert_iterator<AssemblyItems> _out
	)
	{
		if (
			_precedingItem.type() != Tag &&
			_itemA == _itemC &&
			_breakingItem == _breakingItem2 &&
			_tag.type() == Tag &&
			SemanticInformation::terminatesControlFlow(_breakingItem)
		)
		{
			*_out = _precedingItem;
			*_out = _tag;
			*_out = _itemC;
			*_out = _breakingItem2;
			return true;
		}

		return false;
	}
};

struct DeduplicateNextTagSize1 : SimplePeepholeOptimizerMethod<DeduplicateNextTagSize1>
{
	static bool applySimple(
		AssemblyItem const& _precedingItem,
		AssemblyItem const& _breakingItem,
		AssemblyItem const& _tag,
		AssemblyItem const& _breakingItem2,
		std::back_insert_iterator<AssemblyItems> _out
	)
	{
		if (
			_precedingItem.type() != Tag &&
			_breakingItem == _breakingItem2 &&
			_tag.type() == Tag &&
			SemanticInformation::terminatesControlFlow(_breakingItem)
		)
		{
			*_out = _precedingItem;
			*_out = _tag;
			*_out = _breakingItem2;
			return true;
		}

		return false;
	}
};

void applyMethods(OptimiserState&)
{
	assertThrow(false, OptimizerException, "Peephole optimizer failed to apply identity.");
}

template <typename Method, typename... OtherMethods>
void applyMethods(OptimiserState& _state, Method, OtherMethods... _other)
{
	if (!Method::apply(_state))
		applyMethods(_state, _other...);
}

size_t numberOfPops(AssemblyItems const& _items)
{
	return static_cast<size_t>(std::count(_items.begin(), _items.end(), Instruction::POP));
}

}

PeepholeOptimiser::PeepholeOptimiser(AssemblyItems& _items, langutil::EVMVersion const _evmVersion):
	m_items(_items),
	m_evmVersion(_evmVersion)
{
}

bool PeepholeOptimiser::optimise()
{
	// Avoid referencing immutables too early by using approx. counting in bytesRequired()
	auto const approx = evmasm::Precision::Approximate;
	OptimiserState state {m_items, 0, back_inserter(m_optimisedItems), m_evmVersion};
	while (state.i < m_items.size())
		applyMethods(
			state,
			PushPop(),
			OpPop(),
			OpStop(),
			OpReturnRevert(),
			DoublePush(),
			DoubleSwap(),
			CommutativeSwap(),
			SwapComparison(),
			DupSwap(),
			IsZeroIsZeroJumpI(),
			IsZeroIsZeroRJumpI(), // EOF specific
			EqIsZeroJumpI(),
			EqIsZeroRJumpI(),     // EOF specific
			DoubleJump(),
			DoubleRJump(),        // EOF specific
			JumpToNext(),
			RJumpToNext(),        // EOF specific
			UnreachableCode(),
			DeduplicateNextTagSize3(),
			DeduplicateNextTagSize2(),
			DeduplicateNextTagSize1(),
			TagConjunctions(),
			TruthyAnd(),
			Identity()
		);
	if (m_optimisedItems.size() < m_items.size() || (
		m_optimisedItems.size() == m_items.size() && (
			evmasm::bytesRequired(m_optimisedItems, 3, m_evmVersion, approx) < evmasm::bytesRequired(m_items, 3, m_evmVersion, approx) ||
			numberOfPops(m_optimisedItems) > numberOfPops(m_items)
		)
	))
	{
		m_items = std::move(m_optimisedItems);
		return true;
	}
	else
		return false;
}
