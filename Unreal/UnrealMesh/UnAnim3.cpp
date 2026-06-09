#include "Core.h"

#if UNREAL3

#include "UnCore.h"
#include "UnObject.h"
#include "UnMesh3.h"
#include "UnMeshTypes.h"
#include "UnrealPackage/UnPackage.h"	// for checking game type

#include "Mesh/SkeletalMesh.h"
#include "TypeConvert.h"


// following defines will help finding new undocumented compression schemes
#define FIND_HOLES			1
//#define DEBUG_DECOMPRESS	1

/*-----------------------------------------------------------------------------
	UAnimSet
-----------------------------------------------------------------------------*/

// position
#define TP(Enum, VecType)						\
				case Enum:						\
					{							\
						VecType v;				\
						Reader << v;			\
						A->KeyPos.Add(CVT(v));	\
					}							\
					break;
// position ranged
#define TPR(Enum, VecType)						\
				case Enum:						\
					{							\
						VecType v;				\
						Reader << v;			\
						FVector v2 = v.ToVector(Mins, Ranges); \
						A->KeyPos.Add(CVT(v2));	\
					}							\
					break;
// rotation
#define TR(Enum, QuatType)						\
				case Enum:						\
					{							\
						QuatType q;				\
						Reader << q;			\
						A->KeyQuat.Add(CVT(q));	\
					}							\
					break;
// rotation ranged
#define TRR(Enum, QuatType)						\
				case Enum:						\
					{							\
						QuatType q;				\
						Reader << q;			\
						FQuat q2 = q.ToQuat(Mins, Ranges); \
						A->KeyQuat.Add(CVT(q2));\
					}							\
					break;


UAnimSet::~UAnimSet()
{
	delete ConvertedAnim;
}


#if LOST_PLANET3

struct FReducedAnimData_LP3
{
	int16					v1;
	int16					v2;

	friend FArchive& operator<<(FArchive &Ar, FReducedAnimData_LP3 &D)
	{
		return Ar << D.v1 << D.v2;
	}
};

#endif // LOST_PLANET3


void UAnimSet::Serialize(FArchive &Ar)
{
	guard(UAnimSet::Serialize);
	UObject::Serialize(Ar);
#if TUROK
	if (Ar.Game == GAME_Turok)
	{
		// native part of structure
		//?? can simple skip to the end of file - these data are not used
		for (int i = 0; i < BulkDataBlocks.Num(); i++)
			Ar << BulkDataBlocks[i].mBulkData;
		return;
	}
#endif // TUROK
#if FRONTLINES
	if (Ar.Game == GAME_Frontlines && Ar.ArLicenseeVer >= 40)
	{
		guard(SerializeFrontlinesAnimSet);
		TArray<FFrontlinesHashSeq> HashSequences;
		Ar << HashSequences;
		// fill Sequences from HashSequences
		assert(Sequences.Num() == 0);
		Sequences.Empty(HashSequences.Num());
		for (int i = 0; i < HashSequences.Num(); i++) Sequences.Add(HashSequences[i].Seq);
		return;
		unguard;
	}
#endif // FRONTLINES
#if LOST_PLANET3
	if (Ar.Game == GAME_LostPlanet3 && Ar.ArLicenseeVer >= 90)
	{
		TArray<FReducedAnimData_LP3> d;
		Ar << d;
//		appPrintf("LostPlanet AnimSet %s: %d reduced tracks\n", Name, d.Num());
	}
#endif // LOST_PLANET3
	unguard;
}


/*-----------------------------------------------------------------------------
	UAnimSequence
-----------------------------------------------------------------------------*/

#if DEBUG_DECOMPRESS
#define DBG(...)			appPrintf(__VA_ARGS__)
#else
#define DBG(...)
#endif

static bool ReadAO2CompressedByteStream(FArchive &Ar, TArray<uint8> &CompressedByteStream)
{
	guard(ReadAO2CompressedByteStream);

	const int StartPos = Ar.Tell();
	const int StopPos  = Ar.GetStopper();
	if (StopPos <= StartPos)
		return false;

	for (int Pad = 0; Pad <= 0x400 && StartPos + Pad + 4 <= StopPos; Pad++)
	{
		Ar.Seek(StartPos + Pad);

		int Count;
		Ar << Count;
		if (Count < 0)
			continue;

		const int DataStart = Ar.Tell();
		const int DataEnd   = DataStart + Count;
		if (DataEnd < DataStart || DataEnd > StopPos)
			continue;

		CompressedByteStream.Empty(Count);
		CompressedByteStream.AddUninitialized(Count);
		if (Count)
			Ar.Serialize(CompressedByteStream.GetData(), Count);

		if (Pad)
			appPrintf("ArmyOfTwo AnimSequence: skipped %d byte(s) before CompressedByteStream at %X\n", Pad, StartPos);
		return true;
	}

	Ar.Seek(StartPos);
	const int Remaining = StopPos - StartPos;
	CompressedByteStream.Empty(Remaining);
	CompressedByteStream.AddUninitialized(Remaining);
	if (Remaining)
		Ar.Serialize(CompressedByteStream.GetData(), Remaining);
	appPrintf("ArmyOfTwo AnimSequence: using %d raw trailing byte(s) as CompressedByteStream at %X\n", Remaining, StartPos);
	return true;

	unguard;
}

static void ReadAO2RawTrailingBytes(FArchive &Ar, TArray<uint8> &Data)
{
	guard(ReadAO2RawTrailingBytes);

	const int StartPos = Ar.Tell();
	const int StopPos  = Ar.GetStopper();
	const int Remaining = StopPos - StartPos;
	Data.Empty(Remaining);
	if (Remaining > 0)
	{
		Data.AddUninitialized(Remaining);
		Ar.Serialize(Data.GetData(), Remaining);
	}

	unguard;
}

void UAnimSequence::SerializeAO2CompressionInfo(FArchive &Ar, int DataSize)
{
	guard(UAnimSequence::SerializeAO2CompressionInfo);

	const int StartPos = Ar.Tell();
	if (DataSize < 0x58)
	{
		Ar.Seek(StartPos + DataSize);
		return;
	}

	int Header[21];
	for (int i = 0; i < ARRAY_COUNT(Header); i++)
		Ar << Header[i];

	AO2CompressionHeader.Empty(ARRAY_COUNT(Header));
	AO2CompressionHeader.AddUninitialized(ARRAY_COUNT(Header));
	for (int i = 0; i < ARRAY_COUNT(Header); i++)
		AO2CompressionHeader[i] = Header[i];

	const int TrackInfoCount = Header[20];
	if (TrackInfoCount > 0 && TrackInfoCount < 0x10000 && 0x54 + TrackInfoCount * 4 <= DataSize)
	{
		AO2CompressedTrackInfo.Empty(TrackInfoCount);
		AO2CompressedTrackInfo.AddUninitialized(TrackInfoCount);
		for (int i = 0; i < TrackInfoCount; i++)
			Ar << AO2CompressedTrackInfo[i];
	}

	const int ByteStreamOffset = 0x54 + AO2CompressedTrackInfo.Num() * 4;
	const int ByteStreamSize = Header[18];
	if (ByteStreamSize > 0 && ByteStreamOffset + ByteStreamSize <= DataSize)
	{
		Ar.Seek(StartPos + ByteStreamOffset);
		CompressedByteStream.Empty(ByteStreamSize);
		CompressedByteStream.AddUninitialized(ByteStreamSize);
		Ar.Serialize(CompressedByteStream.GetData(), ByteStreamSize);
	}

	const int ExtraOffset = ByteStreamOffset + CompressedByteStream.Num();
	const int ExtraSize = DataSize - ExtraOffset;
	AO2CompressionExtraData.Empty(max(ExtraSize, 0));
	if (ExtraSize > 0)
	{
		Ar.Seek(StartPos + ExtraOffset);
		AO2CompressionExtraData.AddUninitialized(ExtraSize);
		Ar.Serialize(AO2CompressionExtraData.GetData(), ExtraSize);
	}

	if (getenv("AO2_ANIM_DEBUG"))
	{
		appPrintf("AO2CompressionInfo parsed for %s: size=%d", Name, DataSize);
		for (int i = 0; i < ARRAY_COUNT(Header); i++)
			appPrintf(" h%02d=%d", i, Header[i]);
		appPrintf(" trackInfo=%d byteStream=%d@%X extra=%d@%X\n",
			AO2CompressedTrackInfo.Num(), CompressedByteStream.Num(), ByteStreamOffset, AO2CompressionExtraData.Num(), ExtraOffset);

		const int TrackPreview = min(AO2CompressedTrackInfo.Num() / 6, 12);
		for (int TrackIndex = 0; TrackIndex < TrackPreview; TrackIndex++)
		{
			const int* T = &AO2CompressedTrackInfo[TrackIndex * 6];
			appPrintf("  ao2track[%d] = %d %d %d %d %d %d\n",
				TrackIndex, T[0], T[1], T[2], T[3], T[4], T[5]);
		}
		const int BytePreview = min(CompressedByteStream.Num(), 0x80);
		for (int Pos = 0; Pos < BytePreview; Pos += 16)
		{
			appPrintf("  ao2bytes %04X:", Pos);
			for (int i = 0; i < min(16, BytePreview - Pos); i++)
				appPrintf(" %02X", CompressedByteStream[Pos + i]);
			appPrintf("\n");
		}
		if (CompressedByteStream.Num() >= 0x20)
		{
			const uint8* CountData = CompressedByteStream.GetData() + 0x18;
			const int ConstantCount = (CountData[0] << 24) | (CountData[1] << 16) | (CountData[2] << 8) | CountData[3];
			const uint8* DescData = CompressedByteStream.GetData() + 0x10;
			const int AfterConstants = (DescData[0] << 24) | (DescData[1] << 16) | (DescData[2] << 8) | DescData[3];
			const int TailPreview = min(CompressedByteStream.Num() - AfterConstants, 0x80);
			appPrintf("  ao2const count=%d desc=%X tail=%d\n", ConstantCount, AfterConstants, CompressedByteStream.Num() - AfterConstants);
			for (int Pos = 0; Pos < TailPreview; Pos += 16)
			{
				appPrintf("  ao2desc %04X:", AfterConstants + Pos);
				for (int i = 0; i < min(16, TailPreview - Pos); i++)
					appPrintf(" %02X", CompressedByteStream[AfterConstants + Pos + i]);
				appPrintf("\n");
			}
		}
		if (AO2CompressionExtraData.Num())
		{
			const int ExtraPreview = min(AO2CompressionExtraData.Num(), 0x100);
			for (int Pos = 0; Pos < ExtraPreview; Pos += 16)
			{
				appPrintf("  ao2extra %04X:", ExtraOffset + Pos);
				for (int i = 0; i < min(16, ExtraPreview - Pos); i++)
					appPrintf(" %02X", AO2CompressionExtraData[Pos + i]);
				appPrintf("\n");
			}
		}
	}

	Ar.Seek(StartPos + DataSize);

	unguard;
}

void UAnimSequence::Serialize(FArchive &Ar)
{
	guard(UAnimSequence::Serialize);
	assert(Ar.ArVer >= 372 || Ar.Game == GAME_R6Vegas2);		// older version is not yet ready
	Super::Serialize(Ar);
#if TUROK
	if (Ar.Game == GAME_Turok) return;
#endif
#if MASSEFF
	if ((Ar.Game == GAME_MassEffect2 && Ar.ArLicenseeVer >= 110) || (Ar.Game == GAME_MassEffectLE && Ar.ArLicenseeVer == 168)) // ME2 or ME2LE
	{
		guard(SerializeMassEffect2);
		FByteBulkData RawAnimationBulkData;
		RawAnimationBulkData.Serialize(Ar);
		unguard;
	}
	if (Ar.Game == GAME_MassEffect3 || Ar.Game == GAME_MassEffectLE) goto old_code;		// Mass Effect 3 has no RawAnimationData
#endif // MASSEFF
#if MOH2010
	if (Ar.Game == GAME_MOH2010) goto old_code;
#endif
#if TERA
	if (Ar.Game == GAME_Tera && Ar.ArLicenseeVer >= 11) goto new_code; // we have overridden ArVer, so compare by ArLicenseeVer ...
#endif
#if TRANSFORMERS
	if (Ar.Game == GAME_Transformers && Ar.ArLicenseeVer >= 181) // Transformers: Fall of Cybertron, no version in code
	{
		int UseNewFormat;
		Ar << UseNewFormat;
		if (UseNewFormat)
		{
			Ar << Trans3Data;
			return;
		}
	}
#endif // TRANSFORMERS
#if GEARSU
	if (Ar.Game == GAME_GoWU) goto old_code;
#endif
	if (Ar.ArVer >= 577)
	{
	new_code:
		Ar << RawAnimData;			// this field was moved to RawAnimationData, RawAnimData is deprecated
	}
#if PLA
	if (Ar.Game == GAME_PLA && Ar.ArVer >= 900)
	{
		FGuid unk;
		Ar << unk;
	}
#endif // PLA
old_code:
#if R6VEGAS
	if (Ar.Game == GAME_R6Vegas2)
	{
		bIsAdditive = bIsAdditive || m_bIsAdditive;
		// Rainbow Six Vegas 2 stores the compressed animation payload in a
		// Ubisoft-specific native block. It is not a stock UE3 TArray<uint8>,
		// so keep the raw bytes for a game-specific decoder and do not let the
		// generic serializer interpret the first dword as an array count.
		ReadAO2RawTrailingBytes(Ar, CompressedByteStream);
		if (getenv("R6V2_ANIM_DEBUG"))
		{
			appPrintf("R6V2 AnimSequence %s/%s: frames=%d origFrames=%d len=%g rate=%g additive=%d force15=%d ignoreLast=%d compressedQuat=%d compressTimes=%d native=%d bytes\n",
				Outer ? Outer->Name : "None", *SequenceName, NumFrames, m_iOriginalNbFrames, SequenceLength, RateScale,
				bIsAdditive, m_bForce15FPS, m_bIgnoreLastFrame, m_bCompressedQuat, m_bCompressKeytimes, CompressedByteStream.Num());
			const int BytePreview = min(CompressedByteStream.Num(), 0xC0);
			for (int Pos = 0; Pos < BytePreview; Pos += 16)
			{
				appPrintf("  r6v2anim %04X:", Pos);
				for (int i = 0; i < min(16, BytePreview - Pos); i++)
					appPrintf(" %02X", CompressedByteStream[Pos + i]);
				appPrintf("\n");
			}
		}
		return;
	}
#endif // R6VEGAS
#if ARMYOF2
	bool bArmyOfTwoHandled = false;
	if (Ar.Game == GAME_ArmyOf2)
	{
		if (CompressedByteStream.Num())
		{
			ReadAO2RawTrailingBytes(Ar, AO2CompressedAnimData);
			if (getenv("AO2_ANIM_DEBUG"))
			{
				appPrintf("ArmyOfTwo AnimSequence %s: native animated data=%d bytes\n", Name, AO2CompressedAnimData.Num());
				const int BytePreview = min(AO2CompressedAnimData.Num(), 0x40);
				for (int Pos = 0; Pos < BytePreview; Pos += 16)
				{
					appPrintf("  ao2anim %04X:", Pos);
					for (int i = 0; i < min(16, BytePreview - Pos); i++)
						appPrintf(" %02X", AO2CompressedAnimData[Pos + i]);
					appPrintf("\n");
				}
			}
		}
		else if (!ReadAO2CompressedByteStream(Ar, CompressedByteStream))
			appError("ArmyOfTwo AnimSequence: couldn't find CompressedByteStream near %X", Ar.Tell());
		if (Ar.Tell() < Ar.GetStopper())
			Ar.Seek(Ar.GetStopper());
		bArmyOfTwoHandled = true;
	}
	if (!bArmyOfTwoHandled)
	{
#endif // ARMYOF2
	Ar << CompressedByteStream;
#if FIRSTASSAULT
	if (Ar.Game == GAME_FirstAssault && getenv("SWFA_ANIM_DEBUG"))
	{
		appPrintf("SWFA AnimSequence %s/%s raw=%d offsets=%d stream=%d frames=%d len=%g trans=%s rot=%s key=%s tail=%d\n",
			Outer ? Outer->Name : "None", *SequenceName, RawAnimData.Num(), CompressedTrackOffsets.Num(), CompressedByteStream.Num(),
			NumFrames, SequenceLength, EnumToName(TranslationCompressionFormat), EnumToName(RotationCompressionFormat),
			EnumToName(KeyEncodingFormat), Ar.GetStopper() - Ar.Tell());
		if (CompressedByteStream.Num())
		{
			int DebugBytes = 0x60;
			if (const char* DebugEnv = getenv("SWFA_ANIM_DEBUG_BYTES"))
				DebugBytes = atoi(DebugEnv);
			const int BytePreview = min(CompressedByteStream.Num(), DebugBytes);
			for (int Pos = 0; Pos < BytePreview; Pos += 16)
			{
				appPrintf("  swfaanim %04X:", Pos);
				for (int i = 0; i < min(16, BytePreview - Pos); i++)
					appPrintf(" %02X", CompressedByteStream[Pos + i]);
				appPrintf("\n");
			}
		}
	}
#endif
#if ARGONAUTS
	if (Ar.Game == GAME_Argonauts && Ar.ArLicenseeVer >= 30)
	{
		Ar << CompressedTrackTimes;
		if (Ar.ReverseBytes)
		{
			// CompressedTrackTimes is originally serialized as array of words, should swap low and high words
			for (int i = 0; i < CompressedTrackTimes.Num(); i++)
			{
				unsigned v = CompressedTrackTimes[i];
				CompressedTrackTimes[i] = ((v & 0xFFFF) << 16) | ((v >> 16) & 0xFFFF);
			}
		}
	}
#endif // ARGONAUTS
#if BATMAN
	if (Ar.Game >= GAME_Batman2 && Ar.Game <= GAME_Batman4 && Ar.ArLicenseeVer >= 55)
		Ar << AnimZip_Data;
#endif // BATMAN
#if LOST_PLANET3
	if (Ar.Game == GAME_LostPlanet3 && Ar.ArLicenseeVer >= 90)
	{
		TArray<FReducedAnimData_LP3> d;
		Ar << d;
	#if 0
		UAnimSet *AnimSet = static_cast<UAnimSet*>(Outer);
		assert(AnimSet && AnimSet->IsA("AnimSet"));
		//!!!!!
		printf("%s reduced: %d, %d tracks\n", *SequenceName, d.Num(), AnimSet->TrackBoneNames.Num());
		for (int i = 0; i < d.Num(); i++)
		{
			const FReducedAnimData_LP3 &v = d[i];
			printf("   %d  %d\n", v.v1, v.v2);
		}
	#endif
	}
#endif // LOST_PLANET3
#if ARMYOF2
	}
#endif // ARMYOF2
	unguard;
}

