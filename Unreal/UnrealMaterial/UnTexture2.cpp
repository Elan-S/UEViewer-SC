#include "Core.h"
#include "UnCore.h"
#include "UnObject.h"
#include "UnMaterial.h"
#include "UnMaterial2.h"
#include "GameSpecific/UnUbisoft.h"
#include "FileSystem/GameFileSystem.h"
#include "UnrealPackage/UnPackage.h"

//#define XPR_DEBUG			1

#if SPLINTER_CELL
static int GetScdaDxt1MipSize(int USize, int VSize)
{
	return max(1, (USize + 3) / 4) * max(1, (VSize + 3) / 4) * 8;
}

static int GetScdaDxt1ChainSize(int USize, int VSize)
{
	int Size = 0;
	while (true)
	{
		Size += GetScdaDxt1MipSize(USize, VSize);
		if (USize <= 4 && VSize <= 4)
			break;
		USize = max(1, USize >> 1);
		VSize = max(1, VSize >> 1);
	}
	return Size;
}
#endif // SPLINTER_CELL

#if LEAD
static int GetSCConvDxtMipSize(int USize, int VSize, int BlockBytes)
{
	int BlocksX = max(1, (USize + 3) / 4);
	int BlocksY = max(1, (VSize + 3) / 4);
	return BlocksX * BlocksY * BlockBytes;
}

static int SCConvLog2(int Value)
{
	int Log = 0;
	while ((1 << Log) < Value)
		Log++;
	return Log;
}

static unsigned GetSCConvXbox360TiledOffset(int x, int y, int width, int logBpb)
{
	int alignedWidth = Align(width, 32);
	int macro  = ((x >> 5) + (y >> 5) * (alignedWidth >> 5)) << (logBpb + 7);
	int micro  = ((x & 7) + ((y & 0xE) << 2)) << logBpb;
	int offset = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 1) << 4);
	return (((offset & ~0x1FF) << 3) +
			((y & 16) << 7) +
			((offset & 0x1C0) << 2) +
			(((((y & 8) >> 2) + (x >> 3)) & 3) << 6) +
			(offset & 0x3F)
			) >> logBpb;
}

static bool UntileSCConvXbox360Dxt(byte *Data, int USize, int VSize, ETextureFormat Format, int DataSize)
{
	guard(UntileSCConvXbox360Dxt);

	const ETexturePixelFormat PixelFormat = (Format == TEXF_DXT1) ? TPF_DXT1 : TPF_DXT5;
	const CPixelFormatInfo &Info = PixelFormatInfo[PixelFormat];
	if (!Info.X360AlignX || DataSize <= 0)
		return false;

	const int AlignedWidth = Align(USize, Info.X360AlignX);
	const int AlignedHeight = Align(VSize, Info.X360AlignY);
	const int TiledBlockWidth = AlignedWidth / Info.BlockSizeX;
	const int TiledBlockHeight = AlignedHeight / Info.BlockSizeY;
	const int OriginalBlockWidth = max(1, USize / Info.BlockSizeX);
	const int OriginalBlockHeight = max(1, VSize / Info.BlockSizeY);
	const int NeededBlocks = TiledBlockWidth * TiledBlockHeight;
	const int AvailableBlocks = DataSize / Info.BytesPerBlock;
	if (NeededBlocks > AvailableBlocks)
		return false;

	const int LinearSize = OriginalBlockWidth * OriginalBlockHeight * Info.BytesPerBlock;
	byte *Untiled = new byte[LinearSize];
	const int LogBpp = SCConvLog2(Info.BytesPerBlock);
	for (int dy = 0; dy < OriginalBlockHeight; dy++)
	{
		for (int dx = 0; dx < OriginalBlockWidth; dx++)
		{
			unsigned SwzAddr = GetSCConvXbox360TiledOffset(dx, dy, TiledBlockWidth, LogBpp);
			if (SwzAddr >= (unsigned)AvailableBlocks)
			{
				delete[] Untiled;
				return false;
			}
			memcpy(Untiled + (dy * OriginalBlockWidth + dx) * Info.BytesPerBlock,
				Data + SwzAddr * Info.BytesPerBlock,
				Info.BytesPerBlock);
		}
	}

	appReverseBytes(Untiled, LinearSize / 2, 2);
	memcpy(Data, Untiled, LinearSize);
	delete[] Untiled;
	return true;

	unguard;
}

static unsigned SCConvPart1By1(unsigned Value)
{
	Value &= 0x0000ffff;
	Value = (Value | (Value << 8)) & 0x00FF00FF;
	Value = (Value | (Value << 4)) & 0x0F0F0F0F;
	Value = (Value | (Value << 2)) & 0x33333333;
	Value = (Value | (Value << 1)) & 0x55555555;
	return Value;
}

static bool UntileSCConvMortonDxt(byte *Data, int USize, int VSize, ETextureFormat Format, int DataSize)
{
	guard(UntileSCConvMortonDxt);

	const int BlockBytes = (Format == TEXF_DXT1) ? 8 : 16;
	const int BlockWidth = max(1, (USize + 3) / 4);
	const int BlockHeight = max(1, (VSize + 3) / 4);
	const int BlockCount = BlockWidth * BlockHeight;
	if (BlockCount * BlockBytes > DataSize)
		return false;

	byte *Untiled = new byte[BlockCount * BlockBytes];
	for (int y = 0; y < BlockHeight; y++)
	{
		for (int x = 0; x < BlockWidth; x++)
		{
			unsigned SrcBlock = SCConvPart1By1(x) | (SCConvPart1By1(y) << 1);
			if (SrcBlock >= (unsigned)BlockCount)
			{
				delete[] Untiled;
				return false;
			}
			memcpy(Untiled + (y * BlockWidth + x) * BlockBytes, Data + SrcBlock * BlockBytes, BlockBytes);
		}
	}
	if (getenv("SC_CONV_TEX_SWAP_ONLY"))
		appReverseBytes(Untiled, (BlockCount * BlockBytes) / 2, 2);
	memcpy(Data, Untiled, BlockCount * BlockBytes);
	delete[] Untiled;
	return true;

	unguard;
}

