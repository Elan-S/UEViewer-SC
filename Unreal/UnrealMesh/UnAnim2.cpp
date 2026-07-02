#include "Core.h"
#include "UnCore.h"
#include "UnObject.h"
#include "UnrealPackage/UnPackage.h"
#include "UnMesh2.h"
#include "UnMeshTypes.h"
#include "GameSpecific/UnUbisoft.h"

#include "Mesh/SkeletalMesh.h"
#include "TypeConvert.h"

#if SPLINTER_CELL
static bool IsSCCTFaceSequenceName(const char *Name);
#endif


/*-----------------------------------------------------------------------------
	UMeshAnimation class
-----------------------------------------------------------------------------*/

UMeshAnimation::~UMeshAnimation()
{
	delete ConvertedAnim;
}


void UMeshAnimation::ConvertAnims()
{
	guard(UMeshAnimation::ConvertAnims);

	int i, j;

	CAnimSet *AnimSet = new CAnimSet(this);
	ConvertedAnim = AnimSet;

	// TrackBoneNames
	int numBones = RefBones.Num();
	AnimSet->TrackBoneNames.AddUninitialized(numBones);
	for (i = 0; i < numBones; i++)
		AnimSet->TrackBoneNames[i] = RefBones[i].Name;

	// Sequences
	int numSeqs = AnimSeqs.Num();
	AnimSet->Sequences.Empty(numSeqs);
	for (i = 0; i < numSeqs; i++)
	{
		CAnimSequence &S = *new CAnimSequence;
		AnimSet->Sequences.Add(&S);

		const FMeshAnimSeq &Src = AnimSeqs[i];
		const MotionChunk  &M   = Moves[i];

		// attributes
		S.Name      = Src.Name;
		S.NumFrames = Src.NumFrames;
		S.Rate      = Src.Rate;
		S.bAdditive = IsSCCTFaceSequenceName(*Src.Name);

		// S.Tracks
		S.Tracks.Empty(numBones);
		for (j = 0; j < numBones; j++)
		{
			CAnimTrack* T = new CAnimTrack;
			S.Tracks.Add(T);

			const AnalogTrack &A = M.AnimTracks[j];
			CopyArray(T->KeyPos,  CVT(A.KeyPos));
			CopyArray(T->KeyQuat, CVT(A.KeyQuat));
			CopyArray(T->KeyTime, A.KeyTime);
			// usually MotionChunk.TrackTime is identical to NumFrames, but some packages exists where
			// time channel should be adjusted
			if (M.TrackTime > 0)
			{
				float TimeScale = Src.NumFrames / M.TrackTime;
				for (int k = 0; k < T->KeyTime.Num(); k++)
					T->KeyTime[k] *= TimeScale;
			}
		}
	}

	unguard;
}


#if SPLINTER_CELL

static bool IsSCCTFaceSequenceName(const char *Name)
{
	return Name && Name[0] == 'F' && Name[1] == 'a' && Name[2] == 'c';
}

static FVector GetSCCTRootMotionScale()
{
	FVector Scale;
	Scale.Set(0.64f, 0.64f, 0.64f);
	const char *Env = getenv("SCCT_ROOT_MOTION_SCALE");
	if (Env && Env[0])
	{
		float X, Y, Z;
		if (sscanf(Env, "%f,%f,%f", &X, &Y, &Z) == 3)
			Scale.Set(X, Y, Z);
		else if (sscanf(Env, "%f", &X) == 1)
			Scale.Set(X, X, X);
	}
	return Scale;
}

// quaternion with 4 16-bit fixed point fields
struct FQuatComp
{
	int16			X, Y, Z, W;				// signed int16, corresponds to float*32767

	inline operator FQuat() const
	{
		FQuat r;
		r.X = X / 32767.0f;
		r.Y = Y / 32767.0f;
		r.Z = Z / 32767.0f;
		r.W = W / 32767.0f;
		return r;
	}
	inline operator FVector() const			// used for FixedPointTrack; may be separated to FVectorComp etc
	{
		FVector r;
		r.X = X / 64.0f;
		r.Y = Y / 64.0f;
		r.Z = Z / 64.0f;
		return r;
	}

	friend FArchive& operator<<(FArchive &Ar, FQuatComp &Q)
	{
		return Ar << Q.X << Q.Y << Q.Z << Q.W;
	}
};

SIMPLE_TYPE(FQuatComp, int16)

// normalized quaternion with 3 16-bit fixed point fields
struct FQuatComp2
{
	int16			X, Y, Z;				// signed int16, corresponds to float*32767

	inline operator FQuat() const
	{
		FQuat r;
		r.X = X / 32767.0f;
		r.Y = Y / 32767.0f;
		r.Z = Z / 32767.0f;
		// check FQuatFloat96NoW ...
		float wSq = 1.0f - (r.X*r.X + r.Y*r.Y + r.Z*r.Z);
		r.W = (wSq > 0) ? sqrt(wSq) : 0;
		return r;
	}

	friend FArchive& operator<<(FArchive &Ar, FQuatComp2 &Q)
	{
		return Ar << Q.X << Q.Y << Q.Z;
	}
};

SIMPLE_TYPE(FQuatComp2, int16)

struct FVectorComp
{
	int16			X, Y, Z;

	inline operator FVector() const
	{
		FVector r;
		r.X = X / 64.0f;
		r.Y = Y / 64.0f;
		r.Z = Z / 64.0f;
		return r;
	}

	friend FArchive& operator<<(FArchive &Ar, FVectorComp &V)
	{
		return Ar << V.X << V.Y << V.Z;
	}
};

SIMPLE_TYPE(FVectorComp, int16)

struct FVectorCompPandora
{
	int16			X, Y, Z;

	inline operator FVector() const
	{
		FVector r;
		r.X = X / 32.0f;
		r.Y = Y / 32.0f;
		r.Z = Z / 32.0f;
		return r;
	}

	friend FArchive& operator<<(FArchive &Ar, FVectorCompPandora &V)
	{
		return Ar << V.X << V.Y << V.Z;
	}
};

SIMPLE_TYPE(FVectorCompPandora, int16)


#define SCELL_TRACK(Name,Quat,Pos,Time)						\
struct Name													\
{															\
	TArray<Quat>			KeyQuat;						\
	TArray<Pos>				KeyPos;							\
	TArray<Time>			KeyTime;						\
															\
	void Decompress(AnalogTrack &D)							\
	{														\
		D.Flags = 0;										\
		CopyArray(D.KeyQuat, KeyQuat);						\
		CopyArray(D.KeyPos,  KeyPos );						\
		CopyArray(D.KeyTime, KeyTime);						\
	}														\
															\
	friend FArchive& operator<<(FArchive &Ar, Name &A)		\
	{														\
		return Ar << A.KeyQuat << A.KeyPos << A.KeyTime;	\
	}														\
};

SCELL_TRACK(FixedPointTrack, FQuatComp,  FQuatComp,   uint16)
SCELL_TRACK(Quat16Track,     FQuatComp2, FVector,     uint16)	// all types are "large"
SCELL_TRACK(FixPosTrack,     FQuatComp2, FVectorComp, uint16)	// "small" KeyPos
SCELL_TRACK(FixTimeTrack,    FQuatComp2, FVector,     uint8)    // "small" KeyTime
SCELL_TRACK(FixPosTimeTrack, FQuatComp2, FVectorComp, uint8)    // "small" KeyPos and KeyTime
SCELL_TRACK(SCCTQuatTrack,     FQuatComp, FVector,     uint16)	// Chaos Theory keeps full int16 XYZW quats
SCELL_TRACK(SCCTFixPosTrack,   FQuatComp, FVectorComp, uint16)
SCELL_TRACK(SCCTFixTimeTrack,  FQuatComp, FVector,     uint8)
SCELL_TRACK(SCCTFixPosTimeTrack, FQuatComp, FVectorComp, uint8)

struct FSC4AnimTrack
{
	unsigned					Flags;
	TArray<FQuatComp>			KeyQuat;
	TArray<FVectorCompPandora>	KeyPos;
	TArray<uint16>				KeyTime;

	void Decompress(AnalogTrack &D)
	{
		D.Flags = Flags;
		CopyArray(D.KeyQuat, KeyQuat);
		CopyArray(D.KeyPos, KeyPos);
		CopyArray(D.KeyTime, KeyTime);
	}

	friend FArchive& operator<<(FArchive &Ar, FSC4AnimTrack &A)
	{
		return Ar << A.Flags << A.KeyQuat << A.KeyPos << A.KeyTime;
	}
};

struct FSC4MotionChunk
{
	FVector						RootSpeed3D;
	float						TrackTime;
	int							StartBone;
	float						UnknownTime;
	TArray<int>					BoneIndices;
	TArray<FSC4AnimTrack>		AnimTracks;
	FSC4AnimTrack				RootTrack;

	friend FArchive& operator<<(FArchive &Ar, FSC4MotionChunk &M)
	{
		return Ar << M.RootSpeed3D << M.TrackTime << M.StartBone << M.UnknownTime
			<< M.BoneIndices << M.AnimTracks << M.RootTrack;
	}
};

struct FSC4AnimNotify
{
	float						Time;
	FName						Function;
	int							NotifyObjIndex;

	friend FArchive& operator<<(FArchive &Ar, FSC4AnimNotify &N)
	{
		return Ar << N.Time << N.Function << AR_INDEX(N.NotifyObjIndex);
	}
};

struct FSC4AnimSeq
{
	float						f28;
	FName						Name;
	TArray<FName>				Groups;
	int							StartFrame;
	int							NumFrames;
	TArray<FSC4AnimNotify>		Notifys;
	float						Rate;

	friend FArchive& operator<<(FArchive &Ar, FSC4AnimSeq &A)
	{
		return Ar << A.f28 << A.Name << A.Groups << A.StartFrame << A.NumFrames << A.Notifys << A.Rate;
	}
};


void AnalogTrack::SerializeSCell(FArchive &Ar)
{
	TArray<FQuatComp> KeyQuat2;
	TArray<uint16>      KeyTime2;
	Ar << KeyQuat2 << KeyPos << KeyTime2;
	// copy with conversion
	CopyArray(KeyQuat, KeyQuat2);
	CopyArray(KeyTime, KeyTime2);
}

void AnalogTrack::SerializePandora(FArchive &Ar)
{
	TArray<FQuatComp> KeyQuat2;
	TArray<FVectorCompPandora> KeyPos2;
	TArray<uint16> KeyTime2;
	Ar << Flags << KeyQuat2 << KeyPos2 << KeyTime2;
	CopyArray(KeyQuat, KeyQuat2);
	CopyArray(KeyPos, KeyPos2);
	CopyArray(KeyTime, KeyTime2);
}

static bool IsPandoraAnimSeqHeader(FArchive &Ar, const UnPackage *Package, int Pos, int ExpectedFrames)
{
	guard(IsPandoraAnimSeqHeader);
	Ar.Seek(Pos + 4);
	int NameIndex, NameExtra, GroupCount;
	Ar << AR_INDEX(NameIndex) << AR_INDEX(NameExtra) << AR_INDEX(GroupCount);
	if (NameIndex <= 0 || unsigned(NameIndex) >= Package->Summary.NameCount)
		return false;
	if (GroupCount < 0 || GroupCount > 4)
		return false;
	for (int GroupIndex = 0; GroupIndex < GroupCount; GroupIndex++)
	{
		int GroupNameIndex, GroupNameExtra;
		Ar << AR_INDEX(GroupNameIndex) << AR_INDEX(GroupNameExtra);
		if (GroupNameIndex < 0 || unsigned(GroupNameIndex) >= Package->Summary.NameCount)
			return false;
	}
	int StartFrame, NumFrames, NotifyCount;
	Ar << StartFrame << NumFrames << AR_INDEX(NotifyCount);
	if (StartFrame < 0 || StartFrame >= 1000)
		return false;
	if (NumFrames <= 0 || NumFrames >= 1000)
		return false;
	if (ExpectedFrames > 0 && abs(NumFrames - ExpectedFrames) > 1)
		return false;
	if (NotifyCount < 0 || NotifyCount >= 100)
		return false;
	return true;
	unguard;
}


