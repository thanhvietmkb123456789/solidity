{
	let x := staticcall(gas(), 0x45, 0, 0x20, 0x30, 0x20)
	sstore(0x64, x)
}
// ====
// EVMVersion: >=byzantium
// bytecodeFormat: legacy
// ----
// Trace:
//   STATICCALL(153, 69, 0, 32, 48, 32)
// Memory dump:
// Storage dump:
//   0000000000000000000000000000000000000000000000000000000000000064: 0000000000000000000000000000000000000000000000000000000000000001
// Transient storage dump:
