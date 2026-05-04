#include "optifine/pb_reader.h"

#include <string.h>

static int pb_read_varint(PbReader *r, uint64_t *out) {
    uint64_t result = 0;
    int shift = 0;
    while (1) {
        if (r->pos >= r->len) {
            return -1; /* truncated */
        }
        uint8_t byte = r->data[r->pos++];
        result |= (uint64_t)(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            break;
        }
        shift += 7;
        if (shift >= 64) {
            return -1; /* malformed: varint longer than 64 bits */
        }
    }
    *out = result;
    return 0;
}

int pb_read_field(PbReader *r, PbField *out) {
    uint64_t tag;
    if (pb_read_varint(r, &tag) != 0) {
        return -1;
    }
    out->field_number = (uint32_t)(tag >> 3);
    out->wire_type = (uint32_t)(tag & 0x7);

    switch (out->wire_type) {
        case PB_WIRE_VARINT: {
            uint64_t v;
            if (pb_read_varint(r, &v) != 0) {
                return -1;
            }
            out->varint = v;
            return 0;
        }
        case PB_WIRE_LEN: {
            uint64_t length;
            if (pb_read_varint(r, &length) != 0) {
                return -1;
            }
            if (length > r->len - r->pos) {
                return -1; /* runs past the buffer end */
            }
            out->bytes = r->data + r->pos;
            out->bytes_len = (size_t)length;
            r->pos += (size_t)length;
            return 0;
        }
        case PB_WIRE_FIXED32: {
            if (r->len - r->pos < 4) {
                return -1;
            }
            out->bytes = r->data + r->pos;
            out->bytes_len = 4;
            r->pos += 4;
            return 0;
        }
        case PB_WIRE_FIXED64: {
            if (r->len - r->pos < 8) {
                return -1;
            }
            r->pos += 8; /* consumed, not decoded -- unused in our subset */
            return 0;
        }
        default:
            return -1; /* groups (wire types 3/4) are unsupported */
    }
}

float pb_decode_float32(const PbField *field) {
    float value;
    /* Protobuf's fixed32 wire encoding is little-endian; this assumes a
     * little-endian host, true for every realistic build machine for this
     * tool (x86/x64/ARM default configuration). This decoder runs on the
     * build host, not the AVR target it compiles for. */
    memcpy(&value, field->bytes, sizeof(value));
    return value;
}
