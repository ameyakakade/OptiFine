#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "optifine/pb_reader.h"

static void test_varint_and_len(void) {
    uint8_t buf[] = {
        0x08, 0xAC, 0x02,     /* field 1, varint, value 300 */
        0x12, 0x02, 'h', 'i', /* field 2, len-delimited, "hi" */
    };
    PbReader r;
    pb_reader_init(&r, buf, sizeof(buf));

    PbField f1;
    assert(pb_read_field(&r, &f1) == 0);
    assert(f1.field_number == 1);
    assert(f1.wire_type == PB_WIRE_VARINT);
    assert(f1.varint == 300);

    PbField f2;
    assert(pb_read_field(&r, &f2) == 0);
    assert(f2.field_number == 2);
    assert(f2.wire_type == PB_WIRE_LEN);
    assert(f2.bytes_len == 2);
    assert(memcmp(f2.bytes, "hi", 2) == 0);

    assert(pb_reader_done(&r));
}

static void test_fixed32_float(void) {
    float expected = 3.140000104904175f;
    uint8_t buf[5];
    buf[0] = (uint8_t)((5 << 3) | PB_WIRE_FIXED32);
    memcpy(buf + 1, &expected, 4);

    PbReader r;
    pb_reader_init(&r, buf, sizeof(buf));
    PbField f;
    assert(pb_read_field(&r, &f) == 0);
    assert(f.field_number == 5);
    assert(f.wire_type == PB_WIRE_FIXED32);
    assert(pb_decode_float32(&f) == expected);
}

static void test_truncated_varint_fails(void) {
    uint8_t buf[] = { 0x08, 0xFF }; /* continuation bit set, buffer ends */
    PbReader r;
    pb_reader_init(&r, buf, sizeof(buf));
    PbField f;
    assert(pb_read_field(&r, &f) != 0);
}

static void test_len_field_past_end_fails(void) {
    uint8_t buf[] = { 0x12, 0x05, 'h', 'i' }; /* claims length 5, only 2 bytes follow */
    PbReader r;
    pb_reader_init(&r, buf, sizeof(buf));
    PbField f;
    assert(pb_read_field(&r, &f) != 0);
}

int main(void) {
    test_varint_and_len();
    test_fixed32_float();
    test_truncated_varint_fails();
    test_len_field_past_end_fails();
    printf("test_pb_reader: all tests passed\n");
    return 0;
}
