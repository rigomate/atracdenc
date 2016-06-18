#include "atrac1_bitalloc.h"
#include "atrac_psy_common.h"
#include "atrac_scale.h"
#include "atrac1.h"
#include <math.h>
#include <cassert>
#include "../bitstream/bitstream.h"

namespace NAtracDEnc {
namespace NAtrac1 {

using std::vector;
using std::cerr;
using std::endl;
using std::pair;

static const uint32_t FixedBitAllocTableLong[TAtrac1BitStreamWriter::MaxBfus] = {
    7, 7, 7, 6, 6, 6, 6, 6, 6, 6, 6, 5, 5, 5, 5, 5, 5, 5, 5, 6,
    6, 6, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 4,
    4, 4, 3, 3, 3, 3, 3, 3, 2, 2, 2, 1, 1, 0, 0, 0
};

static const uint32_t FixedBitAllocTableShort[TAtrac1BitStreamWriter::MaxBfus] = {
    6, 6, 6, 6,  6, 6, 6, 6,  6, 6, 6, 6,  6, 6, 6, 6,  6, 6, 6, 6,
    6, 6, 6, 6,  5, 5, 5, 5,  5, 5, 5, 5,  5, 5, 5, 5,
    4, 4, 4, 4, 4, 4, 4, 4,   0, 0, 0, 0, 0, 0, 0, 0
};

static const uint32_t BitBoostMask[TAtrac1BitStreamWriter::MaxBfus] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
    1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1,
    1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

TBitsBooster::TBitsBooster() {
    for (uint32_t i = 0; i < MaxBfus; ++i) {
        if (BitBoostMask[i] == 0)
            continue;
        const uint32_t nBits = SpecsPerBlock[i];
        BitsBoostMap.insert(pair<uint32_t, uint32_t>(nBits, i));
    }
    MaxBitsPerIteration = BitsBoostMap.size() ? (--BitsBoostMap.end())->first : 0;
    MinKey = BitsBoostMap.begin()->first;
}

uint32_t TBitsBooster::ApplyBoost(std::vector<uint32_t>* bitsPerEachBlock, uint32_t cur, uint32_t target) {
    uint32_t surplus = target - cur;
    uint32_t key = (surplus > MaxBitsPerIteration) ? MaxBitsPerIteration : surplus;
    std::multimap<uint32_t, uint32_t>::iterator maxIt = BitsBoostMap.upper_bound(key);
    //the key too low
    if (maxIt == BitsBoostMap.begin())
        return surplus;

    while (surplus >= MinKey) {
        bool done = true;
        for (std::multimap<uint32_t, uint32_t>::iterator it = BitsBoostMap.begin(); it != maxIt; ++it) {
            const uint32_t curBits = it->first;
            const uint32_t curPos = it->second;

            assert(key >= curBits);
            if (curPos >= bitsPerEachBlock->size())
                break;
            if ((*bitsPerEachBlock)[curPos] == 16u)
                continue;
            const uint32_t nBitsPerSpec = (*bitsPerEachBlock)[curPos] ? 1 : 2;
            if ((*bitsPerEachBlock)[curPos] == 0u && curBits * 2 > surplus)
                continue;
            if (curBits * nBitsPerSpec > surplus)
                continue;
            (*bitsPerEachBlock)[curPos] += nBitsPerSpec;
            surplus -= curBits * nBitsPerSpec;

            done = false;
        }
        if (done)
            break;
    }

    return surplus;
}


vector<uint32_t> TAtrac1SimpleBitAlloc::CalcBitsAllocation(const std::vector<TScaledBlock>& scaledBlocks,
                                                           const uint32_t bfuNum,
                                                           const TFloat spread,
                                                           const TFloat shift,
                                                           const TBlockSize& blockSize) {
    vector<uint32_t> bitsPerEachBlock(bfuNum);
    for (int i = 0; i < bitsPerEachBlock.size(); ++i) {
        const uint32_t fix = blockSize.LogCount[BfuToBand(i)] ? FixedBitAllocTableShort[i] : FixedBitAllocTableLong[i];
        int tmp = spread * ( (TFloat)scaledBlocks[i].ScaleFactorIndex/3.2) + (1.0 - spread) * fix - shift;
        if (tmp > 16) {
            bitsPerEachBlock[i] = 16;
        } else if (tmp < 2) {
            bitsPerEachBlock[i] = 0;
        } else {
            bitsPerEachBlock[i] = tmp;
        }
    }
    return bitsPerEachBlock;	
}

uint32_t TAtrac1SimpleBitAlloc::GetMaxUsedBfuId(const vector<uint32_t>& bitsPerEachBlock) {
    uint32_t idx = 7;
    for (;;) {
        uint32_t bfuNum = BfuAmountTab[idx];
        if (bfuNum > bitsPerEachBlock.size()) {
            idx--;
        } else if (idx != 0) {
            assert(bfuNum == bitsPerEachBlock.size());
            uint32_t i = 0;
            while (idx && bitsPerEachBlock[bfuNum - 1 - i] == 0) {
                if (++i >= (BfuAmountTab[idx] - BfuAmountTab[idx-1])) {
                    idx--;
                    bfuNum -= i;
                    i = 0;
                }
                assert(bfuNum - i >= 1);
            }
            break;
        } else {
            break;
        }
    }
    return idx;
}

uint32_t TAtrac1SimpleBitAlloc::CheckBfuUsage(bool* changed,
                                              uint32_t curBfuId, const vector<uint32_t>& bitsPerEachBlock) {
    uint32_t usedBfuId = GetMaxUsedBfuId(bitsPerEachBlock);
    if (usedBfuId < curBfuId) {
        *changed = true;
        curBfuId = FastBfuNumSearch ? usedBfuId : (curBfuId - 1);
    }
    return curBfuId;
}

uint32_t TAtrac1SimpleBitAlloc::Write(const std::vector<TScaledBlock>& scaledBlocks, const TBlockSize& blockSize) {
    uint32_t bfuIdx = BfuIdxConst ? BfuIdxConst - 1 : 7;
    bool autoBfu = !BfuIdxConst;
    TFloat spread = AnalizeScaleFactorSpread(scaledBlocks);

    vector<uint32_t> bitsPerEachBlock(BfuAmountTab[bfuIdx]);
    uint32_t targetBitsPerBfus;
    uint32_t curBitsPerBfus;
    for (;;) {
        bitsPerEachBlock.resize(BfuAmountTab[bfuIdx]);
        const uint32_t bitsAvaliablePerBfus = SoundUnitSize * 8 - BitsPerBfuAmountTabIdx - 32 - 2 - 3 -
                                              bitsPerEachBlock.size() * (BitsPerIDWL + BitsPerIDSF);
        TFloat maxShift = 15;
        TFloat minShift = -3;
        TFloat shift = 3.0;
        const uint32_t maxBits = bitsAvaliablePerBfus;
        const uint32_t minBits = bitsAvaliablePerBfus - 110;

        bool bfuNumChanged = false;
        for (;;) {
            const vector<uint32_t>& tmpAlloc = CalcBitsAllocation(scaledBlocks, BfuAmountTab[bfuIdx],
                                                                  spread, shift, blockSize);
            uint32_t bitsUsed = 0;
            for (int i = 0; i < tmpAlloc.size(); i++) {
                bitsUsed += SpecsPerBlock[i] * tmpAlloc[i];
            }

            if (bitsUsed < minBits) {
                if (maxShift - minShift < 0.1) {
                    if (autoBfu) {
                        bfuIdx = CheckBfuUsage(&bfuNumChanged, bfuIdx, tmpAlloc);
                    }
                    if (!bfuNumChanged) {
                        bitsPerEachBlock = tmpAlloc;
                    }
                    curBitsPerBfus = bitsUsed;
                    break;
                }
                maxShift = shift;
                shift -= (shift - minShift) / 2;
            } else if (bitsUsed > maxBits) {
                minShift = shift;
                shift += (maxShift - shift) / 2;
            } else {
                if (autoBfu) {
                    bfuIdx = CheckBfuUsage(&bfuNumChanged, bfuIdx, tmpAlloc);
                }
                if (!bfuNumChanged) {
                    bitsPerEachBlock = tmpAlloc;
                }
                curBitsPerBfus = bitsUsed;
                break;
            }
        }
        if (!bfuNumChanged) {
            targetBitsPerBfus = bitsAvaliablePerBfus;
            break;
        }
    }
    ApplyBoost(&bitsPerEachBlock, curBitsPerBfus, targetBitsPerBfus);
    WriteBitStream(bitsPerEachBlock, scaledBlocks, bfuIdx, blockSize);
    return BfuAmountTab[bfuIdx];
}

void TAtrac1BitStreamWriter::WriteBitStream(const vector<uint32_t>& bitsPerEachBlock,
                                            const std::vector<TScaledBlock>& scaledBlocks,
                                            uint32_t bfuAmountIdx,
                                            const TBlockSize& blockSize) {
    NBitStream::TBitStream bitStream;
    int bitUsed = 0;
    if (bfuAmountIdx >= (1 << BitsPerBfuAmountTabIdx)) {
        cerr << "Wrong bfuAmountIdx (" << bfuAmountIdx << "), frame skiped" << endl;
        return;
    }
    bitStream.Write(0x2 - blockSize.LogCount[0], 2);
    bitUsed+=2;

    bitStream.Write(0x2 - blockSize.LogCount[1], 2);
    bitUsed+=2;

    bitStream.Write(0x3 - blockSize.LogCount[2], 2);
    bitStream.Write(0, 2);
    bitUsed+=4;

    bitStream.Write(bfuAmountIdx, BitsPerBfuAmountTabIdx);
    bitUsed += BitsPerBfuAmountTabIdx;

    bitStream.Write(0, 2);
    bitStream.Write(0, 3);
    bitUsed+= 5;

    for (const auto wordLength : bitsPerEachBlock) {
        const auto tmp = wordLength ? (wordLength - 1) : 0;
        bitStream.Write(tmp, 4);
        bitUsed+=4;
    }
    for (int i = 0; i < bitsPerEachBlock.size(); ++i) {
        bitStream.Write(scaledBlocks[i].ScaleFactorIndex, 6);
        bitUsed+=6;
    }
    for (int i = 0; i < bitsPerEachBlock.size(); ++i) {
        const auto wordLength = bitsPerEachBlock[i];
        if (wordLength == 0 || wordLength == 1)
            continue;

        const TFloat multiple = ((1 << (wordLength - 1)) - 1);
        for (const TFloat val : scaledBlocks[i].Values) {
            const int tmp = round(val * multiple);
            const int testwl = bitsPerEachBlock[i] ? (bitsPerEachBlock[i] - 1) : 0;
            const int a = !!testwl + testwl;
            if (a != wordLength) {
                cerr << "wordlen error " << a << " " << wordLength << endl;
                abort();
            }
            bitStream.Write(NBitStream::MakeSign(tmp, wordLength), wordLength);
            bitUsed+=wordLength;
        }
    }

    bitStream.Write(0x0, 8);
    bitStream.Write(0x0, 8);

    bitUsed+=16;
    bitStream.Write(0x0, 8);

    bitUsed+=8;
    if (bitUsed > SoundUnitSize * 8) {
        cerr << "ATRAC1 bitstream corrupted, used: " << bitUsed << " exp: " << SoundUnitSize * 8 << endl;
        abort();
    }
    Container->WriteFrame(bitStream.GetBytes());
}

} //namespace NAtrac1
} //namespace NAtracDEnc