static byte GetSCConvTextureBits(int Size)
{
	byte Bits = 0;
	while ((1 << Bits) < Size && Bits < 31)
		Bits++;
	return Bits;
}

static int GetSCConvDxtChainSize(int USize, int VSize, int BlockBytes)
{
	int Size = 0;
	while (true)
	{
		Size += GetSCConvDxtMipSize(USize, VSize, BlockBytes);
		if (USize <= 4 || VSize <= 4)
			break;
		USize = max(1, USize >> 1);
		VSize = max(1, VSize >> 1);
	}
	return Size;
}

static bool InferSCConvTextureFormat(int DataSize, int &USize, int &VSize, ETextureFormat &Format)
{
	for (int U = 4096; U >= 1; U >>= 1)
	{
		for (int V = 4096; V >= 1; V >>= 1)
		{
			if (GetSCConvDxtChainSize(U, V, 8) == DataSize)
			{
				USize = U;
				VSize = V;
				Format = TEXF_DXT1;
				return true;
			}
		}
	}
	for (int U = 4096; U >= 1; U >>= 1)
	{
		for (int V = 4096; V >= 1; V >>= 1)
		{
			if (GetSCConvDxtChainSize(U, V, 16) == DataSize)
			{
				USize = U;
				VSize = V;
				Format = TEXF_DXT5;
				return true;
			}
		}
	}
	for (int Side = 4096; Side >= 1; Side >>= 1)
	{
		if (GetSCConvDxtChainSize(Side, Side, 8) == DataSize)
		{
			USize = VSize = Side;
			Format = TEXF_DXT1;
			return true;
		}
		if (GetSCConvDxtChainSize(Side, Side, 16) == DataSize)
		{
			USize = VSize = Side;
			Format = TEXF_DXT5;
			return true;
		}
	}
	return false;
}

static bool ReadSCConvLeadTexturePayload(UTexture &Tex, FArchive &Ar, int Stop, bool bXbox360Tiled = false)
{
	guard(ReadSCConvLeadTexturePayload);

	int PayloadStart = Ar.Tell();

	int HeaderCandidates[5];
	HeaderCandidates[0] = PayloadStart + 4;
	HeaderCandidates[1] = PayloadStart;
	HeaderCandidates[2] = PayloadStart;
	HeaderCandidates[3] = PayloadStart + 8;
	HeaderCandidates[4] = PayloadStart + 5;

	int HeaderStart = -1;
	int DataSize = 0;
	int DataStart = 0;
	for (int i = 0; i < ARRAY_COUNT(HeaderCandidates); i++)
	{
		const int TryHeader = HeaderCandidates[i];
		if (TryHeader < PayloadStart || TryHeader + 0xB8 > Stop)
			continue;
		Ar.Seek(TryHeader + 0xA4);
		int TryDataSize;
		Ar << TryDataSize;
		const int TryDataStart = TryHeader + 0xB4;
		if (TryDataSize <= 0 || TryDataStart + TryDataSize > Stop)
			continue;
		HeaderStart = TryHeader;
		DataSize = TryDataSize;
		DataStart = TryDataStart;
		break;
	}
	if (HeaderStart < 0)
	{
		static const int FallbackDataStarts[] = { 0xB8, 0xB9, 0xB4, 0xB5 };
		for (int i = 0; i < ARRAY_COUNT(FallbackDataStarts); i++)
		{
			const int TryDataStart = PayloadStart + FallbackDataStarts[i];
			const int TryDataSize = Stop - TryDataStart;
			int TryUSize = Tex.USize;
			int TryVSize = Tex.VSize;
			ETextureFormat TryFormat = Tex.Format;
			if (TryDataSize <= 0)
				continue;
			if (TryUSize > 0 && TryVSize > 0 &&
				(GetSCConvDxtChainSize(TryUSize, TryVSize, 8) == TryDataSize ||
				 GetSCConvDxtChainSize(TryUSize, TryVSize, 16) == TryDataSize))
			{
				HeaderStart = PayloadStart;
				DataSize = TryDataSize;
				DataStart = TryDataStart;
				break;
			}
			if (InferSCConvTextureFormat(TryDataSize, TryUSize, TryVSize, TryFormat))
			{
				HeaderStart = PayloadStart;
				DataSize = TryDataSize;
				DataStart = TryDataStart;
				break;
			}
		}
		if (HeaderStart < 0)
			return false;
	}

	int USize = Tex.USize;
	int VSize = Tex.VSize;
	ETextureFormat Format = Tex.Format;
	if (getenv("SC_CONV_TEX_DEBUG"))
	{
		appPrintf("SCConv LeadTexture meta: %s %dx%d %s dataSize=%X header=%08X data=%08X\n",
			Tex.Name, USize, VSize, EnumToName(Format), DataSize, HeaderStart, DataStart);
		int SavePos = Ar.Tell();
		for (int Dbg = 0; Dbg + 4 <= 0xB8 && HeaderStart + Dbg + 4 <= Stop; Dbg += 4)
		{
			int Value;
			Ar.Seek(HeaderStart + Dbg);
			Ar << Value;
			appPrintf("  hdr+%02X = %08X (%d)\n", Dbg, Value, Value);
		}
		Ar.Seek(SavePos);
	}
	if (USize > 0 && VSize > 0 && GetSCConvDxtChainSize(USize, VSize, 8) == DataSize)
	{
		Format = TEXF_DXT1;
	}
	else if (USize > 0 && VSize > 0 && GetSCConvDxtChainSize(USize, VSize, 16) == DataSize)
	{
		Format = TEXF_DXT5;
	}
	else
	{
		if (!InferSCConvTextureFormat(DataSize, USize, VSize, Format))
			return false;
	}

	int BlockBytes = (Format == TEXF_DXT1) ? 8 : 16;
	Tex.Format = Format;
	Tex.USize = USize;
	Tex.VSize = VSize;
	Tex.UBits = GetSCConvTextureBits(USize);
	Tex.VBits = GetSCConvTextureBits(VSize);
	Tex.UClamp = USize;
	Tex.VClamp = VSize;

	Tex.Mips.Empty();
	int Pos = DataStart;
	while (true)
	{
		const int MipSize = GetSCConvDxtMipSize(USize, VSize, BlockBytes);
		if (Pos + MipSize > DataStart + DataSize)
			break;

		FMipmap *Mip = new (Tex.Mips) FMipmap;
		Mip->USize = USize;
		Mip->VSize = VSize;
		Mip->UBits = GetSCConvTextureBits(USize);
		Mip->VBits = GetSCConvTextureBits(VSize);
		Mip->DataArray.AddUninitialized(MipSize);
		Ar.Seek(Pos);
		Ar.Serialize(Mip->DataArray.GetData(), MipSize);
		if (bXbox360Tiled && getenv("SC_CONV_TEX_MORTON"))
			UntileSCConvMortonDxt(Mip->DataArray.GetData(), USize, VSize, Format, MipSize);
		else if (bXbox360Tiled && !getenv("SC_CONV_TEX_NO_UNTILE"))
			UntileSCConvXbox360Dxt(Mip->DataArray.GetData(), USize, VSize, Format, MipSize);
		else if (bXbox360Tiled && getenv("SC_CONV_TEX_SWAP_ONLY"))
			appReverseBytes(Mip->DataArray.GetData(), MipSize / 2, 2);
		Pos += MipSize;

		if (USize <= 4 || VSize <= 4)
			break;
		USize = max(1, USize >> 1);
		VSize = max(1, VSize >> 1);
	}

	if (getenv("SC_CONV_TEX_DEBUG"))
		appPrintf("SCConv LeadTexture: %s %dx%d %s mips=%d data=%08X size=%X\n",
			Tex.Name, Tex.USize, Tex.VSize, EnumToName(Tex.Format), Tex.Mips.Num(), DataStart, DataSize);
	return Tex.Mips.Num() > 0;

	unguard;
}

