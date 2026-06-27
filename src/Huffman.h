// Huffman.h — the codec: build the tree, compress, decompress, and report stats.
#ifndef HUFFMAN_H
#define HUFFMAN_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Statistics about one compression run — the "compression statistics" feature.
struct Stats {
    uint64_t originalBytes   = 0;
    uint64_t compressedBytes = 0;   // header + packed bitstream
    uint64_t headerBytes     = 0;   // overhead of the stored frequency table
    uint64_t payloadBits     = 0;   // bits of actual encoded data
    int      distinctSymbols = 0;
    double   entropyBitsPerSym = 0.0;  // Shannon entropy: the theoretical floor
    double   avgCodeLenBits    = 0.0;  // what Huffman actually achieved

    double ratio() const {                       // original : compressed
        return compressedBytes ? static_cast<double>(originalBytes) / compressedBytes : 0.0;
    }
    double spaceSavingPct() const {              // % smaller
        return originalBytes ? (1.0 - static_cast<double>(compressedBytes) / originalBytes) * 100.0 : 0.0;
    }
};

// HuffmanCodec is stateless between calls; each method is self-contained.
//
// File format written by compress():
//   "HUF1"            4-byte magic
//   uint64  originalSize          (little-endian)
//   uint16  distinctSymbolCount
//   repeated: uint8 symbol, uint64 frequency      (the frequency table)
//   <packed Huffman bitstream>
// Storing the frequency table lets decompress() rebuild the *identical* tree
// (the build is deterministic), so no codes need to be stored explicitly.
class HuffmanCodec {
public:
    // Compress inputPath -> outputPath. Returns the run statistics.
    Stats compress(const std::string& inputPath, const std::string& outputPath) const;

    // Decompress a .huf file produced by compress().
    void decompress(const std::string& inputPath, const std::string& outputPath) const;

    // Analyse a file (frequencies, entropy, what Huffman would achieve) WITHOUT
    // writing anything — backs the `stats` command.
    Stats analyse(const std::string& inputPath) const;

    // Compress to a temp buffer, decompress, and assert byte-for-byte equality —
    // proves the codec is lossless. Returns the stats; throws on mismatch.
    Stats verifyRoundTrip(const std::string& inputPath) const;

private:
    // A tree node held in a flat pool (index-based) — no manual new/delete, no leaks.
    struct Node {
        int      symbol = -1;        // 0..255 for leaves, -1 for internal nodes
        uint64_t freq   = 0;
        int      left   = -1;
        int      right  = -1;
        bool isLeaf() const { return left == -1 && right == -1; }
    };

    // Build the Huffman tree from a 256-entry frequency table into `pool`;
    // returns the root index (or -1 if no symbols). Deterministic tie-breaking
    // guarantees compress and decompress build the same tree.
    static int buildTree(const std::array<uint64_t, 256>& freq, std::vector<Node>& pool);

    // Walk the tree assigning a bit-string code to every leaf (left=0, right=1).
    static void buildCodes(const std::vector<Node>& pool, int root,
                           std::array<std::vector<bool>, 256>& codes);
};

#endif // HUFFMAN_H
