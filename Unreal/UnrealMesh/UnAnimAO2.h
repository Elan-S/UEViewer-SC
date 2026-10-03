#ifndef __UNANIM_AO2_H__
#define __UNANIM_AO2_H__

#include <string.h>
#include <limits.h>

// Return -1 for invalid dimensions or a buffer too large for FArchive/TArray.
static inline int AO2BlockDataSize(int DynamicDofs, int FramesPerBlock, int NumBlocks, int BlockType)
{
	if (DynamicDofs < 0 || FramesPerBlock <= 0 || NumBlocks < 0 || BlockType < 0 || BlockType > 1)
		return -1;
	int Size = BlockType == 0 ? 4 : 2;
	const int Factors[] = { DynamicDofs, FramesPerBlock, NumBlocks };
	for (int i = 0; i < 3; i++)
	{
		if (Size && Factors[i] > INT_MAX / Size) return -1;
		Size *= Factors[i];
	}
	return Size;
}

// Army of Two (Xenon 445/79), native readers at 0x82490D28 / 0x8249D730.
// This is NOT IEEE binary16: exponent bias is 16, all exponent values are
// normal, and only the all-zero word is special (even 0x8000 is nonzero).
static inline float DecodeAO2Float16(unsigned short Packed)
{
	unsigned int Bits = Packed ? ((unsigned int)(Packed & 0x8000) << 16)
		| ((unsigned int)(((Packed >> 10) & 31) + 111) << 23)
		| ((unsigned int)(Packed & 1023) << 13) : 0;
	float Value;
	memcpy(&Value, &Bits, sizeof(Value));
	return Value;
}

static inline float DecodeAO2Sample(const unsigned char* Data, int Type, bool BigEndian)
{
	if (Type == 0)
	{
		unsigned int Bits = BigEndian
			? ((unsigned int)Data[0] << 24) | ((unsigned int)Data[1] << 16) | (Data[2] << 8) | Data[3]
			: ((unsigned int)Data[3] << 24) | ((unsigned int)Data[2] << 16) | (Data[1] << 8) | Data[0];
		float Value;
		memcpy(&Value, &Bits, sizeof(Value));
		return Value;
	}
	unsigned short Packed = BigEndian ? (Data[0] << 8) | Data[1] : (Data[1] << 8) | Data[0];
	if (Type == 1)
		return DecodeAO2Float16(Packed);
	// Match the game's float multiply, including -32768 (slightly below -1).
	return float(Packed < 32768 ? int(Packed) : int(Packed) - 65536) * (1.0f / 32767.0f);
}

#endif