static void GetSCConvLeadTextureName(const char *Name, char *Out, int OutSize)
{
	appStrncpyz(Out, Name, OutSize);
	char *L3d = strstr(Out, "_l3d");
	if (L3d && !L3d[4])
		*L3d = 0;
}

static bool ShouldSCConvSearchLeadTextures()
{
	return getenv("SC_CONV_ENABLE_TEXTURE_SEARCH") != NULL;
}

static bool ReadSCConvSiblingLeadTexture(UTexture &Tex, FArchive &Ar)
{
	guard(ReadSCConvSiblingLeadTexture);

	if (!Tex.Package)
		return false;

	char LeadName[256];
	appSprintf(ARRAY_ARG(LeadName), "%s_l3d", Tex.Name);
	int ExportIndex = Tex.Package->FindExport(LeadName);
	if (ExportIndex < 0)
	{
		for (int i = 0; i < Tex.Package->Summary.ExportCount; i++)
		{
			const FObjectExport &Exp = Tex.Package->GetExport(i);
			if (!stricmp(*Exp.ObjectName, LeadName))
			{
				ExportIndex = i;
				break;
			}
		}
	}
	if (ExportIndex < 0)
		return false;

	const FObjectExport &Exp = Tex.Package->GetExport(ExportIndex);
	int OldPos = Ar.Tell();
	int OldStopper = Ar.GetStopper();
	bool bOk = false;
	if (Exp.SerialSize > 0)
	{
		TArray<byte> SerialData;
		SerialData.AddUninitialized(Exp.SerialSize);
		Ar.Seek(Exp.SerialOffset);
		Ar.SetStopper(Exp.SerialOffset + Exp.SerialSize);
		Ar.Serialize(SerialData.GetData(), Exp.SerialSize);
		bool bLoadedStream = false;
		const int StreamDataOffset = 0xB8;
		const int StreamDataSize = Exp.SerialSize - StreamDataOffset;
		int StreamAdjust = 0;
		if (const char *Env = getenv("SC_CONV_TEX_STREAM_ADJUST"))
			StreamAdjust = strtol(Env, NULL, 0);
		if (Tex.Package->FileInfo)
			bLoadedStream = LoadLeadVfsFileBytes(Tex.Package->FileInfo, Exp.SerialOffset + StreamDataOffset + StreamAdjust, StreamDataSize, SerialData.GetData() + StreamDataOffset);
		if (!bLoadedStream)
			bLoadedStream = LoadLeadUmdFileBytes(*Tex.Package->GetFilename(), Exp.SerialOffset + StreamDataOffset + StreamAdjust, StreamDataSize, SerialData.GetData() + StreamDataOffset);
		if (bLoadedStream)
		{
			FMemReader Mem(SerialData.GetData(), Exp.SerialSize);
			Mem.SetupFrom(Ar);
			bOk = ReadSCConvLeadTexturePayload(Tex, Mem, Exp.SerialSize, true);
		}
	}
	Ar.Seek(Exp.SerialOffset);
	Ar.SetStopper(Exp.SerialOffset + Exp.SerialSize);
	if (!bOk)
		bOk = ReadSCConvLeadTexturePayload(Tex, Ar, Exp.SerialOffset + Exp.SerialSize);
	Ar.SetStopper(OldStopper);
	Ar.Seek(OldPos);
	return bOk;

	unguard;
}
#endif // LEAD

