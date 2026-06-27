# Huffman File Compression Tool

A lossless file compressor/decompressor built on **Huffman coding**, in modular
C++17. It works on **any** file — text *or* binary (images, executables, …) — and
reports detailed compression statistics. Written as a Data Structures & Algorithms
project: the design is justified in DSA terms throughout.

> Verified lossless on text, an empty file, a 1-byte file, a 5000-identical-byte
> file, and on its own compiled `huff.exe` (a binary using all 256 byte values) —
> every round-trip is byte-for-byte identical (SHA-256 checked).

---

## 1. Features

* **Compress / decompress** arbitrary files (binary-safe, not just ASCII).
* **Self-contained archive**: the frequency table is stored in the file header, so
  decompression needs nothing but the `.huf` file.
* **Compression statistics**: original vs compressed size, header overhead, ratio,
  % space saved, Shannon **entropy** (the theoretical floor) vs the **average code
  length** Huffman actually achieved.
* **`verify`** command: compresses, decompresses, and asserts the result equals the
  original — a built-in proof of losslessness.
* Handles every edge case cleanly: empty file, a single distinct byte (degenerate
  tree), and incompressible input (reported honestly, never corrupted).

---

## 2. The three DSA skills, and where they live

| Skill | Where | What it does |
|-------|-------|--------------|
| **Trees** | `Huffman::buildTree` / `buildCodes` / decode walk | The Huffman tree *is* the code. Encoding reads root→leaf paths; decoding walks the tree bit by bit. |
| **Greedy algorithms** | `Huffman::buildTree` | Repeatedly merge the two lowest-frequency nodes — the greedy choice that provably produces an **optimal prefix code**. |
| **File handling** | `BitIO.h`, `readWholeFile`/`writeWholeFile` | Binary-safe I/O plus **bit-level** packing/unpacking (files are byte-addressed; Huffman codes are not). |

---

## 3. Build & run

```sh
# g++ (MinGW/MSYS2, Linux, macOS)
g++ -std=c++17 -O2 -o huff src/main.cpp src/Huffman.cpp
# or CMake
cmake -S . -B build && cmake --build build
```
```sh
./huff compress   report.pdf  report.huf
./huff decompress report.huf  report.copy.pdf
./huff stats      report.pdf          # analyse only, write nothing
./huff verify     report.pdf          # round-trip lossless test + stats
```

---

## 4. How it works

### Compression
1. **Count frequencies** of all 256 possible byte values (one pass).
2. **Build the Huffman tree** with a greedy min-heap: push one leaf per present
   byte, then repeatedly pop the two least-frequent nodes and push their merged
   parent until one root remains.
3. **Assign codes** by a DFS of the tree (left = 0, right = 1). Frequent bytes end
   up near the root → short codes; rare bytes → long codes. The prefix-free
   property (no code is a prefix of another) is automatic because data only lives
   at leaves.
4. **Encode** the file by concatenating each byte's bit-code, packed MSB-first into
   bytes by `BitWriter`.
5. **Write** `magic + originalSize + frequencyTable + bitstream`.

### Decompression
Read the header, **rebuild the identical tree** from the stored frequency table
(the build is deterministic — see below), then walk the tree one bit at a time,
emitting a byte each time a leaf is reached, until `originalSize` bytes are
produced. Using the known original size as the stop condition means the zero-padding
in the final byte is simply never read — no ambiguity.

### File format
```
"HUF1"                       4-byte magic
uint64  originalSize         little-endian
uint16  distinctSymbolCount
repeat:  uint8 symbol, uint64 frequency      (the frequency table)
<packed Huffman bitstream>
```

### Why store frequencies instead of the codes?
Because the tree build is **deterministic** — the min-heap key is
`(frequency, sequence-id)`, so ties always break the same way — the decoder
rebuilds the *exact same tree* from the same frequencies. That keeps the format
simple and the codec symmetric. (A production format like DEFLATE instead stores
length-limited *canonical* code lengths; noted as a possible upgrade in §7.)

### Edge cases handled
* **Empty file** → header only, decompresses to empty.
* **One distinct byte** (e.g. 5000×`'A'`) → the tree is a single leaf, so the code
  is empty and *no payload bits are written*; the decoder just repeats the symbol
  `originalSize` times (217:1 ratio in testing).
* **Incompressible / tiny file** → may not shrink (header overhead); reported
  honestly rather than silently bloating without warning.

---

## 5. Complexity & space analysis

Let **n** = file size in bytes, **k** = number of distinct byte values (k ≤ 256).

| Step | Time | Space |
|------|------|-------|
| Frequency count | O(n) | O(k) — a fixed 256-entry table |
| Build tree (min-heap) | O(k log k) | O(k) tree nodes (≤ 2k−1) |
| Assign codes (DFS) | O(k · L) | O(k) codes, L = max code length |
| Encode | O(n) bit ops | O(n) output |
| Decode | O(n) tree walks | O(n) output |
| **Overall** | **O(n + k log k) ≈ O(n)** | **O(n)** |

Since k is bounded by 256, the `k log k` tree build is effectively constant, so the
codec is **linear in file size** — it scales to large files. The priority queue is
the heart of the greedy step: each pop/push is O(log k), and there are O(k) of them.

**Optimality.** Huffman yields the minimum-redundancy prefix code. Its average code
length **L** satisfies **H ≤ L < H + 1**, where **H** is the Shannon entropy. The
`stats`/`verify` output prints both numbers so you can see the gap is always under
one bit — exactly the theoretical guarantee. (In the text test: entropy 4.397 vs
Huffman 4.467 bits/symbol.)

---

## 6. Why these data-structure choices (defense notes)

* **Min-heap (`std::priority_queue`)** — the greedy algorithm needs the two
  smallest frequencies repeatedly; a binary heap gives O(log k) extract-min, the
  standard optimal structure for this access pattern.
* **Tree held in a flat `vector<Node>` pool, index-based** — no `new`/`delete`, so
  no memory leaks and good cache locality; the whole tree is freed at once.
* **Deterministic tie-breaking** — lets the decoder reconstruct the tree from
  frequencies alone, avoiding storing explicit codes.
* **Codes as `vector<bool>`** — handles arbitrarily deep trees safely (a code can
  in theory be up to k−1 bits; a fixed-width integer could overflow).
* **Bit packing isolated in `BitIO.h`** — the one tricky part (bits vs bytes,
  MSB-first, final-byte padding) lives in one tested place.
* **Stop decoding at `originalSize`** — sidesteps the classic "how many padding
  bits are in the last byte?" bug entirely.

---

## 7. Honest limitations & possible upgrades

* The stored frequency table costs up to ~2.3 KB of header; for very small files
  that can exceed the savings (shown honestly in stats). *Upgrade:* store
  **canonical** code lengths (length-limited to 15 bits) instead — a few hundred
  bytes, the DEFLATE approach.
* Single-pass **static** Huffman. *Upgrades:* adaptive Huffman (no header), or
  arithmetic/range coding to beat the sub-1-bit-per-symbol redundancy, or an
  LZ77 front end (dictionary matching) before Huffman — which is exactly what
  gzip/DEFLATE do — to exploit repeated *strings*, not just byte frequencies.
* Whole file is loaded into memory. *Upgrade:* stream in fixed-size blocks for
  files larger than RAM.

---

## 8. Layout
```
src/
  BitIO.h        bit-level writer/reader (file-handling core)
  Huffman.h      codec interface + Stats struct
  Huffman.cpp    tree build (greedy), encode, decode, statistics, verify
  main.cpp       CLI: compress / decompress / stats / verify
sample/sample.txt   demo input
CMakeLists.txt
```
