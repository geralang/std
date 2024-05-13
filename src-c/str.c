
#include <stdlib.h>
#include <string.h>
#include <gera.h>

GeraString gera_std_str_from_codepoints(GeraArray cps) {
    unsigned char* b = malloc(sizeof(unsigned char) * cps.length * 4);
    size_t co = 0;
    for(size_t cpi = 0; cpi < cps.length; cpi += 1) {
        gera___begin_read(cps.allocation);
        gint scp = ((gint*) cps.allocation->data)[cpi];
        gera___end_read(cps.allocation);
        if(scp < 0) {
            gera___panic("Codepoint is negative!");
        }
        size_t cp = scp;
        if(cp <= 0x007F) {
            b[co] = cp;
            co += 1;
        } else if(cp <= 0x07FF) {
            b[co] = 0xC0 | ((cp >> 6) & 0x1F);
            co += 1;
            b[co] = 0x80 | (cp & 0x3F);
            co += 1;
        } else if(cp <= 0xFFFF) {
            b[co] = 0xE0 | ((cp >> 12) & 0x0F);
            co += 1;
            b[co] = 0x80 | ((cp >> 6) & 0x3F);
            co += 1;
            b[co] = 0x80 | (cp & 0x3F);
            co += 1;
        } else if(cp <= 0x10FFFF) {
            b[co] = 0xF0 | ((cp >> 18) & 0x07);
            co += 1;
            b[co] = 0x80 | ((cp >> 12) & 0x3F);
            co += 1;
            b[co] = 0x80 | ((cp >> 6) & 0x3F);
            co += 1;
            b[co] = 0x80 | (cp & 0x3F);
            co += 1;
        } else {
            gera___panic("Codepoint is too large!");
        }
    }
    GeraAllocation* a = gera___alloc(sizeof(unsigned char) * co, NULL);
    memcpy(a->data, b, co);
    size_t length = 0;
    for(size_t o = 0; o < co; length += 1) {
        o += gera___codepoint_size(b[o]);
    }
    free(b);
    gera___ref_deleted(cps.allocation);
    return (GeraString) {
        .allocation = a,
        .data = a->data,
        .length = length,
        .length_bytes = co
    };
}

gint gera_std_str_as_codepoint(GeraString src) {
    const char* d = src.data;
    if((d[0] & 0x80) == 0x00) {
        return d[0] & 0x7F;
    }
    size_t s = gera___codepoint_size(d[0]);
    size_t offset = 0;
    gint result = 0;
    char fb_mask;
    if(s >= 4) {
        if(s == 4) { fb_mask = 0x07; }
        result |= (d[3] & 0x3F) << offset;
        offset += 6;
    }
    if(s >= 3) {
        if(s == 3) { fb_mask = 0x0F; }
        result |= (d[2] & 0x3F) << offset;
        offset += 6;
    }
    if(s >= 2) {
        if(s == 2) { fb_mask = 0x1F; }
        result |= (d[1] & 0x3F) << offset;
        offset += 6;
    }
    if(s <= 1) {
        gera___panic("invalid character!");
    }
    result |= (d[0] & fb_mask) << offset;
    gera___ref_deleted(src.allocation);
    return result;
}