void UUnreal3Material::Serialize(FArchive &Ar)
{
	guard(UUnreal3Material::Serialize);
#if SPLINTER_CELL
	if (Ar.Game == GAME_SplinterCell && Ar.ArVer >= 173 && Ar.ArLicenseeVer == 0)
	{
		if (getenv("SCDA_DUMP_MATERIAL_RAW"))
		{
			int SavePos = Ar.Tell();
			int Size = Ar.GetStopper() - SavePos;
			char Filename[256];
			appSprintf(ARRAY_ARG(Filename), "scda_material_%s_raw.bin", Name);
			FILE* F = fopen(Filename, "wb");
			if (F)
			{
				TArray<byte> Raw;
				Raw.AddUninitialized(Size);
				Ar.Serialize(Raw.GetData(), Size);
				fwrite(Raw.GetData(), 1, Size, F);
				fclose(F);
				appPrintf("SCDA dumped raw material export: %s size=%d\n", Filename, Size);
				Ar.Seek(SavePos);
			}
		}
		DROP_REMAINING_DATA(Ar);
		return;
	}
#endif
	Super::Serialize(Ar);
#if SPLINTER_CELL
	if (Ar.Game == GAME_SplinterCell && Ar.ArVer >= 173 && Ar.ArLicenseeVer == 0)
		DROP_REMAINING_DATA(Ar);
#endif
#if LEAD
	if (Ar.Game == GAME_SplinterCellConv)
		DROP_REMAINING_DATA(Ar);
#endif
	unguard;
}

void USCX_basic_material::Serialize(FArchive &Ar)
{
	guard(USCX_basic_material::Serialize);
#if SPLINTER_CELL
	if (Ar.Game == GAME_SplinterCell && Ar.ArVer == 100 && Ar.ArLicenseeVer >= 123 &&
		Ar.GetStopper() - Ar.Tell() <= 0x20)
	{
		DROP_REMAINING_DATA(Ar);
		return;
	}
#endif
	Super::Serialize(Ar);
#if SPLINTER_CELL
	if (Ar.Game == GAME_SplinterCell && Ar.ArVer == 100 && Ar.ArLicenseeVer >= 123)
		DROP_REMAINING_DATA(Ar);
#endif
#if LEAD
	if (Ar.Game == GAME_SplinterCellConv)
		DROP_REMAINING_DATA(Ar);
#endif
	unguard;
}

/*-----------------------------------------------------------------------------
	UTexture (Unreal engine 1 and 2)
-----------------------------------------------------------------------------*/

void UTexture::Serialize(FArchive &Ar)
{
	guard(UTexture::Serialize);
	Super::Serialize(Ar);
#if SPLINTER_CELL
	if (Ar.Game == GAME_SplinterCell && Ar.ArVer >= 173 && Ar.ArLicenseeVer == 0 && Ar.Tell() >= Ar.GetStopper())
		return;
#endif
#if LEAD
	if (Ar.Game == GAME_SplinterCellConv)
	{
		if (ShouldSCConvSearchLeadTextures())
		{
			if (strstr(Name, "_l3d"))
			{
				if (!ReadSCConvLeadTexturePayload(*this, Ar, Ar.GetStopper()))
					appPrintf("WARNING: Unable to locate Conviction LeadTexture payload for %s\n", Name);
			}
			else
			{
				if (!ReadSCConvSiblingLeadTexture(*this, Ar))
					appPrintf("WARNING: Unable to locate Conviction LeadTexture payload for %s\n", Name);
			}
		}
		DROP_REMAINING_DATA(Ar);
		return;
	}
#endif // LEAD
#if BIOSHOCK
	TRIBES_HDR(Ar, 0x2E);
	if (Ar.Game == GAME_Bioshock && t3_hdrSV >= 1)
		Ar << CachedBulkDataSize;
	if (Ar.Game == GAME_Bioshock && Format == 12)	// remap format; note: Bioshock used 3DC name, but real format is DXT5N
		Format = TEXF_DXT5N;
#endif // BIOSHOCK
#if SWRC
	if (Ar.Game == GAME_RepCommando)
	{
		if (Format == 14) Format = TEXF_CxV8U8;		//?? not verified
	}
#endif // SWRC
#if VANGUARD
	if (Ar.Game == GAME_Vanguard && Ar.ArVer >= 128 && Ar.ArLicenseeVer >= 25)
	{
		// has some table for fast mipmap lookups
		Ar.Seek(Ar.Tell() + 142);	// skip that table
		// serialize mips using AR_INDEX count (this game uses int for array counts in all other places)
		int Count;
		Ar << AR_INDEX(Count);
		Mips.AddDefaulted(Count);
		for (int i = 0; i < Count; i++)
			Ar << Mips[i];
		return;
	}
#endif // VANGUARD
#if AA2
	if (Ar.Game == GAME_AA2 && Ar.ArLicenseeVer >= 8)
	{
		int unk;		// always 10619
		Ar << unk;
	}
#endif // AA2
	Ar << Mips;
#if SPLINTER_CELL
	if (Ar.Game == GAME_SplinterCell && Ar.ArVer == 100 && Ar.ArLicenseeVer >= 123 && Mips.Num() == 0)
	{
		int StreamUSize, StreamVSize, StreamFormatCode;
		if (GetScdaLinTextureStreamInfo(Name, StreamUSize, StreamVSize, StreamFormatCode))
		{
			USize = StreamUSize;
			VSize = StreamVSize;
			UBits = 0;
			for (int Size = USize; Size > 1; Size >>= 1)
				UBits++;
			VBits = 0;
			for (int Size = VSize; Size > 1; Size >>= 1)
				VBits++;
			if (StreamFormatCode >= 1 && StreamFormatCode <= 8)
				Format = TEXF_DXT1;
		}
	}
	if (Ar.Game == GAME_SplinterCell && Ar.ArVer == 100 && Ar.ArLicenseeVer >= 123 && getenv("SCDA_LIN_DEBUG"))
	{
		appPrintf("SCDA texture serialize: %s fmt=%d props=%dx%d mips=%d tell=%X stop=%X\n",
			Name, Format, USize, VSize, Mips.Num(), Ar.Tell(), Ar.GetStopper());
		for (int i = 0; i < Mips.Num() && i < 4; i++)
			appPrintf("  mip[%d]: data=%d size=%dx%d bits=%d/%d\n",
				i, Mips[i].DataArray.Num(), Mips[i].USize, Mips[i].VSize, Mips[i].UBits, Mips[i].VBits);
	}
#endif
	if (Ar.Engine() == GAME_UE1)
	{
		// UE1
		bMasked = false;			// ignored by UE1, used surface.PolyFlags instead (but UE2 ignores PolyFlags ...)
		if (bHasComp)				// skip compressed mipmaps
		{
			TArray<FMipmap>	CompMips;
			Ar << CompMips;
		}
	}
#if XIII
	if (Ar.Game == GAME_XIII)
	{
		if (Ar.ArLicenseeVer >= 42)
		{
			// serialize palette
			if (Format == TEXF_P8 || Format == 13)	// 13 == TEXF_P4
			{
				assert(!Palette);
				Palette = new UPalette;
				Ar << Palette->Colors;
			}
		}
		if (Ar.ArLicenseeVer >= 55)
			Ar.Seek(Ar.Tell() + 3);
	}
#endif // XIII
#if EXTEEL
	if (Ar.Game == GAME_Exteel)
	{
		// note: this property is serialized as UObject's property too
		byte MaterialType;			// enum GFMaterialType
		Ar << MaterialType;
	}
#endif // EXTEEL
#if SPLINTER_CELL
	if (Ar.Game == GAME_SplinterCell && Ar.ArVer == 100 && Ar.ArLicenseeVer >= 123)
		DROP_REMAINING_DATA(Ar);
#endif
	unguard;
}


