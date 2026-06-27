#include "Huffman.h"
#include "BitIO.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <queue>
#include <stdexcept>
#include <tuple>

namespace {

constexpr char kMagic[4] = {'H', 'U', 'F', '1'};

// ---- little-endian fixed-width serialisation helpers ----------------------
void putU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
}
void putU64(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
uint16_t getU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}
uint64_t getU64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(p[i]) << (8 * i);
    return v;
}

std::vector<uint8_t> readWholeFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open input file: " + path);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
}

void writeWholeFile(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot open output file: " + path);
    if (!data.empty())
        out.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
}

std::array<uint64_t, 256> countFrequencies(const std::vector<uint8_t>& data) {
    std::array<uint64_t, 256> freq{};
    for (uint8_t b : data) ++freq[b];
    return freq;
}

} // namespace

// ---------------------------------------------------------------------------
// GREEDY tree construction. Repeatedly merge the two LOWEST-frequency nodes:
// the classic greedy choice that provably yields an optimal prefix code. A
// min-heap (priority_queue) gives O(k log k) for k distinct symbols.
//
// The heap key is (freq, sequence) — the `sequence` tie-breaker makes the build
// fully deterministic, so decompress() reconstructs an identical tree from the
// same frequency table without us having to store the codes.
// ---------------------------------------------------------------------------
int HuffmanCodec::buildTree(const std::array<uint64_t, 256>& freq,
                            std::vector<Node>& pool) {
    using Key = std::tuple<uint64_t, int, int>;   // (freq, sequence, poolIndex)
    std::priority_queue<Key, std::vector<Key>, std::greater<Key>> pq;

    pool.clear();
    pool.reserve(512);                            // <=256 leaves + <=255 internal

    // One leaf per symbol that actually occurs. Leaves seed the sequence counter
    // with their symbol value so ties break in a stable, reproducible order.
    for (int s = 0; s < 256; ++s) {
        if (freq[s] == 0) continue;
        const int idx = static_cast<int>(pool.size());
        pool.push_back(Node{s, freq[s], -1, -1});
        pq.push({freq[s], s, idx});
    }
    if (pq.empty()) return -1;                    // empty input
    if (pq.size() == 1) return std::get<2>(pq.top());  // single distinct symbol

    int sequence = 256;                           // internal nodes continue the ids
    while (pq.size() > 1) {
        const auto [fa, sa, ia] = pq.top(); pq.pop();
        const auto [fb, sb, ib] = pq.top(); pq.pop();
        const int idx = static_cast<int>(pool.size());
        pool.push_back(Node{-1, fa + fb, ia, ib}); // internal: merged subtree
        pq.push({fa + fb, sequence++, idx});
    }
    return std::get<2>(pq.top());
}

// Iterative DFS so deep trees can't overflow the call stack. Left edge = 0,
// right edge = 1. A single-symbol tree (root is a leaf) yields an EMPTY code:
// nothing is written and decompress() simply emits the symbol originalSize times.
void HuffmanCodec::buildCodes(const std::vector<Node>& pool, int root,
                              std::array<std::vector<bool>, 256>& codes) {
    if (root < 0) return;
    if (pool[root].isLeaf()) {                    // degenerate single-symbol file
        codes[pool[root].symbol] = {};            // empty code
        return;
    }
    std::vector<std::pair<int, std::vector<bool>>> stack;
    stack.push_back({root, {}});
    while (!stack.empty()) {
        auto [idx, path] = stack.back();
        stack.pop_back();
        const Node& n = pool[idx];
        if (n.isLeaf()) { codes[n.symbol] = path; continue; }
        auto lp = path; lp.push_back(false); stack.push_back({n.left,  lp});
        auto rp = path; rp.push_back(true);  stack.push_back({n.right, rp});
    }
}

// ---------------------------------------------------------------------------
Stats HuffmanCodec::compress(const std::string& inputPath,
                             const std::string& outputPath) const {
    const std::vector<uint8_t> data = readWholeFile(inputPath);
    const auto freq = countFrequencies(data);

    std::vector<Node> pool;
    const int root = buildTree(freq, pool);
    std::array<std::vector<bool>, 256> codes;
    buildCodes(pool, root, codes);

    // --- encode the payload ---
    BitWriter bw;
    for (uint8_t b : data) bw.putCode(codes[b]);
    bw.finish();

    // --- assemble the output: header (magic + size + frequency table) + payload ---
    std::vector<uint8_t> out;
    out.insert(out.end(), kMagic, kMagic + 4);
    putU64(out, data.size());

    int distinct = 0;
    for (int s = 0; s < 256; ++s) if (freq[s]) ++distinct;
    putU16(out, static_cast<uint16_t>(distinct));
    for (int s = 0; s < 256; ++s) {
        if (!freq[s]) continue;
        out.push_back(static_cast<uint8_t>(s));
        putU64(out, freq[s]);
    }
    const uint64_t headerBytes = out.size();
    out.insert(out.end(), bw.bytes().begin(), bw.bytes().end());

    writeWholeFile(outputPath, out);

    // --- statistics ---
    Stats st;
    st.originalBytes    = data.size();
    st.compressedBytes  = out.size();
    st.headerBytes      = headerBytes;
    st.payloadBits      = bw.totalBits();
    st.distinctSymbols  = distinct;
    if (!data.empty()) {
        double entropy = 0.0, avgLen = 0.0;
        for (int s = 0; s < 256; ++s) {
            if (!freq[s]) continue;
            const double p = static_cast<double>(freq[s]) / data.size();
            entropy -= p * std::log2(p);
            avgLen  += p * codes[s].size();
        }
        st.entropyBitsPerSym = entropy;
        st.avgCodeLenBits    = avgLen;
    }
    return st;
}