#if ARMYOF2
static inline uint16 ReadAO2BE16(const uint8* Data)
{
	return (Data[0] << 8) | Data[1];
}

static inline uint16 ReadAO2LE16(const uint8* Data)
{
	return Data[0] | (Data[1] << 8);
}

static inline uint32 ReadAO2BE32(const uint8* Data)
{
	return (uint32(Data[0]) << 24) | (uint32(Data[1]) << 16) | (uint32(Data[2]) << 8) | uint32(Data[3]);
}

static inline uint32 ReadAO2LE32(const uint8* Data)
{
	return uint32(Data[0]) | (uint32(Data[1]) << 8) | (uint32(Data[2]) << 16) | (uint32(Data[3]) << 24);
}

static float ReadAO2BEFloat(const uint8* Data)
{
	union
	{
		uint32 I;
		float F;
	} V;
	V.I = ReadAO2BE32(Data);
	return V.F;
}

static float ReadAO2LEFloat(const uint8* Data)
{
	union
	{
		uint32 I;
		float F;
	} V;
	V.I = ReadAO2LE32(Data);
	return V.F;
}

static float DecodeAO2Sample16(uint16 Packed)
{
	return (float(Packed) / 32767.5f) - 1.0f;
}

static float DecodeAO2Sample16Signed(uint16 Packed)
{
	return float((int16)Packed) / 32767.0f;
}

static float DecodeAO2Half(uint16 Packed)
{
	const int Sign = (Packed & 0x8000) ? -1 : 1;
	const int Exp = (Packed >> 10) & 0x1F;
	const int Mant = Packed & 0x3FF;

	if (Exp == 0)
		return Sign * ldexp(float(Mant), -24);
	if (Exp == 31)
		return Sign * 65504.0f;
	return Sign * ldexp(float(Mant + 1024), Exp - 25);
}

static int GetAO2EnvInt(const char* Name, int Default)
{
	const char* Value = getenv(Name);
	return Value ? atoi(Value) : Default;
}

static int GetAO2AutoAnimatedHeader(const TArray<uint8>& Data, int NumFrames, int AnimatedComponents)
{
	if (NumFrames <= 0 || AnimatedComponents <= 0)
		return 0;

	const int Flat16Bytes = AnimatedComponents * NumFrames * 2;
	const int Header = Data.Num() - Flat16Bytes;
	if (Header >= 0 && Header + Flat16Bytes == Data.Num())
		return Header;
	return 0;
}

static float GetAO2Constant(const TArray<uint8>& Data, int Ref)
{
	if (Ref >= 0)
		return 0.0f;

	const int Index = -Ref - 1;
	if (Index < 7)
		return 0.0f;

	const int Offset = 0x20 + (Index - 7) * 4;
	if (Offset < 0 || Offset + 4 > Data.Num())
		return 0.0f;
	return ReadAO2BEFloat(Data.GetData() + Offset);
}

static int GetAO2DescriptorOffset(const TArray<uint8>& Data)
{
	if (Data.Num() < 0x20)
		return -1;

	const int Offset = ReadAO2BE32(Data.GetData() + 0x10);
	return (Offset >= 0 && Offset < Data.Num()) ? Offset : -1;
}

static uint32 ReadAO2BitsMSB(const TArray<uint8>& Data, int BitOffset, int NumBits)
{
	uint32 Value = 0;
	for (int i = 0; i < NumBits; i++)
	{
		const int SourceBit = BitOffset + i;
		const int ByteIndex = SourceBit >> 3;
		if (ByteIndex < 0 || ByteIndex >= Data.Num())
			return Value;
		const int BitIndex = 7 - (SourceBit & 7);
		Value = (Value << 1) | ((Data[ByteIndex] >> BitIndex) & 1);
	}
	return Value;
}

static float GetAO2AnimatedValue(const TArray<uint8>& Data, int Ref, int FrameIndex, int NumFrames, int NumComponents, int Header, int SampleMode)
{
	if (Ref < 0 || NumFrames <= 0 || NumComponents <= 0)
		return 0.0f;

	const bool bFrameMajor = (SampleMode & 2) != 0;
	const bool bLittleEndian = (SampleMode & 1) != 0;
	const bool bSigned = (SampleMode & 4) != 0;
	const bool bHalf = (SampleMode & 8) != 0;

	const int SampleIndex = bFrameMajor
		? FrameIndex * NumComponents + Ref
		: Ref * NumFrames + FrameIndex;
	const int SampleOffset = Header + SampleIndex * 2;
	if (SampleOffset < 0 || SampleOffset + 2 > Data.Num())
		return 0.0f;

	const uint16 Packed = bLittleEndian
		? ReadAO2LE16(Data.GetData() + SampleOffset)
		: ReadAO2BE16(Data.GetData() + SampleOffset);
	if (bHalf)
		return DecodeAO2Half(Packed);
	return bSigned ? DecodeAO2Sample16Signed(Packed) : DecodeAO2Sample16(Packed);
}

static uint16 GetAO2PackedSample16(const TArray<uint8>& Data, int Ref, int FrameIndex, int NumFrames, int NumComponents, int Header, bool bFrameMajor, bool bLittleEndian)
{
	if (Ref < 0 || NumFrames <= 0 || NumComponents <= 0)
		return 0;

	const int SampleIndex = bFrameMajor
		? FrameIndex * NumComponents + Ref
		: Ref * NumFrames + FrameIndex;
	const int SampleOffset = Header + SampleIndex * 2;
	if (SampleOffset < 0 || SampleOffset + 2 > Data.Num())
		return 0;

	return bLittleEndian
		? ReadAO2LE16(Data.GetData() + SampleOffset)
		: ReadAO2BE16(Data.GetData() + SampleOffset);
}

static float GetAO2AnimatedBitstreamValue(
	const TArray<uint8>& Data,
	const TArray<uint8>& DescriptorData,
	int DescriptorOffset,
	int DescriptorSkip,
	int Ref,
	int FrameIndex,
	int SampleMode)
{
	if (Ref < 0 || DescriptorOffset < 0)
		return 0.0f;

	const int DescriptorStride = GetAO2EnvInt("AO2_DESC_STRIDE", 8);
	const int DescPos = DescriptorOffset + DescriptorSkip + Ref * DescriptorStride;
	if (DescPos < 0 || DescPos + 4 > DescriptorData.Num())
		return 0.0f;

	const int BitOffset = ReadAO2BE32(DescriptorData.GetData() + DescPos) + FrameIndex * 16;
	const uint16 Packed = (uint16)ReadAO2BitsMSB(Data, BitOffset, 16);
	if (SampleMode & 8)
		return DecodeAO2Half(Packed);
	return (SampleMode & 4) ? DecodeAO2Sample16Signed(Packed) : DecodeAO2Sample16(Packed);
}

static float GetAO2AnimatedValueForMode(
	const TArray<uint8>& Data,
	const TArray<uint8>& DescriptorData,
	int DescriptorOffset,
	int DescriptorSkip,
	int Ref,
	int FrameIndex,
	int NumFrames,
	int NumComponents,
	int Header,
	int SampleMode)
{
	if (SampleMode & 16)
		return GetAO2AnimatedBitstreamValue(Data, DescriptorData, DescriptorOffset, DescriptorSkip, Ref, FrameIndex, SampleMode);
	return GetAO2AnimatedValue(Data, Ref, FrameIndex, NumFrames, NumComponents, Header, SampleMode);
}

static void BuildAO2Quat(const float* V, int RotMode, FQuat& Q)
{
	const int Perms[6][3] =
	{
		{ 0, 1, 2 },	// XYZ
		{ 0, 2, 1 },	// XZY
		{ 1, 0, 2 },	// YXZ
		{ 1, 2, 0 },	// YZX
		{ 2, 0, 1 },	// ZXY
		{ 2, 1, 0 }		// ZYX
	};
	const int PermIndex = RotMode % 6;
	const int SignMode = (RotMode / 6) & 7;
	const bool bNegW = ((RotMode / 48) & 1) != 0;

	float X = V[Perms[PermIndex][0]];
	float Y = V[Perms[PermIndex][1]];
	float Z = V[Perms[PermIndex][2]];
	if (SignMode & 1) X = -X;
	if (SignMode & 2) Y = -Y;
	if (SignMode & 4) Z = -Z;

	Q.X = X;
	Q.Y = Y;
	Q.Z = Z;
	const float WSq = 1.0f - (Q.X * Q.X + Q.Y * Q.Y + Q.Z * Q.Z);
	Q.W = (WSq > 0.0f) ? sqrt(WSq) : 0.0f;
	if (bNegW)
		Q.W = -Q.W;

	const float LenSq = Q.X * Q.X + Q.Y * Q.Y + Q.Z * Q.Z + Q.W * Q.W;
	if (LenSq > 0.0f)
	{
		const float Scale = 1.0f / sqrt(LenSq);
		Q.X *= Scale;
		Q.Y *= Scale;
		Q.Z *= Scale;
		Q.W *= Scale;
	}
	else
	{
		Q.Set(0, 0, 0, 1);
	}
}

static float ScoreAO2AnimatedOffset(
	const TArray<uint8>& Data,
	const TArray<int32>& TrackInfo,
	int NumTracks,
	int NumFrames,
	int NumComponents,
	int Header,
	int SampleMode)
{
	if (NumFrames <= 1 || NumComponents <= 0)
		return 1e30f;

	float Score = 0.0f;
	int Samples = 0;
	for (int TrackIndex = 0; TrackIndex < NumTracks; TrackIndex++)
	{
		const int* Refs = &TrackInfo[TrackIndex * 6];
		if (Refs[0] < 0 && Refs[1] < 0 && Refs[2] < 0)
			continue;

		float Prev[3] = { 0, 0, 0 };
		for (int FrameIndex = 0; FrameIndex < NumFrames; FrameIndex++)
		{
			float V[3];
			float LenSq = 0.0f;
			for (int ComponentIndex = 0; ComponentIndex < 3; ComponentIndex++)
			{
				const int Ref = Refs[ComponentIndex];
				V[ComponentIndex] = GetAO2AnimatedValue(Data, Ref, FrameIndex, NumFrames, NumComponents, Header, SampleMode);
				LenSq += V[ComponentIndex] * V[ComponentIndex];
			}
			if (LenSq > 1.05f)
				Score += (LenSq - 1.05f) * 50.0f;
			if (FrameIndex > 0)
			{
				Score += fabs(V[0] - Prev[0]) + fabs(V[1] - Prev[1]) + fabs(V[2] - Prev[2]);
				Samples++;
			}
			Prev[0] = V[0];
			Prev[1] = V[1];
			Prev[2] = V[2];
		}
	}

	return Samples ? Score / Samples : 1e30f;
}

static int CountAO2LeadingZeroBytes(const TArray<uint8>& Data);
static int CountAO2LeadingZeroWords16(const TArray<uint8>& Data);

struct FAO2RootProbeCandidate
{
	float Score;
	int TrackIndex;
	int Header;
	int Layout;
	int Refs[3];
	uint16 First[3];
	uint16 Min[3];
	uint16 Max[3];
};

struct FAO2MotionProbeCandidate
{
	float Score;
	float Displacement;
	float PathLength;
	int TrackIndex;
	int Header;
	int SampleMode;
	FVector First;
	FVector Last;
	int Refs[3];
};

static uint32 GetAO2DescriptorWord(const UAnimSequence* Seq, int Ref);

static void AddAO2RootProbeCandidate(TArray<FAO2RootProbeCandidate>& Best, const FAO2RootProbeCandidate& Candidate)
{
	const int MaxBest = 12;
	int InsertAt = 0;
	while (InsertAt < Best.Num() && Best[InsertAt].Score <= Candidate.Score)
		InsertAt++;
	if (InsertAt >= MaxBest)
		return;
	Best.Insert(Candidate, InsertAt);
	if (Best.Num() > MaxBest)
		Best.RemoveAt(MaxBest);
}

static void AddAO2MotionProbeCandidate(TArray<FAO2MotionProbeCandidate>& Best, const FAO2MotionProbeCandidate& Candidate)
{
	const int MaxBest = 16;
	int InsertAt = 0;
	while (InsertAt < Best.Num() && Best[InsertAt].Score >= Candidate.Score)
		InsertAt++;
	if (InsertAt >= MaxBest)
		return;
	Best.Insert(Candidate, InsertAt);
	if (Best.Num() > MaxBest)
		Best.RemoveAt(MaxBest);
}

static FVector ReadAO2ProbeTranslation(const UAnimSequence* Seq, const int* Refs, int FrameIndex, int NumComponents, int Header, int SampleMode)
{
	FVector V;
	float* Values[3] = { &V.X, &V.Y, &V.Z };
	for (int Axis = 0; Axis < 3; Axis++)
	{
		const int Ref = Refs[Axis];
		*Values[Axis] = Ref < 0
			? GetAO2Constant(Seq->CompressedByteStream, Ref)
			: GetAO2AnimatedValue(Seq->AO2CompressedAnimData, Ref, FrameIndex, Seq->NumFrames, NumComponents, Header, SampleMode);
	}
	return V;
}

static float AO2Distance(const FVector& A, const FVector& B)
{
	const float X = A.X - B.X;
	const float Y = A.Y - B.Y;
	const float Z = A.Z - B.Z;
	return sqrt(X * X + Y * Y + Z * Z);
}

static bool AO2FiniteVector(const FVector& V)
{
	return V.X == V.X && V.Y == V.Y && V.Z == V.Z && fabs(V.X) < 1e20f && fabs(V.Y) < 1e20f && fabs(V.Z) < 1e20f;
}

static void ProbeAO2MotionCandidates(const UAnimSequence* Seq, const UAnimSet* Owner, int NumTracks, int AnimatedComponents, int AutoHeader)
{
	guard(ProbeAO2MotionCandidates);

	if (!getenv("AO2_MOTION_PROBE"))
		return;
	if (Seq->NumFrames <= 1 || AnimatedComponents <= 0 || !Seq->AO2CompressedTrackInfo.Num())
		return;

	const int Flat16Bytes = AnimatedComponents * Seq->NumFrames * 2;
	const int Delta16 = Seq->AO2CompressedAnimData.Num() - Flat16Bytes;
	const int LeadingZeroBytes = CountAO2LeadingZeroBytes(Seq->AO2CompressedAnimData);
	const int HeaderCandidates[] =
	{
		AutoHeader,
		max(0, Delta16),
		Align(max(0, Delta16), 4),
		LeadingZeroBytes,
		Align(LeadingZeroBytes, 4),
		64,
		0
	};

	TArray<FAO2MotionProbeCandidate> Best;
	for (int HeaderIndex = 0; HeaderIndex < ARRAY_COUNT(HeaderCandidates); HeaderIndex++)
	{
		const int Header = HeaderCandidates[HeaderIndex];
		if (Header < 0 || Header >= Seq->AO2CompressedAnimData.Num())
			continue;

		for (int SampleMode = 0; SampleMode < 16; SampleMode++)
		{
			for (int TrackIndex = 0; TrackIndex < NumTracks; TrackIndex++)
			{
				const int* Refs = &Seq->AO2CompressedTrackInfo[TrackIndex * 6 + 3];
				if (Refs[0] < 0 && Refs[1] < 0 && Refs[2] < 0)
					continue;

				FVector First = ReadAO2ProbeTranslation(Seq, Refs, 0, AnimatedComponents, Header, SampleMode);
				FVector Prev = First;
				float PathLength = 0.0f;
				float MaxStep = 0.0f;
				bool bBad = false;
				for (int FrameIndex = 1; FrameIndex < Seq->NumFrames; FrameIndex++)
				{
					const FVector Cur = ReadAO2ProbeTranslation(Seq, Refs, FrameIndex, AnimatedComponents, Header, SampleMode);
					if (!AO2FiniteVector(Cur))
					{
						bBad = true;
						break;
					}
					const float Step = AO2Distance(Cur, Prev);
					PathLength += Step;
					MaxStep = max(MaxStep, Step);
					Prev = Cur;
				}
				if (bBad)
					continue;

				const FVector Last = Prev;
				const float Displacement = AO2Distance(Last, First);
				if (PathLength <= 0.0001f)
					continue;

				const float Smoothness = MaxStep / PathLength;
				if (Smoothness > 0.85f)
					continue;

				FAO2MotionProbeCandidate C;
				C.Displacement = Displacement;
				C.PathLength = PathLength;
				C.Score = Displacement / (1.0f + PathLength * 0.25f + Smoothness * 10.0f);
				C.TrackIndex = TrackIndex;
				C.Header = Header;
				C.SampleMode = SampleMode;
				C.First = First;
				C.Last = Last;
				C.Refs[0] = Refs[0];
				C.Refs[1] = Refs[1];
				C.Refs[2] = Refs[2];
				AddAO2MotionProbeCandidate(Best, C);
			}
		}
	}

	appPrintf("AO2MOTION %s frames=%d animated=%d autoHeader=%d candidates=%d\n",
		*Seq->SequenceName, Seq->NumFrames, AnimatedComponents, AutoHeader, Best.Num());
	for (int i = 0; i < Best.Num(); i++)
	{
		const FAO2MotionProbeCandidate& C = Best[i];
		const char* TrackName = (Owner && C.TrackIndex < Owner->TrackBoneNames.Num()) ? *Owner->TrackBoneNames[C.TrackIndex] : "?";
		appPrintf("  motionCand[%02d] score=%g track=%d(%s) header=%d mode=%d refs=(%d,%d,%d) disp=%g path=%g first=(%g,%g,%g) last=(%g,%g,%g)\n",
			i, C.Score, C.TrackIndex, TrackName, C.Header, C.SampleMode,
			C.Refs[0], C.Refs[1], C.Refs[2], C.Displacement, C.PathLength,
			C.First.X, C.First.Y, C.First.Z, C.Last.X, C.Last.Y, C.Last.Z);
	}

	unguard;
}