#if UC2

struct XprEntry
{
	char				Name[64];
	int					DataOffset;
	int					DataSize;
};

struct XprInfo
{
	const CGameFileInfo *File;
	TArray<XprEntry>	Items;
	int					DataStart;
};

static TArray<XprInfo> xprFiles;

static bool ReadXprFile(const CGameFileInfo *file)
{
	guard(ReadXprFile);

	FArchive *Ar = file->CreateReader();

	int Tag, FileLen, DataStart, DataCount;
	*Ar << Tag << FileLen << DataStart << DataCount;
	//?? "XPR0" - xpr variant with a single object (texture) inside
	if (Tag != BYTES4('X','P','R','1'))
	{
#if XPR_DEBUG
		appPrintf("Unknown XPR tag in %s\n", file->RelativeName);
#endif
		delete Ar;
		return true;
	}
#if XPR_DEBUG
	appPrintf("Scanning %s ...\n", file->RelativeName);
#endif

	XprInfo *Info = new(xprFiles) XprInfo;
	Info->File      = file;
	Info->DataStart = DataStart;
	// read filelist
	int i;
	for (i = 0; i < DataCount; i++)
	{
		int NameOffset, DataOffset;
		*Ar << NameOffset << DataOffset;
		int savePos = Ar->Tell();
		Ar->Seek(NameOffset + 12);
		// read name
		char c, buf[256];
		int n = 0;
		while (true)
		{
			*Ar << c;
			if (n < ARRAY_COUNT(buf))
				buf[n++] = c;
			if (!c) break;
		}
		buf[ARRAY_COUNT(buf)-1] = 0;			// just in case
		// create item
		XprEntry *Entry = new(Info->Items) XprEntry;
		appStrncpyz(Entry->Name, buf, ARRAY_COUNT(Entry->Name));
		Entry->DataOffset = DataOffset + 12;
		assert(Entry->DataOffset < DataStart);
		// seek back
		Ar->Seek(savePos);
		// setup size of previous item
		if (i >= 1)
		{
			XprEntry *PrevEntry = &Info->Items[i - 1];
			PrevEntry->DataSize = Entry->DataOffset - PrevEntry->DataOffset;
		}
		// setup size of the last item
		if (i == DataCount - 1)
			Entry->DataSize = DataStart - Entry->DataOffset;
	}
	// scan data
	// data block is either embedded in this block or followed after DataStart position
	for (i = 0; i < DataCount; i++)
	{
		XprEntry *Entry = &Info->Items[i];
#if XPR_DEBUG
//		appPrintf("  %08X [%08X]  %s\n", Entry->DataOffset, Entry->DataSize, Entry->Name);
#endif
		Ar->Seek(Entry->DataOffset);
		uint32 id;
		*Ar << id;
		switch (id)
		{
		case 0x80020001:
			// header is 4 dwords + immediately followed data
			Entry->DataOffset += 4 * 4;
			Entry->DataSize   -= 4 * 4;
			break;

		case 0x00040001:
			// header is 5 dwords + external data
			{
				int pos;
				*Ar << pos;
				Entry->DataOffset = DataStart + pos;
			}
			break;

		case 0x00020001:
			// header is 4 dwords + external data
			{
				int d1, d2, pos;
				*Ar << d1 << d2 << pos;
				Entry->DataOffset = DataStart + pos;
			}
			break;

		default:
			// header is 2 dwords - offset and size + external data
			{
				int pos;
				*Ar << pos;
				Entry->DataOffset = DataStart + pos;
			}
			break;
		}
	}
	// setup sizes of blocks placed after DataStart (not embedded into file list)
	for (i = 0; i < DataCount; i++)
	{
		XprEntry *Entry = &Info->Items[i];
		if (Entry->DataOffset < DataStart) continue; // embedded data
		// Entry points to a data block placed after DataStart position
		// we should find a next block
		int NextPos = FileLen;
		for (int j = i + 1; j < DataCount; j++)
		{
			XprEntry *NextEntry = &Info->Items[j];
			if (NextEntry->DataOffset < DataStart) continue; // embedded data
			NextPos = NextEntry->DataOffset;
			break;
		}
		Entry->DataSize = NextPos - Entry->DataOffset;
	}
#if XPR_DEBUG
	for (i = 0; i < DataCount; i++)
	{
		XprEntry *Entry = &Info->Items[i];
		appPrintf("  %3d %08X [%08X] .. %08X  %s\n", i, Entry->DataOffset, Entry->DataSize, Entry->DataOffset + Entry->DataSize, Entry->Name);
	}
#endif

	delete Ar;
	return true;

	unguardf("%s", *file->GetRelativeName());
}