struct MotionChunkFixedPoint
{
	FVector					RootSpeed3D;
	float					TrackTime;
	int						StartBone;
	unsigned				Flags;
	TArray<FixedPointTrack>	AnimTracks;

	friend FArchive& operator<<(FArchive &Ar, MotionChunkFixedPoint &M)
	{
		return Ar << M.RootSpeed3D << M.TrackTime << M.StartBone << M.Flags << M.AnimTracks;
	}
};

// Note: standard UE2 MotionChunk is equalent to MotionChunkCompress<AnalogTrack>
struct MotionChunkCompressBase
{
	FVector					RootSpeed3D;
	float					TrackTime;
	int						StartBone;
	unsigned				Flags;
	TArray<int>				BoneIndices;

	virtual void Decompress(MotionChunk &D)
	{
		D.RootSpeed3D = RootSpeed3D;
		D.TrackTime   = TrackTime;
		D.StartBone   = StartBone;
		D.Flags       = Flags;
	}
};

template<class T> struct MotionChunkCompress : public MotionChunkCompressBase
{
	TArray<T>				AnimTracks;
	AnalogTrack				RootTrack;		// standard track

	virtual void Decompress(MotionChunk &D)
	{
		guard(Decompress);
		MotionChunkCompressBase::Decompress(D);
		// copy/convert tracks
		CopyArray(D.BoneIndices, BoneIndices);
		int numAnims = AnimTracks.Num();
		D.AnimTracks.Empty(numAnims);
		D.AnimTracks.AddZeroed(numAnims);
		for (int i = 0; i < numAnims; i++)
			AnimTracks[i].Decompress(D.AnimTracks[i]);
		// RootTrack is unused ...
		unguard;
	}

	friend FArchive& operator<<(FArchive &Ar, MotionChunkCompress &M)
	{
		return Ar << M.RootSpeed3D << M.TrackTime << M.StartBone << M.Flags << M.BoneIndices << M.AnimTracks << M.RootTrack;
	}
};

void UMeshAnimation::SerializeSCell(FArchive &Ar)
{
	guard(SerializeSCell);

	// for logic of track decompression check UMeshAnimation::Moves() function
	int OldCompression = 0, CompressType = 0;
	TArray<MotionChunkFixedPoint>					T0;		// OldCompression!=0, CompressType=0
	TArray<MotionChunkCompress<Quat16Track> >		T1;		// CompressType=1
	TArray<MotionChunkCompress<FixPosTrack> >		T2;		// CompressType=2
	TArray<MotionChunkCompress<FixTimeTrack> >		T3;		// CompressType=3
	TArray<MotionChunkCompress<FixPosTimeTrack> >	T4;		// CompressType=4
	if (Version >= 1000)
	{
		Ar << OldCompression << T0;
		// note: this compression type is not supported (absent BoneIndices in MotionChunkFixedPoint)
	}
	if (Version >= 2000)
	{
		Ar << CompressType << T1 << T2 << T3 << T4;
		// decompress motion
		if (CompressType)
		{
			int i = 0, Count = 1;
			while (i < Count)
			{
				MotionChunkCompressBase *M = NULL;
				switch (CompressType)
				{
				case 1: Count = T1.Num(); M = &T1[i]; break;
				case 2: Count = T2.Num(); M = &T2[i]; break;
				case 3: Count = T3.Num(); M = &T3[i]; break;
				case 4: Count = T4.Num(); M = &T4[i]; break;
				default:
					appError("Unsupported CompressType: %d", CompressType);
				}
				if (!Count)
				{
					appNotify("CompressType=%d with no tracks", CompressType);
					break;
				}
				if (!i)
				{
					// 1st iteration, prepare Moves[] array
					Moves.Empty(Count);
					Moves.AddZeroed(Count);
				}
				// decompress current track
				M->Decompress(Moves[i]);
				// next track
				i++;
			}
		}
	}
//	if (OldCompression) appNotify("OldCompression=%d", OldCompression, CompressType);

	unguard;
}

void UMeshAnimation::SerializePandora(FArchive &Ar)
{
	guard(UMeshAnimation::SerializePandora);

	Ar << Moves;

	int MoveCount = Moves.Num();
	int SeqCount;
	Ar << AR_INDEX(SeqCount);
	AnimSeqs.Empty(MoveCount);
	AnimSeqs.AddZeroed(MoveCount);
	for (int i = 0; i < MoveCount; i++)
	{
		FMeshAnimSeq &Seq = AnimSeqs[i];
		int ExpectedFrames = max(1, appRound(Moves[i].TrackTime));
		int CurrentSeq = Ar.Tell();
		if (!IsPandoraAnimSeqHeader(Ar, Package, CurrentSeq, ExpectedFrames))
		{
			int FoundSeq = 0;
			for (int Pos = CurrentSeq + 1; Pos < Ar.GetStopper() - 16; Pos++)
			{
				if (IsPandoraAnimSeqHeader(Ar, Package, Pos, ExpectedFrames))
				{
					FoundSeq = Pos;
					break;
				}
			}
			if (!FoundSeq)
				break;
			CurrentSeq = FoundSeq;
		}
		Ar.Seek(CurrentSeq);

		float f28;
		int NameIndex, NameExtra, GroupCount;
		Ar << f28 << AR_INDEX(NameIndex) << AR_INDEX(NameExtra) << AR_INDEX(GroupCount);
		Seq.Name = Package->GetName(NameIndex);
		Seq.Groups.Empty(GroupCount);
		for (int GroupIndex = 0; GroupIndex < GroupCount; GroupIndex++)
		{
			int GroupNameIndex, GroupNameExtra;
			Ar << AR_INDEX(GroupNameIndex) << AR_INDEX(GroupNameExtra);
			FName* GroupName = new (Seq.Groups) FName;
			*GroupName = Package->GetName(GroupNameIndex);
		}
		Ar << Seq.StartFrame << Seq.NumFrames;
		int NotifyCount;
		Ar << AR_INDEX(NotifyCount);

		Seq.f28 = f28;
		Seq.Notifys.Empty();
		Seq.Rate = 30.0f;

		// Pandora Tomorrow notifies use Ubisoft-specific compact payloads. Scan to the
		// next sequence header, preserving names/timing while safely ignoring notifies.
		int NextSeq = 0;
		for (int Pos = Ar.Tell(); Pos < Ar.GetStopper() - 16; Pos++)
		{
			int NextExpectedFrames = (i + 1 < MoveCount) ? max(1, appRound(Moves[i + 1].TrackTime)) : 0;
			if (IsPandoraAnimSeqHeader(Ar, Package, Pos, NextExpectedFrames))
			{
				NextSeq = Pos;
				break;
			}
		}

		if (NextSeq)
		{
			// Rate is the float immediately before the next sequence's f28.
			Ar.Seek(NextSeq - 4);
			Ar << Seq.Rate;
			Ar.Seek(NextSeq);
		}
		else
		{
			Seq.Rate = 30.0f;
			DROP_REMAINING_DATA(Ar);
		}
	}

	DROP_REMAINING_DATA(Ar);

	unguard;
}

static bool ReadSCDAV2StaticCompactIndex(const byte* Data, int DataSize, int& Pos, int& Value)
{
	if (Pos < 0 || Pos >= DataSize)
		return false;
	byte B = Data[Pos++];
	bool Negative = (B & 0x80) != 0;
	Value = B & 0x3F;
	int Shift = 6;
	if (B & 0x40)
	{
		for (int i = 0; i < 4; i++)
		{
			if (Pos >= DataSize)
				return false;
			B = Data[Pos++];
			Value |= (B & 0x7F) << Shift;
			Shift += 7;
			if (!(B & 0x80))
				break;
		}
	}
	if (Negative)
		Value = -Value;
	return true;
}

static float ReadSCDAV2StaticFloatLE(const byte* Data, int Pos)
{
	float Value;
	memcpy(&Value, Data + Pos, 4);
	return Value;
}

static bool IsSCDAV2AnimSequenceName(const char* Name)
{
	if (!Name || !Name[0])
		return false;
	if (!stricmp(Name, "None") ||
		!stricmp(Name, "NotifyName") ||
		!stricmp(Name, "PlayOnlyFoward") ||
		!stricmp(Name, "PlayOnController") ||
		!stricmp(Name, "PlayPivot") ||
		!stricmp(Name, "Sound") ||
		!stricmp(Name, "SoundSlot") ||
		!stricmp(Name, "AnimSound") ||
		!stricmp(Name, "bScriptCallingSound"))
		return false;
	int Len = 0;
	bool HasLower = false;
	bool HasLetter = false;
	bool HasDigit = false;
	for (const char* C = Name; *C; C++, Len++)
	{
		if (Len >= 64)
			return false;
		char Ch = *C;
		if (Ch >= 'a' && Ch <= 'z')
		{
			HasLower = true;
			HasLetter = true;
			continue;
		}
		if (Ch >= 'A' && Ch <= 'Z')
		{
			HasLetter = true;
			continue;
		}
		if (Ch >= '0' && Ch <= '9')
		{
			HasDigit = true;
			continue;
		}
		if (Ch == '_')
			continue;
		return false;
	}
	return Len >= 3 && HasLetter && HasLower && HasDigit;
}

static bool LoadSCDAV2ManifestSequences(UMeshAnimation& Anim, const TArray<byte>& Data, int SourceLogicalOffset)
{
	guard(LoadSCDAV2ManifestSequences);
	if (!Anim.Package || !Data.Num())
		return false;

	struct FSeqInfo
	{
		int Pos;
		int NameEnd;
		int NameIndex;
		int NumFrames;
		float Rate;
	};

	TArray<FSeqInfo> Candidates;
	const byte* Bytes = Data.GetData();
	const int Size = Data.Num();
	for (int Pos = 0; Pos + 32 < Size; Pos++)
	{
		int NamePos = Pos;
		int NameIndex = 0;
		if (!ReadSCDAV2StaticCompactIndex(Bytes, Size, NamePos, NameIndex))
			continue;
		if (NameIndex <= 0 || NameIndex >= Anim.Package->Summary.NameCount)
			continue;
		const char* SeqName = Anim.Package->GetName(NameIndex);
		if (!IsSCDAV2AnimSequenceName(SeqName))
			continue;
		if (NamePos + 23 > Size)
			continue;
		bool ZeroHeader = true;
		for (int i = 0; i < 6; i++)
		{
			if (Bytes[NamePos + i] != 0)
			{
				ZeroHeader = false;
				break;
			}
		}
		if (!ZeroHeader)
			continue;
		int NumFrames = Bytes[NamePos + 6];
		if (NumFrames <= 0 || NumFrames > 240)
			continue;
		if (Bytes[NamePos + 7] || Bytes[NamePos + 8] || Bytes[NamePos + 9] || Bytes[NamePos + 10])
			continue;
		float Rate = ReadSCDAV2StaticFloatLE(Bytes, NamePos + 11);
		if (Rate < 1.0f || Rate > 120.0f)
			continue;
		if (Candidates.Num() && Pos < Candidates.Last().NameEnd)
			continue; // filters the second byte of a two-byte compact name, e.g. PlayOnlyFoward
		FSeqInfo* Info = new (Candidates) FSeqInfo;
		Info->Pos = Pos;
		Info->NameEnd = NamePos;
		Info->NameIndex = NameIndex;
		Info->NumFrames = NumFrames;
		Info->Rate = Rate;
	}

	if (!Candidates.Num())
		return false;

	Anim.Moves.Empty(Candidates.Num());
	Anim.AnimSeqs.Empty(Candidates.Num());
	FQuat Identity;
	Identity.Set(0, 0, 0, 1);
	for (int SeqIndex = 0; SeqIndex < Candidates.Num(); SeqIndex++)
	{
		const FSeqInfo& Info = Candidates[SeqIndex];
		MotionChunk* Move = new (Anim.Moves) MotionChunk;
		Move->RootSpeed3D.Set(0, 0, 0);
		Move->TrackTime = max(1, Info.NumFrames);
		Move->StartBone = 0;
		Move->Flags = 0;
		const int BoneCount = Anim.RefBones.Num();
		Move->BoneIndices.Empty(BoneCount);
		Move->BoneIndices.AddZeroed(BoneCount);
		Move->AnimTracks.Empty(BoneCount);
		Move->AnimTracks.AddZeroed(BoneCount);
		for (int BoneIndex = 0; BoneIndex < BoneCount; BoneIndex++)
		{
			Move->BoneIndices[BoneIndex] = BoneIndex;
			AnalogTrack& Track = Move->AnimTracks[BoneIndex];
			Track.Flags = 0;
			Track.KeyQuat.Add(Identity);
			Track.KeyTime.Add(0.0f);
			if (BoneIndex == 0)
			{
				FVector Zero;
				Zero.Set(0, 0, 0);
				Track.KeyPos.Add(Zero);
			}
		}

		FMeshAnimSeq* Seq = new (Anim.AnimSeqs) FMeshAnimSeq;
		Seq->f28 = 0;
		Seq->Name = Anim.Package->GetName(Info.NameIndex);
		Seq->Groups.Empty();
		Seq->StartFrame = 0;
		Seq->NumFrames = Info.NumFrames;
		Seq->Notifys.Empty();
		Seq->Rate = Info.Rate;
	}

	if (getenv("SCDA_DEBUG_ANIM"))
	{
		appPrintf("SCDA V2 MeshAnimation %s: sidecar sequences=%d source=%08X\n",
			Anim.Name, Anim.AnimSeqs.Num(), SourceLogicalOffset);
		for (int i = 0; i < Candidates.Num() && i < 32; i++)
			appPrintf("  seq %d pos=%08X name=%s frames=%d rate=%g\n",
				i, SourceLogicalOffset + Candidates[i].Pos,
				Anim.Package->GetName(Candidates[i].NameIndex),
				Candidates[i].NumFrames, Candidates[i].Rate);
	}
	return Anim.AnimSeqs.Num() > 0;
	unguard;
}