static void ProbeAO2RootMotionTracks(const UAnimSequence* Seq, int NumTracks, int AnimatedComponents, int AutoHeader)
{
	guard(ProbeAO2RootMotionTracks);

	if (!getenv("AO2_ROOT_PROBE"))
		return;
	if (Seq->NumFrames <= 0 || AnimatedComponents <= 0 || !Seq->AO2CompressedTrackInfo.Num())
		return;

	const int Flat16Bytes = AnimatedComponents * Seq->NumFrames * 2;
	const int Delta16 = Seq->AO2CompressedAnimData.Num() - Flat16Bytes;
	const int LeadingZeroBytes = CountAO2LeadingZeroBytes(Seq->AO2CompressedAnimData);
	const int HeaderCandidates[] =
	{
		AutoHeader,
		max(0, Delta16),
		Align(max(0, Delta16), 4),
		LeadingZeroBytes,
		Align(LeadingZeroBytes, 4),
		64,
		0
	};

	appPrintf("AO2ROOTPROBE %s frames=%d animated=%d animBytes=%d autoHeader=%d flat16=%d\n",
		*Seq->SequenceName, Seq->NumFrames, AnimatedComponents, Seq->AO2CompressedAnimData.Num(), AutoHeader, Flat16Bytes);

	int ZeroPosTracks = 0;
	for (int TrackIndex = 0; TrackIndex < NumTracks; TrackIndex++)
	{
		const int* Refs = &Seq->AO2CompressedTrackInfo[TrackIndex * 6];
		bool bZeroPos = true;
		for (int Axis = 0; Axis < 3; Axis++)
		{
			const int Ref = Refs[3 + Axis];
			if (Ref >= 0 || fabs(GetAO2Constant(Seq->CompressedByteStream, Ref)) > 0.0001f)
			{
				bZeroPos = false;
				break;
			}
		}
		if (!bZeroPos)
			continue;
		if (ZeroPosTracks < 12)
		{
			appPrintf("  zeroPosTrack[%d] track=%d rot=(%d,%d,%d) pos=(%d,%d,%d)\n",
				ZeroPosTracks, TrackIndex, Refs[0], Refs[1], Refs[2], Refs[3], Refs[4], Refs[5]);
		}
		ZeroPosTracks++;
	}
	if (ZeroPosTracks)
		appPrintf("  zeroPosTrackCount=%d\n", ZeroPosTracks);

	if (Seq->AO2CompressedTrackInfo.Num() >= 6)
	{
		const int* RootRefs = &Seq->AO2CompressedTrackInfo[0];
		appPrintf("  track0 refs rot=(%d,%d,%d) pos=(%d,%d,%d)\n",
			RootRefs[0], RootRefs[1], RootRefs[2], RootRefs[3], RootRefs[4], RootRefs[5]);
		for (int Slot = 0; Slot < 6; Slot++)
		{
			const int Ref = RootRefs[Slot];
			if (Ref < 0)
				continue;
			for (int Layout = 0; Layout < 4; Layout++)
			{
				const bool bFrameMajor = (Layout & 2) != 0;
				const bool bLittleEndian = (Layout & 1) != 0;
				uint16 MinValue = 0xFFFF;
				uint16 MaxValue = 0;
				uint16 First = 0;
				uint16 Last = 0;
				float Delta = 0.0f;
				for (int FrameIndex = 0; FrameIndex < Seq->NumFrames; FrameIndex++)
				{
					const uint16 Packed = GetAO2PackedSample16(Seq->AO2CompressedAnimData, Ref, FrameIndex, Seq->NumFrames, AnimatedComponents, AutoHeader, bFrameMajor, bLittleEndian);
					if (FrameIndex == 0)
						First = Packed;
					else
						Delta += abs((int)Packed - (int)Last);
					Last = Packed;
					MinValue = min(MinValue, Packed);
					MaxValue = max(MaxValue, Packed);
				}
				appPrintf("  rootSlotSample slot=%d ref=%d header=%d layout=%s/%s first=%04X last=%04X min=%04X max=%04X delta=%g desc=%08X\n",
					Slot, Ref, AutoHeader,
					bFrameMajor ? "frameMajor" : "refMajor",
					bLittleEndian ? "LE" : "BE",
					First, Last, MinValue, MaxValue, Delta, GetAO2DescriptorWord(Seq, Ref));
			}

			const uint32 Desc = GetAO2DescriptorWord(Seq, Ref);
			const int Starts[] =
			{
				(int)(Desc & 0xFFFF),
				(int)(Desc & 0xFFFFFF),
				(int)((Desc >> 8) & 0xFFFF),
				(int)((Desc >> 16) & 0xFFFF)
			};
			const char* StartNames[] = { "lo16", "lo24", "mid16", "hi16" };
			for (int StartIndex = 0; StartIndex < ARRAY_COUNT(Starts); StartIndex++)
			{
				for (int bAsBits = 0; bAsBits < 2; bAsBits++)
				{
					uint16 MinValue = 0xFFFF;
					uint16 MaxValue = 0;
					uint16 First = 0;
					uint16 Last = 0;
					float Delta = 0.0f;
					bool bValid = true;
					for (int FrameIndex = 0; FrameIndex < Seq->NumFrames; FrameIndex++)
					{
						uint16 Packed;
						if (bAsBits)
						{
							const int BitOffset = Starts[StartIndex] + FrameIndex * 16;
							if (BitOffset < 0 || BitOffset + 16 > Seq->AO2CompressedAnimData.Num() * 8)
							{
								bValid = false;
								break;
							}
							Packed = (uint16)ReadAO2BitsMSB(Seq->AO2CompressedAnimData, BitOffset, 16);
						}
						else
						{
							const int ByteOffset = Starts[StartIndex] + FrameIndex * 2;
							if (ByteOffset < 0 || ByteOffset + 2 > Seq->AO2CompressedAnimData.Num())
							{
								bValid = false;
								break;
							}
							Packed = ReadAO2BE16(Seq->AO2CompressedAnimData.GetData() + ByteOffset);
						}
						if (FrameIndex == 0)
							First = Packed;
						else
							Delta += abs((int)Packed - (int)Last);
						Last = Packed;
						MinValue = min(MinValue, Packed);
						MaxValue = max(MaxValue, Packed);
					}
					if (bValid)
					{
						appPrintf("  rootSlotDescSample slot=%d ref=%d start=%s/%s value=%d first=%04X last=%04X min=%04X max=%04X delta=%g\n",
							Slot, Ref, StartNames[StartIndex], bAsBits ? "bit" : "byte", Starts[StartIndex],
							First, Last, MinValue, MaxValue, Delta);
					}
				}
			}
		}
	}

	TArray<FAO2RootProbeCandidate> Best;
	for (int HeaderIndex = 0; HeaderIndex < ARRAY_COUNT(HeaderCandidates); HeaderIndex++)
	{
		const int Header = HeaderCandidates[HeaderIndex];
		if (Header < 0 || Header + Flat16Bytes > Seq->AO2CompressedAnimData.Num())
			continue;

		for (int Layout = 0; Layout < 4; Layout++)
		{
			const bool bFrameMajor = (Layout & 2) != 0;
			const bool bLittleEndian = (Layout & 1) != 0;

			for (int TrackIndex = 0; TrackIndex < NumTracks; TrackIndex++)
			{
				const int* Refs = &Seq->AO2CompressedTrackInfo[TrackIndex * 6];
				if (Refs[3] < 0 || Refs[4] < 0 || Refs[5] < 0)
					continue;

				FAO2RootProbeCandidate Candidate;
				Candidate.Score = 0.0f;
				Candidate.TrackIndex = TrackIndex;
				Candidate.Header = Header;
				Candidate.Layout = Layout;
				for (int Axis = 0; Axis < 3; Axis++)
				{
					const int Ref = Refs[3 + Axis];
					Candidate.Refs[Axis] = Ref;
					uint16 MinValue = 0xFFFF;
					uint16 MaxValue = 0;
					uint16 Prev = 0;
					float Delta = 0.0f;
					float CenterBias = 0.0f;
					for (int FrameIndex = 0; FrameIndex < Seq->NumFrames; FrameIndex++)
					{
						const uint16 Packed = GetAO2PackedSample16(Seq->AO2CompressedAnimData, Ref, FrameIndex, Seq->NumFrames, AnimatedComponents, Header, bFrameMajor, bLittleEndian);
						if (FrameIndex == 0)
						{
							Candidate.First[Axis] = Packed;
							Prev = Packed;
						}
						else
						{
							Delta += abs((int)Packed - (int)Prev);
							Prev = Packed;
						}
						MinValue = min(MinValue, Packed);
						MaxValue = max(MaxValue, Packed);
					}
					Candidate.Min[Axis] = MinValue;
					Candidate.Max[Axis] = MaxValue;
					CenterBias += min(abs((int)Candidate.First[Axis]), abs((int)Candidate.First[Axis] - 0x8000));
					Candidate.Score += Delta + float(MaxValue - MinValue) * 4.0f + CenterBias * 0.01f;
				}
				AddAO2RootProbeCandidate(Best, Candidate);
			}
		}
	}

	for (int i = 0; i < Best.Num(); i++)
	{
		const FAO2RootProbeCandidate& C = Best[i];
		appPrintf("  rootCand[%d] score=%g track=%d header=%d layout=%s/%s refs=(%d,%d,%d) first=(%04X,%04X,%04X) min=(%04X,%04X,%04X) max=(%04X,%04X,%04X)\n",
			i, C.Score, C.TrackIndex, C.Header,
			(C.Layout & 2) ? "frameMajor" : "refMajor",
			(C.Layout & 1) ? "LE" : "BE",
			C.Refs[0], C.Refs[1], C.Refs[2],
			C.First[0], C.First[1], C.First[2],
			C.Min[0], C.Min[1], C.Min[2],
			C.Max[0], C.Max[1], C.Max[2]);
	}

	unguard;
}

static int CountAO2LeadingZeroBytes(const TArray<uint8>& Data)
{
	int Count = 0;
	while (Count < Data.Num() && Data[Count] == 0)
		Count++;
	return Count;
}

static int CountAO2LeadingZeroWords16(const TArray<uint8>& Data)
{
	int Count = 0;
	while (Count + 1 < Data.Num() && Data[Count] == 0 && Data[Count + 1] == 0)
	{
		Count += 2;
	}
	return Count / 2;
}

static void AnalyzeAO2NativeBlock(
	const UAnimSequence* Seq,
	int MaxRef,
	int PosRefs,
	int NegRefs,
	int SlotPos[6],
	int SlotNeg[6])
{
	guard(AnalyzeAO2NativeBlock);

	if (!getenv("AO2_STRUCT_DEBUG"))
		return;

	if (MaxRef < 0 || Seq->NumFrames <= 0)
		return;

	const int AnimatedRefs = MaxRef + 1;
	const int AnimBytes = Seq->AO2CompressedAnimData.Num();
	const int Flat16Bytes = AnimatedRefs * Seq->NumFrames * 2;
	const int Flat32Bytes = AnimatedRefs * Seq->NumFrames * 4;
	const int Delta16 = AnimBytes - Flat16Bytes;
	const int Delta32 = AnimBytes - Flat32Bytes;
	const int LeadingZeroBytes = CountAO2LeadingZeroBytes(Seq->AO2CompressedAnimData);
	const int LeadingZeroWords16 = CountAO2LeadingZeroWords16(Seq->AO2CompressedAnimData);

	appPrintf("  nativeSizeCompare flat16=%d delta16=%d flat32=%d delta32=%d leadingZeroBytes=%d leadingZeroWords16=%d\n",
		Flat16Bytes, Delta16, Flat32Bytes, Delta32, LeadingZeroBytes, LeadingZeroWords16);

	const int ExtraPerRef = AnimatedRefs ? Delta16 / AnimatedRefs : 0;
	const int ExtraRemainder = AnimatedRefs ? Delta16 % AnimatedRefs : 0;
	const int ExtraPerPosRef = PosRefs ? Delta16 / PosRefs : 0;
	const int ExtraPosRemainder = PosRefs ? Delta16 % PosRefs : 0;
	appPrintf("  nativeDeltaShape perAnimatedRef=%d rem=%d perPositiveUse=%d rem=%d\n",
		ExtraPerRef, ExtraRemainder, ExtraPerPosRef, ExtraPosRemainder);

	const int Starts[] =
	{
		0,
		LeadingZeroBytes,
		Align(LeadingZeroBytes, 4),
		max(0, Delta16),
		Align(max(0, Delta16), 4),
		64
	};
	for (int i = 0; i < ARRAY_COUNT(Starts); i++)
	{
		const int Start = Starts[i];
		if (Start < 0 || Start > AnimBytes)
			continue;
		const int Remaining = AnimBytes - Start;
		if (Remaining < 0)
			continue;
		const bool Flat16Fits = Remaining >= Flat16Bytes;
		const bool Flat16Exact = Remaining == Flat16Bytes;
		const int Tail = Remaining - Flat16Bytes;
		appPrintf("  candidateSampleStart[%d]=%d remaining=%d flat16Fits=%d exact=%d tail=%d\n",
			i, Start, Remaining, Flat16Fits ? 1 : 0, Flat16Exact ? 1 : 0, Tail);
	}

	const int RotRefs = SlotPos[0] + SlotPos[1] + SlotPos[2];
	const int PosOnlyRefs = SlotPos[3] + SlotPos[4] + SlotPos[5];
	appPrintf("  animatedSlotShape rotRefs=%d posRefs=%d rotStatic=%d posStatic=%d\n",
		RotRefs, PosOnlyRefs, SlotNeg[0] + SlotNeg[1] + SlotNeg[2], SlotNeg[3] + SlotNeg[4] + SlotNeg[5]);

	unguard;
}

static void AnalyzeAO2ConstantStream(const UAnimSequence* Seq, int NegRefs)
{
	guard(AnalyzeAO2ConstantStream);

	if (!getenv("AO2_STRUCT_DEBUG"))
		return;

	const TArray<uint8>& Stream = Seq->CompressedByteStream;
	if (Stream.Num() < 0x20)
		return;

	const int Word00 = ReadAO2BE32(Stream.GetData() + 0x00);
	const int Word08 = ReadAO2BE32(Stream.GetData() + 0x08);
	const int Word10 = ReadAO2BE32(Stream.GetData() + 0x10);
	const int Word18 = ReadAO2BE32(Stream.GetData() + 0x18);
	const int ExpectedFloatTableEnd = 0x20 + max(0, NegRefs - 7) * 4;
	appPrintf("  streamWords be[00]=%X be[08]=%X be[10]=%X be[18]=%X word18MinusNeg=%d descMinusExpected=%d\n",
		Word00, Word08, Word10, Word18, Word18 - NegRefs, Word10 - ExpectedFloatTableEnd);

	if (Word10 >= 0 && Word10 <= Stream.Num())
	{
		appPrintf("  streamRegions header=0x20 specialConstants=7 floatConstants=%d floatRegion=0x20..0x%X tail=%d\n",
			max(0, NegRefs - 7), Word10, Stream.Num() - Word10);
	}

	unguard;
}

static uint32 GetAO2DescriptorWord(const UAnimSequence* Seq, int Ref)
{
	if (Ref < 0 || Seq->CompressedByteStream.Num() < 0x20)
		return 0;
	const int DescriptorOffset = ReadAO2BE32(Seq->CompressedByteStream.GetData() + 0x10);
	const int Offset = DescriptorOffset + Ref * 4;
	if (Offset < 0 || Offset + 4 > Seq->CompressedByteStream.Num())
		return 0;
	return ReadAO2BE32(Seq->CompressedByteStream.GetData() + Offset);
}

static int FindAO2ExtraModeTable(const TArray<uint8>& Data)
{
	if (!Data.Num())
		return -1;

	int BestPos = -1;
	int BestCount = 0;
	for (int Pos = 0; Pos < Data.Num(); Pos++)
	{
		int Count = 0;
		int ModeCounts[4] = { 0, 0, 0, 0 };
		for (int i = Pos; i < Data.Num(); i++)
		{
			const uint8 B = Data[i];
			if (B > 2 || (B == 0 && Count > 0 && ModeCounts[1] + ModeCounts[2] > 8))
				break;
			Count++;
			ModeCounts[B]++;
		}
		if (Count > BestCount && Count >= 16 && ModeCounts[1] + ModeCounts[2] >= Count / 2)
		{
			BestPos = Pos;
			BestCount = Count;
		}
	}
	return BestPos;
}

