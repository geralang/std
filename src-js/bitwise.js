
function gera_std_bw_and(a, b) {
    return a & b;
}

function gera_std_bw_or(a, b) {
    return a | b;
}

function gera_std_bw_xor(a, b) {
    return a ^ b;
}

function gera_std_bw_not(x) {
    return ~x;
}

function gera_std_bw_lshift(x, bits) {
    if(bits < 0) return gera_std_bw_rshift(x, -bits);
    return x << bits;
}

function gera_std_bw_rshift(x, bits) {
    if(bits < 0) return gera_std_bw_lshift(x, -bits);
    return x >> bits;
}

function gera_std_bw_urshift(x, bits) {
    if(bits < 0) return gera_std_bw_lshift(x, -bits);
    return x >>> bits;
}