byte *FindXprData(const char *Name, int *DataSize)
{
	// scan xprs
	static bool ready = false;
	if (!ready)
	{
		ready = true;
		appEnumGameFiles(ReadXprFile, "xpr");
	}
	// find a file
	for (int i = 0; i < xprFiles.Num(); i++)
	{
		XprInfo *Info = &xprFiles[i];
		for (int j = 0; j < Info->Items.Num(); j++)
		{
			XprEntry *File = &Info->Items[j];
			if (strcmp(File->Name, Name) == 0)
			{
				// found
				appPrintf("Loading stream %s from %s (%d bytes)\n", Name, *Info->File->GetRelativeName(), File->DataSize);
				FArchive *Reader = Info->File->CreateReader();
				Reader->Seek(File->DataOffset);
				byte *buf = (byte*)appMalloc(File->DataSize);
				Reader->Serialize(buf, File->DataSize);
				delete Reader;
				if (DataSize) *DataSize = File->DataSize;
				return buf;
			}
		}
	}
	appPrintf("WARNING: external stream %s was not found\n", Name);
	if (DataSize) *DataSize = 0;
	return NULL;
}

#endif // UC2

#if BIOSHOCK

//#define DUMP_BIO_CATALOG			1
//#define DEBUG_BIO_BULK				1

struct BioBulkCatalogItem
{
	FString				ObjectName;
	FString				PackageName;
	int					f10;				// always 0
	int					DataOffset;
	int					DataSize;
	int					DataSize2;			// the same as DataSize
	int					f20;

	friend FArchive& operator<<(FArchive &Ar, BioBulkCatalogItem &S)
	{
		Ar << S.ObjectName << S.PackageName << S.f10 << S.DataOffset << S.DataSize << S.DataSize2 << S.f20;
		assert(S.f10 == 0);
//		assert(S.DataSize == S.DataSize2);	-- the same on PC, but not the same on XBox360
#if DUMP_BIO_CATALOG
		appPrintf("  %s / %s - %08X:%08X %X %X %X\n", *S.ObjectName, *S.PackageName, S.f10, S.DataOffset, S.DataSize, S.DataSize2, S.f20);
#endif
		return Ar;
	}
};


struct BioBulkCatalogFile
{
	int64				f0;
	FString				Filename;
	TArray<BioBulkCatalogItem> Items;

	friend FArchive& operator<<(FArchive &Ar, BioBulkCatalogFile &S)
	{
		Ar << S.f0 << S.Filename;
#if DUMP_BIO_CATALOG
		appPrintf("<<< %s >>>\n", *S.Filename);
#endif
		Ar << S.Items;
#if DEBUG_BIO_BULK
		int minS2 = 99999999, maxS2 = -99999999, min20 = 99999999, max20 = -99999999;
		for (int i = 0; i < S.Items.Num(); i++)
		{
			int n1 = S.Items[i].DataSize2;
			if (n1 < minS2) minS2 = n1;
			if (n1 > maxS2) maxS2 = n1;
			int n2 = S.Items[i].f20;
			if (n2 < min20) min20 = n1;
			if (n2 > max20) max20 = n1;
		}
		appPrintf("DS2=%X..%X  f20=%X..%X", minS2, maxS2, min20, max20);
#endif // DEBUG_BIO_BULK
		return Ar;
	}
};

struct BioBulkCatalog
{
	byte				Endian;	//?? or Platform: 0=PC, 1=XBox360, 2=PS3?
	int64				f4;
	int					fC;
	TArray<BioBulkCatalogFile> Files;

	friend FArchive& operator<<(FArchive &Ar, BioBulkCatalog &S)
	{
		Ar << S.Endian;
		if (S.Endian) Ar.ReverseBytes = true;
		Ar << S.f4 << S.fC << S.Files;
		return Ar;
	}
};

static TArray<BioBulkCatalog> bioCatalog;

static bool BioReadBulkCatalogFile(const CGameFileInfo *file)
{
	guard(BioReadBulkCatalogFile);
	FArchive *Ar = file->CreateReader();
	// setup for reading Bioshock data
	Ar->ArVer         = 141;
	Ar->ArLicenseeVer = 0x38;
	Ar->Game          = GAME_Bioshock;
	// serialize
	appPrintf("Reading %s\n", *file->GetRelativeName());
	BioBulkCatalog *cat = new (bioCatalog) BioBulkCatalog;
	*Ar << *cat;
	// finalize
	delete Ar;
	return true;
	unguardf("%s", *file->GetRelativeName());
}


static void BioReadBulkCatalog()
{
	static bool ready = false;
	if (ready) return;
	ready = true;
	appEnumGameFiles(BioReadBulkCatalogFile, "bdc");
	if (!bioCatalog.Num()) appPrintf("WARNING: no *.bdc files found\n");
}

