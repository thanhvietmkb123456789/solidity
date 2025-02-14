// TODO: Recreate this test for EOF when subassembly deduplication will be supported for EOF too.
abstract contract S {
    function longdata() internal virtual pure returns (bytes memory);
}

abstract contract X is S {
    function longdata() internal override pure returns (bytes memory) {
        return
            "xasopca.pngaibngidak.jbtnudak.cAP.BRRSMCPJAGPD KIAJDOMHUKR,SCPID"
            "xasopca.pngaibngidak.jbtnudak.cAP.BRRSMCPJAGPD KIAJDOMHUKR,SCPID"
            "M,SEYBDXCNTKIMNJGO;DUIAQBQUEHAKMPGIDSAJCOUKANJBCUEBKNA.GIAKMV.TI"
            "AJMO<KXBANJCPGUD ABKCJIDHA NKIMAJU,EKAMHSO;PYCAKUM,L.UCA MR;KITA"
            "M,SEYBDXCNTKIMNJGO;DUIAQBQUEHAKMPGIDSAJCOUKANJBCUEBKNA.GIAKMV.TI"
            "AJMO<KXBANJCPGUD ABKCJIDHA NKIMAJU,EKAMHSO;PYCAKUM,L.UCA MR;KITA"
            " .RPOKIDAS,.CKUMT.,ORKAD ,NOKIDHA .CGKIAD OVHAMS CUAOGT DAKN OIT"
            "xasopca.pngaibngidak.jbtnudak.cAP.BRRSMCPJAGPD KIAJDOMHUKR,SCPID"
            "M,SEYBDXCNTKIMNJGO;DUIAQBQUEHAKMPGIDSAJCOUKANJBCUEBKNA.GIAKMV.TI"
            "AJMO<KXBANJCPGUD ABKCJIDHA NKIMAJU,EKAMHSO;PYCAKUM,L.UCA MR;KITA"
            "apibakrpidbacnidkacjadtnpdkylca.,jda,r.kuadc,jdlkjd',c'dj, ncg d"
            "anosumantkudkc,djntudkantuadnc,ui,c.ud,.nujdncud,j.rsch'pkl.'pih";
        }
}

contract C is X {
    bytes data;
    constructor() {
        data = longdata();
    }
    function test() public view returns (bool) {
        // Tests that the function longdata is not
        // included in the runtime code.
        uint x;
        assembly {
            x := codesize()
        }
        return x < data.length;
    }
}
// ====
// bytecodeFormat: legacy
// ----
// test() -> true