static void ProbeAO2CompressionExtra(const UAnimSequence* Seq, int MaxRef)
{
	guard(ProbeAO2CompressionExtra);

	if (!getenv("AO2_EXTRA_PROBE") && !getenv("AO2_ANIM_DEBUG"))
		return;
	if (!Seq->AO2CompressionExtraData.Num())
		return;

	const TArray<uint8>& Extra = Seq->AO2CompressionExtraData;
	const int ModeTable = FindAO2ExtraModeTable(Extra);
	int ModeCounts[4] = { 0, 0, 0, 0 };
	if (ModeTable >= 0)
	{
		for (int i = ModeTable; i < Extra.Num(); i++)
		{
			const uint8 B = Extra[i];
			if (B > 2)
				break;
			if (B < ARRAY_COUNT(ModeCounts))
				ModeCounts[B]++;
		}
	}

	int ModeLen = 0;
	while (ModeTable >= 0 && ModeTable + ModeLen < Extra.Num() && Extra[ModeTable + ModeLen] <= 2)
		ModeLen++;

	int SlotPositive[6] = { 0, 0, 0, 0, 0, 0 };
	int SlotModeCounts[6][3];
	memset(SlotModeCounts, 0, sizeof(SlotModeCounts));
	for (int i = 0; i < Seq->AO2CompressedTrackInfo.Num(); i++)
	{
		const int Ref = Seq->AO2CompressedTrackInfo[i];
		if (Ref < 0)
			continue;
		const int Slot = i % 6;
		SlotPositive[Slot]++;
		if (ModeTable >= 0 && Ref < ModeLen)
		{
			const int Mode = Extra[ModeTable + Ref];
			if (Mode >= 0 && Mode < 3)
				SlotModeCounts[Slot][Mode]++;
		}
	}

	appPrintf("AO2EXTRA %s bytes=%d modeTable=%d modeLen=%d modeCounts=[%d,%d,%d,%d] maxRef=%d\n",
		*Seq->SequenceName, Extra.Num(), ModeTable, ModeLen,
		ModeCounts[0], ModeCounts[1], ModeCounts[2], ModeCounts[3], MaxRef);
	appPrintf("  extraSlotPos rot=(%d,%d,%d) pos=(%d,%d,%d) mode1BySlot=(%d,%d,%d,%d,%d,%d) mode2BySlot=(%d,%d,%d,%d,%d,%d)\n",
		SlotPositive[0], SlotPositive[1], SlotPositive[2], SlotPositive[3], SlotPositive[4], SlotPositive[5],
		SlotModeCounts[0][1], SlotModeCounts[1][1], SlotModeCounts[2][1], SlotModeCounts[3][1], SlotModeCounts[4][1], SlotModeCounts[5][1],
		SlotModeCounts[0][2], SlotModeCounts[1][2], SlotModeCounts[2][2], SlotModeCounts[3][2], SlotModeCounts[4][2], SlotModeCounts[5][2]);
	if (ModeTable >= 0)
	{
		for (int BytesMode1 = 1; BytesMode1 <= 8; BytesMode1++)
		{
			for (int BytesMode2 = 1; BytesMode2 <= 8; BytesMode2++)
			{
				const int PerFrame = ModeCounts[1] * BytesMode1 + ModeCounts[2] * BytesMode2;
				const int Total = PerFrame * Seq->NumFrames;
				const int Delta = Seq->AO2CompressedAnimData.Num() - Total;
				if (abs(Delta) <= 4096 || (BytesMode1 == 4 && BytesMode2 == 2) || (BytesMode1 == 2 && BytesMode2 == 2))
				{
					appPrintf("  extraBudget m1=%d m2=%d perFrame=%d total=%d delta=%d\n",
						BytesMode1, BytesMode2, PerFrame, Total, Delta);
				}
			}
		}
	}

	if (ModeTable > 0)
	{
		const int WordCount = ModeTable / 4;
		for (int i = 0; i < min(WordCount, 48); i++)
		{
			const uint8* P = Extra.GetData() + i * 4;
			appPrintf("  extraWord[%02d]=%08X (%d) bef=%g\n", i, ReadAO2BE32(P), ReadAO2BE32(P), ReadAO2BEFloat(P));
		}
	}

	if (ModeTable >= 0)
	{
		const int PrintCount = min(ModeLen, 96);
		for (int Pos = 0; Pos < PrintCount; Pos += 32)
		{
			appPrintf("  extraMode %04X:", ModeTable + Pos);
			for (int i = 0; i < min(32, PrintCount - Pos); i++)
				appPrintf(" %02X", Extra[ModeTable + Pos + i]);
			appPrintf("\n");
		}
	}

	if (ModeTable >= 0 && getenv("AO2_EXTRA_MAP_PROBE"))
	{
		int Printed = 0;
		for (int TrackIndex = 0; TrackIndex < Seq->AO2CompressedTrackInfo.Num() / 6 && Printed < 48; TrackIndex++)
		{
			const int* Refs = &Seq->AO2CompressedTrackInfo[TrackIndex * 6];
			for (int Slot = 0; Slot < 6 && Printed < 48; Slot++)
			{
				const int Ref = Refs[Slot];
				if (Ref < 0 || Ref >= ModeLen)
					continue;
				appPrintf("  extraMap[%02d] track=%d slot=%d ref=%d mode=%d desc=%08X\n",
					Printed, TrackIndex, Slot, Ref, Extra[ModeTable + Ref], GetAO2DescriptorWord(Seq, Ref));
				Printed++;
			}
		}
	}

	unguard;
}

static void ProbeAO2TailSamples(const UAnimSequence* Seq, const UAnimSet* Owner, int NumTracks, int MaxRef)
{
	guard(ProbeAO2TailSamples);

	if (!getenv("AO2_TAIL_PROBE"))
		return;
	if (Seq->NumFrames <= 0 || !Seq->AO2CompressionExtraData.Num() || !Seq->AO2CompressedAnimData.Num())
		return;

	const char* Filter = getenv("AO2_TAIL_SEQ");
	if (Filter && Filter[0] && !strstr(*Seq->SequenceName, Filter))
		return;

	const TArray<uint8>& Extra = Seq->AO2CompressionExtraData;
	const int ModeTable = FindAO2ExtraModeTable(Extra);
	if (ModeTable < 0)
		return;

	int ModeLen = 0;
	while (ModeTable + ModeLen < Extra.Num() && Extra[ModeTable + ModeLen] <= 2)
		ModeLen++;
	if (ModeLen <= 0)
		return;

	int ModeCounts[3] = { 0, 0, 0 };
	for (int Ref = 0; Ref < ModeLen; Ref++)
	{
		const int Mode = Extra[ModeTable + Ref];
		if (Mode >= 0 && Mode < ARRAY_COUNT(ModeCounts))
			ModeCounts[Mode]++;
	}
	int MinPositiveRef = 0x7FFFFFFF;
	for (int i = 0; i < Seq->AO2CompressedTrackInfo.Num(); i++)
	{
		const int Ref = Seq->AO2CompressedTrackInfo[i];
		if (Ref >= 0)
			MinPositiveRef = min(MinPositiveRef, Ref);
	}
	if (MinPositiveRef == 0x7FFFFFFF)
		MinPositiveRef = 0;

	appPrintf("AO2TAIL %s frames=%d animBytes=%d maxRef=%d modeTable=%d modeLen=%d modes=[%d,%d,%d]\n",
		*Seq->SequenceName, Seq->NumFrames, Seq->AO2CompressedAnimData.Num(), MaxRef, ModeTable, ModeLen,
		ModeCounts[0], ModeCounts[1], ModeCounts[2]);

	const int PreferredM1 = GetAO2EnvInt("AO2_TAIL_M1", 0);
	const int PreferredM2 = GetAO2EnvInt("AO2_TAIL_M2", 0);
	const int ForcedRefBase = GetAO2EnvInt("AO2_TAIL_REF_BASE", -999999);
	for (int BytesMode1 = 1; BytesMode1 <= 8; BytesMode1++)
	{
		if (PreferredM1 && BytesMode1 != PreferredM1)
			continue;
		for (int BytesMode2 = 1; BytesMode2 <= 8; BytesMode2++)
		{
			if (PreferredM2 && BytesMode2 != PreferredM2)
				continue;

			const int ModeBytes[3] = { 0, BytesMode1, BytesMode2 };
			const int PerFrame = ModeCounts[1] * BytesMode1 + ModeCounts[2] * BytesMode2;
			if (PerFrame <= 0)
				continue;
			const int Header = Seq->AO2CompressedAnimData.Num() - PerFrame * Seq->NumFrames;
			if (Header < 0 || Header > 4096)
				continue;
			if ((Header & 1) && !PreferredM1 && !PreferredM2)
				continue;

			TArray<int> RefOffsets;
			RefOffsets.Empty(ModeLen + 1);
			RefOffsets.AddZeroed(ModeLen + 1);
			for (int Ref = 0; Ref < ModeLen; Ref++)
			{
				const int Mode = Extra[ModeTable + Ref];
				RefOffsets[Ref + 1] = RefOffsets[Ref] + ((Mode >= 0 && Mode < 3) ? ModeBytes[Mode] : 0);
			}
			if (RefOffsets[ModeLen] != PerFrame)
				continue;

			for (int BaseIndex = 0; BaseIndex < 3; BaseIndex++)
			{
				const int RefBase = (BaseIndex == 2) ? ForcedRefBase : (BaseIndex ? MinPositiveRef : 0);
				if (BaseIndex == 2 && ForcedRefBase == -999999)
					continue;
				if (BaseIndex && RefBase == 0)
					continue;

				const int DescriptorOffset = GetAO2DescriptorOffset(Seq->CompressedByteStream);
				for (int Layout = 0; Layout < 3; Layout++)
				{
					appPrintf("  tailLayout m1=%d m2=%d header=%d perFrame=%d end=%d refBase=%d order=%s\n",
						BytesMode1, BytesMode2, Header, PerFrame, Header + PerFrame * Seq->NumFrames, RefBase,
						(Layout == 2) ? "desc-bitstream" : (Layout ? "ref-major" : "frame-major"));
					if (Layout == 2 && DescriptorOffset < 0)
						continue;

					int Printed = 0;
					for (int TrackIndex = 0; TrackIndex < NumTracks && Printed < 18; TrackIndex++)
					{
						const int* Refs = &Seq->AO2CompressedTrackInfo[TrackIndex * 6 + 3];
						for (int Axis = 0; Axis < 3 && Printed < 18; Axis++)
						{
							const int Ref = Refs[Axis];
							const int ModeIndex = Ref - RefBase;
							if (Ref < 0 || ModeIndex < 0 || ModeIndex >= ModeLen || Extra[ModeTable + ModeIndex] != 2)
								continue;

							const int ByteCount = ModeBytes[2];
							const int FramesToRead = min(Seq->NumFrames, 64);
							int MinBE16 = 0x7FFFFFFF, MaxBE16 = -0x7FFFFFFF;
							int MinLE16 = 0x7FFFFFFF, MaxLE16 = -0x7FFFFFFF;
							float MinBEF = 1e30f, MaxBEF = -1e30f;
							float MinLEF = 1e30f, MaxLEF = -1e30f;
							int Bad = 0;
							char FirstBytes[32];
							FirstBytes[0] = 0;

							for (int FrameIndex = 0; FrameIndex < FramesToRead; FrameIndex++)
							{
								uint8 Temp[8];
								const uint8* Data = NULL;
								if (Layout == 2)
								{
									const int DescPos = DescriptorOffset + 24 + Ref * 8;
									if (DescPos < 0 || DescPos + 4 > Seq->CompressedByteStream.Num() || ByteCount > 4)
									{
										Bad = 1;
										break;
									}
									const int BitOffset = ReadAO2BE32(Seq->CompressedByteStream.GetData() + DescPos) + FrameIndex * ByteCount * 8;
									const uint32 Packed = ReadAO2BitsMSB(Seq->AO2CompressedAnimData, BitOffset, ByteCount * 8);
									for (int i = 0; i < ByteCount; i++)
										Temp[i] = (Packed >> ((ByteCount - i - 1) * 8)) & 0xFF;
									Data = Temp;
								}
								else
								{
									const int Offset = Layout
										? Header + RefOffsets[ModeIndex] * Seq->NumFrames + FrameIndex * ByteCount
										: Header + FrameIndex * PerFrame + RefOffsets[ModeIndex];
									if (Offset < 0 || Offset + ByteCount > Seq->AO2CompressedAnimData.Num())
									{
										Bad = 1;
										break;
									}
									Data = Seq->AO2CompressedAnimData.GetData() + Offset;
								}
								if (FrameIndex == 0)
								{
									char* Dst = FirstBytes;
									for (int i = 0; i < ByteCount && i < 8; i++)
									{
										appSprintf(Dst, 32 - int(Dst - FirstBytes), "%02X", Data[i]);
										Dst += 2;
									}
									*Dst = 0;
								}
								if (ByteCount >= 2)
								{
									const int BE16 = (int16)ReadAO2BE16(Data);
									const int LE16 = (int16)ReadAO2LE16(Data);
									MinBE16 = min(MinBE16, BE16); MaxBE16 = max(MaxBE16, BE16);
									MinLE16 = min(MinLE16, LE16); MaxLE16 = max(MaxLE16, LE16);
								}
								if (ByteCount >= 4)
								{
									const float BEF = ReadAO2BEFloat(Data);
									const float LEF = ReadAO2LEFloat(Data);
									if (BEF == BEF && fabs(BEF) < 1e20f) { MinBEF = min(MinBEF, BEF); MaxBEF = max(MaxBEF, BEF); }
									if (LEF == LEF && fabs(LEF) < 1e20f) { MinLEF = min(MinLEF, LEF); MaxLEF = max(MaxLEF, LEF); }
								}
							}

							const char* TrackName = (Owner && TrackIndex < Owner->TrackBoneNames.Num()) ? *Owner->TrackBoneNames[TrackIndex] : "?";
							appPrintf("    posRef track=%d(%s) axis=%d ref=%d modeIndex=%d first=%s bad=%d be16=[%d,%d] le16=[%d,%d] bef=[%g,%g] lef=[%g,%g]\n",
								TrackIndex, TrackName, Axis, Ref, ModeIndex, FirstBytes, Bad, MinBE16, MaxBE16, MinLE16, MaxLE16,
								MinBEF, MaxBEF, MinLEF, MaxLEF);
							Printed++;
						}
					}
				}
			}
		}
	}

	unguard;
}

static void ProbeAO2DescriptorTable(const UAnimSequence* Seq, int MaxRef)
{
	guard(ProbeAO2DescriptorTable);

	if (!getenv("AO2_DESC_TABLE_PROBE"))
		return;
	if (MaxRef < 0 || Seq->CompressedByteStream.Num() < 0x20)
		return;

	const int DescriptorOffset = ReadAO2BE32(Seq->CompressedByteStream.GetData() + 0x10);
	const int DescriptorCount = (Seq->CompressedByteStream.Num() - DescriptorOffset) / 4;
	appPrintf("AO2DESCTABLE %s descOffset=%X descCount=%d maxRef=%d animBytes=%d animBits=%d\n",
		*Seq->SequenceName, DescriptorOffset, DescriptorCount, MaxRef, Seq->AO2CompressedAnimData.Num(), Seq->AO2CompressedAnimData.Num() * 8);

	if (Seq->AO2CompressedTrackInfo.Num() >= 6)
	{
		const int* RootRefs = &Seq->AO2CompressedTrackInfo[0];
		for (int Slot = 0; Slot < 6; Slot++)
		{
			const int Ref = RootRefs[Slot];
			if (Ref >= 0)
			{
				const uint32 Word = GetAO2DescriptorWord(Seq, Ref);
				appPrintf("  rootSlot%d ref=%d desc=%08X hi=%04X lo=%04X bit0=%d bit1=%d masked30=%d masked28=%d bef=%g\n",
					Slot, Ref, Word, Word >> 16, Word & 0xFFFF, Word & 1, (Word >> 1) & 1,
					Word & 0x3FFFFFFF, Word & 0x0FFFFFFF, ReadAO2BEFloat(Seq->CompressedByteStream.GetData() + DescriptorOffset + Ref * 4));
			}
			else
			{
				appPrintf("  rootSlot%d ref=%d constant=%g\n", Slot, Ref, GetAO2Constant(Seq->CompressedByteStream, Ref));
			}
		}
	}

	const int PrintCount = min(MaxRef + 1, 32);
	for (int Ref = 0; Ref < PrintCount; Ref++)
	{
		const uint32 Word = GetAO2DescriptorWord(Seq, Ref);
		appPrintf("  desc[%d]=%08X hi=%04X lo=%04X bits01=%d%d masked30=%d masked28=%d bef=%g\n",
			Ref, Word, Word >> 16, Word & 0xFFFF, (Word >> 1) & 1, Word & 1,
			Word & 0x3FFFFFFF, Word & 0x0FFFFFFF, ReadAO2BEFloat(Seq->CompressedByteStream.GetData() + DescriptorOffset + Ref * 4));
	}

	unguard;
}