void UMeshAnimation::SerializeSC4(FArchive &Ar)
{
	guard(UMeshAnimation::SerializeSC4);

	bool ManifestSkeleton = false;
	if (!RefBones.Num() && Package)
	{
		TArray<FString> BoneNames;
		TArray<int> ParentIndices;
		if (GetScdaV2ManifestSkeleton(*Package->GetFilename(), BoneNames, ParentIndices) &&
			BoneNames.Num() == ParentIndices.Num())
		{
			RefBones.Empty(BoneNames.Num());
			for (int BoneIndex = 0; BoneIndex < BoneNames.Num(); BoneIndex++)
			{
				FNamedBone *Bone = new (RefBones) FNamedBone;
				Bone->Name = *BoneNames[BoneIndex];
				Bone->Flags = 0;
				Bone->ParentIndex = ParentIndices[BoneIndex];
			}
			ManifestSkeleton = true;
			if (getenv("SC4_DEBUG_ANIM"))
				appPrintf("SCDA manifest MeshAnimation skeleton: %s bones=%d\n",
					Name, RefBones.Num());
		}
	}

	TArray<FSC4MotionChunk> SrcMoves;
	Ar << SrcMoves;

	Moves.Empty(SrcMoves.Num());
	Moves.AddZeroed(SrcMoves.Num());
	for (int MoveIndex = 0; MoveIndex < SrcMoves.Num(); MoveIndex++)
	{
		FSC4MotionChunk &Src = SrcMoves[MoveIndex];
		MotionChunk &Dst = Moves[MoveIndex];
		Dst.RootSpeed3D = Src.RootSpeed3D;
		Dst.TrackTime = Src.TrackTime;
		Dst.StartBone = Src.StartBone;
		Dst.Flags = 0;
		CopyArray(Dst.BoneIndices, Src.BoneIndices);

		Dst.AnimTracks.Empty(Src.AnimTracks.Num());
		Dst.AnimTracks.AddZeroed(Src.AnimTracks.Num());
		for (int TrackIndex = 0; TrackIndex < Src.AnimTracks.Num(); TrackIndex++)
			Src.AnimTracks[TrackIndex].Decompress(Dst.AnimTracks[TrackIndex]);
		Src.RootTrack.Decompress(Dst.RootTrack);
	}

	TArray<FSC4AnimSeq> SrcSeqs;
	Ar << SrcSeqs;
	AnimSeqs.Empty(SrcSeqs.Num());
	AnimSeqs.AddZeroed(SrcSeqs.Num());
	for (int SeqIndex = 0; SeqIndex < SrcSeqs.Num(); SeqIndex++)
	{
		const FSC4AnimSeq &Src = SrcSeqs[SeqIndex];
		FMeshAnimSeq &Dst = AnimSeqs[SeqIndex];
		Dst.f28 = Src.f28;
		Dst.Name = Src.Name;
		CopyArray(Dst.Groups, Src.Groups);
		Dst.StartFrame = Src.StartFrame;
		Dst.NumFrames = Src.NumFrames;
		Dst.Rate = Src.Rate;
	}

	if (getenv("SC4_DEBUG_ANIM"))
		appPrintf("SC4 MeshAnimation %s: moves=%d sequences=%d end=%08X stopper=%08X\n",
			Name, Moves.Num(), AnimSeqs.Num(), Ar.Tell(), Ar.GetStopper());

	if (ManifestSkeleton && !Moves.Num() && !AnimSeqs.Num())
	{
		TArray<byte> ScdaAnimData;
		int ScdaAnimDataSource = 0;
		if (GetScdaV2ManifestAnimationData(*Package->GetFilename(), Name, ScdaAnimData, ScdaAnimDataSource))
			LoadSCDAV2ManifestSequences(*this, ScdaAnimData, ScdaAnimDataSource);
	}

	if (getenv("SCDA_DEBUG_ANIM") && Ar.Tell() < Ar.GetStopper())
	{
		int SavePos = Ar.Tell();
		int FirstNonZero = 0;
		for (int Pos = SavePos; Pos < Ar.GetStopper(); Pos++)
		{
			byte B = 0;
			Ar.Seek(Pos);
			Ar << B;
			if (B)
			{
				FirstNonZero = Pos;
				break;
			}
		}
		if (FirstNonZero)
			appPrintf("SCDA MeshAnimation first nonzero after SC4 arrays: %08X (+%X)\n",
				FirstNonZero, FirstNonZero - SavePos);
		Ar.Seek(FirstNonZero ? FirstNonZero : SavePos);
		int ProbePos = Ar.Tell();
		int DumpSize = min(128, Ar.GetStopper() - SavePos);
		byte Bytes[128];
		memset(Bytes, 0, sizeof(Bytes));
		DumpSize = min(128, Ar.GetStopper() - ProbePos);
		Ar.Serialize(Bytes, DumpSize);
		appPrintf("SCDA MeshAnimation probe %s: pos=%08X stopper=%08X next=%d\n",
			Name, ProbePos, Ar.GetStopper(), DumpSize);
		for (int Line = 0; Line < DumpSize; Line += 16)
		{
			appPrintf("  %08X:", ProbePos + Line);
			for (int i = 0; i < 16 && Line + i < DumpSize; i++)
				appPrintf(" %02X", Bytes[Line + i]);
			appPrintf("\n");
		}
		Ar.Seek(SavePos);
	}

	if (ManifestSkeleton)
		DROP_REMAINING_DATA(Ar);

	unguard;
}

