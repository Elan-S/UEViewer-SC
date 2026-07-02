#include "Core.h"
#include "UnCore.h"

#include "UnObject.h"
#include "UnSound.h"
#include "UnrealPackage/UnPackage.h"

#include "Exporters.h"


#define XMA_EXPORT		1


static void SaveSound(const UObject *Obj, void *Data, int DataSize, const char *DefExt)
{
	// check for enough place for header
	if (DataSize < 16)
	{
		appPrintf("... empty sound %s ?\n", Obj->Name);
		return;
	}

	const char *ext = DefExt;

	if (!memcmp(Data, "OggS", 4))
		ext = "ogg";
	else if (!memcmp(Data, "RIFF", 4))
		ext = "wav";
	else if (!memcmp(Data, "FSB4", 4))
		ext = "fsb";		// FMOD sound bank
	else if (!memcmp(Data, "MSFC", 4))
		ext = "mp3";		// PS3 MP3 codec

	FArchive *Ar = CreateExportArchive(Obj, EFileArchiveOptions::Default, "%s.%s", Obj->Name, ext);
	if (Ar)
	{
		Ar->Serialize(Data, DataSize);
		delete Ar;
	}
}

static const char* GetDareExportName(const USound* Snd)
{
	return !strnicmp(Snd->Name, "Play_", 5) ? Snd->Name + 5 : Snd->Name;
}

static uint32 ReadDareU32(const byte* Data)
{
	return Data[0] | (Data[1] << 8) | (Data[2] << 16) | (Data[3] << 24);
}

static void NormalizeDareName(const char* Src, char* Dst, int DstSize);

static bool IsDareSoundRef(const USound* Snd)
{
	if (!Snd->Package || Snd->Package->Game != GAME_SplinterCell) return false;
	if (Snd->RawData.Num() < 18) return false;

	const byte* Ref = &Snd->RawData[0];
	if (Snd->RawData.Num() == 18)
	{
		return Ref[1] == 0 && Ref[3] == 0 &&
			Ref[4] == 0xFF && Ref[5] == 0xFF && Ref[6] == 0xFF && Ref[7] == 0xFF;
	}
	else
	{
		return Ref[1] == 0 &&
			Ref[4] == 0xFF && Ref[5] == 0xFF && Ref[6] == 0xFF && Ref[7] == 0xFF;
	}
}

static bool IsDareLongNamedRef(const USound* Snd)
{
	return Snd->RawData.Num() > 18 && Snd->RawData[18] == '\\';
}

static bool DareBankHasExt(const char* BankName, const char* Ext)
{
	const char* Dot = strrchr(BankName, '.');
	return Dot && !stricmp(Dot, Ext);
}

static bool DareBankMatchesName(const char* BankName, const char* PackageName, const char* SoundName)
{
	char NormBank[128], NormPackage[128], NormSound[128];
	appStrncpyz(NormBank, BankName, ARRAY_COUNT(NormBank));
	char* Dot = strrchr(NormBank, '.');
	if (Dot) *Dot = 0;

	NormalizeDareName(NormBank, NormBank, ARRAY_COUNT(NormBank));
	NormalizeDareName(PackageName, NormPackage, ARRAY_COUNT(NormPackage));
	NormalizeDareName(SoundName, NormSound, ARRAY_COUNT(NormSound));

	return (NormBank[0] && (strstr(NormPackage, NormBank) || strstr(NormSound, NormBank)));
}

static void NormalizeDareName(const char* Src, char* Dst, int DstSize)
{
	int Pos = 0;
	for (const char* S = Src; *S && Pos < DstSize - 1; S++)
	{
		char C = *S;
		if (C >= 'A' && C <= 'Z') C = C - 'A' + 'a';
		if ((C >= 'a' && C <= 'z') || (C >= '0' && C <= '9'))
			Dst[Pos++] = C;
	}
	Dst[Pos] = 0;
}

static void GetCleanNameNoExt(const char* Filename, char* Out, int OutSize)
{
	const char* S = strrchr(Filename, '/');
	const char* S2 = strrchr(Filename, '\\');
	if (!S || S2 > S) S = S2;
	S = S ? S + 1 : Filename;

	appStrncpyz(Out, S, OutSize);
	char* Dot = strrchr(Out, '.');
	if (Dot) *Dot = 0;
}

static bool FileExistsInDareRoot(const char* Base, const char* Suffix, char* Out, int OutSize)
{
	appSprintf(Out, OutSize, "%s/%s", Base, Suffix);
	return appFileExists(Out);
}

