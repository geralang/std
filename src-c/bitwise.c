
#include "gera.h"

gint gera_std_bw_and(gint a, gint b) {
    return a & b;
}

gint gera_std_bw_or(gint a, gint b) {
    return a | b;
}

gint gera_std_bw_xor(gint a, gint b) {
    return a ^ b;
}

gint gera_std_bw_not(gint x) {
    return ~x;
}

gint gera_std_bw_rshift(gint x, gint bits);

gint gera_std_bw_lshift(gint x, gint bits) {
    if(bits < 0) return gera_std_bw_rshift(x, -bits);
    return x << bits;
}

gint gera_std_bw_rshift(gint x, gint bits) {
    if(bits < 0) return gera_std_bw_lshift(x, -bits);
    return x >> bits;
}

gint gera_std_bw_urshift(gint x, gint bits) {
    if(bits < 0) return gera_std_bw_lshift(x, -bits);
    uint64_t xu = (uint64_t) x;
    uint64_t ru = xu >> bits;
    return (gint) ru;
}