static void DumpAO2StructDebug(const UAnimSequence* Seq, const UAnimSet* Owner)
{
	guard(DumpAO2StructDebug);

	if (!getenv("AO2_STRUCT_DEBUG"))
		return;

	const int NumTracks = Owner ? Owner->TrackBoneNames.Num() : 0;
	const int TrackRows = Seq->AO2CompressedTrackInfo.Num() / 6;
	appPrintf("AO2STRUCT %s/%s frames=%d length=%g rateScale=%g ownerTracks=%d trackRows=%d trackInts=%d constBytes=%d animBytes=%d\n",
		Owner ? Owner->Name : "?", *Seq->SequenceName, Seq->NumFrames, Seq->SequenceLength, Seq->RateScale,
		NumTracks, TrackRows, Seq->AO2CompressedTrackInfo.Num(), Seq->CompressedByteStream.Num(), Seq->AO2CompressedAnimData.Num());

	if (Seq->AO2CompressionHeader.Num())
	{
		appPrintf("  header:");
		for (int i = 0; i < Seq->AO2CompressionHeader.Num(); i++)
			appPrintf(" h%02d=%d", i, Seq->AO2CompressionHeader[i]);
		appPrintf("\n");

		if (Seq->AO2CompressionHeader.Num() >= 21)
		{
			const int ExpectedTrackBytes = Seq->AO2CompressionHeader[20] * 4;
			const int ByteStreamOffset = 0x54 + ExpectedTrackBytes;
			appPrintf("  layout dataSizeFields: trackCount=%d trackBytes=%d byteStreamOffset=%X byteStreamSize(h18)=%d tableA(h14)=%d tableB(h16)=%d\n",
				Seq->AO2CompressionHeader[20], ExpectedTrackBytes, ByteStreamOffset, Seq->AO2CompressionHeader[18],
				Seq->AO2CompressionHeader[14], Seq->AO2CompressionHeader[16]);
		}
	}

	int MinRef = 0;
	int MaxRef = -1;
	int PosRefs = 0;
	int NegRefs = 0;
	int ZeroRefs = 0;
	int SlotPos[6] = { 0, 0, 0, 0, 0, 0 };
	int SlotNeg[6] = { 0, 0, 0, 0, 0, 0 };
	int SlotMin[6] = { 0, 0, 0, 0, 0, 0 };
	int SlotMax[6] = { -1, -1, -1, -1, -1, -1 };
	for (int i = 0; i < Seq->AO2CompressedTrackInfo.Num(); i++)
	{
		const int Ref = Seq->AO2CompressedTrackInfo[i];
		const int Slot = i % 6;
		if (Ref >= 0)
		{
			PosRefs++;
			if (Ref == 0)
				ZeroRefs++;
			MaxRef = max(MaxRef, Ref);
			SlotPos[Slot]++;
			SlotMax[Slot] = max(SlotMax[Slot], Ref);
		}
		else
		{
			NegRefs++;
			MinRef = min(MinRef, Ref);
			SlotNeg[Slot]++;
			SlotMin[Slot] = min(SlotMin[Slot], Ref);
		}
	}
	appPrintf("  refs pos=%d neg=%d zero=%d min=%d max=%d animatedComponentGuess=%d\n",
		PosRefs, NegRefs, ZeroRefs, MinRef, MaxRef, MaxRef + 1);
	AnalyzeAO2NativeBlock(Seq, MaxRef, PosRefs, NegRefs, SlotPos, SlotNeg);
	ProbeAO2DescriptorTable(Seq, MaxRef);
	ProbeAO2CompressionExtra(Seq, MaxRef);
	for (int Slot = 0; Slot < 6; Slot++)
		appPrintf("  slot%d pos=%d neg=%d min=%d max=%d\n", Slot, SlotPos[Slot], SlotNeg[Slot], SlotMin[Slot], SlotMax[Slot]);

	if (MaxRef >= 0 && MaxRef < 0x20000)
	{
		TArray<int> UseCount;
		UseCount.Empty(MaxRef + 1);
		UseCount.AddZeroed(MaxRef + 1);
		for (int i = 0; i < Seq->AO2CompressedTrackInfo.Num(); i++)
		{
			const int Ref = Seq->AO2CompressedTrackInfo[i];
			if (Ref >= 0 && Ref <= MaxRef)
				UseCount[Ref]++;
		}
		int Missing = 0;
		int Duplicated = 0;
		for (int i = 0; i <= MaxRef; i++)
		{
			if (!UseCount[i])
				Missing++;
			else if (UseCount[i] > 1)
				Duplicated++;
		}
		appPrintf("  positiveRefRange count=%d missing=%d duplicatedRefs=%d\n", MaxRef + 1, Missing, Duplicated);
	}

	if (Seq->CompressedByteStream.Num() >= 0x20)
	{
		AnalyzeAO2ConstantStream(Seq, NegRefs);
	}

	const int StreamPreview = min(Seq->CompressedByteStream.Num(), 0x60);
	for (int Pos = 0; Pos < StreamPreview; Pos += 16)
	{
		appPrintf("  stream %04X:", Pos);
		for (int i = 0; i < min(16, StreamPreview - Pos); i++)
			appPrintf(" %02X", Seq->CompressedByteStream[Pos + i]);
		appPrintf("\n");
	}
	const int AnimPreview = min(Seq->AO2CompressedAnimData.Num(), 0x60);
	for (int Pos = 0; Pos < AnimPreview; Pos += 16)
	{
		appPrintf("  anim %04X:", Pos);
		for (int i = 0; i < min(16, AnimPreview - Pos); i++)
			appPrintf(" %02X", Seq->AO2CompressedAnimData[Pos + i]);
		appPrintf("\n");
	}

	unguard;
}

bool UAnimSequence::DecodeAO2Anims(CAnimSequence *Dst, UAnimSet *Owner) const
{
	guard(UAnimSequence::DecodeAO2Anims);

	static const CVec3 nullVec  = { 0, 0, 0 };
	static const CQuat nullQuat = { 0, 0, 0, 1 };

	const int NumTracks = Owner->TrackBoneNames.Num();
	DumpAO2StructDebug(this, Owner);
	if (AO2CompressedTrackInfo.Num() != NumTracks * 6)
		return false;

	Dst->Tracks.Empty(NumTracks);

	int MaxAnimatedRef = -1;
	int MinConstantRef = 0;
	for (int i = 0; i < AO2CompressedTrackInfo.Num(); i++)
	{
		const int Ref = AO2CompressedTrackInfo[i];
		if (Ref >= 0)
			MaxAnimatedRef = max(MaxAnimatedRef, Ref);
		else
			MinConstantRef = min(MinConstantRef, Ref);
	}

	const int AnimatedComponents = MaxAnimatedRef + 1;
	int AnimatedHeader = GetAO2AutoAnimatedHeader(AO2CompressedAnimData, NumFrames, AnimatedComponents);
	ProbeAO2DescriptorTable(this, MaxAnimatedRef);
	ProbeAO2RootMotionTracks(this, NumTracks, AnimatedComponents, AnimatedHeader);
	ProbeAO2MotionCandidates(this, Owner, NumTracks, AnimatedComponents, AnimatedHeader);
	ProbeAO2TailSamples(this, Owner, NumTracks, MaxAnimatedRef);

	const int SampleMode = GetAO2EnvInt("AO2_SAMPLE_MODE", 0);
	const int RotMode = GetAO2EnvInt("AO2_ROT_MODE", 0);
	const int DescriptorOffset = GetAO2DescriptorOffset(CompressedByteStream);
	const int DescriptorSkip = GetAO2EnvInt("AO2_DESC_SKIP", 24);
	const int HeaderOverride = GetAO2EnvInt("AO2_ANIM_OFFSET", -1);
	if (HeaderOverride >= 0)
		AnimatedHeader = HeaderOverride;

	if (getenv("AO2_ANIM_DEBUG"))
		appPrintf("AO2 decode %s: tracks=%d frames=%d refs animated=%d const=%d animBytes=%d header=%d flat16=%d sampleMode=%d rotMode=%d\n",
			Name, NumTracks, NumFrames, AnimatedComponents, -MinConstantRef, AO2CompressedAnimData.Num(), AnimatedHeader,
			AnimatedComponents * NumFrames * 2, SampleMode, RotMode);
	if (getenv("AO2_ANIM_DEBUG") && DescriptorOffset >= 0)
		appPrintf("  ao2descriptor offset=%X skip=%d count=%d\n", DescriptorOffset, DescriptorSkip, (CompressedByteStream.Num() - DescriptorOffset - DescriptorSkip) / GetAO2EnvInt("AO2_DESC_STRIDE", 8));
	if (getenv("AO2_DESC_DEBUG") && DescriptorOffset >= 0)
	{
		const int DescCount = (CompressedByteStream.Num() - DescriptorOffset) / 4;
		const int PrintCount = min(DescCount, min(AnimatedComponents, 24));
		const int PairCount = max(0, (CompressedByteStream.Num() - DescriptorOffset - 24) / 8);
		appPrintf("  ao2descShape direct4=%d skip24pair8=%d animatedRefs=%d\n", DescCount, PairCount, AnimatedComponents);
		for (int RefIndex = 0; RefIndex < PrintCount; RefIndex++)
		{
			const uint8* D = CompressedByteStream.GetData() + DescriptorOffset + RefIndex * 4;
			const uint32 BE = ReadAO2BE32(D);
			const uint32 LE = ReadAO2LE32(D);
			appPrintf("  ao2descRef[%d] bytes=%02X %02X %02X %02X be=%08X le=%08X bef=%g lef=%g lo16=%u hi16=%u lo15=%u hi17=%u\n",
				RefIndex, D[0], D[1], D[2], D[3], BE, LE, ReadAO2BEFloat(D), ReadAO2LEFloat(D),
				BE & 0xFFFF, BE >> 16, BE & 0x7FFF, BE >> 15);
		}
	}

	if (getenv("AO2_ANIM_SCAN"))
	{
		const int NeededBytes = AnimatedComponents * NumFrames * 2;
		const int MaxScan = max(0, min(AO2CompressedAnimData.Num() - NeededBytes, 0x800));
		for (int Mode = 0; Mode < 16; Mode++)
		{
			float BestScore = 1e30f;
			int BestOffset = 0;
			for (int Offset = 0; Offset < MaxScan; Offset += 2)
			{
				const float Score = ScoreAO2AnimatedOffset(AO2CompressedAnimData, AO2CompressedTrackInfo, NumTracks, NumFrames, AnimatedComponents, Offset, Mode);
				if (Score < BestScore)
				{
					BestScore = Score;
					BestOffset = Offset;
				}
			}
			appPrintf("  ao2scan %s mode=%d bestOffset=%d score=%g\n", Name, Mode, BestOffset, BestScore);
		}
	}

	for (int TrackIndex = 0; TrackIndex < NumTracks; TrackIndex++)
	{
		CAnimTrack *A = new CAnimTrack;
		Dst->Tracks.Add(A);

		const int* Refs = &AO2CompressedTrackInfo[TrackIndex * 6];
		A->KeyQuat.Empty(NumFrames);
		A->KeyPos.Empty(NumFrames);

		for (int FrameIndex = 0; FrameIndex < NumFrames; FrameIndex++)
		{
			float V[6];
			for (int ComponentIndex = 0; ComponentIndex < 6; ComponentIndex++)
			{
				const int Ref = Refs[ComponentIndex];
				if (Ref < 0)
				{
					V[ComponentIndex] = GetAO2Constant(CompressedByteStream, Ref);
				}
				else
				{
					V[ComponentIndex] = GetAO2AnimatedValueForMode(AO2CompressedAnimData, CompressedByteStream, DescriptorOffset, DescriptorSkip, Ref, FrameIndex, NumFrames, AnimatedComponents, AnimatedHeader, SampleMode);
				}
			}

			FQuat Q;
			BuildAO2Quat(V, RotMode, Q);
			if (getenv("AO2_ANIM_DEBUG_VALUES") && TrackIndex < 4 && FrameIndex < 4)
				appPrintf("  ao2val track=%d frame=%d raw=(%g %g %g | %g %g %g) quat=(%g %g %g %g)\n",
					TrackIndex, FrameIndex, V[0], V[1], V[2], V[3], V[4], V[5], Q.X, Q.Y, Q.Z, Q.W);
			A->KeyQuat.Add(CVT(Q));

			CVec3 Pos;
			Pos.Set(V[3], V[4], V[5]);
			A->KeyPos.Add(Pos);
		}

		if (!A->KeyQuat.Num())
			A->KeyQuat.Add(nullQuat);
		if (!A->KeyPos.Num())
			A->KeyPos.Add(nullVec);
	}

	return true;

	unguard;
}
#endif // ARMYOF2

#if R6VEGAS

static uint16 R6V2ReadU16(const uint8* Data)
{
	return Data[0] | (Data[1] << 8);
}

static int16 R6V2ReadS16(const uint8* Data)
{
	return (int16)R6V2ReadU16(Data);
}

static uint32 R6V2ReadU32(const uint8* Data)
{
	return Data[0] | (Data[1] << 8) | (Data[2] << 16) | (Data[3] << 24);
}

static bool R6V2FindTrackHeader(const uint8* Data, int DataSize, int& Pos, int NumFrames)
{
	const int StartPos = Pos;
	const int MaxProbe = min(32, DataSize - StartPos - 8);
	for (int Probe = 0; Probe <= MaxProbe; Probe += 4)
	{
		const int TestPos = StartPos + Probe;
		const uint16 TransCount = R6V2ReadU16(Data + TestPos);
		const uint16 RotCount   = R6V2ReadU16(Data + TestPos + 2);
		const uint32 TransSize  = R6V2ReadU32(Data + TestPos + 4);

		if (TransCount > max(NumFrames * 2, 1) && TransCount != 0xFFFF)
			continue;
		if (RotCount > max(NumFrames * 2, 1) && RotCount != 0xFFFF)
			continue;
		if (TransSize > (uint32)(DataSize - TestPos - 8))
			continue;
		if (TransSize % 6)
			continue;
		if (TransSize > 0x4000)
			continue;

		Pos = TestPos;
		return true;
	}
	return false;
}

static bool R6V2ShouldUseTranslationTrack(const UAnimSet* Owner, int TrackIndex)
{
	if (TrackIndex == 0)
		return true;
	if (!Owner->bAnimRotationOnly)
		return true;
	if (!Owner->TrackBoneNames.IsValidIndex(TrackIndex))
		return false;

	const FName BoneName = Owner->TrackBoneNames[TrackIndex];
	for (int i = 0; i < Owner->UseTranslationBoneNames.Num(); i++)
		if (Owner->UseTranslationBoneNames[i] == BoneName)
			return true;
	for (int i = 0; i < Owner->ForceMeshTranslationBoneNames.Num(); i++)
		if (Owner->ForceMeshTranslationBoneNames[i] == BoneName)
			return false;
	return false;
}

bool UAnimSequence::DecodeR6V2Anims(CAnimSequence *Dst, UAnimSet *Owner) const
{
	guard(UAnimSequence::DecodeR6V2Anims);

	const int NumTracks = Owner->TrackBoneNames.Num();
	const uint8* Data = CompressedByteStream.GetData();
	const int DataSize = CompressedByteStream.Num();
	if (!Data || DataSize < 32 || NumTracks <= 0)
		return false;

	int Pos = 0;
	if (DataSize >= 8 && R6V2ReadU32(Data + 4) == (uint32)NumTracks)
		Pos += 4;	// native block starts with an absolute package/file pointer

	const int TrackCount = R6V2ReadU32(Data + Pos);
	if (TrackCount != NumTracks)
	{
		appNotify("R6V2 AnimSequence %s/%s has %d native tracks, expected %d",
			Owner->Name, *SequenceName, TrackCount, NumTracks);
		return false;
	}

	Pos += 4;
	Pos += 20;		// zero/flags header seen before the per-track blocks

	Dst->Tracks.Empty(NumTracks);
	static const CVec3 nullVec = { 0, 0, 0 };
	static const CQuat nullQuat = { 0, 0, 0, 1 };

	for (int TrackIndex = 0; TrackIndex < NumTracks; TrackIndex++)
	{
		if (!R6V2FindTrackHeader(Data, DataSize, Pos, max(m_iOriginalNbFrames, NumFrames)))
			return false;
		if (Pos + 8 > DataSize)
			return false;

		uint16 StoredTransKeys = R6V2ReadU16(Data + Pos);
		uint16 StoredRotKeys   = R6V2ReadU16(Data + Pos + 2);
		uint32 TransSize       = R6V2ReadU32(Data + Pos + 4);
		Pos += 8;

		if (TransSize > (uint32)(DataSize - Pos))
			return false;

		CAnimTrack *A = new CAnimTrack;
		Dst->Tracks.Add(A);

		const int TransKeys = TransSize / 6;
		const bool UseTranslation = R6V2ShouldUseTranslationTrack(Owner, TrackIndex);
		if (UseTranslation && TransKeys > 0 && TransSize == (uint32)(TransKeys * 6))
		{
			A->KeyPos.Empty(TransKeys);
			for (int KeyIndex = 0; KeyIndex < TransKeys; KeyIndex++)
			{
				const uint8* Key = Data + Pos + KeyIndex * 6;
				CVec3 V;
				V[0] = R6V2ReadS16(Key    ) / 64.0f;
				V[1] = R6V2ReadS16(Key + 2) / 64.0f;
				V[2] = R6V2ReadS16(Key + 4) / 64.0f;
				A->KeyPos.Add(V);
			}
		}
		Pos += TransSize;
		Pos = Align(Pos, 4);
		if (Pos > DataSize)
			return false;

		if (Pos + 4 > DataSize)
			return false;

		uint32 RotSize = R6V2ReadU32(Data + Pos);
		Pos += 4;
		if (RotSize > (uint32)(DataSize - Pos))
			return false;

		const int RotKeys = RotSize / 6;
		if (RotKeys > 0 && RotSize == (uint32)(RotKeys * 6))
		{
			A->KeyQuat.Empty(RotKeys);
			for (int KeyIndex = 0; KeyIndex < RotKeys; KeyIndex++)
			{
				const uint8* Key = Data + Pos + KeyIndex * 6;
				FQuatFixed48NoW Q;
				Q.X = R6V2ReadU16(Key);
				Q.Y = R6V2ReadU16(Key + 2);
				Q.Z = R6V2ReadU16(Key + 4);
				FQuat Q2 = Q;
				A->KeyQuat.Add(CVT(Q2));
			}
		}
		else if (RotSize == 0)
		{
			// Leave the rotation empty so the bind pose is used. This mirrors
			// the meaning of a missing track better than forcing identity.
		}
		Pos += RotSize;
		Pos = Align(Pos, 4);
		if (Pos > DataSize)
			return false;

		if (getenv("R6V2_ANIM_DEBUG") && TrackIndex < 6)
		{
			appPrintf("  r6v2 track[%d] pos=%X stored=%d/%d transBytes=%d rotBytes=%d keys=%d/%d\n",
				TrackIndex, Pos, StoredTransKeys, StoredRotKeys, TransSize, RotSize, A->KeyPos.Num(), A->KeyQuat.Num());
		}
	}

	return true;

	unguard;
}

#endif // R6VEGAS


