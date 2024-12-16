{
	let x := callcode(gas(), 0x45, 0x5, 0, 0x20, 0x30, 0x20)
	sstore(100, x)
}
// ====
// bytecodeFormat: legacy
// ----
// Trace:
//   CALLCODE(153, 69, 5, 0, 32, 48, 32)
// Memory dump:
// Storage dump:
//   0000000000000000000000000000000000000000000000000000000000000064: 0000000000000000000000000000000000000000000000000000000000000001
// Transient storage dump:
