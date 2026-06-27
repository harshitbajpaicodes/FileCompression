// main.cpp — command-line front end for the Huffman codec.
//
//   huff compress   <input>      <output.huf>
//   huff decompress <input.huf>  <output>
//   huff stats      <input>          analyse only, write nothing
//   huff verify     <input>          lossless round-trip test + stats
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <string>

#include "Huffman.h"

namespace {

void printStats(const Stats& s, const char* title) {
    std::cout << "\n--- " << title << " ---\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "  original size      : " << s.originalBytes   << " bytes\n";
    std::cout << "  compressed size    : " << s.compressedBytes << " bytes"
              << "  (header " << s.headerBytes << " B + payload "
              << (s.payloadBits + 7) / 8 << " B)\n";
    std::cout << "  distinct symbols   : " << s.distinctSymbols << " / 256\n";
    std::cout << "  entropy (theory)   : " << s.entropyBitsPerSym << " bits/symbol\n";
    std::cout << "  Huffman avg length : " << s.avgCodeLenBits   << " bits/symbol"
              << "   (within 1 bit of entropy — optimal prefix code)\n";
    std::cout << "  compression ratio  : " << s.ratio() << " : 1\n";
    std::cout << "  space saving       : " << std::setprecision(2)
              << s.spaceSavingPct() << " %\n";
    if (s.originalBytes > 0 && s.compressedBytes >= s.originalBytes)
        std::cout << "  note: file did not shrink — input is near-incompressible\n"
                     "        and/or so small the header dominates.\n";
}

int usage() {
    std::cout <<
        "Huffman file compressor\n"
        "Usage:\n"
        "  huff compress   <input>      <output.huf>\n"
        "  huff decompress <input.huf>  <output>\n"
        "  huff stats      <input>          (analyse only)\n"
        "  huff verify     <input>          (lossless round-trip test)\n";
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    const std::string cmd = argv[1];
    HuffmanCodec codec;

    try {
        if (cmd == "compress" && argc == 4) {
            const Stats s = codec.compress(argv[2], argv[3]);
            std::cout << "Compressed '" << argv[2] << "' -> '" << argv[3] << "'\n";
            printStats(s, "compression statistics");
        } else if (cmd == "decompress" && argc == 4) {
            codec.decompress(argv[2], argv[3]);
            std::cout << "Decompressed '" << argv[2] << "' -> '" << argv[3] << "'\n";
        } else if (cmd == "stats" && argc == 3) {
            const Stats s = codec.analyse(argv[2]);
            printStats(s, "analysis (predicted)");
        } else if (cmd == "verify" && argc == 3) {
            const Stats s = codec.verifyRoundTrip(argv[2]);
            std::cout << "ROUND-TRIP OK: decompressed output is byte-for-byte "
                         "identical to the original.\n";
            printStats(s, "compression statistics");
        } else {
            return usage();
        }
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