void UMeshAnimation::SerializeSCCT(FArchive &Ar)
{
	guard(UMeshAnimation::SerializeSCCT);

	struct FSCCTSeqInfo
	{
		int Pos;
		int End;
		int NameIndex;
		int StartFrame;
		int NumFrames;
		int NotifyCount;
		float Rate;
		int ExtraSize;
	};
	auto IsSCCTSequenceName = [](const char *S) -> bool
	{
		if (!S || !S[0])
			return false;
		int Len = 0;
		bool HasLetter = false;
		bool HasLower = false;
		bool HasDigit = false;
		for (const char *C = S; *C; C++, Len++)
		{
			if (Len >= 64)
				return false;
			char Ch = *C;
			if (Ch >= 'a' && Ch <= 'z')
			{
				HasLetter = true;
				HasLower = true;
				continue;
			}
			if (Ch >= 'A' && Ch <= 'Z')
			{
				HasLetter = true;
				continue;
			}
			if (Ch >= '0' && Ch <= '9')
			{
				HasDigit = true;
				continue;
			}
			if (Ch == '_')
				continue;
			return false;
		}
		return Len >= 3 && HasLetter && (HasLower || HasDigit);
	};
	auto ReadSCCTNameIndex = [&](int &NameIndex) -> bool
	{
		Ar << AR_INDEX(NameIndex);
		return NameIndex >= 0 && unsigned(NameIndex) < Package->Summary.NameCount;
	};
	auto TryReadSCCTSeq = [&](int Pos, int ExpectedFrames, FSCCTSeqInfo &Seq) -> bool
	{
		if (!Package || Pos < 0 || Pos >= Ar.GetStopper() - 16)
			return false;
		int SavePos = Ar.Tell();
		Ar.Seek(Pos);

		int NameIndex;
		if (!ReadSCCTNameIndex(NameIndex) || NameIndex <= 0)
		{
			Ar.Seek(SavePos);
			return false;
		}
		if (!IsSCCTSequenceName(Package->GetName(NameIndex)))
		{
			Ar.Seek(SavePos);
			return false;
		}

		int GroupCount;
		Ar << AR_INDEX(GroupCount);
		if (GroupCount < 0 || GroupCount > 4)
		{
			Ar.Seek(SavePos);
			return false;
		}
		for (int GroupIndex = 0; GroupIndex < GroupCount; GroupIndex++)
		{
			int GroupNameIndex;
			if (!ReadSCCTNameIndex(GroupNameIndex))
			{
				Ar.Seek(SavePos);
				return false;
			}
		}

		int StartFrame, NumFrames;
		Ar << StartFrame << NumFrames;
		if (StartFrame < 0 || StartFrame > 100000 || NumFrames <= 0 || NumFrames > 10000)
		{
			Ar.Seek(SavePos);
			return false;
		}
		if (ExpectedFrames > 1 && abs(NumFrames - ExpectedFrames) > 1)
		{
			Ar.Seek(SavePos);
			return false;
		}

		int NotifyCount;
		Ar << AR_INDEX(NotifyCount);
		if (NotifyCount < 0 || NotifyCount > 512)
		{
			Ar.Seek(SavePos);
			return false;
		}
		float Rate = 15.0f;
		int ExtraSize = -1;
		bool bGoodNotifies = true;
		for (int NotifyIndex = 0; NotifyIndex < NotifyCount; NotifyIndex++)
		{
			if (Ar.Tell() + 4 > Ar.GetStopper())
			{
				bGoodNotifies = false;
				break;
			}
			float NotifyTime;
			Ar << NotifyTime;
			if (NotifyTime < -0.001f || NotifyTime > NumFrames + 0.001f)
			{
				bGoodNotifies = false;
				break;
			}
			int FunctionNameIndex;
			if (!ReadSCCTNameIndex(FunctionNameIndex))
			{
				bGoodNotifies = false;
				break;
			}
			int FunctionNameExtra;
			Ar << AR_INDEX(FunctionNameExtra);
			if (FunctionNameExtra < 0 || FunctionNameExtra > 10000)
			{
				bGoodNotifies = false;
				break;
			}
			int NotifyObjectNameIndex;
			if (!ReadSCCTNameIndex(NotifyObjectNameIndex))
			{
				bGoodNotifies = false;
				break;
			}
		}
		if (!bGoodNotifies || Ar.Tell() + 4 > Ar.GetStopper())
		{
			Ar.Seek(SavePos);
			return false;
		}
		float SerializedRate;
		Ar << SerializedRate;
		if (SerializedRate >= 1.0f && SerializedRate <= 120.0f)
			Rate = SerializedRate;
		else
		{
			Ar.Seek(SavePos);
			return false;
		}
		if (Ar.Tell() + 4 <= Ar.GetStopper())
			Ar << ExtraSize;

		Seq.Pos = Pos;
		Seq.End = Ar.Tell();
		Seq.NameIndex = NameIndex;
		Seq.StartFrame = StartFrame;
		Seq.NumFrames = NumFrames;
		Seq.NotifyCount = NotifyCount;
		Seq.Rate = Rate;
		Seq.ExtraSize = ExtraSize;
		Ar.Seek(SavePos);
		return true;
	};
	auto DecodeSCCTQuat32 = [](unsigned Packed) -> FQuat
	{
		FQuat Q;
		Q.Set(0, 0, 0, 1);
		int Selector = (Packed >> 30) & 3;
		static const int SelectorMap[4] = { 0, 1, 2, 3 };
		static const int FieldOrder[4][3] =
		{
			{ 2, 1, 0 },
			{ 2, 1, 0 },
			{ 2, 1, 0 },
			{ 2, 1, 0 }
		};
		int Missing = SelectorMap[Selector];
		unsigned Data = Packed & 0x3FFFFFFF;
		static const float Shift = 0.70710678118f;
		static const float Scale = 1.41421356237f;
		float Raw[3];
		Raw[0] = ((Data & 0x3FF) + 0.5f) / 1024.0f * Scale - Shift;
		Raw[1] = (((Data >> 10) & 0x3FF) + 0.5f) / 1024.0f * Scale - Shift;
		Raw[2] = (((Data >> 20) & 0x3FF) + 0.5f) / 1024.0f * Scale - Shift;
		float C[4] = { 0, 0, 0, 0 };
		int Slot = 0;
		for (int i = 0; i < 4; i++)
		{
			if (i == Missing) continue;
			C[i] = Raw[FieldOrder[Missing][Slot++]];
		}
		float MissingSq = 1.0f - (C[0] * C[0] + C[1] * C[1] + C[2] * C[2] + C[3] * C[3]);
		C[Missing] = (MissingSq > 0) ? sqrt(MissingSq) : 0;
		Q.Set(C[0], C[1], C[2], C[3]);
		float LenSq = Q.X * Q.X + Q.Y * Q.Y + Q.Z * Q.Z + Q.W * Q.W;
		if (LenSq > 0.000001f)
		{
			float Scale = 1.0f / sqrt(LenSq);
			Q.X *= Scale;
			Q.Y *= Scale;
			Q.Z *= Scale;
			Q.W *= Scale;
		}
		return Q;
	};
	auto DecodeSCCTQuat48 = [](uint16 X, uint16 Y, uint16 Z) -> FQuat
	{
		static const float Shift = 0.70710678118f;
		static const float Scale = 1.41421356237f;
		float A = (X & 0x7FFF) / 32767.0f * Scale - Shift;
		float B = (Y & 0x7FFF) / 32767.0f * Scale - Shift;
		float C = (Z & 0x7FFF) / 32767.0f * Scale - Shift;
		float MissingSq = 1.0f - (A * A + B * B + C * C);
		float D = (MissingSq > 0) ? sqrt(MissingSq) : 0.0f;

		FQuat Q;
		int Selector = ((X >> 15) & 1) | ((Y >> 14) & 2);
		switch (Selector)
		{
		case 0:
			Q.Set(D, A, B, C);
			break;
		case 1:
			Q.Set(A, D, B, C);
			break;
		case 2:
			Q.Set(A, B, D, C);
			break;
		default:
			Q.Set(A, B, C, D);
			break;
		}
		return Q;
	};
	auto DecodeSCCTVector = [](const int16 *Packed, float Scale, bool MirrorY) -> FVector
	{
		FVector V;
		V.X = Packed[0] * Scale;
		V.Y = (MirrorY ? -Packed[1] : Packed[1]) * Scale;
		V.Z = Packed[2] * Scale;
		return V;
	};
	auto SerializeSCCTRawTrack = [&](AnalogTrack &Track, int NumFrames, int CompressType, bool KeepPositionKeys, bool KeepConstantPositionKeys, bool MirrorPositionY, float PosScale) -> bool
	{
		uint16 NumKeys, RotSize, PosSize;
		Ar << NumKeys << RotSize << PosSize;
		if (NumKeys < 1 || NumKeys > 10000 ||
			(RotSize != 4 && RotSize != NumKeys * 4 && RotSize != 6 && RotSize != NumKeys * 6) ||
			(PosSize != 6 && PosSize != NumKeys * 6))
			return false;
		int RotStride = (RotSize == 6 || RotSize == NumKeys * 6) ? 6 : 4;
		int RotKeys = RotSize / RotStride;
		int PosKeys = PosSize / 6;
		Track.Flags = 0;
		Track.KeyQuat.Empty(RotKeys);
		Track.KeyPos.Empty(PosKeys);
		Track.KeyTime.Empty(NumKeys);
		for (int i = 0; i < NumKeys; i++)
		{
			byte Time;
			Ar << Time;
			Track.KeyTime.Add((float)Time);
		}
		for (int i = 0; i < RotKeys; i++)
		{
			if (RotStride == 6)
			{
				if (CompressType == 0)
				{
					uint16 PackedQuat[3];
					Ar << PackedQuat[0] << PackedQuat[1] << PackedQuat[2];
					Track.KeyQuat.Add(DecodeSCCTQuat48(PackedQuat[0], PackedQuat[1], PackedQuat[2]));
				}
				else
				{
					FQuatComp2 PackedQuat;
					Ar << PackedQuat;
					Track.KeyQuat.Add(PackedQuat);
				}
			}
			else
			{
				unsigned PackedQuat;
				Ar << PackedQuat;
				Track.KeyQuat.Add(DecodeSCCTQuat32(PackedQuat));
			}
		}
		for (int i = 1; i < Track.KeyQuat.Num(); i++)
		{
			const FQuat &Prev = Track.KeyQuat[i - 1];
			FQuat &Cur = Track.KeyQuat[i];
			float Dot = Prev.X * Cur.X + Prev.Y * Cur.Y + Prev.Z * Cur.Z + Prev.W * Cur.W;
			if (Dot < 0.0f)
			{
				Cur.X = -Cur.X;
				Cur.Y = -Cur.Y;
				Cur.Z = -Cur.Z;
				Cur.W = -Cur.W;
			}
		}
		for (int i = 0; i < PosKeys; i++)
		{
			int16 PackedPos[3];
			Ar << PackedPos[0] << PackedPos[1] << PackedPos[2];
			if (KeepPositionKeys && (KeepConstantPositionKeys || PosKeys > 1))
				Track.KeyPos.Add(DecodeSCCTVector(PackedPos, PosScale, MirrorPositionY));
		}
		int UsedKeys = max(Track.KeyQuat.Num(), Track.KeyPos.Num());
		if (UsedKeys > 0 && Track.KeyTime.Num() > UsedKeys)
			Track.KeyTime.RemoveAt(UsedKeys, Track.KeyTime.Num() - UsedKeys);
		return true;
	};
	int SeqCount = 0;
	auto IsPlausibleSCCTCompressionStart = [&](int Pos, int *OutMode) -> bool
	{
		if (OutMode)
			*OutMode = 0;
		if (Pos < 0 || Pos + 10 >= Ar.GetStopper())
			return false;
		auto CheckCompressedArrayCounts = [&](int CompressType) -> bool
		{
			if (CompressType < 1 || CompressType > 4)
				return false;
			for (int i = 1; i <= 4; i++)
			{
				int Count;
				Ar << AR_INDEX(Count);
				if (Count < 0 || Count > 10000)
					return false;
				if (i < CompressType)
				{
					if (Count != 0)
						return false;
				}
				else if (i == CompressType)
				{
					if (Count != SeqCount)
						return false;
					return true;
				}
				else
				{
					return true;
				}
			}
			return false;
		};
		int SavePos = Ar.Tell();
		int CompressTypeOnly;
		Ar.Seek(Pos);
		Ar << CompressTypeOnly;
		if (CheckCompressedArrayCounts(CompressTypeOnly))
		{
			if (OutMode)
				*OutMode = 1;
			Ar.Seek(SavePos);
			return true;
		}

		int OldCompression, T0Count, CompressType;
		Ar.Seek(Pos);
		Ar << OldCompression << AR_INDEX(T0Count) << CompressType;
		if (OldCompression == 0 && T0Count == 0 && CheckCompressedArrayCounts(CompressType))
		{
			if (OutMode)
				*OutMode = 2;
			Ar.Seek(SavePos);
			return true;
		}
		Ar.Seek(SavePos);
		return false;
	};

	int SeqArrayStart = Ar.Tell();
	Ar << AR_INDEX(SeqCount);
	if (SeqCount < 0 || SeqCount > 10000)
		appError("Invalid SCCT sequence count: %d", SeqCount);

	TArray<FSCCTSeqInfo> SeqInfos;
	int PayloadStart = Ar.Tell();

	TArray<FSCCTSeqInfo> SeqCandidates;
	TArray<FSCCTSeqInfo> AllSeqCandidates;
	int SeqPos = PayloadStart;
	for (int SeqIndex = 0; SeqIndex < SeqCount; SeqIndex++)
	{
		FSCCTSeqInfo Info;
		if (!TryReadSCCTSeq(SeqPos, 0, Info))
			break;
		new (AllSeqCandidates) FSCCTSeqInfo(Info);
		if (SeqIndex + 1 >= SeqCount)
			break;
		int NextSeqPos = 0;
		for (int Pos = Info.End; Pos < Ar.GetStopper() - 16; Pos++)
		{
			FSCCTSeqInfo NextInfo;
			if (TryReadSCCTSeq(Pos, 0, NextInfo))
			{
				NextSeqPos = Pos;
				break;
			}
		}
		if (!NextSeqPos)
			break;
		SeqPos = NextSeqPos;
	}
	if (AllSeqCandidates.Num() < SeqCount)
	{
		AllSeqCandidates.Empty();
		int SeqScanStart = max(PayloadStart, Ar.GetStopper() - 0x10000);
		for (int Pos = SeqScanStart; Pos < Ar.GetStopper() - 16; Pos++)
		{
			FSCCTSeqInfo Info;
			if (TryReadSCCTSeq(Pos, 0, Info))
			{
				new (AllSeqCandidates) FSCCTSeqInfo(Info);
			}
		}
		if (AllSeqCandidates.Num() < SeqCount && SeqScanStart > PayloadStart)
		{
			AllSeqCandidates.Empty();
			for (int Pos = PayloadStart; Pos < Ar.GetStopper() - 16; Pos++)
			{
				FSCCTSeqInfo Info;
				if (TryReadSCCTSeq(Pos, 0, Info))
				{
					new (AllSeqCandidates) FSCCTSeqInfo(Info);
				}
			}
		}
	}
	TArray<FSCCTSeqInfo> FilteredSeqCandidates;
	auto HasCorruptSCCTSeqTail = [](const FSCCTSeqInfo &Info) -> bool
	{
		// Ubisoft notifies may carry variable-size payloads. If the fixed-size
		// notify skip lands in the middle of that payload, the following bytes can
		// resemble a sequence header; the bogus tail value is the tell.
		return Info.NotifyCount > 0 && Info.ExtraSize > 0x04000000;
	};
	int BadNotifyChainEnd = 0;
	for (int i = 0; i < AllSeqCandidates.Num(); i++)
	{
		const FSCCTSeqInfo &Info = AllSeqCandidates[i];
		if (BadNotifyChainEnd)
		{
			if (Info.Pos == BadNotifyChainEnd)
			{
				BadNotifyChainEnd = Info.End;
				continue;
			}
			if (Info.Pos > BadNotifyChainEnd)
			{
				BadNotifyChainEnd = 0;
			}
		}
		if (FilteredSeqCandidates.Num())
		{
			const FSCCTSeqInfo &Prev = FilteredSeqCandidates[FilteredSeqCandidates.Num() - 1];
			if (Info.Pos > Prev.Pos && Info.Pos < Prev.End)
			{
				continue;
			}
		}
		new (FilteredSeqCandidates) FSCCTSeqInfo(Info);
		if (HasCorruptSCCTSeqTail(Info))
			BadNotifyChainEnd = Info.End;
	}
	AllSeqCandidates.Empty(FilteredSeqCandidates.Num());
	for (int i = 0; i < FilteredSeqCandidates.Num(); i++)
		new (AllSeqCandidates) FSCCTSeqInfo(FilteredSeqCandidates[i]);
	int FirstSeqCandidate = (AllSeqCandidates.Num() > SeqCount) ? AllSeqCandidates.Num() - SeqCount : 0;
	for (int i = FirstSeqCandidate; i < AllSeqCandidates.Num(); i++)
		new (SeqCandidates) FSCCTSeqInfo(AllSeqCandidates[i]);
	if (getenv("SCCT_DEBUG_SEQMAP"))
	{
		appPrintf("SCCT candidates all=%d used=%d first=%d seqCount=%d\n", AllSeqCandidates.Num(), SeqCandidates.Num(), FirstSeqCandidate, SeqCount);
		for (int i = 0; i < SeqCandidates.Num(); i++)
		{
			appPrintf("SCCT candidate %d: pos=%X end=%X name=%s frames=%d notify=%d rate=%g extra=%d\n",
				i, SeqCandidates[i].Pos, SeqCandidates[i].End, Package->GetName(SeqCandidates[i].NameIndex), SeqCandidates[i].NumFrames,
				SeqCandidates[i].NotifyCount, SeqCandidates[i].Rate, SeqCandidates[i].ExtraSize);
		}
	}

	Moves.Empty(SeqCount);
	TArray<int> MoveRawKeyCounts;
	MoveRawKeyCounts.Empty(SeqCount);
	TArray<int> MoveRawFrameSpans;
	MoveRawFrameSpans.Empty(SeqCount);
	auto CopySCCTAnalogTrack = [](AnalogTrack &Dst, const AnalogTrack &Src)
	{
		Dst.Flags = Src.Flags;
		CopyArray(Dst.KeyPos, Src.KeyPos);
		CopyArray(Dst.KeyQuat, Src.KeyQuat);
		CopyArray(Dst.KeyTime, Src.KeyTime);
	};
	auto CopySCCTMotionChunk = [&](MotionChunk &Dst, const MotionChunk &Src)
	{
		Dst.RootSpeed3D = Src.RootSpeed3D;
		Dst.TrackTime = Src.TrackTime;
		Dst.StartBone = Src.StartBone;
		Dst.Flags = Src.Flags;
		CopyArray(Dst.BoneIndices, Src.BoneIndices);
		Dst.AnimTracks.Empty(Src.AnimTracks.Num());
		Dst.AnimTracks.AddZeroed(Src.AnimTracks.Num());
		for (int TrackIndex = 0; TrackIndex < Src.AnimTracks.Num(); TrackIndex++)
			CopySCCTAnalogTrack(Dst.AnimTracks[TrackIndex], Src.AnimTracks[TrackIndex]);
		CopySCCTAnalogTrack(Dst.RootTrack, Src.RootTrack);
	};
	auto MergeSCCTRootCompanion = [&](MotionChunk &Dst, const MotionChunk &RootSrc)
	{
		if (!RootSrc.AnimTracks.Num() || !Dst.AnimTracks.Num())
			return;
		// In SCCT NPC sets these bare root-only blocks are companion timing records.
		// Their translation payload is not in the same space as the compressed
		// full-body root track, so keep the real root transform from Dst.
		Dst.TrackTime = max(Dst.TrackTime, RootSrc.TrackTime);
	};
	MotionChunk PendingRootCompanion;
	int PendingRootKeyCount = 1;
	int PendingRootFrameSpan = 1;
	bool bHasPendingRootCompanion = false;
	int TotalTracks = 0;
	int MotionSearchPos = PayloadStart;
	int MotionDataEnd = SeqCandidates.Num() ? SeqCandidates[0].Pos : Ar.GetStopper();
	int MaxRawMoves = SeqCount + min(64, SeqCount / 8 + 16);
	auto IsPlausibleInlineSCCTTracks = [&](int Pos, int SeqIndex, int *OutCompressType, bool AllowEmpty) -> bool
	{
		if (OutCompressType)
			*OutCompressType = 0;
		int SavePos = Ar.Tell();
		Ar.Seek(Pos);
		int CompressType, BoneIndexCount;
		Ar << CompressType << AR_INDEX(BoneIndexCount);
		if (CompressType < 1 || CompressType > 4 || BoneIndexCount < 0 || BoneIndexCount > 256)
		{
			Ar.Seek(SavePos);
			return false;
		}
		if (BoneIndexCount == 0)
		{
			if (OutCompressType)
				*OutCompressType = CompressType;
			Ar.Seek(SavePos);
			return AllowEmpty;
		}
		for (int TrackIndex = 0; TrackIndex < BoneIndexCount; TrackIndex++)
		{
			uint16 NumKeys, RotSize, PosSize;
			Ar << NumKeys << RotSize << PosSize;
			if (NumKeys == 0 && RotSize == 0 && PosSize == 0)
				continue;
			if (NumKeys < 1 || NumKeys > 10000 ||
				(RotSize != 4 && RotSize != NumKeys * 4 && RotSize != 6 && RotSize != NumKeys * 6) ||
				(PosSize != 6 && PosSize != NumKeys * 6))
			{
				Ar.Seek(SavePos);
				return false;
			}
			Ar.Seek(Ar.Tell() + NumKeys + RotSize + PosSize);
			if (Ar.Tell() > Ar.GetStopper())
			{
				Ar.Seek(SavePos);
				return false;
			}
		}
		if (OutCompressType)
			*OutCompressType = CompressType;
		Ar.Seek(SavePos);
		return true;
	};
	auto IsPlausibleBareSCCTTracks = [&](int Pos, int *OutTrackCount) -> bool
	{
		if (OutTrackCount)
			*OutTrackCount = 0;
		if (Pos < 0 || Pos + 8 >= Ar.GetStopper())
			return false;
		int SavePos = Ar.Tell();
		Ar.Seek(Pos);
		int TrackCount;
		Ar << AR_INDEX(TrackCount);
		if (TrackCount < 1 || TrackCount > 256)
		{
			Ar.Seek(SavePos);
			return false;
		}
		bool HasRealTrack = false;
		for (int TrackIndex = 0; TrackIndex < TrackCount; TrackIndex++)
		{
			uint16 NumKeys, RotSize, PosSize;
			Ar << NumKeys << RotSize << PosSize;
			if (NumKeys == 0 && RotSize == 0 && PosSize == 0)
				continue;
			HasRealTrack = true;
			if (NumKeys < 1 || NumKeys > 10000 ||
				(RotSize != 4 && RotSize != NumKeys * 4 && RotSize != 6 && RotSize != NumKeys * 6) ||
				(PosSize != 6 && PosSize != NumKeys * 6))
			{
				Ar.Seek(SavePos);
				return false;
			}
			Ar.Seek(Ar.Tell() + NumKeys + RotSize + PosSize);
			if (Ar.Tell() > Ar.GetStopper())
			{
				Ar.Seek(SavePos);
				return false;
			}
		}
		if (!HasRealTrack && TrackCount == 1 && Ar.Tell() + 8 < Ar.GetStopper())
		{
			int EmptyEnd = Ar.Tell();
			for (int PadBytes = 0; PadBytes <= 4 && EmptyEnd + PadBytes + 8 < Ar.GetStopper(); PadBytes++)
			{
				bool bZeroPadding = true;
				for (int PadIndex = 0; PadIndex < PadBytes; PadIndex++)
				{
					byte PadByte;
					Ar.Seek(EmptyEnd + PadIndex);
					Ar << PadByte;
					if (PadByte != 0)
					{
						bZeroPadding = false;
						break;
					}
				}
				if (!bZeroPadding)
					break;
				Ar.Seek(EmptyEnd + PadBytes);
				int NextTrackCount;
				Ar << AR_INDEX(NextTrackCount);
				if (NextTrackCount > 1 && NextTrackCount <= 256)
				{
					uint16 NextNumKeys, NextRotSize, NextPosSize;
					Ar << NextNumKeys << NextRotSize << NextPosSize;
					if (NextNumKeys >= 1 && NextNumKeys <= 10000 &&
						(NextRotSize == 4 || NextRotSize == NextNumKeys * 4 || NextRotSize == 6 || NextRotSize == NextNumKeys * 6) &&
						(NextPosSize == 6 || NextPosSize == NextNumKeys * 6))
					{
						Ar.Seek(SavePos);
						return false;
					}
				}
			}
			Ar.Seek(EmptyEnd);
		}
		if (OutTrackCount)
			*OutTrackCount = TrackCount;
		Ar.Seek(SavePos);
		return true;
	};
	auto PeekSCCTTrackMaxKeys = [&](int Pos) -> int
	{
		int SavePos = Ar.Tell();
		Ar.Seek(Pos);
		int CompressType, TrackCount;
		Ar << CompressType;
		Ar << AR_INDEX(TrackCount);
		if (CompressType < 1 || CompressType > 4 || TrackCount < 1 || TrackCount > 256)
		{
			Ar.Seek(SavePos);
			return 0;
		}
		int MaxKeys = 1;
		for (int TrackIndex = 0; TrackIndex < TrackCount; TrackIndex++)
		{
			uint16 NumKeys, RotSize, PosSize;
			Ar << NumKeys << RotSize << PosSize;
			if (NumKeys == 0 && RotSize == 0 && PosSize == 0)
				continue;
			if (NumKeys < 1 || NumKeys > 10000 ||
				(RotSize != 4 && RotSize != NumKeys * 4 && RotSize != 6 && RotSize != NumKeys * 6) ||
				(PosSize != 6 && PosSize != NumKeys * 6))
			{
				Ar.Seek(SavePos);
				return 0;
			}
			MaxKeys = max(MaxKeys, (int)NumKeys);
			Ar.Seek(Ar.Tell() + NumKeys + RotSize + PosSize);
			if (Ar.Tell() > Ar.GetStopper())
			{
				Ar.Seek(SavePos);
				return 0;
			}
		}
		Ar.Seek(SavePos);
		return MaxKeys;
	};
	for (int SeqIndex = 0; SeqIndex < MaxRawMoves && MotionSearchPos < MotionDataEnd - 8; SeqIndex++)
	{
		int RangeEnd = MotionDataEnd;
		int TrackPos = 0;
		int CompressType = 0;
		int TrackCount = 0;
		int RangeStart = MotionSearchPos;
		if (IsPlausibleInlineSCCTTracks(RangeStart, SeqIndex, &CompressType, true))
		{
			TrackPos = RangeStart;
		}
		else if (IsPlausibleBareSCCTTracks(RangeStart, &TrackCount))
		{
			TrackPos = RangeStart;
			CompressType = 0;
		}
		for (int Pos = RangeStart; !TrackPos && Pos < RangeEnd - 8; Pos++)
		{
			if (IsPlausibleInlineSCCTTracks(Pos, SeqIndex, &CompressType, false))
			{
				TrackPos = Pos;
				break;
			}
			if (IsPlausibleBareSCCTTracks(Pos, &TrackCount))
			{
				TrackPos = Pos;
				CompressType = 0;
				break;
			}
		}
		if (TrackPos)
		{
			int SavePos = Ar.Tell();
			Ar.Seek(TrackPos);
			if (CompressType)
			{
				int StoredCompressType;
				Ar << StoredCompressType;
			}
			Ar << AR_INDEX(TrackCount);
			Ar.Seek(SavePos);
		}
		if (getenv("SCCT_DEBUG_SEQMAP"))
			appPrintf("SCCT raw move %d: pos=%X trackCount=%d compress=%d\n", SeqIndex, TrackPos, TrackCount, CompressType);
		MotionChunk *Dst = new (Moves) MotionChunk;
		Dst->RootSpeed3D.Set(0, 0, 0);
		Dst->TrackTime = 1.0f;
		Dst->StartBone = 0;
		Dst->Flags = 0;
		int NumOutTracks = max(RefBones.Num(), TrackCount);
		Dst->BoneIndices.Empty(NumOutTracks);
		Dst->BoneIndices.AddZeroed(NumOutTracks);
		Dst->AnimTracks.Empty(NumOutTracks);
		Dst->AnimTracks.AddZeroed(NumOutTracks);
		for (int BoneIndex = 0; BoneIndex < NumOutTracks; BoneIndex++)
			Dst->BoneIndices[BoneIndex] = BoneIndex;

		if (!TrackPos)
		{
			if (SeqIndex >= SeqCount)
			{
				Moves.RemoveAt(Moves.Num() - 1);
				break;
			}
			appPrintf("SCCT MeshAnimation %s: no compressed tracks for sequence %d/%d\n", Name, SeqIndex, SeqCount);
			MoveRawKeyCounts.Add(1);
			MoveRawFrameSpans.Add(1);
			continue;
		}

		Ar.Seek(TrackPos);
		if (CompressType)
			Ar << CompressType;
		Ar << AR_INDEX(TrackCount);

		int MaxTrackKeys = 1;
		int MaxTrackFrameSpan = 1;
		int NonEmptyTrackCount = 0;
		int NonEmptyNonRootTrackCount = 0;
		for (int TrackIndex = 0; TrackIndex < TrackCount; TrackIndex++)
		{
			int SavePos = Ar.Tell();
			uint16 NumKeys, RotSize, PosSize;
			Ar << NumKeys << RotSize << PosSize;
			if (NumKeys == 0 && RotSize == 0 && PosSize == 0)
				continue;
			NonEmptyTrackCount++;
			if (TrackIndex > 0)
				NonEmptyNonRootTrackCount++;
			if (getenv("SCCT_DEBUG_TRACKS") && TrackIndex < 8)
			{
				appPrintf("SCCT track seq=%d track=%d pos=%X keys=%d rotSize=%d posSize=%d compress=%d\n",
					SeqIndex, TrackIndex, SavePos, NumKeys, RotSize, PosSize, CompressType);
			}
			Ar.Seek(SavePos);
			if (NumKeys > MaxTrackKeys)
				MaxTrackKeys = NumKeys;

			AnalogTrack TempTrack;
			float PosScale = 1.0f / 64.0f;
			bool KeepPositionKeys = (TrackIndex == 0);
			bool MirrorPositionY = (TrackIndex == 0);
			if (!SerializeSCCTRawTrack(TempTrack, MaxTrackKeys, CompressType, KeepPositionKeys, true, MirrorPositionY, PosScale))
				appError("Bad SCCT track %d/%d in sequence %d", TrackIndex, TrackCount, SeqIndex);
			for (int KeyIndex = 0; KeyIndex < TempTrack.KeyTime.Num(); KeyIndex++)
				MaxTrackFrameSpan = max(MaxTrackFrameSpan, appRound(TempTrack.KeyTime[KeyIndex]) + 1);
			if (Dst->AnimTracks.IsValidIndex(TrackIndex))
			{
				AnalogTrack &DstTrack = Dst->AnimTracks[TrackIndex];
				DstTrack.Flags = TempTrack.Flags;
				CopyArray(DstTrack.KeyQuat, TempTrack.KeyQuat);
				CopyArray(DstTrack.KeyPos, TempTrack.KeyPos);
				CopyArray(DstTrack.KeyTime, TempTrack.KeyTime);
				if (getenv("SCCT_DEBUG_TRACKS") && TrackIndex < 8)
				{
					appPrintf("  kept q=%d p=%d t=%d\n", DstTrack.KeyQuat.Num(), DstTrack.KeyPos.Num(), DstTrack.KeyTime.Num());
				}
			}
			TotalTracks++;
		}
		Dst->TrackTime = MaxTrackKeys;
		if (getenv("SCCT_DEBUG_SEQMAP"))
			appPrintf("SCCT raw move %d: end=%X keys=%d span=%d\n", SeqIndex, Ar.Tell(), MaxTrackKeys, MaxTrackFrameSpan);
		MotionSearchPos = Ar.Tell();

		if (CompressType == 0 && TrackCount <= 2 && NonEmptyTrackCount == 0)
		{
			if (getenv("SCCT_DEBUG_SEQMAP"))
				appPrintf("SCCT raw move %d: ignored empty bare block\n", SeqIndex);
			Moves.RemoveAt(Moves.Num() - 1);
			SeqIndex--;
			continue;
		}

		const bool bRootOnlyBareCompanion =
			CompressType == 0 &&
			TrackCount <= 2 &&
			NonEmptyTrackCount <= 1 &&
			NonEmptyNonRootTrackCount == 0 &&
			Dst->AnimTracks.Num() &&
			(Dst->AnimTracks[0].KeyQuat.Num() || Dst->AnimTracks[0].KeyPos.Num());
		if (bRootOnlyBareCompanion)
		{
			bool bMerged = false;
			if (Moves.Num() >= 2)
			{
				MotionChunk &PrevMove = Moves[Moves.Num() - 2];
				if (PrevMove.AnimTracks.Num() > 2 && MoveRawKeyCounts.Num())
				{
					MergeSCCTRootCompanion(PrevMove, *Dst);
					MoveRawKeyCounts[MoveRawKeyCounts.Num() - 1] = max(MoveRawKeyCounts[MoveRawKeyCounts.Num() - 1], MaxTrackKeys);
					MoveRawFrameSpans[MoveRawFrameSpans.Num() - 1] = max(MoveRawFrameSpans[MoveRawFrameSpans.Num() - 1], MaxTrackFrameSpan);
					bMerged = true;
					if (getenv("SCCT_DEBUG_SEQMAP"))
						appPrintf("SCCT raw move %d: merged root companion into previous move\n", SeqIndex);
				}
			}
			if (!bMerged)
			{
				CopySCCTMotionChunk(PendingRootCompanion, *Dst);
				PendingRootKeyCount = MaxTrackKeys;
				PendingRootFrameSpan = MaxTrackFrameSpan;
				bHasPendingRootCompanion = true;
				if (getenv("SCCT_DEBUG_SEQMAP"))
					appPrintf("SCCT raw move %d: holding root companion for next move\n", SeqIndex);
			}
			Moves.RemoveAt(Moves.Num() - 1);
			continue;
		}

		if (bHasPendingRootCompanion)
		{
			MergeSCCTRootCompanion(*Dst, PendingRootCompanion);
			MaxTrackKeys = max(MaxTrackKeys, PendingRootKeyCount);
			MaxTrackFrameSpan = max(MaxTrackFrameSpan, PendingRootFrameSpan);
			bHasPendingRootCompanion = false;
			if (getenv("SCCT_DEBUG_SEQMAP"))
				appPrintf("SCCT raw move %d: merged held root companion\n", SeqIndex);
		}

		MoveRawKeyCounts.Add(MaxTrackKeys);
		MoveRawFrameSpans.Add(MaxTrackFrameSpan);
	}

	TArray<MotionChunk> PairedMoves;
	int MoveIndex = 0;
	auto SCCTFramesMatch = [](int Frames, int Span) -> bool
	{
		if (Frames == Span)
			return true;
		// Single-key constant tracks are used for 2-frame pose clips.
		if (Frames == 2 && Span == 1)
			return true;
		if (Frames > 2 && abs(Frames - Span) <= 1)
			return true;
		return false;
	};
	auto SCCTFrameCost = [&](int Frames, int Span) -> int
	{
		if (Span <= 0)
			return 1000;
		if (SCCTFramesMatch(Frames, Span))
			return 0;
		int Diff = abs(Frames - Span);
		int SevereMismatch = Diff > max(4, Frames / 2);
		return (SevereMismatch ? 100 : 20) + Diff;
	};
	auto SCCTWindowCost = [&](int CandidateStart, int MoveStart, int Count) -> int
	{
		int Cost = 0;
		for (int i = 0; i < Count; i++)
		{
			int CandidatePos = CandidateStart + i;
			int MovePos = MoveStart + i;
			if (!SeqCandidates.IsValidIndex(CandidatePos) || !MoveRawFrameSpans.IsValidIndex(MovePos))
				return Cost + 1000 * (Count - i);
			Cost += SCCTFrameCost(SeqCandidates[CandidatePos].NumFrames, MoveRawFrameSpans[MovePos]);
		}
		return Cost;
	};
	if (!SeqCandidates.Num() && Moves.Num())
	{
		for (int RawMoveIndex = 0; RawMoveIndex < Moves.Num(); RawMoveIndex++)
		{
			FSCCTSeqInfo *Info = new (SeqInfos) FSCCTSeqInfo;
			memset(Info, 0, sizeof(FSCCTSeqInfo));
			Info->NameIndex = -1;
			Info->NumFrames = MoveRawFrameSpans.IsValidIndex(RawMoveIndex) ? max(1, MoveRawFrameSpans[RawMoveIndex]) : 1;
			Info->Rate = 15.0f;
			MotionChunk *PairedMove = new (PairedMoves) MotionChunk;
			CopySCCTMotionChunk(*PairedMove, Moves[RawMoveIndex]);
		}
	}
	for (int CandidateIndex = 0; SeqCandidates.Num() && CandidateIndex < SeqCandidates.Num() && MoveIndex < Moves.Num(); CandidateIndex++)
	{
		const FSCCTSeqInfo &Info = SeqCandidates[CandidateIndex];
		int RawSpan = MoveRawFrameSpans.IsValidIndex(MoveIndex) ? MoveRawFrameSpans[MoveIndex] : -1;
		if (CandidateIndex + 1 < SeqCandidates.Num())
		{
			const FSCCTSeqInfo &NextInfo = SeqCandidates[CandidateIndex + 1];
			bool CurrentExact = SCCTFramesMatch(Info.NumFrames, RawSpan);
			int LookaheadCount = min(8, min(SeqCandidates.Num() - CandidateIndex - 1, MoveRawFrameSpans.Num() - MoveIndex));
			int KeepCost = SCCTFrameCost(Info.NumFrames, RawSpan) + SCCTWindowCost(CandidateIndex + 1, MoveIndex + 1, LookaheadCount);
			int SkipCost = SCCTWindowCost(CandidateIndex + 1, MoveIndex, LookaheadCount);
			int SkipPenalty = (Info.ExtraSize == 0 || Info.NumFrames <= 2) ? 4 : 18;
			if (!CurrentExact && SCCTFramesMatch(NextInfo.NumFrames, RawSpan) && SkipCost + SkipPenalty + 20 < KeepCost)
			{
				if (getenv("SCCT_DEBUG_SEQMAP"))
					appPrintf("SCCT skip unpaired candidate %d (%s frames=%d pos=%X), raw move %d span=%d matches next %s (keepCost=%d skipCost=%d penalty=%d)\n",
						CandidateIndex, Package->GetName(Info.NameIndex), Info.NumFrames, Info.Pos, MoveIndex, RawSpan,
						Package->GetName(NextInfo.NameIndex), KeepCost, SkipCost, SkipPenalty);
				continue;
			}
		}
		if (MoveIndex + 1 < Moves.Num())
		{
			bool CurrentExact = SCCTFramesMatch(Info.NumFrames, RawSpan);
			int NextRawSpan = MoveRawFrameSpans[MoveIndex + 1];
			int LookaheadCount = min(8, min(SeqCandidates.Num() - CandidateIndex - 1, MoveRawFrameSpans.Num() - MoveIndex - 2));
			int KeepCost = SCCTFrameCost(Info.NumFrames, RawSpan) + SCCTWindowCost(CandidateIndex + 1, MoveIndex + 1, LookaheadCount);
			int SkipCost = SCCTFrameCost(Info.NumFrames, NextRawSpan) + SCCTWindowCost(CandidateIndex + 1, MoveIndex + 2, LookaheadCount);
			int SkipPenalty = (RawSpan <= 1) ? 4 : 18;
			if (!CurrentExact && SCCTFramesMatch(Info.NumFrames, NextRawSpan) && SkipCost + SkipPenalty + 20 < KeepCost)
			{
				if (getenv("SCCT_DEBUG_SEQMAP"))
					appPrintf("SCCT skip raw move %d span=%d before candidate %d (%s frames=%d pos=%X), next raw span=%d (keepCost=%d skipCost=%d penalty=%d)\n",
						MoveIndex, RawSpan, CandidateIndex, Package->GetName(Info.NameIndex), Info.NumFrames, Info.Pos,
						NextRawSpan, KeepCost, SkipCost, SkipPenalty);
				MoveIndex++;
				CandidateIndex--;
				continue;
			}
		}
		if (getenv("SCCT_DEBUG_SEQMAP"))
		{
			appPrintf("SCCT pair candidate %d (%s frames=%d pos=%X) -> raw move %d keys=%d span=%d\n",
				CandidateIndex, Package->GetName(Info.NameIndex), Info.NumFrames, Info.Pos, MoveIndex,
				MoveRawKeyCounts.IsValidIndex(MoveIndex) ? MoveRawKeyCounts[MoveIndex] : -1,
				MoveRawFrameSpans.IsValidIndex(MoveIndex) ? MoveRawFrameSpans[MoveIndex] : -1);
		}
		new (SeqInfos) FSCCTSeqInfo(Info);
		MotionChunk *PairedMove = new (PairedMoves) MotionChunk;
		CopySCCTMotionChunk(*PairedMove, Moves[MoveIndex]);
		MoveIndex++;
	}
	Moves.Empty(PairedMoves.Num());
	Moves.AddZeroed(PairedMoves.Num());
	for (int MoveCopyIndex = 0; MoveCopyIndex < PairedMoves.Num(); MoveCopyIndex++)
		CopySCCTMotionChunk(Moves[MoveCopyIndex], PairedMoves[MoveCopyIndex]);

	for (int i = 0; i < Moves.Num(); i++)
	{
		if (!Moves[i].AnimTracks.Num())
			continue;
		AnalogTrack &RootTrack = Moves[i].AnimTracks[0];
		if (!RootTrack.KeyPos.Num())
			continue;
		FVector RootBase = RootTrack.KeyPos[0];
		FVector RootMotionScale = GetSCCTRootMotionScale();
		for (int KeyIndex = 0; KeyIndex < RootTrack.KeyPos.Num(); KeyIndex++)
		{
			RootTrack.KeyPos[KeyIndex].X = (RootTrack.KeyPos[KeyIndex].X - RootBase.X) * RootMotionScale.X;
			RootTrack.KeyPos[KeyIndex].Y = -(RootTrack.KeyPos[KeyIndex].Y - RootBase.Y) * RootMotionScale.Y;
			RootTrack.KeyPos[KeyIndex].Z *= RootMotionScale.Z;
		}
		if (getenv("SCCT_DEBUG_ROOT") && SeqInfos.IsValidIndex(i))
		{
			appPrintf("SCCT root %d %s keys=%d times=%d\n", i, Package->GetName(SeqInfos[i].NameIndex), RootTrack.KeyPos.Num(), RootTrack.KeyTime.Num());
			for (int KeyIndex = 0; KeyIndex < RootTrack.KeyPos.Num(); KeyIndex++)
			{
				float Time = RootTrack.KeyTime.IsValidIndex(KeyIndex) ? RootTrack.KeyTime[KeyIndex] : (float)KeyIndex;
				const FVector &Pos = RootTrack.KeyPos[KeyIndex];
				appPrintf("  key %d t=%g pos=(%g,%g,%g)\n", KeyIndex, Time, Pos.X, Pos.Y, Pos.Z);
			}
		}
	}

	for (int i = 0; i < SeqInfos.Num() && i < Moves.Num(); i++)
		Moves[i].TrackTime = max(1, SeqInfos[i].NumFrames);

	AnimSeqs.Empty(SeqInfos.Num());
	for (int i = 0; i < SeqInfos.Num(); i++)
	{
		const FSCCTSeqInfo &Info = SeqInfos[i];
		FMeshAnimSeq *Seq = new (AnimSeqs) FMeshAnimSeq;
		if (Info.NameIndex > 0 && unsigned(Info.NameIndex) < Package->Summary.NameCount)
			Seq->Name = Package->GetName(Info.NameIndex);
		else
			Seq->Name = Name;
		Seq->Groups.Empty();
		Seq->StartFrame = Info.StartFrame;
		Seq->NumFrames = Info.NumFrames;
		Seq->Notifys.Empty();
		Seq->Rate = Info.Rate;
		Seq->f28 = 0;
	}

	appPrintf("SCCT MeshAnimation %s: standard-ish sequences=%d chunks=%d tracks=%d\n", Name, AnimSeqs.Num(), Moves.Num(), TotalTracks);
	DROP_REMAINING_DATA(Ar);

	unguard;
}

