add_subdirectory(ossfuzz)

add_subdirectory(yulInterpreter)
add_executable(yulrun yulrun.cpp)
target_link_libraries(yulrun PRIVATE yulInterpreter libsolc evmasm Boost::boost Boost::program_options)

add_executable(solfuzzer afl_fuzzer.cpp fuzzer_common.cpp)
target_link_libraries(solfuzzer PRIVATE libsolc evmasm Boost::boost Boost::program_options Boost::system)

add_executable(yulopti yulopti.cpp)
target_link_libraries(yulopti PRIVATE solidity Boost::boost Boost::program_options Boost::system)

add_executable(isoltest
	isoltest.cpp
	IsolTestOptions.cpp
	../Common.cpp
	../CommonSyntaxTest.cpp
	../EVMHost.cpp
	../TestCase.cpp
	../TestCaseReader.cpp
	../libsolidity/util/BytesUtils.cpp
	../libsolidity/util/Common.cpp
	../libsolidity/util/ContractABIUtils.cpp
	../libsolidity/util/TestFileParser.cpp
	../libsolidity/util/TestFunctionCall.cpp
	../libsolidity/GasTest.cpp
	../libsolidity/MemoryGuardTest.cpp
	../libsolidity/NatspecJSONTest.cpp
	../libsolidity/OptimizedIRCachingTest.cpp
	../libsolidity/OptimizedIRCachingTest.h
	../libsolidity/SyntaxTest.cpp
	../libsolidity/SemanticTest.cpp
	../libsolidity/AnalysisFramework.cpp
	../libsolidity/SolidityExecutionFramework.cpp
	../ExecutionFramework.cpp
	../libsolidity/ABIJsonTest.cpp
	../libsolidity/ASTJSONTest.cpp
	../libsolidity/ASTPropertyTest.cpp
	../libsolidity/FunctionDependencyGraphTest.cpp
	../libsolidity/SMTCheckerTest.cpp
	../libyul/Common.cpp
	../libyul/ControlFlowGraphTest.cpp
	../libyul/SSAControlFlowGraphTest.cpp
	../libyul/ControlFlowSideEffectsTest.cpp
	../libyul/EVMCodeTransformTest.cpp
	../libyul/FunctionSideEffects.cpp
	../libyul/ObjectCompilerTest.cpp
	../libyul/SyntaxTest.cpp
	../libyul/StackShufflingTest.cpp
	../libyul/StackLayoutGeneratorTest.cpp
	../libyul/YulOptimizerTest.cpp
	../libyul/YulOptimizerTestCommon.cpp
	../libyul/YulInterpreterTest.cpp
)
target_compile_definitions(isoltest PRIVATE ISOLTEST)
target_link_libraries(isoltest PRIVATE evmc libsolc solidity yulInterpreter evmasm Boost::boost Boost::program_options Boost::unit_test_framework Threads::Threads)
