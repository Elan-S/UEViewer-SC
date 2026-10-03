#include "Core.h"

#if UNREAL3

#include "UnCore.h"
#include "UnObject.h"
#include "UnMesh3.h"
#include "UnMeshTypes.h"
#include "UnrealPackage/UnPackage.h"	// for checking game type

#include "Mesh/SkeletalMesh.h"
#include "TypeConvert.h"
#if ARMYOF2
#include "UnAnimAO2.h"
#endif


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
	if (Ar.Game == GAME_ArmyOf2 && AO2CompressionInfo.NumTracks > 0)
	{
		// UAnimSequence::Serialize (0x82824EC8) and 0x8248F8E8:
		// native Blocks has no array count; the tagged metadata gives its size.
		FAO2CompressedAnimationInfo& Info = AO2CompressionInfo;
		if (Info.Version != 2 || Info.BlockType < 0 || Info.BlockType > 1 ||
			Info.NumDynamicDofs < 0 || Info.FramesPerBlock <= 0 || Info.NumBlocks < 0)
			appError("ArmyOfTwo %s: invalid or unsupported compression metadata", Name);
		const int Remaining = Ar.GetStopper() - Ar.Tell();
		const int DataSize = AO2BlockDataSize(Info.NumDynamicDofs, Info.FramesPerBlock, Info.NumBlocks, Info.BlockType);
		if (DataSize < 0 || Remaining < 4 || DataSize > Remaining - 4)
			appError("ArmyOfTwo %s: compressed blocks exceed animation data", Name);
		Info.Blocks.Empty(DataSize);
		Info.Blocks.AddUninitialized(DataSize);
		if (DataSize) Ar.Serialize(Info.Blocks.GetData(), DataSize);
	}
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
	unguard;
}

#if ARMYOF2