#endif // SPLINTER_CELL


#if LINEAGE2

void UMeshAnimation::SerializeLineageMoves(FArchive &Ar)
{
	guard(UMeshAnimation::SerializeLineageMoves);
	if (Ar.ArVer < 123 || Ar.ArLicenseeVer < 0x19)
	{
		// standard UE2 format
		Ar << Moves;
		return;
	}
	assert(Ar.IsLoading);
	int pos, count;						// pos = global skip pos, count = data count
	Ar << pos << AR_INDEX(count);
	Moves.Empty(count);
	for (int i = 0; i < count; i++)
	{
		int localPos;
		Ar << localPos;
		MotionChunk *M = new(Moves) MotionChunk;
		Ar << *M;
		assert(Ar.Tell() == localPos);
	}
	assert(Ar.Tell() == pos);
	unguard;
}

#endif // LINEAGE2


#if SWRC

struct FVectorShortSWRC
{
	int16					X, Y, Z;

	friend FArchive& operator<<(FArchive &Ar, FVectorShortSWRC &V)
	{
		return Ar << V.X << V.Y << V.Z;
	}

	FVector ToFVector(float Scale) const
	{
		FVector r;
		float s = Scale / 32767.0f;
		r.X = X * s;
		r.Y = Y * s;
		r.Z = Z * s;
		return r;
	}

