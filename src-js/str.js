
function gera_std_str_from_codepoints(cps) {
    return String.fromCodePoint(...cps.map(Number))
}

function gera_std_str_as_codepoint(src) {
    return src.codePointAt(0);
}


