#include "zone_stream.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace bomap {

ZoneStream::ZoneStream(const std::vector<uint8_t>& data, size_t start) : mData(data), mFilePos(start) {}

size_t ZoneStream::Load(size_t size) {
    if (size == 0) {
        return mBlock == 1 ? kNoData : mFilePos;
    }
    if (mBlock == 1) {
        // Runtime block: reserved and zeroed by the game, not stored in the file.
        mCursor[1] += static_cast<uint32_t>(size);
        return kNoData;
    }
    if (mBlock == 2 || mBlock == 3) {
        throw std::runtime_error("data in a delayed block (" + std::to_string(mBlock) + "), which bomap doesn't read yet");
    }
    if (mFilePos + size > mData.size()) {
        throw std::runtime_error("zone ends early: need " + std::to_string(size) + " bytes at " +
                                 std::to_string(mFilePos));
    }
    size_t at = mFilePos;
    std::vector<Segment>& segs = mSegments[mBlock];
    if (!segs.empty() && segs.back().blockOffset + segs.back().size == mCursor[mBlock] &&
        segs.back().fileOffset + segs.back().size == at) {
        segs.back().size += static_cast<uint32_t>(size);
    } else {
        segs.push_back({ mCursor[mBlock], at, static_cast<uint32_t>(size) });
    }
    mCursor[mBlock] += static_cast<uint32_t>(size);
    mFilePos += size;
    return at;
}

std::string ZoneStream::LoadString(size_t* fileOffset) {
    size_t end = mFilePos;
    while (end < mData.size() && mData[end] != 0) {
        end++;
    }
    if (end >= mData.size()) {
        throw std::runtime_error("string runs off the end of the zone at " + std::to_string(mFilePos));
    }
    size_t len = end - mFilePos;
    size_t at = Load(len + 1);
    if (fileOffset != nullptr) {
        *fileOffset = at;
    }
    return at == kNoData ? std::string() : std::string(reinterpret_cast<const char*>(&mData[at]), len);
}

void ZoneStream::Align(uint32_t mask) {
    mCursor[mBlock] = (mCursor[mBlock] + mask) & ~mask;
}

void ZoneStream::Push(int block) {
    mStack.push_back({ mBlock, mCursor[block] });
    mBlock = block;
}

void ZoneStream::Pop() {
    if (mStack.empty()) {
        throw std::runtime_error("stream position stack underflow");
    }
    Saved saved = mStack.back();
    mStack.pop_back();
    if (mBlock == 0) {
        // Leaving the temp block gives its space back.
        mCursor[0] = saved.cursor;
        std::vector<Segment>& segs = mSegments[0];
        while (!segs.empty() && segs.back().blockOffset >= saved.cursor) {
            segs.pop_back();
        }
        if (!segs.empty() && segs.back().blockOffset + segs.back().size > saved.cursor) {
            segs.back().size = saved.cursor - segs.back().blockOffset;
        }
    }
    mBlock = saved.block;
}

uint32_t ZoneStream::InsertPointer() {
    Push(4);
    Align(3);
    uint32_t slot = mCursor[4];
    mCursor[4] += 4;
    Pop();
    return ((4u << 29) | slot) + 1;
}

size_t ZoneStream::Resolve(uint32_t token) const {
    if (token == 0 || token >= kPtrInsert) {
        return kNoData;
    }
    uint32_t block = (token - 1) >> 29;
    uint32_t offset = (token - 1) & 0x1FFFFFFF;
    if (block >= 7) {
        return kNoData;
    }
    const std::vector<Segment>& segs = mSegments[block];
    auto it = std::upper_bound(segs.begin(), segs.end(), offset,
                               [](uint32_t off, const Segment& s) { return off < s.blockOffset; });
    if (it == segs.begin()) {
        return kNoData;
    }
    --it;
    if (offset >= it->blockOffset + it->size) {
        return kNoData;
    }
    return it->fileOffset + (offset - it->blockOffset);
}

uint32_t ZoneStream::TokenFor(size_t fileOffset) const {
    if (fileOffset == kNoData) {
        return 0;
    }
    for (int block = 1; block < 7; block++) {
        const std::vector<Segment>& segs = mSegments[block];
        auto it = std::upper_bound(segs.begin(), segs.end(), fileOffset,
                                   [](size_t off, const Segment& s) { return off < s.fileOffset; });
        if (it == segs.begin()) {
            continue;
        }
        --it;
        if (fileOffset < it->fileOffset + it->size) {
            return ((static_cast<uint32_t>(block) << 29) | (it->blockOffset + static_cast<uint32_t>(fileOffset - it->fileOffset))) + 1;
        }
    }
    return 0;
}

uint8_t ZoneStream::U8(size_t at) const {
    return at < mData.size() ? mData[at] : 0;
}

uint16_t ZoneStream::U16(size_t at) const {
    if (at == kNoData || at + 2 > mData.size()) {
        return 0;
    }
    return static_cast<uint16_t>(mData[at] | mData[at + 1] << 8);
}

uint32_t ZoneStream::U32(size_t at) const {
    if (at == kNoData || at + 4 > mData.size()) {
        return 0;
    }
    return static_cast<uint32_t>(mData[at]) | static_cast<uint32_t>(mData[at + 1]) << 8 |
           static_cast<uint32_t>(mData[at + 2]) << 16 | static_cast<uint32_t>(mData[at + 3]) << 24;
}

float ZoneStream::F32(size_t at) const {
    uint32_t bits = U32(at);
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

std::string ZoneStream::CString(size_t at) const {
    if (at == kNoData || at >= mData.size()) {
        return std::string();
    }
    size_t end = at;
    while (end < mData.size() && mData[end] != 0) {
        end++;
    }
    return std::string(reinterpret_cast<const char*>(&mData[at]), end - at);
}

} // namespace bomap