static byte *FindBioTexture(const UTexture *Tex)
{
	int needSize = Tex->CachedBulkDataSize & 0xFFFFFFFF;
#if DEBUG_BIO_BULK
	appPrintf("Search for ... %s (size=%X)\n", Tex->Name, needSize);
#endif
	BioReadBulkCatalog();
	for (int i = 0; i < bioCatalog.Num(); i++)
	{
		BioBulkCatalog &Cat = bioCatalog[i];
		for (int j = 0; j < Cat.Files.Num(); j++)
		{
			const BioBulkCatalogFile &File = Cat.Files[j];
			for (int k = 0; k < File.Items.Num(); k++)
			{
				const BioBulkCatalogItem &Item = File.Items[k];
				if (!strcmp(Tex->Name, *Item.ObjectName))
				{
					if (abs(needSize - Item.DataSize) > 0x4000)		// differs in 16k
					{
#if DEBUG_BIO_BULK
						appPrintf("... Found %s in %s with wrong BulkDataSize %X (need %X)\n", Tex->Name, *File.Filename, Item.DataSize, needSize);
#endif
						continue;
					}
#if DEBUG_BIO_BULK
					appPrintf("... Found %s in %s at %X size %X (%dx%d fmt=%d bpp=%g strip:%d mips:%d)\n", Tex->Name, *File.Filename, Item.DataOffset, Item.DataSize,
						Tex->USize, Tex->VSize, Tex->Format, (float)Item.DataSize / (Tex->USize * Tex->VSize),
						Tex->HasBeenStripped, Tex->StrippedNumMips);
#endif
					// found
					const CGameFileInfo *bulkFile = CGameFileInfo::Find(*File.Filename);
					if (!bulkFile)
					{
						// no bulk file
						appPrintf("Decompressing %s: %s is missing\n", Tex->Name, *File.Filename);
						return NULL;
					}

					appPrintf("Reading %s mip level %d (%dx%d) from %s\n", Tex->Name, 0, Tex->USize, Tex->VSize, *bulkFile->GetRelativeName());
					FArchive *Reader = bulkFile->CreateReader();
					Reader->Seek(Item.DataOffset);
					byte *buf = (byte*)appMalloc(max(Item.DataSize, needSize));
					Reader->Serialize(buf, Item.DataSize);
					delete Reader;
					return buf;
				}
			}
		}
	}
#if DEBUG_BIO_BULK
	appPrintf("... Bulk for %s was not found\n", Tex->Name);
#endif
	return NULL;
}

#endif // BIOSHOCK

ETexturePixelFormat UTexture::GetTexturePixelFormat() const
{
	ETexturePixelFormat intFormat = TPF_UNKNOWN;
	const FArchive* PackageAr = GetPackageArchive();

	//?? return old code back - UE1 and UE2 differs in codes 6 and 7 only
	if (Package && (PackageAr->Engine() == GAME_UE1))
	{
		// UE1 has different ETextureFormat layout
		switch (Format)
		{
		case 0:
			intFormat = TPF_P8;
			break;
//		case 1:
//			intFormat = TPF_RGB32; // in script source code: TEXF_RGB32, but TEXF_RGBA7 in .h
//			break;
//		case 2:
//			intFormat = TPF_RGB64; // in script source code: TEXF_RGB64, but TEXF_RGB16 in .h
//			break;
		case 3:
			intFormat = TPF_DXT1;
			break;
		case 4:
			intFormat = TPF_RGB8;
			break;
		case 5:
			intFormat = TPF_BGRA8;
			break;
		// newer UE1 versions has DXT3 and DXT5
		case 6:
			intFormat = TPF_DXT3;
			break;
		case 7:
			intFormat = TPF_DXT5;
			break;
		default:
			appNotify("Unknown UE1 texture format: %d", Format);
		}
	}
	else
	{
		// UE2
		switch (Format)
		{
		case TEXF_P8:
			intFormat = TPF_P8;
			break;
		case TEXF_DXT1:
			intFormat = TPF_DXT1;
			break;
		case TEXF_RGB8:
			intFormat = TPF_RGB8;
			break;
		case TEXF_RGBA8:
			intFormat = TPF_BGRA8;
			break;
		case TEXF_DXT3:
			intFormat = TPF_DXT3;
			break;
		case TEXF_DXT5:
			intFormat = TPF_DXT5;
			break;
		case TEXF_L8:
			intFormat = TPF_G8;
			break;
		case TEXF_CxV8U8:
			intFormat = TPF_V8U8_2;
			break;
		case TEXF_DXT5N:
			intFormat = TPF_DXT5N;
			break;
		case TEXF_3DC:
			intFormat = TPF_BC5;
			break;
		case TEXF_SC4_DXT5N:
			intFormat = TPF_DXT5N;
			break;
		default:
			appNotify("Unknown UE2 texture format: %s (%d)", EnumToName(Format), Format);
		}
	}

	return intFormat;
}

