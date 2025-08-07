
typedef struct {
    const uint8_t *data;
    int size;
    int bit_pos;
} BitReader;

void init_bitreader(BitReader *br, const uint8_t *data, int size)
{
    br->data = data;
    br->size = size;
    br->bit_pos = 0;
}

int read_bit(BitReader *br)
{
    if (br->bit_pos >= br->size * 8) return -1;
    int byte_offset = br->bit_pos / 8;
    int bit_offset = 7 - (br->bit_pos % 8);
    br->bit_pos++;
    return (br->data[byte_offset] >> bit_offset) & 0x01;
}

uint32_t read_bits(BitReader *br, int n)
{
    uint32_t result = 0;
    for (int i = 0; i < n; i++) {
        int bit = read_bit(br);
        if (bit < 0) return 0xFFFFFFFF;
        result = (result << 1) | bit;
    }
    return result;
}

// Unsigned Exp-Golomb code
int read_ue(BitReader *br)
{
    int leadingZeroBits = -1;
    for (int b = 0; !b; leadingZeroBits++) {
        b = read_bit(br);
        if (b < 0) return -1;
    }
    if (leadingZeroBits > 31) return -1;

    int infoBits = read_bits(br, leadingZeroBits);
    return (1 << leadingZeroBits) - 1 + infoBits;
}