static void ReadTimeArray(FArchive &Ar, int NumKeys, TArray<float> &Times, int NumFrames)
{
	guard(ReadTimeArray);

	Times.Empty(NumKeys);
	if (NumKeys <= 1) return;

//	appPrintf("  pos=%4X keys (max=%X)[ ", Ar.Tell(), NumFrames);
	if (NumFrames < 256)
	{
		for (int k = 0; k < NumKeys; k++)
		{
			uint8 v;
			Ar << v;
			Times.Add(v);
//			if (k < 4 || k > NumKeys - 5) appPrintf(" %02X ", v);
//			else if (k == 4) appPrintf("...");
		}
	}
	else
	{
		for (int k = 0; k < NumKeys; k++)
		{
			uint16 v;
			Ar << v;
			Times.Add(v);
//			if (k < 4 || k > NumKeys - 5) appPrintf(" %04X ", v);
//			else if (k == 4) appPrintf("...");
		}
	}
//	appPrintf(" ]\n");

	// align to 4 bytes
	Ar.Seek(Align(Ar.Tell(), 4));

	unguard;
}


#if ARGONAUTS

static void ReadArgonautsTimeArray(const TArray<unsigned> &SourceArray, int FirstKey, int NumKeys, TArray<float> &Times, float TimeScale)
{
	guard(ReadArgonautsTimeArray);

	Times.Empty(NumKeys);
	if (NumKeys <= 1) return;

	TimeScale /= 65535.0f;			// 0 -> 0.0f, 65535 -> track length

	for (int i = 0; i < NumKeys; i++)
	{
		int index = FirstKey + i;
		unsigned v = SourceArray[index / 2];
		if (!(index & 1))
			v &= 0xFFFF;			// low word
		else
			v >>= 16;				// high word
		Times.Add(v * TimeScale);
	}

	unguard;
}

#endif // ARGONAUTS


#if TRANSFORMERS

bool UAnimSequence::DecodeTrans3Anims(CAnimSequence *Dst, UAnimSet *Owner) const
{
	guard(UAnimSequence::DecodeTrans3Anims);

	if (CompressedByteStream.Num() == 0)
	{
		// This situation is true for some sequences
		return false;
	}

	// read some counts first
	FMemReader Reader1(Trans3Data.GetData(), Trans3Data.Num());
	Reader1.SetupFrom(*Package);

	int NumberOfStaticRotations, NumberOfStaticTranslations, NumberOfAnimatedRotations, NumberOfAnimatedTranslations,
		NumberOfAnimatedUncompressedTranslations;
	Reader1 << NumberOfStaticRotations << NumberOfStaticTranslations
			<< NumberOfAnimatedRotations << NumberOfAnimatedTranslations
			<< NumberOfAnimatedUncompressedTranslations;

	// create new reader for keyframe data
	int StartOffset = Reader1.Tell();	// always equals to 20
	FMemReader Reader((uint8*)Trans3Data.GetData() + StartOffset, Trans3Data.Num() - StartOffset);

	// key index offsets
	int StartOfStaticRotations        = 0;
	int StartOfStaticTranslations     = StartOfStaticRotations + NumberOfStaticRotations;
	int StartOfAnimatedRotations      = StartOfStaticTranslations + NumberOfStaticTranslations;
	int StartOfAnimatedTranslations   = StartOfAnimatedRotations + NumberOfAnimatedRotations;
	int StartOfAnimUncompTranslations = StartOfAnimatedTranslations + NumberOfAnimatedTranslations;

	// determine quaternion size for this format
	int QuatSize;
	switch (RotationCompressionFormat)
	{
	case ACF_None:
		QuatSize = 16; break;
	case ACF_Float96NoW:
		QuatSize = 12; break;
	case ACF_Fixed48NoW:
	case ACF_IntervalFixed48NoW:
		QuatSize = 6; break;
	case ACF_IntervalFixed32NoW:
	case ACF_Fixed32NoW:
	case ACF_Float32NoW:
		QuatSize = 4; break;
	default:
		appError("Unknown RotationCompressionFormat %d (%s)", RotationCompressionFormat, EnumToName(RotationCompressionFormat));
	}

	// block sizes
	int StaticRotationSize        = 16 * NumberOfStaticRotations;					// FQuat
	int StaticTranslationsSize    = 12 * NumberOfStaticTranslations;				// FVector
	int AnimatedRotationSize      = QuatSize * NumberOfAnimatedRotations;
	int AnimatedTranslationSize   = 4  * NumberOfAnimatedTranslations;				// FPackedVector_Trans
	int AnimUncompTranslationSize = 12 * NumberOfAnimatedUncompressedTranslations;	// FVector
	int AnimatedDataSize          = AnimatedRotationSize + AnimatedTranslationSize + AnimUncompTranslationSize;
	// interval data blocks
	int RotationIntervalSize = 0;
	if (RotationCompressionFormat == ACF_IntervalFixed32NoW || RotationCompressionFormat == ACF_IntervalFixed48NoW)
		RotationIntervalSize = 40 * NumberOfAnimatedRotations;						// 3+3+4 floats
	int TranslationIntervalSize = 24 * NumberOfAnimatedTranslations;

	// compute offsets, in order of appearance in data
	int StaticRotationOffset       = 0;
	int StaticTranslationOffset    = StaticRotationOffset + StaticRotationSize;
	int RotationIntervalOffset     = StaticTranslationOffset + StaticTranslationsSize;
	int TranslationIntervalOffset  = RotationIntervalOffset + RotationIntervalSize;
	int AnimatedRotationOffset     = TranslationIntervalOffset + TranslationIntervalSize;
	int AnimatedTranslationOffset  = AnimatedRotationOffset + AnimatedRotationSize;
	int AnimatedUncompTranslationOffset = AnimatedTranslationOffset + AnimatedTranslationSize;

	// Differences in original game code: serialization function copies data to anither array with alignment of AnimatedRotationSize to 4 and
	// RotationIntervalOffset to 16, plus is makes rotation interval data in size of 48 bytes each (it takes 40 bytes in a package)

	// verification
	int TotalDataSize = AnimatedRotationOffset + AnimatedDataSize * NumFrames;
	assert(TotalDataSize == Trans3Data.Num() - StartOffset);

	DBG("          TF3: StatRot=%d StatTrans=%d AnimRot=%d AnimTrans=%d UncompTrans=%d TotalSize=%d (%d)\n",
		NumberOfStaticRotations, NumberOfStaticTranslations, NumberOfAnimatedRotations, NumberOfAnimatedTranslations,
		NumberOfAnimatedUncompressedTranslations, Trans3Data.Num() - StartOffset, TotalDataSize
	);
/*	DBG("  StatRot: %08X [%d]\n"
		"  StatTr:  %08X [%d]\n"
		"  RotInt:  %08X [%d]\n"
		"  TrInt:   %08X [%d]\n"
		"  AnRot:   %08X [%d] + N\n"
		"  AnTr:    %08X [%d]\n"
		"  AnTrU:   %08X [%d]\n",
		StaticRotationOffset, NumberOfStaticRotations,
		StaticTranslationOffset, NumberOfStaticTranslations,
		RotationIntervalOffset, NumberOfAnimatedRotations,
		TranslationIntervalOffset, NumberOfAnimatedTranslations,
		AnimatedRotationOffset, NumberOfAnimatedRotations,
		AnimatedTranslationOffset, NumberOfAnimatedTranslations,
		AnimatedUncompTranslationOffset, NumberOfAnimatedUncompressedTranslations
	); */

	static const CVec3 nullVec  = { 0, 0, 0 };
	static const CQuat nullQuat = { 0, 0, 0, 1 };

	int NumTracks = Owner->TrackBoneNames.Num();
	assert(TrackOffsets.Num() == NumTracks * 2);
	Dst->Tracks.Empty(NumTracks);
	int i;
	for (int Bone = 0; Bone < NumTracks; Bone++)
	{
		CAnimTrack* A = new CAnimTrack;
		Dst->Tracks.Add(A);

		int RotKeyIndex   = TrackOffsets[Bone * 2    ];
		int TransKeyIndex = TrackOffsets[Bone * 2 + 1];

		// decode translation
		DBG("          Trans: key=%d -> ", TransKeyIndex);
		if (TransKeyIndex < StartOfStaticTranslations)
		{
			// null vector
			DBG("null ");
			A->KeyPos.Add(nullVec);
		}
		else if (TransKeyIndex < StartOfAnimatedTranslations)
		{
			// static vector (single key)
			TransKeyIndex -= StartOfStaticTranslations;
			DBG("static[%d] ", TransKeyIndex);
			Reader.Seek(StaticTranslationOffset + 12 * TransKeyIndex);
			FVector pos;
			Reader << pos;
			A->KeyPos.Add(CVT(pos));
		}
		else if (TransKeyIndex < StartOfAnimUncompTranslations)
		{
			// animated compressed translation
			TransKeyIndex -= StartOfAnimatedTranslations;
			DBG("comp[%d] ", TransKeyIndex);
			A->KeyPos.Empty(NumFrames);
			Reader.Seek(TranslationIntervalOffset + 24 * TransKeyIndex);
			FVector Mins, Ranges;
			Reader << Mins << Ranges;
			for (i = 0; i < NumFrames; i++)
			{
				Reader.Seek(AnimatedTranslationOffset + 4 * TransKeyIndex + i * AnimatedDataSize);
				FPackedVector_Trans pos;
				Reader << pos;
				FVector pos2 = pos.ToVector(Mins, Ranges); // convert
				A->KeyPos.Add(CVT(pos2));
			}
		}
		else
		{
			// animated uncompressed translation
			TransKeyIndex -= StartOfAnimUncompTranslations;
			DBG("uncomp[%d] ", TransKeyIndex);
			A->KeyPos.Empty(NumFrames);
			for (i = 0; i < NumFrames; i++)
			{
				Reader.Seek(AnimatedUncompTranslationOffset + 12 * TransKeyIndex + i * AnimatedDataSize);
				FVector pos;
				Reader << pos;
				A->KeyPos.Add(CVT(pos));
			}
		}

		// decode rotation
		DBG("; Rot: key=%d -> ", RotKeyIndex);
		if (RotKeyIndex < StartOfStaticRotations)
		{
			// null quaternion
			DBG("null ");
			A->KeyQuat.Add(nullQuat);
		}
		else if (RotKeyIndex < StartOfAnimatedRotations)
		{
			// static rotation
			RotKeyIndex -= StartOfStaticRotations;
			DBG("static[%d] ", RotKeyIndex);
			Reader.Seek(StaticRotationOffset + 16 * RotKeyIndex);
			FQuat q;
			Reader << q;
			q.W *= -1;
			A->KeyQuat.Add(CVT(q));
		}
		else
		{
			// animated rotation
			RotKeyIndex -= StartOfAnimatedRotations;
			DBG("comp[%d] ", RotKeyIndex);
			A->KeyQuat.Empty(NumFrames);
			FVector Mins, Ranges;
			FQuat TransQuatBase;
			if (RotationIntervalSize)
			{
				// get interval data
				Reader.Seek(RotationIntervalOffset + 40 * RotKeyIndex);
				Reader << Mins << Ranges;
				Reader << TransQuatBase.W << TransQuatBase.X << TransQuatBase.Y << TransQuatBase.Z;
			}
			for (i = 0; i < NumFrames; i++)
			{
				Reader.Seek(AnimatedRotationOffset + QuatSize * RotKeyIndex + i * AnimatedDataSize);
				switch (RotationCompressionFormat)
				{
				TR (ACF_None, FQuat)
				TR (ACF_Float96NoW, FQuatFloat96NoW)
				TR (ACF_Fixed48NoW, FQuatFixed48NoW)
				TR (ACF_Fixed32NoW, FQuatFixed32NoW)
				TRR(ACF_IntervalFixed32NoW, FQuatIntervalFixed32NoW)
				TR (ACF_Float32NoW, FQuatFloat32NoW)
				TRR(ACF_IntervalFixed48NoW, FQuatIntervalFixed48NoW_Trans)
				default:
					appError("Unknown rotation compression method: %d (%s)", RotationCompressionFormat, EnumToName(RotationCompressionFormat));
				}
			}
			if (RotationIntervalSize)
			{
				for (i = 0; i < NumFrames; i++)
				{
					CQuat q = A->KeyQuat[i];
					q.Mul(CVT(TransQuatBase));
					q.W *= -1;
					A->KeyQuat[i] = q;
				}
			}
		}

		DBG(" - %s\n", *Owner->TrackBoneNames[Bone]);
	}

	return true;
	unguard;
}

#endif // TRANSFORMERS


#if FIRSTASSAULT

static uint32 SWFAReadBE32(const uint8* Data)
{
	return (Data[0] << 24) | (Data[1] << 16) | (Data[2] << 8) | Data[3];
}

static uint16 SWFAReadBE16(const uint8* Data)
{
	return (Data[0] << 8) | Data[1];
}

static int SWFASignExtend(uint32 Value, int Bits)
{
	const uint32 Mask = 1u << (Bits - 1);
	return (int)((Value ^ Mask) - Mask);
}

static CQuat SWFADecodeQuat32(uint32 Packed)
{
	// EdgeAnim stores 32-bit "smallest three" quaternions. A zero word decodes
	// to identity: W is omitted and XYZ are zero.
	const int Missing = Packed >> 30;
	const float Scale = 0.7071067811865475f / 511.0f;
	float Stored[3];
	Stored[0] = SWFASignExtend((Packed >> 20) & 0x3FF, 10) * Scale;
	Stored[1] = SWFASignExtend((Packed >> 10) & 0x3FF, 10) * Scale;
	Stored[2] = SWFASignExtend( Packed        & 0x3FF, 10) * Scale;

	float Components[4];
	int StoredIndex = 0;
	float Sum = 0;
	for (int i = 0; i < 4; i++)
	{
		if (i == Missing)
			continue;
		Components[i] = Stored[StoredIndex++];
		Sum += Components[i] * Components[i];
	}
	Components[Missing] = sqrt(max(0.0f, 1.0f - Sum));

	CQuat Q;
	Q.X = Components[0];
	Q.Y = Components[1];
	Q.Z = Components[2];
	Q.W = Components[3];
	return Q;
}

static bool SWFAIsEdgeAnimStream(const TArray<uint8>& Data)
{
	return Data.Num() >= 4 && Data[0] == 'E' && Data[1] == 'A' && Data[2] == '0' && Data[3] == '5';
}

bool UAnimSequence::DecodeSWFAAnims(CAnimSequence *Dst, UAnimSet *Owner) const
{
	guard(UAnimSequence::DecodeSWFAAnims);

	if (!SWFAIsEdgeAnimStream(CompressedByteStream))
		return false;

	const int NumTracks = Owner->TrackBoneNames.Num();
	if (NumTracks <= 0)
		return false;

	static const CVec3 nullVec  = { 0, 0, 0 };
	static const CQuat nullQuat = { 0, 0, 0, 1 };

	Dst->Tracks.Empty(NumTracks);
	for (int TrackIndex = 0; TrackIndex < NumTracks; TrackIndex++)
	{
		CAnimTrack *A = new CAnimTrack;
		Dst->Tracks.Add(A);
		A->KeyPos.Add(nullVec);
		A->KeyQuat.Add(nullQuat);
	}

	const uint8* Data = CompressedByteStream.GetData();
	const int StaticRotCount = SWFAReadBE16(Data + 0x16);
	const int StaticTransCount = SWFAReadBE16(Data + 0x18);
	const int AnimatedRotCount = SWFAReadBE16(Data + 0x1E);
	const int AnimatedTransCount = SWFAReadBE16(Data + 0x20);
	int TablePos = 0x60;
	const int StaticRotTable = TablePos;
	TablePos += StaticRotCount * 2;
	TablePos += StaticTransCount * 2;
	if (AnimatedRotCount || AnimatedTransCount)
		TablePos = Align(TablePos, 16);
	const int AnimatedRotTable = TablePos;
	TablePos += AnimatedRotCount * 2;
	TablePos += AnimatedTransCount * 2;
	const int StaticRotData = Align(TablePos, 16);

	if (StaticRotCount >= 0 && StaticRotData + StaticRotCount * 4 <= CompressedByteStream.Num())
	{
		for (int i = 0; i < StaticRotCount; i++)
		{
			const int TrackIndex = SWFAReadBE16(Data + StaticRotTable + i * 2);
			if (TrackIndex < 0 || TrackIndex >= Dst->Tracks.Num())
				continue;
			CAnimTrack* A = Dst->Tracks[TrackIndex];
			A->KeyQuat.Empty(1);
			A->KeyQuat.Add(SWFADecodeQuat32(SWFAReadBE32(Data + StaticRotData + i * 4)));
		}
	}

	const int AnimatedRotData = SWFAReadBE32(Data + 0x5C);
	const int MinAnimatedRotData = StaticRotData + StaticRotCount * 4;
	if (AnimatedRotCount > 0 && AnimatedRotData >= MinAnimatedRotData && AnimatedRotData + AnimatedRotCount * 4 <= CompressedByteStream.Num())
	{
		for (int i = 0; i < AnimatedRotCount; i++)
		{
			const int TrackIndex = SWFAReadBE16(Data + AnimatedRotTable + i * 2);
			if (TrackIndex < 0 || TrackIndex >= Dst->Tracks.Num())
				continue;
			CAnimTrack* A = Dst->Tracks[TrackIndex];
			A->KeyQuat.Empty(1);
			A->KeyQuat.Add(SWFADecodeQuat32(SWFAReadBE32(Data + AnimatedRotData + i * 4)));
		}
	}

	if (getenv("SWFA_ANIM_DEBUG"))
	{
		appPrintf("SWFA EdgeAnim %s/%s: stream=%d hdr0=%08X hdr1=%08X tracks=%d frames=%d staticRot=%d staticTrans=%d animRot=%d animTrans=%d staticRotData=%X animRotData=%X\n",
			Owner->Name, *SequenceName, CompressedByteStream.Num(), SWFAReadBE32(Data + 4), SWFAReadBE32(Data + 8), NumTracks, NumFrames,
			StaticRotCount, StaticTransCount, AnimatedRotCount, AnimatedTransCount, StaticRotData, AnimatedRotData);
	}

	return true;
	unguard;
}