	FQuat ToFQuatOld() const							// for version older than 151
	{
		static const float s = 0.000095876726845745f;	// pi/32767
		float X2 = X * s;
		float Y2 = Y * s;
		float Z2 = Z * s;
		float tmp = sqrt(X2*X2 + Y2*Y2 + Z2*Z2);
		if (tmp > 0)
		{
			float scale = sin(tmp / 2) / tmp;			// strange code ...
			X2 *= scale;
			Y2 *= scale;
			Z2 *= scale;
		}
		float W2 = 1.0f - (X2*X2 + Y2*Y2 + Z2*Z2);
		if (W2 < 0) W2 = 0;
		else W2 = sqrt(W2);
		FQuat r;
		r.Set(X2, Y2, Z2, W2);
		return r;
	}

	FQuat ToFQuat() const
	{
		static const float s = 0.70710678118f / 32767;	// int16 -> range(sqrt(2))
		float A = int16(X & 0xFFFE) * s;
		float B = int16(Y & 0xFFFE) * s;
		float C = int16(Z & 0xFFFE) * s;
		float D = sqrt(1.0f - (A*A + B*B + C*C));
		if (Z & 1) D = -D;
		FQuat r;
		if (Y & 1)
		{
			if (X & 1)	r.Set(D, A, B, C);
			else		r.Set(C, D, A, B);
		}
		else
		{
			if (X & 1)	r.Set(B, C, D, A);
			else		r.Set(A, B, C, D);
		}
		return r;
	}
};

