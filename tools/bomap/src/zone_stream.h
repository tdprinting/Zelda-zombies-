#pragma once

// The memory model a Black Ops zone is written against.
//
// The inflated zone is one flat byte stream, but the game copies it into seven
// memory blocks as it reads: the temp block (0), runtime blocks that are zeroed
// instead of read (1, and 2/3 which are read after everything else), and the
// blocks that hold the loaded assets (4, 5, 6). Each block has its own write
// cursor, and structures are aligned in their block, not in the file, so the
// file itself has no padding. A pointer written in the file is a token:
//   0           null
//   0xFFFFFFFF  the data follows in the stream, at the current block's cursor
//   0xFFFFFFFE  same, and a pointer slot is also reserved in block 4 so later
//               assets can refer to this one (an "alias")
//   otherwise   ((block << 29) | offset) + 1, data loaded earlier
// ZoneStream tracks the cursors and remembers where each block range came from
// in the file, so earlier data can be found again from a token.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bomap {

constexpr uint32_t kPtrInline = 0xFFFFFFFF;
constexpr uint32_t kPtrInsert = 0xFFFFFFFE;
constexpr size_t kNoData = SIZE_MAX; // a block range that isn't in the file (zeroed at load)

class ZoneStream {
public:
    // `start` is the file offset where streamed data begins (after the asset list header).
    ZoneStream(const std::vector<uint8_t>& data, size_t start);

    // Reads `size` bytes at the current block's cursor. Returns the file offset of the
    // data, or kNoData when the block isn't read from the file.
    size_t Load(size_t size);
    // Reads a null-terminated string (no alignment) and returns it.
    std::string LoadString(size_t* fileOffset = nullptr);
    // Aligns the current block's cursor: `mask` is alignment - 1.
    void Align(uint32_t mask);
    void Push(int block);
    void Pop();
    // Reserves the block-4 pointer slot of an aliased asset and returns the token
    // later data uses to refer to it.
    uint32_t InsertPointer();

    // File offset of the data a token points at, or kNoData.
    size_t Resolve(uint32_t token) const;
    // The token for data loaded from `fileOffset` (the reverse of Resolve), or 0 when
    // that data lives in the temp block and can't be referred to later.
    uint32_t TokenFor(size_t fileOffset) const;

    size_t FilePos() const { return mFilePos; }
    int Block() const { return mBlock; }
    uint32_t Cursor(int block) const { return mCursor[block]; }
    const std::vector<uint8_t>& Data() const { return mData; }

    uint8_t U8(size_t at) const;
    uint16_t U16(size_t at) const;
    uint32_t U32(size_t at) const;
    int32_t I32(size_t at) const { return static_cast<int32_t>(U32(at)); }
    float F32(size_t at) const;
    std::string CString(size_t at) const;

private:
    struct Segment {
        uint32_t blockOffset;
        size_t fileOffset;
        uint32_t size;
    };
    struct Saved {
        int block;
        uint32_t cursor;
    };

    const std::vector<uint8_t>& mData;
    size_t mFilePos;
    int mBlock = 0;
    uint32_t mCursor[7] = {};
    std::vector<Segment> mSegments[7];
    std::vector<Saved> mStack;
};

} // namespace bomap