#endif // FIRSTASSAULT


#if BLADENSOUL

void ReadBnS_ZOnlyRLE(FArchive& Reader, int RotKeys, CAnimTrack* A)
{
	guard(ReadBnS_ZOnlyRLE);

	int32 keyMode, numRLE_keys;
	Reader << keyMode << numRLE_keys;
	assert(keyMode >= 0 || keyMode <= 3);
	// Read RLE decoding table
	TArray<int16> RLETable;
	RLETable.AddUninitialized(numRLE_keys * 2);
	for (int i = 0; i < numRLE_keys; i++)
	{
		Reader << RLETable[i*2] << RLETable[i*2+1];
	}
	int nextRLE_index = 0;
	int numAddedKeys = 0;
	while (numAddedKeys < RotKeys)
	{
		FQuatFloat96NoW q;
		if (keyMode == 0)
		{
			Reader << q;
		}
		else
		{
			q.X = q.Y = q.Z = 0;
			float v;
			Reader << v;
			(&q.X)[keyMode - 1] = v;
		}
		FQuat q2 = q;		// conversion
		// Now decode RLE table
		if ((nextRLE_index < RLETable.Num()) && (RLETable[nextRLE_index] == numAddedKeys))
		{
			int numSameKeys = RLETable[nextRLE_index+1] - RLETable[nextRLE_index] + 1;
			for (int i = 0; i < numSameKeys; i++)
			{
				A->KeyQuat.Add(CVT(q2));
			}
			nextRLE_index += 2;
			numAddedKeys += numSameKeys;
		}
		else
		{
			A->KeyQuat.Add(CVT(q2));
			numAddedKeys++;
		}
	}

	unguard;
}

#endif // BLADENSOUL

void UAnimSet::ConvertAnims()
{
	guard(UAnimSet::ConvertAnims);

	int i, j;

	CAnimSet *AnimSet = new CAnimSet(this);
	ConvertedAnim = AnimSet;

	int ArVer  = GetArVer();
	int ArGame = GetGame();

#if MASSEFF
	UBioAnimSetData *BioData = NULL;
	if ((ArGame >= GAME_MassEffect && ArGame <= GAME_MassEffectLE) && !TrackBoneNames.Num() && Sequences.Num())
	{
		// Mass Effect has separated TrackBoneNames from UAnimSet to UBioAnimSetData
		BioData = Sequences[0]->m_pBioAnimSetData;
		if (BioData)
		{
			bAnimRotationOnly = BioData->bAnimRotationOnly;
			CopyArray(TrackBoneNames, BioData->TrackBoneNames);
			CopyArray(UseTranslationBoneNames, BioData->UseTranslationBoneNames);
		}
	}
#endif // MASSEFF

	if (!TrackBoneNames.Num())
	{
		// in Mass Effect 2 it is possible that BioAnimSetData placed in other package which is missing (for umodel),
		// so m_pBioAnimSetData would be NULL and we will get the following error
		appPrintf("WARNING: AnimSet %s has %d sequences, but empty TrackBoneNames\n", Name, Sequences.Num());
		return;
	}
	CopyArray(AnimSet->TrackBoneNames, TrackBoneNames);

#if FIND_HOLES
	bool findHoles = true;
#endif
	int NumTracks = TrackBoneNames.Num();

	if (UseTranslationBoneNames.Num() || ForceMeshTranslationBoneNames.Num())
	{
		// Setup animation retargeting
		AnimSet->BoneModes.Init(EBoneRetargetingMode::Mesh, NumTracks);
		if (UseTranslationBoneNames.Num() && bAnimRotationOnly)
		{
			for (i = 0; i < UseTranslationBoneNames.Num(); i++)
			{
				for (j = 0; j < TrackBoneNames.Num(); j++)
					if (UseTranslationBoneNames[i] == TrackBoneNames[j])
						AnimSet->BoneModes[j] = EBoneRetargetingMode::Animation;
			}
		}
		if (ForceMeshTranslationBoneNames.Num())
		{
			// This array overrides bones set as "EBoneRetargetingMode::Animation" to use "Mesh" mode again.
			// We're no longer storing this array in CAnimSet separately. Probably it is not good (for UE3 games),
			// because in UE3 it was possible to set up AnimRotationOnly per mesh, or from AnimTree, so this
			// setting wasn't global.
			for (i = 0; i < ForceMeshTranslationBoneNames.Num(); i++)
			{
				for (j = 0; j < TrackBoneNames.Num(); j++)
					if (ForceMeshTranslationBoneNames[i] == TrackBoneNames[j])
						AnimSet->BoneModes[j] = EBoneRetargetingMode::Mesh;
			}
		}
	}

	DBG("----------- AnimSet %s: %d seq, %d bones -----------\n", Name, Sequences.Num(), TrackBoneNames.Num());

	for (i = 0; i < Sequences.Num(); i++)
	{
		const UAnimSequence *Seq = Sequences[i];
		if (!Seq)
		{
			appPrintf("WARNING: %s: no sequence %d\n", Name, i);
			continue;
		}
#if DEBUG_DECOMPRESS
		appPrintf("Sequence %d (%s, %s):%s %d bones, %d offsets (%g per bone), %d frames, %d compressed data\n"
			   "          trans %s, rot %s, key %s\n",
			i, *Seq->SequenceName, Seq->Name,
			Seq->bIsAdditive ? " [additive]" : "",
			NumTracks, Seq->CompressedTrackOffsets.Num(), Seq->CompressedTrackOffsets.Num() / (float)NumTracks,
			Seq->NumFrames,
			Seq->CompressedByteStream.Num(),
			EnumToName(Seq->TranslationCompressionFormat),
			EnumToName(Seq->RotationCompressionFormat),
			EnumToName(Seq->KeyEncodingFormat)
		);
	#if TRANSFORMERS
		if (ArGame == GAME_Transformers && Seq->Trans3Data.Num()) goto no_track_details;
	#endif
		for (int i2 = 0; i2 < Seq->CompressedTrackOffsets.Num(); /*empty*/)
		{
			if (Seq->KeyEncodingFormat != AKF_PerTrackCompression)
			{
				int TransOffset = Seq->CompressedTrackOffsets[i2  ];
				int TransKeys   = Seq->CompressedTrackOffsets[i2+1];
				int RotOffset   = Seq->CompressedTrackOffsets[i2+2];
				int RotKeys     = Seq->CompressedTrackOffsets[i2+3];
				appPrintf("    [%d] = trans %d[%d] rot %d[%d] - %s\n", i2/4,
					TransOffset, TransKeys, RotOffset, RotKeys, *TrackBoneNames[i2/4]
				);
				i2 += 4;
			}
			else
			{
				int TransOffset = Seq->CompressedTrackOffsets[i2  ];
				int RotOffset   = Seq->CompressedTrackOffsets[i2+1];
				appPrintf("    [%d] = trans %d rot %d - %s\n", i2/2,
					TransOffset, RotOffset, *TrackBoneNames[i2/2]
				);
				i2 += 2;
			}
		}
	no_track_details: ;
#endif // DEBUG_DECOMPRESS
#if TRANSFORMERS
		if (ArGame == GAME_Transformers && Seq->Trans3Data.Num())
		{
			CAnimSequence *Dst = new CAnimSequence(Seq);
			Dst->Name      = Seq->SequenceName;
			Dst->NumFrames = Seq->NumFrames;
			Dst->Rate      = Seq->NumFrames / Seq->SequenceLength * Seq->RateScale;
			Dst->bAdditive = Seq->bIsAdditive;

			if (Seq->DecodeTrans3Anims(Dst, this))
			{
				AnimSet->Sequences.Add(Dst);
			}
			else
			{
				// Failed to decode, drop the track
				delete Dst;
			}
			continue;
		}
#endif // TRANSFORMERS
#if MASSEFF
		if (Seq->m_pBioAnimSetData != BioData)
		{
			appNotify("Mass Effect AnimSequence %s/%s has different BioAnimSetData object, removing track",
				Name, *Seq->SequenceName);
			continue;
		}
#endif // MASSEFF
#if BATMAN
		if (ArGame >= GAME_Batman2 && ArGame <= GAME_Batman4 && Seq->AnimZip_Data.Num())
		{
			CAnimSequence *Dst = new CAnimSequence(Seq);
			AnimSet->Sequences.Add(Dst);
			Dst->Name      = Seq->SequenceName;
			Dst->NumFrames = Seq->NumFrames;
			Dst->Rate      = Seq->NumFrames / Seq->SequenceLength * Seq->RateScale;
			Dst->bAdditive = Seq->bIsAdditive;
			Seq->DecodeBatman2Anims(Dst, this);
			continue;
		}
#endif // BATMAN
#if R6VEGAS
		if (ArGame == GAME_R6Vegas2 && Seq->CompressedByteStream.Num())
		{
			CAnimSequence *Dst = new CAnimSequence(Seq);
			Dst->Name      = Seq->SequenceName;
			Dst->NumFrames = Seq->NumFrames;
			Dst->Rate      = Seq->SequenceLength ? Seq->NumFrames / Seq->SequenceLength * Seq->RateScale : 30.0f;
			Dst->bAdditive = Seq->bIsAdditive;

			if (Seq->DecodeR6V2Anims(Dst, this))
			{
				AnimSet->Sequences.Add(Dst);
			}
			else
			{
				delete Dst;
			}
			continue;
		}
#endif // R6VEGAS
#if FIRSTASSAULT
		if (ArGame == GAME_FirstAssault && Seq->CompressedByteStream.Num())
		{
			CAnimSequence *Dst = new CAnimSequence(Seq);
			Dst->Name      = Seq->SequenceName;
			Dst->NumFrames = Seq->NumFrames;
			Dst->Rate      = Seq->SequenceLength ? Seq->NumFrames / Seq->SequenceLength * Seq->RateScale : 30.0f;
			Dst->bAdditive = Seq->bIsAdditive;

			if (Seq->DecodeSWFAAnims(Dst, this))
			{
				AnimSet->Sequences.Add(Dst);
			}
			else
			{
				delete Dst;
			}
			continue;
		}
#endif // FIRSTASSAULT
#if ARMYOF2
		if (ArGame == GAME_ArmyOf2 && Seq->AO2CompressedTrackInfo.Num() == NumTracks * 6)
		{
			CAnimSequence *Dst = new CAnimSequence(Seq);
			Dst->Name      = Seq->SequenceName;
			Dst->NumFrames = Seq->NumFrames;
			Dst->Rate      = Seq->SequenceLength ? Seq->NumFrames / Seq->SequenceLength * Seq->RateScale : 30.0f;
			Dst->bAdditive = Seq->bIsAdditive;

			if (Seq->DecodeAO2Anims(Dst, this))
			{
				AnimSet->Sequences.Add(Dst);
				if (getenv("AO2_ANIM_DEBUG"))
					appPrintf("AO2 AnimSequence %s/%s: decoded %d track refs, %d constant bytes, %d animated bytes\n",
						Name, *Seq->SequenceName, Seq->AO2CompressedTrackInfo.Num(), Seq->CompressedByteStream.Num(), Seq->AO2CompressedAnimData.Num());
			}
			else
			{
				delete Dst;
			}
			continue;
		}
#endif // ARMYOF2
		// some checks
		int offsetsPerBone = 4;
		if (Seq->KeyEncodingFormat == AKF_PerTrackCompression)
			offsetsPerBone = 2;
#if TLR
		if (ArGame == GAME_TLR) offsetsPerBone = 6;
#endif
#if XMEN
		if (ArGame == GAME_XMen) offsetsPerBone = 6;		// has additional CutInfo array
#endif
		if (Seq->CompressedTrackOffsets.Num() != NumTracks * offsetsPerBone && !Seq->RawAnimData.Num())
		{
			appNotify("AnimSequence %s/%s has wrong CompressedTrackOffsets size (has %d, expected %d), removing track",
				Name, *Seq->SequenceName, Seq->CompressedTrackOffsets.Num(), NumTracks * offsetsPerBone);
			continue;
		}

		// create CAnimSequence
		CAnimSequence *Dst = new CAnimSequence(Seq);
		AnimSet->Sequences.Add(Dst);
		Dst->Name      = Seq->SequenceName;
		Dst->NumFrames = Seq->NumFrames;
		Dst->Rate      = Seq->NumFrames / Seq->SequenceLength * Seq->RateScale;
		Dst->bAdditive = Seq->bIsAdditive;

		// bone tracks ...
		Dst->Tracks.Empty(NumTracks);

		// There could be an animation consisting of only trans with offsets == -1, what means
		// use of RefPose. In this case there's no point adding the animation to AnimSet. We'll
		// create FMemReader even for empty CompressedByteStream, otherwise it would be hard to
		// create a valid CAnimSequence which won't crash animation export.
		FMemReader Reader(
			Seq->CompressedByteStream.Num() ? Seq->CompressedByteStream.GetData() : (const uint8*)"",
			Seq->CompressedByteStream.Num());
		Reader.SetupFrom(*Package);

		bool HasTimeTracks = (Seq->KeyEncodingFormat == AKF_VariableKeyLerp);

		int offsetIndex = 0;
		for (j = 0; j < NumTracks; j++, offsetIndex += offsetsPerBone)
		{
			CAnimTrack *A = new CAnimTrack;
			Dst->Tracks.Add(A);

			int k;

			if (!Seq->CompressedTrackOffsets.Num())	//?? or if RawAnimData.Num() != 0
			{
				// using RawAnimData array
				assert(Seq->RawAnimData.Num() == NumTracks);
				CopyArray(A->KeyPos,  CVT(Seq->RawAnimData[j].PosKeys));
				CopyArray(A->KeyQuat, CVT(Seq->RawAnimData[j].RotKeys));
				CopyArray(A->KeyTime, Seq->RawAnimData[j].KeyTimes);	// may be empty
				for (int k = 0; k < A->KeyTime.Num(); k++)
					A->KeyTime[k] *= Dst->Rate;
				continue;
			}

			FVector Mins, Ranges;	// common ...
			static const CVec3 nullVec  = { 0, 0, 0 };
			static const CQuat nullQuat = { 0, 0, 0, 1 };

			//----------------------------------------------
			// decode AKF_PerTrackCompression data
			//----------------------------------------------
			if (Seq->KeyEncodingFormat == AKF_PerTrackCompression)
			{
				// this format uses different key storage
				guard(PerTrackCompression);
				assert(Seq->TranslationCompressionFormat == ACF_Identity);
				assert(Seq->RotationCompressionFormat == ACF_Identity);

				int TransOffset = Seq->CompressedTrackOffsets[offsetIndex  ];
				int RotOffset   = Seq->CompressedTrackOffsets[offsetIndex+1];

				uint32 PackedInfo;
				AnimationCompressionFormat KeyFormat;
				int ComponentMask;
				int NumKeys;

#define DECODE_PER_TRACK_INFO(info)										\
				KeyFormat = (AnimationCompressionFormat)(info >> 28);	\
				ComponentMask = (info >> 24) & 0xF;						\
				NumKeys       = info & 0xFFFFFF;						\
				HasTimeTracks = (ComponentMask & 8) != 0;

				guard(TransKeys);
				// read translation keys
				if (TransOffset == -1)
				{
					A->KeyPos.Add(nullVec);
					DBG("    [%d] no translation data\n", j);
				}
				else
				{
					Reader.Seek(TransOffset);
					Reader << PackedInfo;
					DECODE_PER_TRACK_INFO(PackedInfo);
					A->KeyPos.Empty(NumKeys);
					DBG("    [%d] trans: fmt=%d (%s), %d keys, mask %d\n", j,
						KeyFormat, EnumToName(KeyFormat), NumKeys, ComponentMask
					);
					if (KeyFormat == ACF_IntervalFixed32NoW)
					{
						// read mins/maxs
						Mins.Set(0, 0, 0);
						Ranges.Set(0, 0, 0);
						if (ComponentMask & 1) Reader << Mins.X << Ranges.X;
						if (ComponentMask & 2) Reader << Mins.Y << Ranges.Y;
						if (ComponentMask & 4) Reader << Mins.Z << Ranges.Z;
					}
					for (k = 0; k < NumKeys; k++)
					{
						switch (KeyFormat)
						{
//						case ACF_None:
						case ACF_Float96NoW:
							{
								FVector v;
								if (ComponentMask & 7)
								{
									v.Set(0, 0, 0);
									if (ComponentMask & 1) Reader << v.X;
									if (ComponentMask & 2) Reader << v.Y;
									if (ComponentMask & 4) Reader << v.Z;
								}
								else
								{
									// ACF_Float96NoW has a special case for ((ComponentMask & 7) == 0)
									Reader << v;
								}
								A->KeyPos.Add(CVT(v));
							}
							break;
						TPR(ACF_IntervalFixed32NoW, FVectorIntervalFixed32)
						case ACF_Fixed48NoW:
							{
								uint16 X, Y, Z;
								CVec3 v;
								v.Set(0, 0, 0);
								if (ComponentMask & 1)
								{
									Reader << X; v[0] = DecodeFixed48_PerTrackComponent<7>(X);
								}
								if (ComponentMask & 2)
								{
									Reader << Y; v[1] = DecodeFixed48_PerTrackComponent<7>(Y);
								}
								if (ComponentMask & 4)
								{
									Reader << Z; v[2] = DecodeFixed48_PerTrackComponent<7>(Z);
								}
								A->KeyPos.Add(v);
							}
							break;
						case ACF_Identity:
							A->KeyPos.Add(nullVec);
							break;
						default:
							appError("Unknown translation compression method: %d (%s)", KeyFormat, EnumToName(KeyFormat));
						}
					}
					// align to 4 bytes
					Reader.Seek(Align(Reader.Tell(), 4));
					if (HasTimeTracks)
						ReadTimeArray(Reader, NumKeys, A->KeyPosTime, Seq->NumFrames);
				}
				unguard;

				guard(RotKeys);
				// read rotation keys
				if (RotOffset == -1)
				{
					A->KeyQuat.Add(nullQuat);
					DBG("    [%d] no rotation data\n", j);
				}
				else
				{
					Reader.Seek(RotOffset);
					Reader << PackedInfo;
					DECODE_PER_TRACK_INFO(PackedInfo);
#if BORDERLANDS
					if (ArGame == GAME_Borderlands || ArGame == GAME_AliensCM)	// Borderlands 2
					{
						// this game has more different key formats; each described by number. which
						// could differ from numbers in UnMesh3.h; so, transcode format
						switch (KeyFormat)
						{
						case 6:  KeyFormat = ACF_Delta40NoW; break; // not used
						case 7:  KeyFormat = ACF_Delta48NoW; break; // not used
						case 8:  KeyFormat = ACF_Identity;   break;
						case 9:  KeyFormat = ACF_PolarEncoded32; break;
						case 10: KeyFormat = ACF_PolarEncoded48; break;
						}
					}
#endif // BORDERLANDS
					A->KeyQuat.Empty(NumKeys);
					DBG("    [%d] rot  : fmt=%d (%s), %d keys, mask %d\n", j,
						KeyFormat, EnumToName(KeyFormat), NumKeys, ComponentMask
					);
					if (KeyFormat == ACF_IntervalFixed32NoW)
					{
						// read mins/maxs
						Mins.Set(0, 0, 0);
						Ranges.Set(0, 0, 0);
						if (ComponentMask & 1) Reader << Mins.X << Ranges.X;
						if (ComponentMask & 2) Reader << Mins.Y << Ranges.Y;
						if (ComponentMask & 4) Reader << Mins.Z << Ranges.Z;
					}
					for (k = 0; k < NumKeys; k++)
					{
						switch (KeyFormat)
						{
//						TR (ACF_None, FQuat)
						case ACF_Float96NoW:
							{
								FQuatFloat96NoW q;
								Reader << q;
								FQuat q2 = q;				// convert
								A->KeyQuat.Add(CVT(q2));
							}
							break;
						case ACF_Fixed48NoW:
							{
								FQuatFixed48NoW q;
								q.X = q.Y = q.Z = 32767;	// corresponds to 0
								if (ComponentMask & 1) Reader << q.X;
								if (ComponentMask & 2) Reader << q.Y;
								if (ComponentMask & 4) Reader << q.Z;
								FQuat q2 = q;				// convert
								A->KeyQuat.Add(CVT(q2));
							}
							break;
						TR (ACF_Fixed32NoW, FQuatFixed32NoW)
						TRR(ACF_IntervalFixed32NoW, FQuatIntervalFixed32NoW)
						TR (ACF_Float32NoW, FQuatFloat32NoW)
#if BORDERLANDS
						TR (ACF_PolarEncoded32, FQuatPolarEncoded32)
						TR (ACF_PolarEncoded48, FQuatPolarEncoded48)
#endif // BORDERLANDS
						case ACF_Identity:
							A->KeyQuat.Add(nullQuat);
							break;
						default:
							appError("Unknown rotation compression method: %d (%s)", KeyFormat, EnumToName(KeyFormat));
						}
					}
					// align to 4 bytes
					Reader.Seek(Align(Reader.Tell(), 4));
					if (HasTimeTracks)
						ReadTimeArray(Reader, NumKeys, A->KeyQuatTime, Seq->NumFrames);
				}
				unguard;

				unguard;
				continue;
				// end of AKF_PerTrackCompression block ...
			}

			//----------------------------------------------
			// end of AKF_PerTrackCompression decoder
			//----------------------------------------------

			// read animations
			int TransOffset = Seq->CompressedTrackOffsets[offsetIndex  ];
			int TransKeys   = Seq->CompressedTrackOffsets[offsetIndex+1];
			int RotOffset   = Seq->CompressedTrackOffsets[offsetIndex+2];
			int RotKeys     = Seq->CompressedTrackOffsets[offsetIndex+3];
#if TLR
			int ScaleOffset = 0, ScaleKeys = 0;
			if (ArGame == GAME_TLR)
			{
				ScaleOffset  = Seq->CompressedTrackOffsets[offsetIndex+4];
				ScaleKeys    = Seq->CompressedTrackOffsets[offsetIndex+5];
			}
#endif // TLR
//			appPrintf("[%d:%d:%d] :  %d[%d]  %d[%d]  %d[%d]\n", j, Seq->RotationCompressionFormat, Seq->TranslationCompressionFormat, TransOffset, TransKeys, RotOffset, RotKeys, ScaleOffset, ScaleKeys);

			A->KeyPos.Empty(TransKeys);
			A->KeyQuat.Empty(RotKeys);

			// read translation keys
			if (TransKeys)
			{
#if FIND_HOLES
				int hole = TransOffset - Reader.Tell();
				if (findHoles && hole/** && abs(hole) > 4*/)	//?? should not be holes at all
				{
					appNotify("AnimSet:%s Seq:%s [%d] hole (%d) before TransTrack (KeyFormat=%d/%d)",
						Name, *Seq->SequenceName, j, hole, Seq->KeyEncodingFormat, Seq->TranslationCompressionFormat);
///					findHoles = false;
				}
#endif // FIND_HOLES
				Reader.Seek(TransOffset);
				AnimationCompressionFormat TranslationCompressionFormat = Seq->TranslationCompressionFormat;
#if ARGONAUTS
				if (ArGame == GAME_Argonauts) goto do_not_override_trans_format;
#endif
				if (TransKeys == 1)
					TranslationCompressionFormat = ACF_None;	// single key is stored without compression
			do_not_override_trans_format:
				// read mins/ranges
				if (TranslationCompressionFormat == ACF_IntervalFixed32NoW)
				{
					assert(ArVer >= 761);
					Reader << Mins << Ranges;
				}
#if BORDERLANDS
				FVector Base;
				if (ArGame == GAME_Borderlands && (TranslationCompressionFormat == ACF_Delta40NoW || TranslationCompressionFormat == ACF_Delta48NoW))
				{
					Reader << Mins << Ranges << Base;
				}
#endif // BORDERLANDS

#if TRANSFORMERS
				if (ArGame == GAME_Transformers && TransKeys >= 4 && GetLicenseeVer() >= 100)
				{
					FVector Scale, Offset;
					Reader << Scale.X;
					if (Scale.X != -1)
					{
						Reader << Scale.Y << Scale.Z << Offset;
//						appPrintf("  trans: %g %g %g -- %g %g %g\n", VECTOR_ARG(Offset), VECTOR_ARG(Scale));
						for (k = 0; k < TransKeys; k++)
						{
							FPackedVector_Trans pos;
							Reader << pos;
							FVector pos2 = pos.ToVector(Offset, Scale); // convert
							A->KeyPos.Add(CVT(pos2));
						}
						goto trans_keys_done;
					} // else - original code with 4-byte overhead
				} // else - original code for uncompressed vector
#endif // TRANSFORMERS

				for (k = 0; k < TransKeys; k++)
				{
					switch (TranslationCompressionFormat)
					{
					TP (ACF_None,               FVector)
					TP (ACF_Float96NoW,         FVector)
					TPR(ACF_IntervalFixed32NoW, FVectorIntervalFixed32)
					TP (ACF_Fixed48NoW,         FVectorFixed48)
					case ACF_Identity:
						A->KeyPos.Add(nullVec);
						break;
#if BORDERLANDS
					case ACF_Delta48NoW:
						{
							if (k == 0)
							{
								// "Base" works as 1st key
								A->KeyPos.Add(CVT(Base));
								continue;
							}
							FVectorDelta48NoW V;
							Reader << V;
							FVector V2;
							V2 = V.ToVector(Mins, Ranges, Base);
							Base = V2;			// for delta
							A->KeyPos.Add(CVT(V2));
						}
						break;
#endif // BORDERLANDS
#if ARGONAUTS
					case ATCF_Float16:
						{
							uint16 x, y, z;
							Reader << x << y << z;
							FVector v;
							v.X = half2float(x) / 2;	// Argonauts has "half" with biased exponent, so fix it with division by 2
							v.Y = half2float(y) / 2;
							v.Z = half2float(z) / 2;
							A->KeyPos.Add(CVT(v));
						}
						break;
#endif // ARGONAUTS
					default:
						appError("Unknown translation compression method: %d (%s)", TranslationCompressionFormat, EnumToName(TranslationCompressionFormat));
					}
				}

			trans_keys_done:
				// align to 4 bytes
				Reader.Seek(Align(Reader.Tell(), 4));
				if (HasTimeTracks)
					ReadTimeArray(Reader, TransKeys, A->KeyPosTime, Seq->NumFrames);
			}
			else
			{
//				A->KeyPos.Add(nullVec);
//				appNotify("No translation keys!");
			}

#if DEBUG_DECOMPRESS
			int TransEnd = Reader.Tell();
#endif
#if FIND_HOLES
			int hole = RotOffset - Reader.Tell();
			if (findHoles && hole/** && abs(hole) > 4*/)	//?? should not be holes at all
			{
				appNotify("AnimSet:%s Seq:%s [%d] hole (%d) before RotTrack (KeyFormat=%d/%d)",
					Name, *Seq->SequenceName, j, hole, Seq->KeyEncodingFormat, Seq->RotationCompressionFormat);
///				findHoles = false;
			}
#endif // FIND_HOLES
			// read rotation keys
			Reader.Seek(RotOffset);
			AnimationCompressionFormat RotationCompressionFormat = Seq->RotationCompressionFormat;
			if (RotKeys <= 0)
				goto rot_keys_done;
			if (RotKeys == 1)
			{
				RotationCompressionFormat = ACF_Float96NoW;	// single key is stored without compression
			}
			else if (RotationCompressionFormat == ACF_IntervalFixed32NoW || ArVer < 761)
			{
#if SHADOWS_DAMNED
				if (ArGame == GAME_ShadowsDamned) goto skip_ranges;
#endif
				// starting with version 761 Mins/Ranges are read only when needed - i.e. for ACF_IntervalFixed32NoW
				Reader << Mins << Ranges;
			skip_ranges: ;
			}
#if BORDERLANDS
			FQuat Base;
			if (ArGame == GAME_Borderlands && (RotationCompressionFormat == ACF_Delta40NoW || RotationCompressionFormat == ACF_Delta48NoW))
			{
				Reader << Base;			// in addition to Mins and Ranges
			}
#endif // BORDERLANDS
#if TRANSFORMERS
			FQuat TransQuatBase;
			if (ArGame == GAME_Transformers && RotKeys >= 2)
				Reader << TransQuatBase;
#endif // TRANSFORMERS
#if BLADENSOUL
			if (ArGame == GAME_BladeNSoul && RotationCompressionFormat == ACF_ZOnlyRLE)
			{
				ReadBnS_ZOnlyRLE(Reader, RotKeys, A);
				goto rot_keys_done;
			}
#endif // BLADENSOUL

			for (k = 0; k < RotKeys; k++)
			{
				switch (RotationCompressionFormat)
				{
				TR (ACF_None, FQuat)
				TR (ACF_Float96NoW, FQuatFloat96NoW)
				TR (ACF_Fixed48NoW, FQuatFixed48NoW)
				TR (ACF_Fixed32NoW, FQuatFixed32NoW)
				TRR(ACF_IntervalFixed32NoW, FQuatIntervalFixed32NoW)
				TR (ACF_Float32NoW, FQuatFloat32NoW)
				case ACF_Identity:
					A->KeyQuat.Add(nullQuat);
					break;
#if BATMAN
				TR (ACF_Fixed48Max, FQuatFixed48Max)
#endif
#if MASSEFF
				TR (ACF_BioFixed48, FQuatBioFixed48)	// Mass Effect 2 animation compression
#endif
#if BORDERLANDS
				case ACF_Delta48NoW:
					{
						if (k == 0)
						{
							// "Base" works as 1st key
							A->KeyQuat.Add(CVT(Base));
							continue;
						}
						FQuatDelta48NoW q;
						Reader << q;
						FQuat q2;
						q2 = q.ToQuat(Mins, Ranges, Base);
						Base = q2;			// for delta
						A->KeyQuat.Add(CVT(q2));
					}
					break;
				TR (ACF_PolarEncoded32, FQuatPolarEncoded32)
				TR (ACF_PolarEncoded48, FQuatPolarEncoded48)
#endif // BORDERLANDS
#if TRANSFORMERS || ARGONAUTS
				case ACF_IntervalFixed48NoW:
	#if TRANSFORMERS
					if (ArGame == GAME_Transformers)
					{
						FQuatIntervalFixed48NoW_Trans q;
						FQuat q2;
						Reader << q;
						q2 = q.ToQuat(Mins, Ranges);
						A->KeyQuat.Add(CVT(q2));
					}
	#endif
	#if ARGONAUTS
					if (ArGame == GAME_Argonauts)
					{
						FQuatIntervalFixed48NoW_Argo q;
						FQuat q2;
						Reader << q;
						q2 = q.ToQuat(Mins, Ranges);
						A->KeyQuat.Add(CVT(q2));
					}
	#endif // ARGONAUTS
					break;
#endif // TRANSFORMERS || ARGONAUTS
#if ARGONAUTS
				TR (ACF_Fixed64NoW, FQuatFixed64NoW_Argo)
				TR (ACF_Float48NoW, FQuatFloat48NoW_Argo)
#endif // ARGONAUTS
				default:
					appError("Unknown rotation compression method: %d (%s)", RotationCompressionFormat, EnumToName(RotationCompressionFormat));
				}
			}

#if TRANSFORMERS
			if (ArGame == GAME_Transformers && RotKeys >= 2 &&
				(RotationCompressionFormat == ACF_IntervalFixed32NoW || RotationCompressionFormat == ACF_IntervalFixed48NoW))
			{
				for (int i = 0; i < RotKeys; i++)
				{
					CQuat q = A->KeyQuat[i];
					q.Mul(CVT(TransQuatBase));
					A->KeyQuat[i] = q;
				}
			}
#endif // TRANSFORMERS

		rot_keys_done:
			// align to 4 bytes
			Reader.Seek(Align(Reader.Tell(), 4));
			if (HasTimeTracks)
				ReadTimeArray(Reader, RotKeys, A->KeyQuatTime, Seq->NumFrames);

#if TLR
			if (ScaleKeys)
			{
				// no ScaleKeys support, simply drop data
				Reader.Seek(ScaleOffset + ScaleKeys * 12);
				Reader.Seek(Align(Reader.Tell(), 4));
			}
#endif // TLR

#if ARGONAUTS
			if (ArGame == GAME_Argonauts && Seq->CompressedTrackTimeOffsets.Num())
			{
				// convert time tracks
				ReadArgonautsTimeArray(Seq->CompressedTrackTimes, Seq->CompressedTrackTimeOffsets[j*2  ], TransKeys, A->KeyPosTime,  Seq->NumFrames);
				ReadArgonautsTimeArray(Seq->CompressedTrackTimes, Seq->CompressedTrackTimeOffsets[j*2+1], RotKeys,   A->KeyQuatTime, Seq->NumFrames);
			}
#endif // ARGONAUTS

#if DEBUG_DECOMPRESS
//			appPrintf("[%s : %s] Frames=%d KeyPos.Num=%d KeyQuat.Num=%d KeyFmt=%s\n", *Seq->SequenceName, *TrackBoneNames[j],
//				Seq->NumFrames, A->KeyPos.Num(), A->KeyQuat.Num(), *Seq->KeyEncodingFormat);
			appPrintf("  ->[%d]: t %d .. %d + r %d .. %d (%d/%d keys)\n", j,
				TransOffset, TransEnd, RotOffset, Reader.Tell(), TransKeys, RotKeys);
#endif // DEBUG_DECOMPRESS
		}
	}

	unguard;
}