// ---------------------------------------------------------------------------
void HuffmanCodec::decompress(const std::string& inputPath,
                              const std::string& outputPath) const {
    const std::vector<uint8_t> in = readWholeFile(inputPath);
    if (in.size() < 14 || std::memcmp(in.data(), kMagic, 4) != 0)
        throw std::runtime_error("not a HUF1 file (bad magic): " + inputPath);

    size_t pos = 4;
    const uint64_t originalSize = getU64(&in[pos]); pos += 8;
    const uint16_t distinct     = getU16(&in[pos]); pos += 2;

    // Rebuild the exact frequency table, then the exact same tree.
    std::array<uint64_t, 256> freq{};
    for (int i = 0; i < distinct; ++i) {
        if (pos + 9 > in.size()) throw std::runtime_error("truncated header");
        const uint8_t sym = in[pos]; pos += 1;
        freq[sym] = getU64(&in[pos]); pos += 8;
    }

    std::vector<uint8_t> out;
    out.reserve(originalSize);

    if (originalSize == 0) { writeWholeFile(outputPath, out); return; }

    std::vector<Node> pool;
    const int root = buildTree(freq, pool);

    if (pool[root].isLeaf()) {
        // Single distinct symbol: no bits were written; just repeat it.
        out.assign(originalSize, static_cast<uint8_t>(pool[root].symbol));
        writeWholeFile(outputPath, out);
        return;
    }

    // Walk the tree bit by bit, emitting a symbol each time a leaf is reached.
    // originalSize is the stop condition, so trailing padding bits are ignored.
    BitReader br(in.data() + pos, in.size() - pos);
    int node = root;
    while (out.size() < originalSize) {
        const int bit = br.getBit();
        if (bit < 0) throw std::runtime_error("unexpected end of compressed stream");
        node = bit ? pool[node].right : pool[node].left;
        if (pool[node].isLeaf()) {
            out.push_back(static_cast<uint8_t>(pool[node].symbol));
            node = root;
        }
    }
    writeWholeFile(outputPath, out);
}

// ---------------------------------------------------------------------------
Stats HuffmanCodec::analyse(const std::string& inputPath) const {
    const std::vector<uint8_t> data = readWholeFile(inputPath);
    const auto freq = countFrequencies(data);

    std::vector<Node> pool;
    const int root = buildTree(freq, pool);
    std::array<std::vector<bool>, 256> codes;
    buildCodes(pool, root, codes);

    int distinct = 0;
    for (int s = 0; s < 256; ++s) if (freq[s]) ++distinct;

    Stats st;
    st.originalBytes   = data.size();
    st.distinctSymbols = distinct;
    uint64_t payloadBits = 0;
    if (!data.empty()) {
        double entropy = 0.0, avgLen = 0.0;
        for (int s = 0; s < 256; ++s) {
            if (!freq[s]) continue;
            const double p = static_cast<double>(freq[s]) / data.size();
            entropy -= p * std::log2(p);
            avgLen  += p * codes[s].size();
            payloadBits += freq[s] * codes[s].size();
        }
        st.entropyBitsPerSym = entropy;
        st.avgCodeLenBits    = avgLen;
    }
    // Predicted compressed size: 4 magic + 8 size + 2 count + 9 per symbol + payload.
    st.headerBytes     = 4 + 8 + 2 + static_cast<uint64_t>(distinct) * 9;
    st.payloadBits     = payloadBits;
    st.compressedBytes = st.headerBytes + (payloadBits + 7) / 8;
    return st;
}

// ---------------------------------------------------------------------------
Stats HuffmanCodec::verifyRoundTrip(const std::string& inputPath) const {
    const std::string tmpC = inputPath + ".huf.tmp";
    const std::string tmpD = inputPath + ".out.tmp";
    const Stats st = compress(inputPath, tmpC);
    decompress(tmpC, tmpD);

    const std::vector<uint8_t> original = readWholeFile(inputPath);
    const std::vector<uint8_t> restored = readWholeFile(tmpD);

    std::remove(tmpC.c_str());
    std::remove(tmpD.c_str());

    if (original != restored)
        throw std::runtime_error("ROUND-TRIP FAILED: restored data differs from original");
    return st;
}