static bool FindDareSoundRoot(const UObject* Obj, char* Out, int OutSize)
{
	const char* Root = appGetRootDirectory();
	if (!Root) Root = ".";

	char Base[MAX_PACKAGE_PATH];
	appStrncpyz(Base, Root, ARRAY_COUNT(Base));
	for (int i = 0; i < 8; i++)
	{
		if (FileExistsInDareRoot(Base, "Echelon.SP0", Out, OutSize))
		{
			char* Slash = strrchr(Out, '/');
			if (Slash) *Slash = 0;
			return true;
		}
		if (FileExistsInDareRoot(Base, "Sounds/Echelon.SP0", Out, OutSize))
		{
			appSprintf(Out, OutSize, "%s/Sounds", Base);
			return true;
		}
		if (FileExistsInDareRoot(Base, "Data/Sounds/Echelon.SP0", Out, OutSize))
		{
			appSprintf(Out, OutSize, "%s/Data/Sounds", Base);
			return true;
		}

		char* Slash = strrchr(Base, '/');
		char* Slash2 = strrchr(Base, '\\');
		if (!Slash || Slash2 > Slash) Slash = Slash2;
		if (!Slash) break;
		*Slash = 0;
	}

	appPrintf("WARNING: %s: couldn't locate DARE sound root near %s\n", Obj->Name, Root);
	return false;
}

static bool ReadFileSlice(const char* Filename, int64 Offset, int Size, TArray<byte>& Out)
{
	FFileReader Ar(Filename, EFileArchiveOptions::NoOpenError);
	if (!Ar.IsOpen()) return false;
	if (Offset < 0 || Size <= 0 || Offset + Size > Ar.GetFileSize64()) return false;
	Out.SetNumUninitialized(Size);
	Ar.Seek64(Offset);
	Ar.Serialize(&Out[0], Size);
	return true;
}

struct FDareLevelChunk
{
	char	MapName[64];
	int		Offset;
	int		Size;
};

static bool FindDareMapChunk(const char* MapsFile, const char* PackageName, FDareLevelChunk& OutChunk)
{
	TArray<byte> Header;
	if (!ReadFileSlice(MapsFile, 0, 0x1000, Header)) return false;

	char NormPackage[128];
	NormalizeDareName(PackageName, NormPackage, ARRAY_COUNT(NormPackage));

	for (int Pos = 0x48; Pos + 0x34 <= Header.Num(); Pos += 0x34)
	{
		int Offset = ReadDareU32(&Header[Pos]);
		int Size   = ReadDareU32(&Header[Pos + 4]);
		const char* Name = (const char*)&Header[Pos + 8];
		if (Offset <= 0 || Size <= 0 || !Name[0]) continue;

		char NormMap[128];
		NormalizeDareName(Name, NormMap, ARRAY_COUNT(NormMap));
		if (!NormMap[0]) continue;

		if (strstr(NormPackage, NormMap))
		{
			appStrncpyz(OutChunk.MapName, Name, ARRAY_COUNT(OutChunk.MapName));
			OutChunk.Offset = Offset;
			OutChunk.Size = Size;
			return true;
		}
	}

	return false;
}

struct FDareBankName
{
	char Name[128];
};

struct FDarePackageCache
{
	char PackageName[128];
	char SoundRoot[MAX_PACKAGE_PATH];
	TArray<byte> Chunk;
	TArray<FDareBankName> Banks;

	void Clear()
	{
		PackageName[0] = 0;
		SoundRoot[0] = 0;
		Chunk.Empty();
		Banks.Empty();
	}
};

static bool GetDareBankPath(const char* SoundRoot, const char* BankName, char* Out, int OutSize);