bool UAnimSequence::DecodeAO2Anims(CAnimSequence *Dst, UAnimSet *Owner) const
{
	guard(UAnimSequence::DecodeAO2Anims);
	const FAO2CompressedAnimationInfo& Info = AO2CompressionInfo;
	const int NumTracks = Owner->TrackBoneNames.Num();
	if (Info.Version != 2 || NumFrames <= 0 || Info.NumTracks != NumTracks ||
		int64(Info.EncodedDofDataIndices.Num()) != int64(NumTracks) * 6 ||
		Info.NumDynamicDofs < 0 || Info.DynamicDofCompressedDatumTypes.Num() != Info.NumDynamicDofs ||
		Info.FramesPerBlock <= 0 || Info.NumBlocks <= 0 ||
		Info.NumBlocks != (int64(NumFrames) + Info.FramesPerBlock - 1) / Info.FramesPerBlock ||
		Info.BlockType < 0 || Info.BlockType > 1)
	{
		appNotify("ArmyOfTwo %s/%s: invalid or unsupported compression metadata; skipping animation",
			Owner->Name, *SequenceName);
		return false;
	}
	const int SampleBytes = Info.BlockType == 0 ? 4 : 2;
	if (AO2BlockDataSize(Info.NumDynamicDofs, Info.FramesPerBlock, Info.NumBlocks, Info.BlockType) != Info.Blocks.Num())
	{
		appNotify("ArmyOfTwo %s/%s: incorrect compressed block size; skipping animation", Owner->Name, *SequenceName);
		return false;
	}
	for (int i = 0; i < Info.NumDynamicDofs; i++)
	{
		const byte Type = Info.DynamicDofCompressedDatumTypes[i];
		if ((Info.BlockType == 0 && Type != 0) || (Info.BlockType == 1 && Type != 1 && Type != 2))
		{
			appNotify("ArmyOfTwo %s/%s: unsupported sample type %d in block type %d", Owner->Name, *SequenceName, Type, Info.BlockType);
			return false;
		}
	}
	for (int i = 0; i < Info.EncodedDofDataIndices.Num(); i++)
	{
		const int Ref = Info.EncodedDofDataIndices[i];
		if (Ref >= Info.NumDynamicDofs || (Ref < 0 && -int64(Ref) > Info.StaticDofs.Num()))
		{
			appNotify("ArmyOfTwo %s/%s: invalid component reference %d", Owner->Name, *SequenceName, Ref);
			return false;
		}
	}

	Dst->Tracks.Empty(NumTracks);
	for (int TrackIndex = 0; TrackIndex < NumTracks; TrackIndex++)
	{
		CAnimTrack* Track = new CAnimTrack;
		Dst->Tracks.Add(Track);
		Track->KeyPos.Empty(NumFrames);
		Track->KeyQuat.Empty(NumFrames);
		// Explicit integer times avoid resampling roundoff in PSA export.
		Track->KeyTime.Empty(NumFrames);
		const int* Refs = &Info.EncodedDofDataIndices[TrackIndex * 6];
		for (int Frame = 0; Frame < NumFrames; Frame++)
		{
			float V[6];
			for (int Component = 0; Component < 6; Component++)
			{
				const int Ref = Refs[Component];
				if (Ref < 0)
				{
					V[Component] = Info.StaticDofs[-Ref - 1];
				}
				else
				{
					const int Offset = int((int64(Frame / Info.FramesPerBlock) * Info.NumDynamicDofs + Ref)
						* Info.FramesPerBlock + Frame % Info.FramesPerBlock) * SampleBytes;
					V[Component] = DecodeAO2Sample(Info.Blocks.GetData() + Offset,
						Info.DynamicDofCompressedDatumTypes[Ref], Package->ReverseBytes);
				}
				if (!isfinite(V[Component]))
				{
					appNotify("ArmyOfTwo %s/%s: non-finite animation sample", Owner->Name, *SequenceName);
					return false;
				}
			}
			// 0x827E3478: translation XYZ, then quaternion XYZ with positive W.
			CVec3 Pos;
			Pos.Set(V[0], V[1], V[2]);
			CQuat Q;
			Q.Set(V[3], V[4], V[5], sqrt(max(0.0f, 1.0f - V[3]*V[3] - V[4]*V[4] - V[5]*V[5])));
			// 0x82802130 uses the unquantized root transforms at the endpoints.
			if (TrackIndex == 0 && (Frame == 0 || Frame == NumFrames - 1))
			{
				const FBoneAtomScript& Root = Frame == 0 ? Info.TrackZeroStart : Info.TrackZeroEndNonLooping;
				if (!isfinite(Root.Tx) || !isfinite(Root.Ty) || !isfinite(Root.Tz) ||
					!isfinite(Root.Qx) || !isfinite(Root.Qy) || !isfinite(Root.Qz) || !isfinite(Root.Qw) ||
					Root.Qx*Root.Qx + Root.Qy*Root.Qy + Root.Qz*Root.Qz + Root.Qw*Root.Qw < 0.5f)
				{
					appNotify("ArmyOfTwo %s/%s: invalid root endpoint", Owner->Name, *SequenceName);
					return false;
				}
				Pos.Set(Root.Tx, Root.Ty, Root.Tz);
				Q.Set(Root.Qx, Root.Qy, Root.Qz, Root.Qw);
			}
			Track->KeyPos.Add(Pos);
			Track->KeyQuat.Add(Q);
			Track->KeyTime.Add(float(Frame));
		}
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
		if (ArGame == GAME_ArmyOf2 && Seq->AO2CompressionInfo.NumTracks > 0)
		{
			CAnimSequence *Dst = new CAnimSequence(Seq);
			Dst->Name      = Seq->SequenceName;
			Dst->NumFrames = Seq->NumFrames;
			Dst->Rate      = Seq->SequenceLength ? Seq->NumFrames / Seq->SequenceLength * Seq->RateScale : 30.0f;
			Dst->bAdditive = Seq->bIsAdditive;

			if (Seq->DecodeAO2Anims(Dst, this))
			{
				AnimSet->Sequences.Add(Dst);

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
