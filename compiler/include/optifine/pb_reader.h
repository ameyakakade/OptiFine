/* Minimal protobuf wire-format decoder -- just enough of the spec (varints,
 * length-delimited fields, fixed32) to walk ONNX's ModelProto/GraphProto/
 * NodeProto/AttributeProto/TensorProto messages. Not a general protobuf
 * library: no support for groups, maps, or anything ONNX doesn't use.
 * Wire format reference: https://protobuf.dev/programming-guides/encoding/ */
#ifndef OPTIFINE_PB_READER_H
#define OPTIFINE_PB_READER_H

#include <stddef.h>
#include <stdint.h>

#define PB_WIRE_VARINT 0
#define PB_WIRE_FIXED64 1
#define PB_WIRE_LEN 2
#define PB_WIRE_FIXED32 5

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;
} PbReader;

typedef struct {
    uint32_t field_number;
    uint32_t wire_type;
    uint64_t varint;       /* valid when wire_type == PB_WIRE_VARINT */
    const uint8_t *bytes;  /* valid when wire_type == PB_WIRE_LEN */
    size_t bytes_len;      /* valid when wire_type == PB_WIRE_LEN */
} PbField;

static inline void pb_reader_init(PbReader *r, const uint8_t *data, size_t len) {
    r->data = data;
    r->len = len;
    r->pos = 0;
}

static inline int pb_reader_done(const PbReader *r) {
    return r->pos >= r->len;
}

/* Reads one (tag, value) field at the reader's current position, advancing
 * past it. Returns 0 on success, non-zero on malformed input (truncated
 * varint, length-delimited field running past the buffer end, or an
 * unsupported wire type like groups). PB_WIRE_FIXED64 is consumed (8 bytes
 * skipped) but not decoded -- nothing in ONNX's subset we parse uses it. */
int pb_read_field(PbReader *r, PbField *out);

/* Decodes a PB_WIRE_FIXED32 field's 4 raw bytes as a little-endian IEEE754
 * float (protobuf's wire encoding for the `float` scalar type). */
float pb_decode_float32(const PbField *field);

/* Reads a length-delimited field's contents as a fresh PbReader, to walk an
 * embedded submessage (e.g. GraphProto inside ModelProto). */
static inline void pb_reader_from_field(PbReader *out, const PbField *field) {
    pb_reader_init(out, field->bytes, field->bytes_len);
}

#endif /* OPTIFINE_PB_READER_H */