static bool IsPrintableDarePathChar(byte C)
{
	return (C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') ||
		C == '_' || C == '-' || C == '.' || C == '/';
}

static void AddDareBankName(TArray<FDareBankName>& Banks, const char* Name)
{
	for (int i = 0; i < Banks.Num(); i++)
	{
		if (!stricmp(Banks[i].Name, Name)) return;
	}
	FDareBankName& Bank = Banks.AddZeroed_GetRef();
	appStrncpyz(Bank.Name, Name, ARRAY_COUNT(Bank.Name));
}

static void ExtractDareBankNames(const TArray<byte>& Chunk, TArray<FDareBankName>& Banks)
{
	for (int i = 0; i + 4 < Chunk.Num(); i++)
	{
		bool IsBank = (!memcmp(&Chunk[i], ".SS0", 4) || !memcmp(&Chunk[i], ".LS0", 4));
		if (!IsBank) continue;

		int Start = i - 1;
		while (Start >= 0 && IsPrintableDarePathChar(Chunk[Start])) Start--;
		Start++;

		int Len = i + 4 - Start;
		if (Len <= 4 || Len >= 120) continue;

		char Name[128];
		memcpy(Name, &Chunk[Start], Len);
		Name[Len] = 0;
		AddDareBankName(Banks, Name);
	}
}

static bool GetDarePackageData(const USound* Snd, const char* SoundRoot, const char* PackageName, const TArray<byte>*& Chunk, const TArray<FDareBankName>*& Banks)
{
	static FDarePackageCache Cache;

	if (!stricmp(Cache.PackageName, PackageName) && !stricmp(Cache.SoundRoot, SoundRoot) && Cache.Chunk.Num() && Cache.Banks.Num())
	{
		Chunk = &Cache.Chunk;
		Banks = &Cache.Banks;
		return true;
	}

	Cache.Clear();
	appStrncpyz(Cache.PackageName, PackageName, ARRAY_COUNT(Cache.PackageName));
	appStrncpyz(Cache.SoundRoot, SoundRoot, ARRAY_COUNT(Cache.SoundRoot));

	const char* DefaultMapsNames[] = { "MAPS.SM0", "int/MAPS.LM0", "org/MAPS.LM0" };
	const char* VoiceMapsNames[] = { "int/MAPS.LM0", "org/MAPS.LM0", "MAPS.SM0" };
	const char** MapsNames = appStristr(PackageName, "Voice") ? VoiceMapsNames : DefaultMapsNames;
	for (int MapIndex = 0; MapIndex < 3; MapIndex++)
	{
		char MapsPath[MAX_PACKAGE_PATH];
		if (!GetDareBankPath(SoundRoot, MapsNames[MapIndex], MapsPath, ARRAY_COUNT(MapsPath))) continue;

		FDareLevelChunk ChunkInfo;
		if (!FindDareMapChunk(MapsPath, PackageName, ChunkInfo)) continue;
		if (!ReadFileSlice(MapsPath, ChunkInfo.Offset, ChunkInfo.Size, Cache.Chunk)) continue;

		ExtractDareBankNames(Cache.Chunk, Cache.Banks);
		if (!Cache.Banks.Num()) continue;

		Chunk = &Cache.Chunk;
		Banks = &Cache.Banks;
		return true;
	}

	return false;
}

static bool GetDareBankPath(const char* SoundRoot, const char* BankName, char* Out, int OutSize)
{
	if (FileExistsInDareRoot(SoundRoot, BankName, Out, OutSize)) return true;

	char LocalBank[MAX_PACKAGE_PATH];
	appSprintf(ARRAY_ARG(LocalBank), "int/%s", BankName);
	if (FileExistsInDareRoot(SoundRoot, LocalBank, Out, OutSize)) return true;

	appSprintf(ARRAY_ARG(LocalBank), "org/%s", BankName);
	if (FileExistsInDareRoot(SoundRoot, LocalBank, Out, OutSize)) return true;

	return false;
}

static bool LooksLikeDareSampleHeader(const byte* Data)
{
	if (Data[0] == 0x05 && Data[1] == 0 && Data[2] == 0 && Data[3] == 0)
		return true;
	if (!memcmp(Data, "RIFF", 4) || !memcmp(Data, "OggS", 4))
		return true;
	return false;
}

static int FindNextDareSampleHeader(const TArray<byte>& Data, int Start)
{
	for (int Pos = Start; Pos + 16 <= Data.Num(); Pos += 4)
	{
		if (Data[Pos] == 0x05 && Data[Pos + 1] == 0 && Data[Pos + 2] == 0 && Data[Pos + 3] == 0)
			return Pos;
	}
	return -1;
}

static int GetOggStreamSize(const byte* Data, int DataSize)
{
	int Pos = 0;
	while (Pos + 27 <= DataSize)
	{
		if (memcmp(Data + Pos, "OggS", 4))
			return 0;
		int PageSegments = Data[Pos + 26];
		if (Pos + 27 + PageSegments > DataSize)
			return 0;
		int PageSize = 27 + PageSegments;
		for (int i = 0; i < PageSegments; i++)
			PageSize += Data[Pos + 27 + i];
		if (Pos + PageSize > DataSize)
			return 0;
		byte HeaderType = Data[Pos + 5];
		Pos += PageSize;
		if (HeaderType & 0x04)
			return Pos;
	}
	return 0;
}

static int ClampDareInt(int Value, int MinValue, int MaxValue)
{
	if (Value < MinValue) return MinValue;
	if (Value > MaxValue) return MaxValue;
	return Value;
}

static int DecodeDareImaNibble(int Nibble, int& Predictor, int& StepIndex)
{
	static const int IndexTable[16] =
	{
		-1, -1, -1, -1, 2, 4, 6, 8,
		-1, -1, -1, -1, 2, 4, 6, 8
	};
	static const int StepTable[89] =
	{
		7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
		19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
		50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
		130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
		337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
		876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
		2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
		5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
		15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
	};

	int Step = StepTable[StepIndex];
	int Diff = Step >> 3;
	if (Nibble & 1) Diff += Step >> 2;
	if (Nibble & 2) Diff += Step >> 1;
	if (Nibble & 4) Diff += Step;
	if (Nibble & 8) Predictor -= Diff;
	else            Predictor += Diff;

	Predictor = ClampDareInt(Predictor, -32768, 32767);
	StepIndex = ClampDareInt(StepIndex + IndexTable[Nibble & 15], 0, 88);
	return Predictor;
}

static bool SaveDareDecodedWav(const USound* Snd, const byte* Data, int DataSize, const char* BankName, int BankOffset, const char* Note)
{
	if (DataSize < 0x30) return false;
	if (ReadDareU32(Data) != 5) return false;

	float Duration = *(const float*)(Data + 4);
	int Channels = ReadDareU32(Data + 8);
	int CompressedSize = ReadDareU32(Data + 0x0C);
	if (Duration <= 0.0f || Channels <= 0 || Channels > 2 || CompressedSize <= 0)
		return false;

	int HeaderSize = 0x30;
	if (HeaderSize + CompressedSize > DataSize)
		CompressedSize = DataSize - HeaderSize;
	if (CompressedSize <= 0) return false;

	int SamplesPerChannel = (CompressedSize * 2) / Channels;
	int SampleRate = (int)(SamplesPerChannel / Duration + 0.5f);
	if (SampleRate <= 0) SampleRate = 22050;

	TArray<byte> Pcm;
	Pcm.Empty(SamplesPerChannel * Channels * 2);
	Pcm.AddUninitialized(SamplesPerChannel * Channels * 2);

	int Predictor[2] = { 0, 0 };
	int StepIndex[2] = { 0, 0 };
	int OutPos = 0;
	int Channel = 0;
	const byte* Enc = Data + HeaderSize;
	for (int i = 0; i < CompressedSize; i++)
	{
		int Nibbles[2] = { Enc[i] & 15, Enc[i] >> 4 };
		for (int n = 0; n < 2; n++)
		{
			int Sample = DecodeDareImaNibble(Nibbles[n], Predictor[Channel], StepIndex[Channel]);
			Pcm[OutPos++] = Sample & 0xFF;
			Pcm[OutPos++] = (Sample >> 8) & 0xFF;
			Channel++;
			if (Channel >= Channels) Channel = 0;
		}
	}
	Pcm.SetNumUninitialized(OutPos);

	const char* ExportName = GetDareExportName(Snd);
	FArchive* Ar = CreateExportArchive(Snd, EFileArchiveOptions::Default, "%s.wav", ExportName);
	if (!Ar) return true;

	int DataChunkSize = Pcm.Num();
	int RiffSize = 36 + DataChunkSize;
	int FmtSize = 16;
	uint16 AudioFormat = 1;
	uint16 WavChannels = Channels;
	uint32 WavSampleRate = SampleRate;
	uint16 BitsPerSample = 16;
	uint16 BlockAlign = WavChannels * BitsPerSample / 8;
	uint32 ByteRate = WavSampleRate * BlockAlign;

	static const char* RIFF = "RIFF";
	static const char* WAVE = "WAVE";
	static const char* FMT  = "fmt ";
	static const char* DATA = "data";
	Ar->Serialize((char*)RIFF, 4);
	*Ar << RiffSize;
	Ar->Serialize((char*)WAVE, 4);
	Ar->Serialize((char*)FMT, 4);
	*Ar << FmtSize << AudioFormat << WavChannels << WavSampleRate << ByteRate << BlockAlign << BitsPerSample;
	Ar->Serialize((char*)DATA, 4);
	*Ar << DataChunkSize;
	Ar->Serialize(&Pcm[0], Pcm.Num());
	delete Ar;

	Ar = CreateExportArchive(Snd, EFileArchiveOptions::Default, "%s.dare.txt", ExportName);
	if (Ar)
	{
		char Text[1024];
		appSprintf(ARRAY_ARG(Text),
			"DARE sound reference\nobject=%s\nbank=%s\noffset=0x%X\nencoded_size=0x%X\nwav_sample_rate=%d\nchannels=%d\nnote=%s\n",
			ExportName, BankName, BankOffset, CompressedSize, SampleRate, Channels, Note);
		Ar->Serialize(Text, strlen(Text));
		delete Ar;
	}

	appPrintf("Decoded DARE sound %s from %s @ 0x%X to PCM WAV (%d Hz, %d ch)\n", Snd->Name, BankName, BankOffset, SampleRate, Channels);
	return true;
}

static bool ExportDareBankSlice(const USound* Snd, const char* BankPath, const char* BankName, int Offset)
{
	FFileReader Bank(BankPath, EFileArchiveOptions::NoOpenError);
	if (!Bank.IsOpen()) return false;
	const int64 BankSize64 = Bank.GetFileSize64();
	if (Offset < 0 || Offset + 16 > BankSize64) return false;

	const int MaxProbeSize = 8 * 1024 * 1024;
	int64 AvailableSize = BankSize64 - Offset;
	int ProbeSize = (AvailableSize < MaxProbeSize) ? (int)AvailableSize : MaxProbeSize;
	TArray<byte> Probe;
	Probe.SetNumUninitialized(ProbeSize);
	Bank.Seek(Offset);
	Bank.Serialize(&Probe[0], ProbeSize);
	if (!LooksLikeDareSampleHeader(&Probe[0])) return false;

	int End = FindNextDareSampleHeader(Probe, 16);
	int SliceSize = (End > 0) ? End : ProbeSize;
	if (ReadDareU32(&Probe[0]) == 5 && ProbeSize >= 0x30)
	{
		int CompressedSize = ReadDareU32(&Probe[0x0C]);
		if (CompressedSize > 0 && 0x30 + CompressedSize <= ProbeSize)
			SliceSize = 0x30 + CompressedSize;
	}

	if (!memcmp(&Probe[0], "OggS", 4))
	{
		int OggSize = GetOggStreamSize(&Probe[0], ProbeSize);
		if (OggSize > 0) SliceSize = OggSize;

		const char* ExportName = GetDareExportName(Snd);
		FArchive* OggAr = CreateExportArchive(Snd, EFileArchiveOptions::Default, "%s.ogg", ExportName);
		if (OggAr)
		{
			OggAr->Serialize(&Probe[0], SliceSize);
			delete OggAr;
		}

		FArchive* Meta = CreateExportArchive(Snd, EFileArchiveOptions::Default, "%s.dare.txt", ExportName);
		if (Meta)
		{
			char Text[1024];
			appSprintf(ARRAY_ARG(Text),
				"DARE sound reference\nobject=%s\nbank=%s\nbank_path=%s\noffset=0x%X\nsize=0x%X\nnote=bank slice is Ogg/Vorbis and was exported directly\n",
				ExportName, BankName, BankPath, Offset, SliceSize);
			Meta->Serialize(Text, strlen(Text));
			delete Meta;
		}
		appPrintf("Exported DARE sound %s from %s @ 0x%X as Ogg/Vorbis (%d bytes)\n", Snd->Name, BankName, Offset, SliceSize);
		return true;
	}

	const char* ExportName = GetDareExportName(Snd);
	FArchive* Ar = CreateExportArchive(Snd, EFileArchiveOptions::Default, "%s.dare", ExportName);
	if (!Ar) return true;
	Ar->Serialize(&Probe[0], SliceSize);
	delete Ar;

	Ar = CreateExportArchive(Snd, EFileArchiveOptions::Default, "%s.dare.txt", ExportName);
	if (Ar)
	{
		char Text[1024];
		appSprintf(ARRAY_ARG(Text),
			"DARE sound reference\nobject=%s\nbank=%s\nbank_path=%s\noffset=0x%X\nsize=0x%X\nnote=raw DARE bank slice; WAV decode is not implemented yet\n",
			ExportName, BankName, BankPath, Offset, SliceSize);
		Ar->Serialize(Text, strlen(Text));
		delete Ar;
	}

	appPrintf("Exported DARE sound %s from %s @ 0x%X (%d bytes)\n", Snd->Name, BankName, Offset, SliceSize);
	return true;
}

static bool TryExportDareSound(const USound* Snd)
{
	if (!IsDareSoundRef(Snd)) return false;

	const byte* Ref = &Snd->RawData[0];
	const byte EventId = (Snd->RawData.Num() == 18) ? Ref[0] : Ref[1];
	const byte GroupId = (Snd->RawData.Num() == 18) ? Ref[2] : Ref[3];
	const bool PreferOggStream = IsDareLongNamedRef(Snd) || appStristr(*Snd->Package->GetFilename(), "Voice");

	char SoundRoot[MAX_PACKAGE_PATH];
	if (!FindDareSoundRoot(Snd, SoundRoot, ARRAY_COUNT(SoundRoot))) return false;

	char PackageName[128];
	GetCleanNameNoExt(*Snd->Package->GetFilename(), PackageName, ARRAY_COUNT(PackageName));

	const TArray<byte>* ChunkPtr = NULL;
	const TArray<FDareBankName>* BanksPtr = NULL;
	if (GetDarePackageData(Snd, SoundRoot, PackageName, ChunkPtr, BanksPtr))
	{
		const TArray<byte>& Chunk = *ChunkPtr;
		const TArray<FDareBankName>& Banks = *BanksPtr;

		byte Pattern[4] = { EventId, 0, GroupId, 0 };
		for (int Pos = 0; Pos + 0x40 <= Chunk.Num(); Pos++)
		{
			if (memcmp(&Chunk[Pos], Pattern, 4)) continue;

			bool HasPreferredBank = false;
			for (int BankIndex = 0; BankIndex < Banks.Num(); BankIndex++)
			{
				if (DareBankMatchesName(Banks[BankIndex].Name, PackageName, Snd->Name))
				{
					HasPreferredBank = true;
					break;
				}
			}

			int PassCount = HasPreferredBank ? 1 : 2;
			for (int PreferPass = 0; PreferPass < PassCount; PreferPass++)
			{
				for (int BankIndex = 0; BankIndex < Banks.Num(); BankIndex++)
				{
					bool Preferred = DareBankMatchesName(Banks[BankIndex].Name, PackageName, Snd->Name);
					if ((PreferPass == 0) != Preferred) continue;
					if (PreferOggStream && !DareBankHasExt(Banks[BankIndex].Name, ".LS0")) continue;

					char BankPath[MAX_PACKAGE_PATH];
					if (!GetDareBankPath(SoundRoot, Banks[BankIndex].Name, BankPath, ARRAY_COUNT(BankPath))) continue;

					FFileReader Bank(BankPath, EFileArchiveOptions::NoOpenError);
					if (!Bank.IsOpen()) continue;
					int64 BankSize = Bank.GetFileSize64();

					if (PreferOggStream)
					{
						int LeadOffset = ReadDareU32(&Chunk[Pos + 8]);
						if (LeadOffset <= 0 || LeadOffset + 16 >= BankSize) continue;
					}

					int CandidatePositions[2];
					int CandidateCount;
					if (PreferOggStream)
					{
						CandidatePositions[0] = Pos + 0x10;
						CandidatePositions[1] = Pos + 0x30;
						CandidateCount = 2;
					}
					else
					{
						CandidatePositions[0] = Pos + 8;
						CandidatePositions[1] = Pos + 0x10;
						CandidateCount = 2;
					}
					for (int CandidateIndex = 0; CandidateIndex < CandidateCount; CandidateIndex++)
					{
						int CandidatePos = CandidatePositions[CandidateIndex];
						if (CandidatePos + 4 > Chunk.Num()) continue;

						int CandidateOffset = ReadDareU32(&Chunk[CandidatePos]);
						if (CandidateOffset <= 0 || CandidateOffset + 16 >= BankSize) continue;

						byte Header[16];
						Bank.Seek(CandidateOffset);
						Bank.Serialize(Header, 16);
						if (!LooksLikeDareSampleHeader(Header)) continue;
						if (PreferOggStream && memcmp(Header, "OggS", 4)) continue;

						return ExportDareBankSlice(Snd, BankPath, Banks[BankIndex].Name, CandidateOffset);
					}
				}
			}
		}
	}

	appPrintf("WARNING: %s: unresolved DARE sound ref event=0x%02X group=0x%02X\n", Snd->Name, EventId, GroupId);
	return false;
}


#if XMA_EXPORT

static void WriteRiffHeader(FArchive &Ar, int FileLength)
{
	assert(!Ar.IsLoading);

	static const char *RIFF = "RIFF";
	Ar.Serialize((char*)RIFF, 4);

	Ar << FileLength;

	static const char *WAVE = "WAVE";
	Ar.Serialize((char*)WAVE, 4);
}


static void WriteRiffChunk(FArchive &Ar, const char *id, int len)
{
	Ar.Serialize((char*)id, 4);
	Ar << len;
}


struct FXmaInfoHeader
{
	int				WaveFormatLength;
	int				SeekTableSize;
	int				CompressedDataSize;

	friend FArchive& operator<<(FArchive &Ar, FXmaInfoHeader &H)
	{
		return Ar << H.WaveFormatLength << H.SeekTableSize << H.CompressedDataSize;
	}
};


// structure from DX10 audiodefs.h
struct WAVEFORMATEX
{
	uint16			wFormatTag;				// Integer identifier of the format
	uint16			nChannels;				// Number of audio channels
	unsigned		nSamplesPerSec;			// Audio sample rate
	unsigned		nAvgBytesPerSec;		// Bytes per second (possibly approximate)
	uint16			nBlockAlign;			// Size in bytes of a sample block (all channels)
	uint16			wBitsPerSample;			// Size in bits of a single per-channel sample
	uint16			cbSize;					// Bytes of extra data appended to this struct

	friend FArchive& operator<<(FArchive &Ar, WAVEFORMATEX &V)
	{
		return Ar << V.wFormatTag << V.nChannels << V.nSamplesPerSec << V.nAvgBytesPerSec
				  << V.nBlockAlign << V.wBitsPerSample << V.cbSize;
	}
};


// structure from DX10 xma2defs.h
struct XMA2WAVEFORMATEX
{
	WAVEFORMATEX	wfx;

	uint16			NumStreams;				// Number of audio streams (1 or 2 channels each)
	unsigned		ChannelMask;			// Spatial positions of the channels in this file,
											// stored as SPEAKER_xxx values (see audiodefs.h)
	unsigned		SamplesEncoded;			// Total number of PCM samples the file decodes to
	unsigned		BytesPerBlock;			// XMA block size (but the last one may be shorter)
	unsigned		PlayBegin;				// First valid sample in the decoded audio
	unsigned		PlayLength;				// Length of the valid part of the decoded audio
	unsigned		LoopBegin;				// Beginning of the loop region in decoded sample terms
	unsigned		LoopLength;				// Length of the loop region in decoded sample terms
	byte			LoopCount;				// Number of loop repetitions; 255 = infinite
	byte			EncoderVersion;			// Version of XMA encoder that generated the file
	uint16			BlockCount;				// XMA blocks in file (and entries in its seek table)

	friend FArchive& operator<<(FArchive &Ar, XMA2WAVEFORMATEX &V)
	{
		return Ar << V.wfx << V.NumStreams << V.ChannelMask << V.SamplesEncoded << V.BytesPerBlock
				  << V.PlayBegin << V.PlayLength << V.LoopBegin << V.LoopLength << V.LoopCount
				  << V.EncoderVersion << V.BlockCount;
	}
};


static bool SaveXMASound(const UObject *Obj, void *Data, int DataSize, const char *DefExt)
{
	// check for enough place for header
	if (DataSize < 16)
	{
		appPrintf("ERROR: %s'%s': empty data\n", Obj->GetClassName(), Obj->Name);
		return false;
	}

	FMemReader Reader(Data, DataSize);
	Reader.ReverseBytes = true;

	FXmaInfoHeader Hdr;
	Reader << Hdr;

	int ComputedDataSize = Reader.Tell() + Hdr.WaveFormatLength + Hdr.SeekTableSize + Hdr.CompressedDataSize;
	if (ComputedDataSize != DataSize)
	{
		if (ComputedDataSize > DataSize)
		{
			// does not fit into
			appPrintf("ERROR: %s'%s': wrong data\n", Obj->GetClassName(), Obj->Name);
			return false;
		}
		appPrintf("WARNING: %s'%s': wrong data\n", Obj->GetClassName(), Obj->Name);
	}

	// +4 bytes - RIFF "WAVE" id
	// +8 bytes - fmt or XMA2 chunk header
	// +8 bytes - data chunk header
	int ResultFileSize = Hdr.WaveFormatLength + /*??Hdr.SeekTableSize+*/ Hdr.CompressedDataSize + (4+8+8);
	FArchive *Ar;

	if (Hdr.WaveFormatLength == 0x34)						// sizeof(XMA2WAVEFORMATEX)
	{
		Ar = CreateExportArchive(Obj, EFileArchiveOptions::Default, "%s.%s", Obj->Name, DefExt);
		if (!Ar) return false;

		WriteRiffHeader(*Ar, ResultFileSize);
		WriteRiffChunk(*Ar, "fmt ", Hdr.WaveFormatLength);

		XMA2WAVEFORMATEX fmt;
		// read with conversion from big-endian to little-endian
		Reader << fmt;
		// write in little-endian format
		(*Ar) << fmt;
	}
	else if (Hdr.WaveFormatLength == 0x2C)					// sizeof(XMA2WAVEFORMAT)
	{
		Ar = CreateExportArchive(Obj, EFileArchiveOptions::Default, "%s.%s", Obj->Name, DefExt);
		if (!Ar) return false;

		WriteRiffHeader(*Ar, ResultFileSize);
		WriteRiffChunk(*Ar, "XMA2", Hdr.WaveFormatLength);

		// XMA2WAVEFORMAT should be stored in big-endian format, so no byte swapping performed
		Ar->Serialize((byte*)Data + Reader.Tell(), Hdr.WaveFormatLength);
		Reader.Seek(Reader.Tell() + Hdr.WaveFormatLength);	// skip WAVEFORMAT
	}
	else
	{
		appPrintf("ERROR: %s'%s': unknown XBox360 WAVEFORMAT - %X bytes\n", Obj->GetClassName(), Obj->Name, Hdr.WaveFormatLength);
		return false;
	}

	//?? create "seek chunk"
	// write data chunk
	WriteRiffChunk(*Ar, "data", Hdr.CompressedDataSize);
	Ar->Serialize((byte*)Data + Reader.Tell() + Hdr.SeekTableSize, Hdr.CompressedDataSize);

	// check correctness of ResultFileSize - should equal to file length -8 bytes (exclude RIFF header)
	assert(Ar->Tell() == ResultFileSize + 8);

	delete Ar;
	return true;
}

#endif // XMA_EXPORT


void ExportSound(const USound *Snd)
{
	if (Snd->Package && Snd->Package->Game == GAME_SplinterCell && getenv("SC_DARE_DEBUG"))
	{
		appPrintf("SC DARE sound %s raw=%d", Snd->Name, Snd->RawData.Num());
		int Count = Snd->RawData.Num() < 96 ? Snd->RawData.Num() : 96;
		for (int i = 0; i < Count; i++)
			appPrintf(" %02X", Snd->RawData[i]);
		appPrintf("\n");
	}
	if (Snd->Package && Snd->Package->Game == GAME_SplinterCell && IsDareSoundRef(Snd))
	{
		if (strnicmp(Snd->Name, "Play_", 5))
			return;
		if (TryExportDareSound(Snd))
			return;
		appPrintf("WARNING: %s: unresolved DARE Play event, skipping raw .unk export\n", Snd->Name);
		return;
	}
	if (TryExportDareSound(Snd))
		return;
	SaveSound(Snd, (void*)&Snd->RawData[0], Snd->RawData.Num(), "unk");
}


#if UNREAL3

void ExportSoundNodeWave(const USoundNodeWave *Snd)
{
	// select bulk containing data
	const FByteBulkData *bulk = NULL;
	const char *ext = "unk";
	int extraHeaderSize = 0;

	if (Snd->RawData.ElementCount)
	{
		bulk = &Snd->RawData;
	}
	else if (Snd->CompressedPCData.ElementCount)
	{
		bulk = &Snd->CompressedPCData;
	}
	else if (Snd->CompressedXbox360Data.ElementCount)
	{
		bulk = &Snd->CompressedXbox360Data;
		ext  = "x360audio";
#if XMA_EXPORT
		if (SaveXMASound(Snd, bulk->BulkData, bulk->ElementCount, "xma")) return;
		// else - detect format by data tags, like for PC
#endif
	}
	else if (Snd->CompressedPS3Data.ElementCount)
	{
		bulk = &Snd->CompressedPS3Data;
		ext  = "ps3audio";
		extraHeaderSize = 16;
		//!! note: has up to 4 sounds in single object
		//!! bulk data starts with int32[4] holding sizes of all sounds
		//!! 0 means no data for particular object
		//!! data encoded in MP3 format
	}
	else if (Snd->CompressedWiiUData.ElementCount)
	{
		bulk = &Snd->CompressedWiiUData;
		ext = "wiiu";
	}
	else if (Snd->CompressedIPhoneData.ElementCount)
	{
		bulk = &Snd->CompressedIPhoneData;
	}

	if (bulk)
	{
		SaveSound(Snd, OffsetPointer(bulk->BulkData, extraHeaderSize), bulk->ElementCount - extraHeaderSize, ext);
	}
}

#endif // UNREAL3

#if UNREAL4

void ExportSoundWave4(const USoundWave *Snd)
{
	// select bulk containing data
	const FByteBulkData *bulk = NULL;
	const char *ext = "unk";
	int extraHeaderSize = 0;

	if (Snd->RawData.ElementCount)
	{
		bulk = &Snd->RawData;
	}
	else if (Snd->CompressedFormatData.Num())
	{
		bulk = &Snd->CompressedFormatData[0].Data;
		ext = *Snd->CompressedFormatData[0].FormatName; // "OGG"
	}

	if (bulk)
	{
		SaveSound(Snd, bulk->BulkData, bulk->ElementCount, ext);
	}
	else if (Snd->StreamingChunks.Num())
	{
		guard(StreamedSound);
		const char* ext = *Snd->StreamedFormat;
		if (!strcmp(ext, "OPUS")) ext = "ue4opus";
		FArchive *Ar = CreateExportArchive(Snd, EFileArchiveOptions::Default, "%s.%s", Snd->Name, ext);
		if (Ar)
		{
			for (int i = 0; i < Snd->StreamingChunks.Num(); i++)
			{
				const FStreamedAudioChunk& Chunk = Snd->StreamingChunks[i];
				assert(Chunk.DataSize >= Chunk.AudioDataSize);
				assert(Chunk.DataSize == Chunk.Data.ElementCount);
				// Load bulk into memory
				Chunk.Data.SerializeData(Snd);
				// Export data
				Ar->Serialize(Chunk.Data.BulkData, Chunk.AudioDataSize);
			}
			delete Ar;
		}
		unguardf("Format=%s", *Snd->StreamedFormat);
	}
	else
	{
		appPrintf("... empty sound %s (streamed sound or unrecognized format)\n", Snd->Name);
	}
}

#endif // UNREAL4