SIMPLE_TYPE(FVectorShortSWRC, int16)


void AnalogTrack::SerializeSWRC(FArchive &Ar)
{
	guard(AnalogTrack::SerializeSWRC);

	float					 PosScale;
	TArray<FVectorShortSWRC> PosTrack;		// scaled by PosScale
	TArray<FVectorShortSWRC> RotTrack;
	TArray<uint8>			 TimeTrack;		// frame duration

	Ar << PosScale << PosTrack << RotTrack << TimeTrack;

	// unpack data

	// time track
	int NumKeys, i;
	NumKeys = TimeTrack.Num();
	KeyTime.Empty(NumKeys);
	KeyTime.AddUninitialized(NumKeys);
	int Time = 0;
	for (i = 0; i < NumKeys; i++)
	{
		KeyTime[i] = Time;
		Time += TimeTrack[i];
	}

	// rotation track
	NumKeys = RotTrack.Num();
	KeyQuat.Empty(NumKeys);
	KeyQuat.AddUninitialized(NumKeys);
	for (i = 0; i < NumKeys; i++)
	{
		FQuat Q;
		if (Ar.ArVer >= 151) Q = RotTrack[i].ToFQuat();
		else				 Q = RotTrack[i].ToFQuatOld();
		// note: FMeshBone rotation is mirrored for ArVer >= 142
		Q.X *= -1;
		Q.Y *= -1;
		Q.Z *= -1;
		KeyQuat[i] = Q;
	}

	// translation track
	NumKeys = PosTrack.Num();
	KeyPos.Empty(NumKeys);
	KeyPos.AddUninitialized(NumKeys);
	for (i = 0; i < NumKeys; i++)
		KeyPos[i] = PosTrack[i].ToFVector(PosScale);

	unguard;
}


void UMeshAnimation::SerializeSWRCAnims(FArchive &Ar)
{
	guard(UMeshAnimation::SerializeSWRCAnims);

	// serialize TArray<FSkelAnimSeq>
	// FSkelAnimSeq is a combined (and modified) FMeshAnimSeq and MotionChunk
	// count
	int NumAnims;
	Ar << AR_INDEX(NumAnims);		// TArray.Num
	// prepare arrays
	Moves.Empty(NumAnims);
	Moves.AddZeroed(NumAnims);
	AnimSeqs.Empty(NumAnims);
	AnimSeqs.AddZeroed(NumAnims);
	// serialize items
	for (int i = 0; i < NumAnims; i++)
	{
		// serialize
		int						f50;
		int						f54;
		int						f58;
		guard(FSkelAnimSeq<<);
		Ar << AnimSeqs[i];
		int drop;
		if (Ar.ArVer < 143) Ar << drop;
		Ar << f50 << f54;
		if (Ar.ArVer >= 143) Ar << f58;
		Ar << Moves[i].AnimTracks;
		unguard;
	}

	unguard;
}

#endif // SWRC


#if UC1

struct FVectorShortUC1
{
	int16					X, Y, Z;

	friend FArchive& operator<<(FArchive &Ar, FVectorShortUC1 &V)
	{
		return Ar << V.X << V.Y << V.Z;
	}

	operator FVector() const
	{
		FVector r;
		float s = 1.0f / 100.0f;
		r.X = X * s;
		r.Y = Y * s;
		r.Z = Z * s;
		return r;
	}
};

SIMPLE_TYPE(FVectorShortUC1, int16)

struct FQuatShortUC1
{
	int16					X, Y, Z, W;

	friend FArchive& operator<<(FArchive &Ar, FQuatShortUC1 &Q)
	{
		return Ar << Q.X << Q.Y << Q.Z << Q.W;
	}

	operator FQuat() const
	{
		FQuat r;
		float s = 1.0f / 16383.0f;
		r.X = X * s;
		r.Y = Y * s;
		r.Z = Z * s;
		r.W = W * s;
		return r;
	}
};

SIMPLE_TYPE(FQuatShortUC1, int16)


void AnalogTrack::SerializeUC1(FArchive &Ar)
{
	guard(AnalogTrack::SerializeUC1);
	TArray<FQuatShortUC1>   PackedKeyQuat;
	TArray<FVectorShortUC1> PackedKeyPos;
	Ar << Flags << PackedKeyQuat << PackedKeyPos << KeyTime;
	CopyArray(KeyQuat, PackedKeyQuat);
	CopyArray(KeyPos, PackedKeyPos);
	unguard;
}

#endif // UC1


#if UC2

// Special array type ...
// This array holds data in a separate stream, these data are originally serialized with
// a single FArchive::Serialize() call and may be placed inside any memory block
// (multiple arrays may be stored in a single memory block) - so, this is a good memory
// fragmentation and loading speed optimization, plus ability to place data into GPU
// memory.
template<class T> class TRawArrayUC2 : public TArray<T>
{
	// We require "using TArray<T>::*" for gcc 3.4+ compilation
	// http://gcc.gnu.org/gcc-3.4/changes.html
	// - look for "unqualified names"
	// - "temp.dep/3" section of the C++ standard [ISO/IEC 14882:2003]
	using TArray<T>::DataCount;
	using TArray<T>::Empty;
	using TArray<T>::GetData;
public:
	void Serialize(FArchive &DataAr, FArchive &CountAr)
	{
		guard(TRawArrayUC2<<);

		assert(DataAr.IsLoading && CountAr.IsLoading);
		// serialize memory size from "CountAr"
		unsigned DataSize;
		CountAr << DataSize;
		// compute items count
		int Count = DataSize / sizeof(T);
		assert(Count * sizeof(T) == DataSize);
		// setup array
		Empty(Count);
		DataCount = Count;
		// serialize items from "DataAr"
		T* Item = GetData();
		while (Count > 0)
		{
			DataAr << *Item;
			Item++;
			Count--;
		}

		unguard;
	}
};