bool UTexture::GetTextureData(CTextureData &TexData) const
{
	guard(UTexture::GetTextureData);

	TexData.SetObject(this);
	TexData.Platform           = PLATFORM_PC;
	TexData.OriginalFormatEnum = Format;
	TexData.OriginalFormatName = EnumToName(Format);
	TexData.Palette            = Palette;
	TexData.Format             = GetTexturePixelFormat();

	const FArchive* PackageAr = GetPackageArchive();

	// process external sources for some games
#if SPLINTER_CELL
	if (PackageAr && PackageAr->Game == GAME_SplinterCell && PackageAr->ArVer == 100 &&
		PackageAr->ArLicenseeVer >= 123)
	{
		TArray<byte> StreamData;
		int GpuSize, TextureId, StreamUSize, StreamVSize, FormatCode;
		if (LoadScdaLinTextureStream(Name, StreamData, GpuSize, TextureId, StreamUSize, StreamVSize, FormatCode) &&
			StreamUSize > 0 && StreamVSize > 0)
		{
			ETexturePixelFormat StreamFormat = TPF_UNKNOWN;
			int ExpectedSize = 0;
			bool SizeMatched = false;
			if (FormatCode == 1)
			{
				StreamFormat = TPF_DXT1;
				ExpectedSize = GetScdaDxt1MipSize(StreamUSize, StreamVSize);
				SizeMatched = ExpectedSize == StreamData.Num();
			}
			else if (FormatCode >= 2 && FormatCode <= 8) // mipmapped DXT1 resource; BIN stores an aligned mip tail
			{
				StreamFormat = TPF_DXT1;
				while (true)
				{
					int ChainSize = GetScdaDxt1ChainSize(StreamUSize, StreamVSize);
					if (Align(ChainSize, 0x200) == StreamData.Num())
					{
						SizeMatched = true;
						break;
					}
					if (StreamUSize <= 1 && StreamVSize <= 1)
						break;
					StreamUSize = max(1, StreamUSize >> 1);
					StreamVSize = max(1, StreamVSize >> 1);
				}
				ExpectedSize = GetScdaDxt1ChainSize(StreamUSize, StreamVSize);
			}

			if (StreamFormat != TPF_UNKNOWN && SizeMatched && ExpectedSize > 0 && ExpectedSize <= StreamData.Num())
			{
				TexData.Format = StreamFormat;
				int DataOffset = 0;
				while (DataOffset < ExpectedSize)
				{
					int MipSize = GetScdaDxt1MipSize(StreamUSize, StreamVSize);
					if (DataOffset + MipSize > ExpectedSize)
						break;
					byte *OwnedData = new byte[MipSize];
					memcpy(OwnedData, StreamData.GetData() + DataOffset, MipSize);
					CMipMap *DstMip = new (TexData.Mips) CMipMap;
					DstMip->SetOwnedDataBuffer(OwnedData, MipSize);
					DstMip->USize = StreamUSize;
					DstMip->VSize = StreamVSize;
					DataOffset += MipSize;
					if (FormatCode == 1 || (StreamUSize <= 4 && StreamVSize <= 4))
						break;
					StreamUSize = max(1, StreamUSize >> 1);
					StreamVSize = max(1, StreamVSize >> 1);
				}
				if (getenv("SCDA_LIN_DEBUG"))
					appPrintf("SCDA external mips: %s fmt=%d size=%X mips=%d\n",
						Name, FormatCode, StreamData.Num(), TexData.Mips.Num());
			}
		}
	}
#endif // SPLINTER_CELL
#if BIOSHOCK
	if (PackageAr && PackageAr->Game == GAME_Bioshock && CachedBulkDataSize) //?? check bStripped or Baked ?
	{
		byte* CompressedData = FindBioTexture(this);	// may be NULL
		if (CompressedData)
		{
			CMipMap* DstMip = new (TexData.Mips) CMipMap;
			DstMip->SetOwnedDataBuffer(CompressedData, (int)CachedBulkDataSize);
			DstMip->USize = USize;
			DstMip->VSize = VSize;
		}
		TexData.Platform = PackageAr->Platform;
	}
#endif // BIOSHOCK
#if UC2
	if (PackageAr && PackageAr->Engine() == GAME_UE2X)
	{
		// try to find texture inside XBox xpr files
		int DataSize;
		byte* CompressedData = FindXprData(Name, &DataSize);	// may be NULL
		if (CompressedData)
		{
			CMipMap* DstMip = new (TexData.Mips) CMipMap;
			DstMip->SetOwnedDataBuffer(CompressedData, DataSize);
			DstMip->USize = USize;
			DstMip->VSize = VSize;
		}
	}
#endif // UC2

	if (TexData.Mips.Num() == 0)
	{
		// texture was not taken from external source
		for (int n = 0; n < Mips.Num(); n++)
		{
			// find 1st mipmap with non-null data array
			// reference: DemoPlayerSkins.utx/DemoSkeleton have null-sized 1st 2 mips
			const FMipmap &Mip = Mips[n];
			if (!Mip.DataArray.Num())
				continue;
			CMipMap* DstMip = new (TexData.Mips) CMipMap;
			DstMip->CompressedData = &Mip.DataArray[0];
			DstMip->ShouldFreeData = false;
			DstMip->USize = Mip.USize;
			DstMip->VSize = Mip.VSize;
			DstMip->DataSize = Mip.DataArray.Num();
		}
	}

#if BIOSHOCK && SUPPORT_XBOX360
	if (TexData.Mips.Num() && TexData.Platform == PLATFORM_XBOX360)
	{
		for (int MipLevel = 0; MipLevel < TexData.Mips.Num(); MipLevel++)
		{
			if (!TexData.DecodeXBox360(MipLevel))
			{
				// failed to decode this mip
				TexData.Mips.RemoveAt(MipLevel, TexData.Mips.Num() - MipLevel);
				break;
			}
		}
	}
#endif // BIOSHOCK && SUPPORT_XBOX360
#if BIOSHOCK
	if (Package && PackageAr->Game == GAME_Bioshock)
	{
		// This game has DataSize stored for all mipmaps, we should compute side of 1st mipmap
		// in order to accept this value when uploading texture to video card (some vendors rejects
		// large values)
		//?? Place code to CTextureData method?
		const CPixelFormatInfo &Info = PixelFormatInfo[TexData.Format];
		int numBlocks = TexData.Mips[0].USize * TexData.Mips[0].VSize / (Info.BlockSizeX * Info.BlockSizeY);	// used for validation only
		int requiredDataSize = numBlocks * Info.BytesPerBlock;
		if (requiredDataSize > TexData.Mips[0].DataSize)
		{
			appNotify("Bioshock texture %s: data too small; %dx%d, requires %X bytes, got %X\n",
				Name, TexData.Mips[0].USize, TexData.Mips[0].VSize, requiredDataSize, TexData.Mips[0].DataSize);
		}
		else if (requiredDataSize < TexData.Mips[0].DataSize)
		{
//			appPrintf("Bioshock texture %s: stripping data size from %X to %X\n", Name, TexData.Mips[0].DataSize, requiredDataSize);
			TexData.Mips[0].DataSize = requiredDataSize;
		}
	}
#endif // BIOSHOCK

	return (TexData.Mips.Num() > 0);

	unguardf("%s", Name);
}