#if MASSEFF

void UBioAnimSetData::PostLoad()
{
	TArray<UAnimSequence*> LinkedSequences;

	for (int i = 0; i < Package->Summary.ExportCount; i++)
	{
		FObjectExport &Exp = Package->ExportTable[i];
		UObject *Obj = Exp.Object;
		if (!Obj) continue;

		if (Obj->IsA("AnimSet"))
		{
			UAnimSet *Set = static_cast<UAnimSet*>(Obj);
			if (Set->m_pBioAnimSetData == this)
				return;					// this UBioAnimSetData already has
		}
		else if (Obj->IsA("AnimSequence"))
		{
			UAnimSequence *Seq = static_cast<UAnimSequence*>(Obj);
			if (Seq->m_pBioAnimSetData == this)
				LinkedSequences.Add(Seq);
		}
	}

	if (!LinkedSequences.Num()) return;	// there is no UAnimSequence for this UBioAnimSetData

	// generate UAnimSet
	char AnimSetName[256];
	strcpy(AnimSetName, Name);
	int len = strlen(AnimSetName);
	if (len > 15 && !strcmp(AnimSetName + len - 15, "_BioAnimSetData"))	// truncate "_BioAnimSetData" suffix
		AnimSetName[len - 15] = 0;
	appPrintf("Generating AnimSet %s (%d sequences)\n", AnimSetName, LinkedSequences.Num());
	UAnimSet *AnimSet = static_cast<UAnimSet*>(CreateClass("AnimSet"));
	AnimSet->Name              = appStrdupPool(AnimSetName);
	AnimSet->Package           = Package;
	AnimSet->m_pBioAnimSetData = this;
	CopyArray(AnimSet->Sequences, LinkedSequences);

	AnimSet->PostLoad();
}

#endif // MASSEFF

void UAnimSet::GetMetadata(FArchive& Ar) const
{
	guard(UAnimSet::GetMetadata);

	int NumAnims = ConvertedAnim ? ConvertedAnim->Sequences.Num() : 0;
	Ar << NumAnims;

	for (int i = 0; i < NumAnims; i++)
	{
		CAnimSequence* Seq = ConvertedAnim->Sequences[i];
		Ar << Seq->NumFrames << Seq->Name;
	}

	unguard;
}

#endif // UNREAL3
