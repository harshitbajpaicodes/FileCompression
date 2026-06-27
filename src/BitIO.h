// BitIO.h — bit-level writer/reader over byte buffers.
//
// Huffman codes are variable-length bit strings, but files are byte-addressed.
// These two helpers are the bridge: BitWriter packs individual bits MSB-first
// into bytes (padding the final byte), and BitReader unpacks them in the same
// order. Keeping bit packing in one place is what makes the codec correct and
// the rest of the code readable.
#ifndef BITIO_H
#define BITIO_H

#include <cstdint>
#include <vector>

class BitWriter {
public:
    // Append a single bit (0/1), MSB-first within each byte.
    void putBit(int bit) {
        current_ = static_cast<uint8_t>((current_ << 1) | (bit & 1));
        ++nbits_;
        ++totalBits_;
        if (nbits_ == 8) { bytes_.push_back(current_); current_ = 0; nbits_ = 0; }
    }

    // Append a code (sequence of bits, MSB-first).
    void putCode(const std::vector<bool>& code) {
        for (bool b : code) putBit(b ? 1 : 0);
    }

    // Flush the partially-filled final byte, padding the low bits with zeros.
    // (Decoding stops at the known symbol count, so padding is never misread.)
    void finish() {
        if (nbits_ > 0) {
            current_ = static_cast<uint8_t>(current_ << (8 - nbits_));
            bytes_.push_back(current_);
            current_ = 0;
            nbits_ = 0;
        }
    }

    const std::vector<uint8_t>& bytes() const { return bytes_; }
    uint64_t totalBits() const { return totalBits_; }

private:
    std::vector<uint8_t> bytes_;
    uint8_t  current_   = 0;   // bits accumulated for the byte in progress
    int      nbits_     = 0;   // how many bits are in `current_`
    uint64_t totalBits_ = 0;   // total bits written (for statistics)
};

class BitReader {
public:
    BitReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    // Return the next bit (0/1), or -1 when the buffer is exhausted.
    int getBit() {
        if (bytePos_ >= size_) return -1;
        const int bit = (data_[bytePos_] >> (7 - bitPos_)) & 1;   // MSB-first
        if (++bitPos_ == 8) { bitPos_ = 0; ++bytePos_; }
        return bit;
    }

private:
    const uint8_t* data_;
    size_t size_;
    size_t bytePos_ = 0;
    int    bitPos_  = 0;
};

#endif // BITIO_H