// helper function for RAW_ARRAY macro
template<class T> inline TRawArrayUC2<T>& ToRawArrayUC2(TArray<T> &Arr)
{
	return (TRawArrayUC2<T>&)Arr;
}

#define RAW_ARRAY_UC2(Arr)		ToRawArrayUC2(Arr)


#endif // UC2


#if UNREAL25

struct FlexTrackBase
{
	virtual ~FlexTrackBase()
	{}
	virtual void Serialize(FArchive &Ar) = 0;
#if UC2
	virtual void Serialize2(FArchive &Ar, FArchive &Ar2)
	{
		appError("FlexTrack::Serialize2() is not implemented");
	}
#endif // UC2
	virtual void Decompress(AnalogTrack &T) = 0;
};

struct FlexTrackStatic : public FlexTrackBase
{
	FQuatFloat96NoW		KeyQuat;
	TArray<FVector>		KeyPos;

	virtual void Serialize(FArchive &Ar)
	{
		Ar << KeyQuat;
#if UC2
		if (Ar.Engine() == GAME_UE2X && Ar.ArVer >= 130)
			Ar << KeyPos;
		else
#endif // UC2
		{
			FVector pos;
			Ar << pos;
			KeyPos.Add(pos);
		}
	}
#if UC2
	virtual void Serialize2(FArchive &Ar, FArchive &Ar2)
	{
		int Count;
		Ar2 << Count; assert(Count == 12);
		Ar << KeyQuat;
		RAW_ARRAY_UC2(KeyPos).Serialize(Ar, Ar2);
	}
#endif // UC2

	virtual void Decompress(AnalogTrack &T)
	{
		T.KeyQuat.Add(KeyQuat);
		CopyArray(T.KeyPos, KeyPos);
	}
};

struct FlexTrack48 : public FlexTrackBase
{
	TArray<FQuatFixed48NoW>	KeyQuat;
	TArray<int16>		KeyTime;
	TArray<FVector>		KeyPos;

	virtual void Serialize(FArchive &Ar)
	{
		Ar << KeyQuat << KeyPos << KeyTime;
	}
#if UC2
	virtual void Serialize2(FArchive &Ar, FArchive &Ar2)
	{
		RAW_ARRAY_UC2(KeyQuat).Serialize(Ar, Ar2);
		RAW_ARRAY_UC2(KeyPos).Serialize(Ar, Ar2);
		RAW_ARRAY_UC2(KeyTime).Serialize(Ar, Ar2);
	}
#endif // UC2

	virtual void Decompress(AnalogTrack &T)
	{
		CopyArray(T.KeyQuat, KeyQuat);
		CopyArray(T.KeyPos,  KeyPos );
		CopyArray(T.KeyTime, KeyTime);
	}
};

struct FlexTrack48RotOnly : public FlexTrackBase
{
	TArray<FQuatFixed48NoW>	KeyQuat;
	TArray<int16>		KeyTime;
	FVector				KeyPos;

	virtual void Serialize(FArchive &Ar)
	{
		Ar << KeyQuat << KeyTime << KeyPos;
	}

	virtual void Decompress(AnalogTrack &T)
	{
		CopyArray(T.KeyQuat, KeyQuat);
		CopyArray(T.KeyTime, KeyTime);
		T.KeyPos.Empty(1);
		T.KeyPos.Add(KeyPos);
	}
};

#if UC2

// Animation without translation (translation is from bind pose)
struct FlexTrack5 : public FlexTrackBase
{
	TArray<FQuatFixed48NoW>	KeyQuat;
	TArray<int16>		KeyTime;

	virtual void Serialize(FArchive &Ar)
	{}

	virtual void Serialize2(FArchive &Ar, FArchive &Ar2)
	{
		RAW_ARRAY_UC2(KeyQuat).Serialize(Ar, Ar2);
		RAW_ARRAY_UC2(KeyTime).Serialize(Ar, Ar2);
	}

	virtual void Decompress(AnalogTrack &T)
	{
		CopyArray(T.KeyQuat, KeyQuat);
		CopyArray(T.KeyTime, KeyTime);
	}
};

// static pose with packed quaternion
struct FlexTrack6 : public FlexTrackBase
{
	FQuatFixed48NoW		KeyQuat;
	FVector				KeyPos;

	virtual void Serialize(FArchive &Ar)
	{}

	virtual void Serialize2(FArchive &Ar, FArchive &Ar2)
	{
		int Count;
		Ar2 << Count; assert(Count == 6);
		Ar << KeyQuat;
		Ar2 << Count; assert(Count == 12);
		Ar << KeyPos;
	}

	virtual void Decompress(AnalogTrack &T)
	{
		T.KeyQuat.Add(KeyQuat);
		T.KeyPos.Add(KeyPos);
	}
};

// static pose without translation
struct FlexTrack7 : public FlexTrackBase
{
	FQuatFixed48NoW		KeyQuat;

	virtual void Serialize(FArchive &Ar)
	{}

	virtual void Serialize2(FArchive &Ar, FArchive &Ar2)
	{
		int Count;
		Ar2 << Count;
		assert(Count == 6);
		Ar << KeyQuat;
	}

	virtual void Decompress(AnalogTrack &T)
	{
		T.KeyQuat.Add(KeyQuat);
	}
};

#endif // UC2


FlexTrackBase *CreateFlexTrack(int TrackType)
{
	switch (TrackType)
	{
	case 0:
		return NULL;		// no track for this bone (bone should be in a bind pose)

	case 1:
		return new FlexTrackStatic;

//	case 2:
//		// This type uses structure with TArray<FVector>, TArray<FQuatFloat96NoW> and TArray<int16>.
//		// It's Footprint() method returns 0, GetRotPos() does nothing, but serializer is working.
//		appError("Unsupported FlexTrack type=2");

	case 3:
		return new FlexTrack48;

	case 4:
		return new FlexTrack48RotOnly;

#if UC2
	case 5:
		return new FlexTrack5;

	case 6:
		return new FlexTrack6;

	case 7:
		return new FlexTrack7;
#endif // UC2

	default:
		appError("Unknown FlexTrack type=%d", TrackType);
	}
	return NULL;
}

struct FlexTrackBasePtr
{
	FlexTrackBase* Track;

	~FlexTrackBasePtr()
	{
		if (Track) delete Track;
	}

	friend FArchive& operator<<(FArchive &Ar, FlexTrackBasePtr &T)
	{
		guard(SerializeFlexTrack);
		int TrackType;
		Ar << TrackType;
#if UC2
		if (Ar.Engine() == GAME_UE2X && Ar.ArVer >= 130 && TrackType == 4) TrackType = 3;	// replaced type: FlexTrack48RotOnly -> FlexTrack48
#endif
		T.Track = CreateFlexTrack(TrackType);
#if UC2
		if (Ar.Engine() == GAME_UE2X && Ar.ArVer >= 147) return Ar;
#endif
		if (T.Track) T.Track->Serialize(Ar);
		return Ar;
		unguard;
	}
};

// serialize TArray<FlexTrackBasePtr>
void SerializeFlexTracks(FArchive &Ar, MotionChunk &M)
{
	guard(SerializeFlexTracks);

	TArray<FlexTrackBasePtr> FT;
	Ar << FT;
	int numTracks = FT.Num();
	if (!numTracks) return;
	M.AnimTracks.Empty(numTracks);
	M.AnimTracks.AddZeroed(numTracks);
	for (int i = 0; i < numTracks; i++)
		FT[i].Track->Decompress(M.AnimTracks[i]);

	unguard;
}

#endif // UNREAL25

#if TRIBES3

void FixTribesMotionChunk(MotionChunk &M)
{
	int numBones = M.AnimTracks.Num();
	for (int i = 0; i < numBones; i++)
	{
		AnalogTrack &A = M.AnimTracks[i];
		if (A.Flags & 0x1000)
		{
			// bone overridden by Impersonator LipSinc
			// remove translation and rotation tracks (they are not correct anyway)
			A.KeyQuat.Empty();
			A.KeyPos.Empty();
			A.KeyTime.Empty();
		}
	}
}

#endif // TRIBES3


#if UC2

struct MotionChunkUC2 : public MotionChunk
{
	TArray<FlexTrackBasePtr> FlexTracks;

	friend FArchive& operator<<(FArchive &Ar, MotionChunkUC2 &M)
	{
		//?? note: can merge this structure into MotionChunk (will require FlexTrack declarations in h-file)
		guard(MotionChunkUC2<<);
		assert(Ar.ArVer >= 147);
		// start is the same, but arrays are serialized in a different way
		Ar << M.RootSpeed3D << M.TrackTime << M.StartBone << M.Flags;
		if (Ar.ArVer < 149) Ar << M.BoneIndices;
		Ar << M.AnimTracks << M.RootTrack;
		if (M.Flags >= 3)
			Ar << M.FlexTracks;
		assert(M.AnimTracks.Num() == 0 || M.FlexTracks.Num() == 0); // only one kind of tracks at a time
		assert(M.Flags != 3);				// Version == 3 has TLazyArray<FlexTrack2>

		return Ar;
		unguard;
	}
};


bool UMeshAnimation::SerializeUE2XMoves(FArchive &Ar)
{
	guard(UMeshAnimation::SerializeUE2XMoves);
	if (Ar.ArVer < 147)
	{
		// standard UE2 format
		Ar << Moves;
		return true;
	}
	assert(Ar.IsLoading);

	// read FByteBuffer
	uint8 *BufferData = NULL;
	int DataSize;
	int DataFlag;
	Ar << DataSize;
	DataFlag = 0;
	if (Ar.ArLicenseeVer == 1)
		Ar << DataFlag;
#if 0
	assert(DataFlag == 0);
#else
	if (DataFlag != 0)
	{
		guard(GetExternalAnim);
		// animation is stored in xpr file
		int Size;
		BufferData = FindXprData(va("%s_anim", Name), &Size);
		if (!BufferData)
		{
			appNotify("Missing external animations for %s", Name);
			return false;
		}
		assert(DataSize <= Size);
		appPrintf("Loading external animation for %s\n", Name);
		unguard;
	}
#endif
	if (!DataFlag && DataSize)
	{
		BufferData = (uint8*)appMalloc(DataSize);
		Ar.Serialize(BufferData, DataSize);
	}
	TArray<MotionChunkUC2> Moves2;
	Ar << Moves2;

	FMemReader Reader(BufferData, DataSize);

	// serialize Moves2 and copy Moves2 to Moves
	int numMoves = Moves2.Num();
	Moves.Empty(numMoves);
	Moves.AddZeroed(numMoves);
	for (int mi = 0; mi < numMoves; mi++)
	{
		MotionChunkUC2 &M = Moves2[mi];
		MotionChunk &DM = Moves[mi];

		// serialize AnimTracks
		int numATracks = M.AnimTracks.Num();
		if (numATracks)
		{
			DM.AnimTracks.AddZeroed(numATracks);
			for (int ti = 0; ti < numATracks; ti++)
			{
				AnalogTrack &A = DM.AnimTracks[ti];
				RAW_ARRAY_UC2(A.KeyQuat).Serialize(Reader, Ar);
				RAW_ARRAY_UC2(A.KeyPos).Serialize(Reader, Ar);
				RAW_ARRAY_UC2(A.KeyTime).Serialize(Reader, Ar);
			}
		}
		// "serialize" RootTrack
		int i1, i2, i3;
		Ar << i1 << i2 << i3;				// KeyQuat, KeyPos and KeyTime
		assert(i1 == 0 && i2 == 0 && i3 == 0);
		// serialize FlexTracks
		int numFTracks = M.FlexTracks.Num();
		if (numFTracks)
		{
			DM.AnimTracks.AddZeroed(numFTracks);
			for (int ti = 0; ti < numFTracks; ti++)
			{
				FlexTrackBase *Track = M.FlexTracks[ti].Track;
				if (Track)
				{
					Track->Serialize2(Reader, Ar);
					Track->Decompress(DM.AnimTracks[ti]);
				}
//				else -- keep empty track, will be ignored by animation system
			}
		}
	}
	assert(Reader.GetStopper() == Reader.Tell());

	// cleanup
	if (BufferData) appFree(BufferData);

	return true;

	unguard;
}

#endif // UC2
