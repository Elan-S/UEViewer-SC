#include "Core.h"

#include "UnCore.h"
#include "UnObject.h"
#include "FileSystem/GameFileSystem.h"
#if THREADING
#include "Parallel.h"
#endif

#if _WIN32
#include <io.h>
#endif


/*-----------------------------------------------------------------------------
	Splinter Cell Double Agent Xbox seekless LIN containers
-----------------------------------------------------------------------------*/

#if SPLINTER_CELL

struct FScdaLinManifestEntry
{
	FString		Filename;
	FString		PackageName;
	int			OriginalOffset;
	int			OriginalSize;
};

struct FScdaLinExportRange
{
	int			SerialOffset;
	int			SerialSize;
	int			ClassIndex;
	int			ObjectNameIndex;
	FString		ObjectName;
	FString		ClassName;
};

struct FScdaLinPackage
{
	FString		Filename;
	FString		NativeSourceFilename;
	TArray<byte>	Data;
	struct FSegment
	{
		int		VirtualOffset;
		int		DataOffset;
		int		Size;
		FString	SourceFilename;
		int		SourceLogicalOffset;

		FSegment()
		:	VirtualOffset(0)
		,	DataOffset(-1)
		,	Size(0)
		,	SourceLogicalOffset(0)
		{}
	};
	TArray<FSegment> Segments;
	TArray<FString> Names;
	TArray<FScdaLinExportRange> Exports;
	int			OriginalSize;
	int			ImportCount;
	bool		PayloadStreamsMapped;

	FScdaLinPackage()
	:	OriginalSize(0)
	,	ImportCount(0)
	,	PayloadStreamsMapped(false)
	{}
};

struct FScdaLinMipCandidate
{
	int			LogicalSkipField;
	int			OriginalSkipField;
};

struct FScdaLinTextureStreamEntry
{
	FString		TextureName;
	FString		PackageFilename;
	FString		BinFilename;
	int			LogicalOffset;
	int			GpuOffset;
	int			GpuSize;
	int			TextureId;
	int			BinOffset;
	int			BinSize;
	int			USize;
	int			VSize;
	int			FormatCode;

	FScdaLinTextureStreamEntry()
	:	LogicalOffset(0)
	,	GpuOffset(0)
	,	GpuSize(0)
	,	TextureId(0)
	,	BinOffset(0)
	,	BinSize(0)
	,	USize(0)
	,	VSize(0)
	,	FormatCode(-1)
	{}
};

struct FScdaLinSourceFile
{
	FString		Filename;
	FString		RelativeName;
	int64		Size;
};

struct FScdaLinManifestSourceFile
{
	FString		RelativeName;
	int64		Size;
};

static TArray<FScdaLinTextureStreamEntry> GScdaLinTextureStreams;
static FString GScdaLinManifestFilename;
static TArray<FScdaLinPackage> *GScdaLinPackages;
static TArray<FScdaLinSourceFile> GScdaLinPayloadFiles;
static FScdaLinSourceFile GScdaLinCommonSourceFile;
static bool GScdaLinCommonSourceFileValid = false;
static bool GScdaLinCommonCacheLoaded = false;
static FString GScdaLinCommonCacheFilename;
static TArray<FScdaLinManifestEntry> GScdaLinCommonManifestCache;
static TArray<FScdaLinSourceFile> GScdaLinPayloadFilesCache;
#if THREADING
static CMutex GScdaLinTextureStreamMutex;
#endif

#define SCDA_LIN_MANIFEST_VERSION 3

static void AddScdaLinSegmentRef(FScdaLinPackage& Package, int VirtualOffset, int Size,
	const char *SourceFilename, int SourceLogicalOffset);
static void AddScdaLinSegment(FScdaLinPackage& Package, int VirtualOffset, const byte *Data, int Size,
	const char *SourceFilename = NULL, int SourceLogicalOffset = 0);
static bool IsScdaLinRangeMapped(const FScdaLinPackage& Package, int Offset, int Size);
static bool HasScdaLinTextureStream(const char *TextureName, const char *PackageFilename = NULL);
static bool ShouldScanScdaLinPayloads();
static bool ShouldSynthesizeScdaLinLevelTexturePackages();
static bool ShouldSynthesizeScdaLinLevelSkeletalPackages();
static bool DecompressScdaLin(FArchive *Reader, TArray<byte>& LogicalData);
static void AddScdaLinLevelPackagesFromData(TArray<FScdaLinPackage>& Packages, const byte *Data, int DataSize,
	const FString& LevelBase, const char *SourceFilename);

template<class T>
static void CopyScdaLinArray(TArray<T>& Dst, const TArray<T>& Src)
{
	Dst.Empty(Src.Num());
	for (const T& Item : Src)
		Dst.Add(Item);
}

static bool ScdaLinPackageBasenameMatches(const char *PackageFilename, const char *ManifestName)
{
	if (!PackageFilename || !ManifestName || !ManifestName[0])
		return false;
	// Only use basename matching for root package names. Full paths should
	// continue to be exact so stale manifests fail loudly instead of binding
	// to the wrong package.
	if (strchr(ManifestName, '/') || strchr(ManifestName, '\\') || strchr(ManifestName, '.'))
		return false;

	const char *Start1 = strrchr(PackageFilename, '/');
	const char *Start2 = strrchr(PackageFilename, '\\');
	const char *Start = (!Start1 || (Start2 && Start2 > Start1)) ? Start2 : Start1;
	Start = Start ? Start + 1 : PackageFilename;

	char Base[MAX_PACKAGE_PATH];
	appStrncpyz(Base, Start, ARRAY_COUNT(Base));
	char *Dot = strrchr(Base, '.');
	if (Dot)
		*Dot = 0;
	return !stricmp(Base, ManifestName);
}

static FScdaLinPackage* FindScdaLinPackage(const char *Filename)
{
	if (!GScdaLinPackages)
		return NULL;
	for (FScdaLinPackage& Package : *GScdaLinPackages)
		if (!stricmp(*Package.Filename, Filename))
			return &Package;
	for (FScdaLinPackage& Package : *GScdaLinPackages)
		if (ScdaLinPackageBasenameMatches(*Package.Filename, Filename))
			return &Package;
	return NULL;
}

static void GetScdaLinManifestFilename(const char *CommonFilename, FString& OutFilename)
{
	char Text[MAX_PACKAGE_PATH];
	appStrncpyz(Text, CommonFilename, ARRAY_COUNT(Text));
	char *Slash1 = strrchr(Text, '/');
	char *Slash2 = strrchr(Text, '\\');
	char *Slash = (!Slash1 || (Slash2 && Slash2 > Slash1)) ? Slash2 : Slash1;
	if (Slash)
		Slash[1] = 0;
	else
		Text[0] = 0;
	appStrcatn(Text, ARRAY_COUNT(Text), "umodel_lin_manifest.txt");
	OutFilename = Text;
}

static void CollectScdaLinManifestSourceFiles(TArray<FScdaLinManifestSourceFile>& Files)
{
	Files.Empty(GScdaLinPayloadFiles.Num() + (GScdaLinCommonSourceFileValid ? 1 : 0));
	if (GScdaLinCommonSourceFileValid)
	{
		FScdaLinManifestSourceFile& File = Files[Files.AddDefaulted()];
		File.RelativeName = GScdaLinCommonSourceFile.RelativeName;
		File.Size = GScdaLinCommonSourceFile.Size;
	}
	for (const FScdaLinSourceFile& Source : GScdaLinPayloadFiles)
	{
		FScdaLinManifestSourceFile& File = Files[Files.AddDefaulted()];
		File.RelativeName = Source.RelativeName;
		File.Size = Source.Size;
	}
}

static bool ValidateScdaLinManifestFile(const char *Filename, int64 ExpectedSize)
{
	TArray<FScdaLinManifestSourceFile> SourceFiles;
	CollectScdaLinManifestSourceFiles(SourceFiles);
	for (const FScdaLinManifestSourceFile& File : SourceFiles)
	{
		if (!stricmp(*File.RelativeName, Filename))
			return File.Size == ExpectedSize;
	}
	return false;
}

static bool ReadScdaLinPayloadManifest()
{
	if (getenv("SCDA_LIN_IGNORE_MANIFEST"))
		return false;
	if (GScdaLinManifestFilename.IsEmpty() || !appFileExists(*GScdaLinManifestFilename))
		return false;

	FFileReader Reader(*GScdaLinManifestFilename, EFileArchiveOptions::OpenWarning);
	if (!Reader.IsOpen() || Reader.GetFileSize() <= 0 || Reader.GetFileSize() > 64 * 1024 * 1024)
		return false;
	TArray<char> Text;
	Text.AddUninitialized(Reader.GetFileSize() + 1);
	Reader.Serialize(Text.GetData(), Reader.GetFileSize());
	Text[Text.Num() - 1] = 0;

	TArray<FScdaLinTextureStreamEntry> TextureStreams;
	struct FSegmentRecord
	{
		FString PackageFilename;
		FString SourceFilename;
		int VirtualOffset;
		int Size;
		int SourceLogicalOffset;
	};
	TArray<FSegmentRecord> Segments;
	TArray<FString> ScannedPackages;
	bool HeaderSeen = false;
	bool FileSeen = false;
	int ManifestFileCount = 0;
	int ManifestVersion = 0;

	for (char *Line = strtok(Text.GetData(), "\r\n"); Line; Line = strtok(NULL, "\r\n"))
	{
		if (!Line[0] || Line[0] == '#')
			continue;
		int Version;
		if (sscanf(Line, "UMODEL_LIN_MANIFEST %d", &Version) == 1)
		{
			if (Version < SCDA_LIN_MANIFEST_VERSION || Version > SCDA_LIN_MANIFEST_VERSION)
				return false;
			ManifestVersion = Version;
			HeaderSeen = true;
			continue;
		}

		char Name1[MAX_PACKAGE_PATH], Name2[MAX_PACKAGE_PATH];
		unsigned long long FileSize;
		if (sscanf(Line, "file \"%511[^\"]\" %llx", Name1, &FileSize) == 2)
		{
			if (!ValidateScdaLinManifestFile(Name1, FileSize))
				return false;
			FileSeen = true;
			ManifestFileCount++;
			continue;
		}

		unsigned VirtualOffset, Size, LogicalOffset;
		if (sscanf(Line, "segment \"%511[^\"]\" %x %x \"%511[^\"]\" %x",
			Name1, &VirtualOffset, &Size, Name2, &LogicalOffset) == 5)
		{
			FSegmentRecord& Segment = Segments[Segments.AddDefaulted()];
			Segment.PackageFilename = Name1;
			Segment.SourceFilename = Name2;
			Segment.VirtualOffset = VirtualOffset;
			Segment.Size = Size;
			Segment.SourceLogicalOffset = LogicalOffset;
			continue;
		}

		if (ManifestVersion >= 2 && sscanf(Line, "scanned \"%511[^\"]\"", Name1) == 1)
		{
			ScannedPackages.Add(Name1);
			continue;
		}

		unsigned GpuOffset, GpuSize, TextureId, BinOffset, BinSize, USize, VSize;
		int FormatCode;
		if (sscanf(Line, "texture \"%511[^\"]\" \"%511[^\"]\" %x %x %x %x %x %x %x %d",
			Name1, Name2, &GpuOffset, &GpuSize, &TextureId, &BinOffset, &BinSize, &USize, &VSize, &FormatCode) == 10)
		{
			FScdaLinTextureStreamEntry& Entry = TextureStreams[TextureStreams.AddDefaulted()];
			Entry.TextureName = Name1;
			Entry.BinFilename = Name2;
			Entry.GpuOffset = GpuOffset;
			Entry.GpuSize = GpuSize;
			Entry.TextureId = TextureId;
			Entry.BinOffset = BinOffset;
			Entry.BinSize = BinSize;
			Entry.USize = USize;
			Entry.VSize = VSize;
			Entry.FormatCode = FormatCode;
			continue;
		}
	}
	if (!HeaderSeen || !FileSeen)
		return false;
	TArray<FScdaLinManifestSourceFile> CurrentSourceFiles;
	CollectScdaLinManifestSourceFiles(CurrentSourceFiles);
	if (ManifestFileCount != CurrentSourceFiles.Num())
		return false;

	for (const FSegmentRecord& Segment : Segments)
	{
		FScdaLinPackage *Package = FindScdaLinPackage(*Segment.PackageFilename);
		if (Package)
			AddScdaLinSegmentRef(*Package, Segment.VirtualOffset, Segment.Size,
				*Segment.SourceFilename, Segment.SourceLogicalOffset);
	}
	GScdaLinTextureStreams.Empty(TextureStreams.Num());
	for (const FScdaLinTextureStreamEntry& Entry : TextureStreams)
		GScdaLinTextureStreams.Add(Entry);
	for (const FString& PackageFilename : ScannedPackages)
	{
		FScdaLinPackage *Package = FindScdaLinPackage(*PackageFilename);
		if (Package)
			Package->PayloadStreamsMapped = true;
	}
	appPrintf("Loaded SCDA LIN payload manifest: %s (%d segments, %d textures)\n",
		*GScdaLinManifestFilename, Segments.Num(), TextureStreams.Num());
	return true;
}

static void WriteScdaLinPayloadManifest()
{
	if (getenv("SCDA_LIN_NO_WRITE_MANIFEST"))
		return;
	if (GScdaLinManifestFilename.IsEmpty() || !GScdaLinPackages)
		return;

	TArray<FScdaLinManifestSourceFile> SourceFiles;
	CollectScdaLinManifestSourceFiles(SourceFiles);

	FString TempFilename = GScdaLinManifestFilename;
	TempFilename += ".tmp";
	FILE *Writer = fopen(*TempFilename, "wt");
	if (!Writer)
		return;
	fprintf(Writer, "UMODEL_LIN_MANIFEST %d\n", SCDA_LIN_MANIFEST_VERSION);
	fprintf(Writer, "# Generated by UModel. Delete this file to force a full LIN payload rescan.\n");
	for (const FScdaLinManifestSourceFile& File : SourceFiles)
	{
		fprintf(Writer, "file \"%s\" %llX\n", *File.RelativeName, File.Size);
	}
	for (const FScdaLinPackage& Package : *GScdaLinPackages)
	{
		if (Package.PayloadStreamsMapped)
			fprintf(Writer, "scanned \"%s\"\n", *Package.Filename);
		for (const FScdaLinPackage::FSegment& Segment : Package.Segments)
		{
			if (Segment.SourceFilename.IsEmpty())
				continue;
			fprintf(Writer, "segment \"%s\" %X %X \"%s\" %X\n", *Package.Filename,
				Segment.VirtualOffset, Segment.Size, *Segment.SourceFilename, Segment.SourceLogicalOffset);
		}
	}
	for (const FScdaLinTextureStreamEntry& Entry : GScdaLinTextureStreams)
	{
		fprintf(Writer, "texture \"%s\" \"%s\" %X %X %X %X %X %X %X %d\n",
			*Entry.TextureName, *Entry.BinFilename, Entry.GpuOffset, Entry.GpuSize, Entry.TextureId,
			Entry.BinOffset, Entry.BinSize, Entry.USize, Entry.VSize, Entry.FormatCode);
	}
	bool WriteOk = !ferror(Writer);
	if (fclose(Writer) != 0)
		WriteOk = false;
	if (!WriteOk)
	{
		remove(*TempFilename);
		appPrintf("WARNING: unable to write SCDA LIN payload manifest: %s\n", *GScdaLinManifestFilename);
		return;
	}
	remove(*GScdaLinManifestFilename);
	if (rename(*TempFilename, *GScdaLinManifestFilename) == 0)
		appPrintf("Wrote SCDA LIN payload manifest: %s\n", *GScdaLinManifestFilename);
}

static bool ReadLinCompactIndex(const byte *Data, int DataSize, int& Pos, int& Value)
{
	if (Pos < 0 || Pos >= DataSize)
		return false;

	byte First = Data[Pos++];
	bool Negative = (First & 0x80) != 0;
	Value = First & 0x3F;
	int Shift = 6;
	if (First & 0x40)
	{
		for (int i = 1; i < 5; i++)
		{
			if (Pos >= DataSize)
				return false;
			byte Next = Data[Pos++];
			Value |= (Next & 0x7F) << Shift;
			Shift += 7;
			if (!(Next & 0x80))
				break;
			if (i == 4)
				return false;
		}
	}
	if (Negative)
		Value = -Value;
	return true;
}

static bool ReadLinInt32(const byte *Data, int DataSize, int& Pos, int& Value)
{
	if (Pos < 0 || Pos + 4 > DataSize)
		return false;
	memcpy(&Value, Data + Pos, 4);
	Pos += 4;
	return true;
}

static bool IsLinPackageExtension(const char *Filename)
{
	const char *Ext = strrchr(Filename, '.');
	if (!Ext)
		return false;
	Ext++;
	return !stricmp(Ext, "utx") || !stricmp(Ext, "ukx") || !stricmp(Ext, "usx") ||
		!stricmp(Ext, "uax") || !stricmp(Ext, "unr") || !stricmp(Ext, "u");
}

static FString GetLinPackageName(const char *Filename)
{
	const char *Start = strrchr(Filename, '\\');
	const char *Start2 = strrchr(Filename, '/');
	if (!Start || (Start2 && Start2 > Start))
		Start = Start2;
	Start = Start ? Start + 1 : Filename;

	const char *Dot = strrchr(Start, '.');
	int Len = Dot ? int(Dot - Start) : int(strlen(Start));
	return FString(Len, Start);
}

static bool ReadScdaLinManifest(const byte *Data, int DataSize, TArray<FScdaLinManifestEntry>& Entries)
{
	// Manifest-bearing LIN files begin with a compact-string list of original package paths.
	// The entries are: path, original package offset, original package size, flags.
	int Pos = -1;
	for (int Probe = 0; Probe < DataSize && Probe < 0x1000; Probe++)
	{
		int StringPos = Probe;
		int Length;
		if (!ReadLinCompactIndex(Data, DataSize, StringPos, Length) || Length <= 1 || Length > MAX_PACKAGE_PATH)
			continue;
		if (StringPos + Length + 12 > DataSize || Data[StringPos + Length - 1] != 0)
			continue;
		const char *Path = (const char*)Data + StringPos;
		if (strnicmp(Path, "DataXb\\", 7) == 0 || strnicmp(Path, "DataXb/", 7) == 0)
		{
			Pos = Probe;
			break;
		}
	}
	if (Pos < 0)
		return false;

	while (Pos < DataSize)
	{
		int Length;
		if (!ReadLinCompactIndex(Data, DataSize, Pos, Length) || Length <= 1 || Length > MAX_PACKAGE_PATH)
			break;
		if (Pos + Length + 12 > DataSize || Data[Pos + Length - 1] != 0)
			break;

		const char *Path = (const char*)Data + Pos;
		if (strnicmp(Path, "DataXb\\", 7) != 0 && strnicmp(Path, "DataXb/", 7) != 0)
			break;
		Pos += Length;

		FScdaLinManifestEntry Entry;
		Entry.Filename = Path;
		for (char& C : Entry.Filename.GetDataArray())
			if (C == '\\') C = '/';
		Entry.PackageName = GetLinPackageName(Path);
		int Flags;
		if (!ReadLinInt32(Data, DataSize, Pos, Entry.OriginalOffset) ||
			!ReadLinInt32(Data, DataSize, Pos, Entry.OriginalSize) ||
			!ReadLinInt32(Data, DataSize, Pos, Flags))
			return false;
		if (Entry.OriginalSize > 0 && IsLinPackageExtension(Path))
			Entries.Add(Entry);
	}
	return Entries.Num() > 0;
}

struct FScdaLinHeader
{
	int			Offset;
	int			NameCount;
	int			NameOffset;
	int			ExportCount;
	int			ExportOffset;
	int			ImportCount;
	int			ImportOffset;
	int			NameEnd;
	int			ImportEnd;
	int			ExportEnd;
	int			MaxExportEnd;
	TArray<FString> Names;
	TArray<FString> Imports;
	TArray<FScdaLinExportRange> Exports;
};

static bool ReadScdaLinHeader(const byte *Data, int DataSize, int Offset, FScdaLinHeader& Header)
{
	if (Offset < 0 || Offset + 0x28 > DataSize)
		return false;

	int Tag, Version;
	memcpy(&Tag, Data + Offset, 4);
	memcpy(&Version, Data + Offset + 4, 4);
	if (Tag != PACKAGE_FILE_TAG || (Version & 0xFFFF) != 100)
		return false;

	Header.Offset = Offset;
	memcpy(&Header.NameCount, Data + Offset + 0x10, 4);
	memcpy(&Header.NameOffset, Data + Offset + 0x14, 4);
	memcpy(&Header.ExportCount, Data + Offset + 0x18, 4);
	memcpy(&Header.ExportOffset, Data + Offset + 0x1C, 4);
	memcpy(&Header.ImportCount, Data + Offset + 0x20, 4);
	memcpy(&Header.ImportOffset, Data + Offset + 0x24, 4);
	if (Header.NameCount <= 0 || Header.NameCount > 100000 ||
		Header.ExportCount < 0 || Header.ExportCount > 100000 ||
		Header.ImportCount < 0 || Header.ImportCount > 100000 ||
		Header.NameOffset < 0x28 || Offset + Header.NameOffset >= DataSize)
		return false;

	int Pos = Offset + Header.NameOffset;
	Header.Names.Empty(Header.NameCount);
	for (int i = 0; i < Header.NameCount; i++)
	{
		if (Pos >= DataSize)
			return false;
		int Length = Data[Pos++];
		if (Length <= 0 || Length >= 256 || Pos + Length + 5 > DataSize || Data[Pos + Length] != 0)
			return false;
		Header.Names.Add(FString(Length, (const char*)Data + Pos));
		Pos += Length + 1 + 4;
	}
	Header.NameEnd = Pos;

	// The seekless LIN keeps package tables physically contiguous even though their
	// serialized offsets still point into the original source package.
	Header.Imports.Empty(Header.ImportCount);
	for (int i = 0; i < Header.ImportCount; i++)
	{
		int ClassPackage, ClassName, PackageIndex, ObjectName;
		if (!ReadLinCompactIndex(Data, DataSize, Pos, ClassPackage) ||
			!ReadLinCompactIndex(Data, DataSize, Pos, ClassName) ||
			!ReadLinInt32(Data, DataSize, Pos, PackageIndex) ||
			!ReadLinCompactIndex(Data, DataSize, Pos, ObjectName))
			return false;
		if (ObjectName < 0 || ObjectName >= Header.Names.Num())
			return false;
		Header.Imports.Add(Header.Names[ObjectName]);
	}
	Header.ImportEnd = Pos;

	Header.MaxExportEnd = 0;
	Header.Exports.Empty(Header.ExportCount);
	for (int i = 0; i < Header.ExportCount; i++)
	{
		int ClassIndex, Dummy, ObjectNameIndex, SerialSize, SerialOffset = 0;
		if (!ReadLinCompactIndex(Data, DataSize, Pos, ClassIndex) ||
			!ReadLinCompactIndex(Data, DataSize, Pos, Dummy) ||
			!ReadLinInt32(Data, DataSize, Pos, Dummy) ||
			!ReadLinCompactIndex(Data, DataSize, Pos, ObjectNameIndex) ||
			!ReadLinInt32(Data, DataSize, Pos, Dummy) ||
			!ReadLinCompactIndex(Data, DataSize, Pos, SerialSize))
			return false;
		if (ObjectNameIndex < 0 || ObjectNameIndex >= Header.Names.Num())
			return false;
		if (SerialSize)
		{
			if (!ReadLinCompactIndex(Data, DataSize, Pos, SerialOffset))
				return false;
			if (SerialOffset < 0 || SerialSize < 0 || SerialOffset > 0x7FFFFFFF - SerialSize)
				return false;
			int SerialEnd = SerialOffset + SerialSize;
			if (SerialEnd > Header.MaxExportEnd)
				Header.MaxExportEnd = SerialEnd;
		}
		FScdaLinExportRange& Export = Header.Exports[Header.Exports.AddDefaulted()];
		Export.SerialOffset = SerialOffset;
		Export.SerialSize = SerialSize;
		Export.ClassIndex = ClassIndex;
		Export.ObjectNameIndex = ObjectNameIndex;
		Export.ObjectName = Header.Names[ObjectNameIndex];
		if (ClassIndex < 0)
		{
			int ImportIndex = -ClassIndex - 1;
			if (ImportIndex >= 0 && ImportIndex < Header.Imports.Num())
				Export.ClassName = Header.Imports[ImportIndex];
		}
		else if (ClassIndex > 0)
		{
			int ExportIndex = ClassIndex - 1;
			if (ExportIndex >= 0 && ExportIndex < Header.Exports.Num())
				Export.ClassName = Header.Exports[ExportIndex].ObjectName;
		}
	}
	Header.ExportEnd = Pos;
	return Header.ExportEnd > Header.NameEnd && Header.ExportEnd <= DataSize;
}

static bool LinHeaderHasName(const FScdaLinHeader& Header, const char *Name)
{
	for (const FString& Item : Header.Names)
		if (!stricmp(*Item, Name))
			return true;
	return false;
}

static bool IsPowerOfTwo(int Value)
{
	return Value > 0 && (Value & (Value - 1)) == 0;
}

static int GetPowerOfTwoBits(int Value)
{
	int Bits = 0;
	while (Value > 1)
	{
		Value >>= 1;
		Bits++;
	}
	return Bits;
}

static bool IsScdaLinGenericExportPackage(const FString& Filename)
{
	const char *Ext = strrchr(*Filename, '.');
	if (!Ext)
		return false;
	return !stricmp(Ext, ".ukx") || !stricmp(Ext, ".usx");
}

static bool IsScdaLinTexturePackage(const FString& Filename)
{
	const char *Ext = strrchr(*Filename, '.');
	return Ext && !stricmp(Ext, ".utx");
}

static bool IsScdaLinTextureExportClass(const FString& ClassName)
{
	return !stricmp(*ClassName, "Texture") ||
		!stricmp(*ClassName, "Texture2D") ||
		!stricmp(*ClassName, "ProceduralTexture") ||
		!stricmp(*ClassName, "Cubemap");
}

static bool IsScdaLinPaletteExportClass(const FString& ClassName)
{
	return !stricmp(*ClassName, "Palette");
}

static bool ScdaLinContainsNoCase(const char *Text, const char *Find);

static bool IsScdaLinTextureHeader(const FScdaLinHeader& Header)
{
	if (!LinHeaderHasName(Header, "TexVerNumber") || !LinHeaderHasName(Header, "Texture"))
		return false;
	for (const FScdaLinExportRange& Export : Header.Exports)
	{
		if (Export.SerialSize >= 0x100 && (
			ScdaLinContainsNoCase(*Export.ObjectName, "_d") ||
			ScdaLinContainsNoCase(*Export.ObjectName, "_n") ||
			ScdaLinContainsNoCase(*Export.ObjectName, "_s")))
			return true;
	}
	return false;
}

static bool IsScdaLinSkeletalHeader(const FScdaLinHeader& Header)
{
	bool HasSkeletalClass = LinHeaderHasName(Header, "SkeletalMesh") || LinHeaderHasName(Header, "MeshAnimation");
	if (!HasSkeletalClass)
		return false;
	for (const FScdaLinExportRange& Export : Header.Exports)
	{
		if (Export.SerialSize > 0 &&
			(!stricmp(*Export.ClassName, "SkeletalMesh") || !stricmp(*Export.ClassName, "MeshAnimation")))
			return true;
	}
	return false;
}

static void GetScdaLinLevelBaseName(const char *RelativeName, FString& OutName)
{
	const char *Start = strrchr(RelativeName, '/');
	Start = Start ? Start + 1 : RelativeName;
	const char *Dot = strrchr(Start, '.');
	if (Dot && Dot > Start)
		OutName = FString(Dot - Start, Start);
	else
		OutName = Start;
}

static bool ScdaLinContainsNoCase(const char *Text, const char *Find)
{
	int FindLen = strlen(Find);
	if (FindLen <= 0)
		return true;
	for (const char *Scan = Text; *Scan; Scan++)
		if (!strnicmp(Scan, Find, FindLen))
			return true;
	return false;
}

static bool ScdaLinEndsWithNoCase(const char *Text, const char *Suffix)
{
	int TextLen = strlen(Text);
	int SuffixLen = strlen(Suffix);
	return TextLen >= SuffixLen && !stricmp(Text + TextLen - SuffixLen, Suffix);
}

static void NormalizeScdaLinPath(FString& Filename)
{
	for (char& C : Filename.GetDataArray())
		if (C == '\\') C = '/';
}

static void AddScdaLinPhysicalPayloadFile(const char *Filename, const char *RelativeName, int64 Size)
{
	FString CleanName = RelativeName;
	NormalizeScdaLinPath(CleanName);
	const char *ShortName = strrchr(*CleanName, '/');
	ShortName = ShortName ? ShortName + 1 : *CleanName;
	if (!stricmp(ShortName, "common.lin"))
		return;

	const char *DevScan = getenv("SCDA_LIN_DEV_SCAN");
	if (DevScan && stricmp(DevScan, "0"))
	{
		const char *DevFilter = getenv("SCDA_LIN_DEV_FILTER");
		if (DevFilter && DevFilter[0])
		{
			if (!ScdaLinContainsNoCase(*CleanName, DevFilter))
				return;
		}
		else if (!ScdaLinContainsNoCase(*CleanName, "x01_iceland_a") &&
			!ScdaLinContainsNoCase(*CleanName, "x01_iceland_b") &&
			!ScdaLinContainsNoCase(*CleanName, "x02_prison_a") &&
			!ScdaLinContainsNoCase(*CleanName, "02_prison_a"))
			return;
	}

	FScdaLinSourceFile& Source = GScdaLinPayloadFiles[GScdaLinPayloadFiles.AddDefaulted()];
	Source.Filename = Filename;
	Source.RelativeName = CleanName;
	Source.Size = Size;
}

static void CollectScdaLinPhysicalPayloadFilesWorker(const char *Dir, const char *RelDir)
{
	char Pattern[MAX_PACKAGE_PATH];
	appSprintf(ARRAY_ARG(Pattern), "%s\\*", Dir);

	_finddata_t FindData;
	intptr_t Find = _findfirst(Pattern, &FindData);
	if (Find == -1)
		return;
	do
	{
		if (!strcmp(FindData.name, ".") || !strcmp(FindData.name, ".."))
			continue;
		char FullName[MAX_PACKAGE_PATH];
		char RelName[MAX_PACKAGE_PATH];
		appSprintf(ARRAY_ARG(FullName), "%s\\%s", Dir, FindData.name);
		appSprintf(ARRAY_ARG(RelName), "%s/%s", RelDir, FindData.name);
		if (FindData.attrib & _A_SUBDIR)
		{
			CollectScdaLinPhysicalPayloadFilesWorker(FullName, RelName);
			continue;
		}
		const char *Ext = strrchr(FindData.name, '.');
		bool IsSidecarBin = Ext && !stricmp(Ext, ".bin") &&
			(ScdaLinEndsWithNoCase(FindData.name, "_sm.bin") || ScdaLinEndsWithNoCase(FindData.name, "_tex.bin"));
		if ((Ext && !stricmp(Ext, ".lin")) || IsSidecarBin)
			AddScdaLinPhysicalPayloadFile(FullName, RelName, FindData.size);
	} while (_findnext(Find, &FindData) == 0);
	_findclose(Find);
}

static void CollectScdaLinPhysicalPayloadFiles(const char *CommonFilename)
{
	GScdaLinPayloadFiles.Empty();
	GScdaLinCommonSourceFile = FScdaLinSourceFile();
	GScdaLinCommonSourceFileValid = false;
	char LMapsDir[MAX_PACKAGE_PATH];
	appStrncpyz(LMapsDir, CommonFilename, ARRAY_COUNT(LMapsDir));
	char *Slash1 = strrchr(LMapsDir, '/');
	char *Slash2 = strrchr(LMapsDir, '\\');
	char *Slash = (!Slash1 || (Slash2 && Slash2 > Slash1)) ? Slash2 : Slash1;
	if (!Slash)
		return;
	*Slash = 0;
	FFileReader CommonReader(CommonFilename, EFileArchiveOptions::OpenWarning);
	if (CommonReader.IsOpen())
	{
		GScdaLinCommonSourceFile.Filename = CommonFilename;
		GScdaLinCommonSourceFile.RelativeName = "LMaps/common.lin";
		GScdaLinCommonSourceFile.Size = CommonReader.GetFileSize();
		GScdaLinCommonSourceFileValid = true;
	}
	CollectScdaLinPhysicalPayloadFilesWorker(LMapsDir, "LMaps");
}

static bool GetScdaLinCommonFilenameFromPath(const char *Filename, FString& OutFilename)
{
	char CleanName[MAX_PACKAGE_PATH];
	appStrncpyz(CleanName, Filename, ARRAY_COUNT(CleanName));
	for (char *C = CleanName; *C; C++)
		if (*C == '\\') *C = '/';

	char *LMaps = NULL;
	for (char *Scan = CleanName; *Scan; Scan++)
	{
		if (!strnicmp(Scan, "/LMaps/", 7) || !stricmp(Scan, "/LMaps"))
		{
			LMaps = Scan + 1;
			break;
		}
		if (!strnicmp(Scan, "LMaps/", 6) || !stricmp(Scan, "LMaps"))
		{
			LMaps = Scan;
			break;
		}
	}
	if (!LMaps)
		return false;

	char *After = LMaps + 5;
	*After = 0;
	appStrcatn(CleanName, ARRAY_COUNT(CleanName), "/common.lin");
	OutFilename = CleanName;
	return true;
}

static bool EnsureScdaLinCommonCache(const char *Filename)
{
	if (GScdaLinCommonCacheLoaded)
	{
		CopyScdaLinArray(GScdaLinPayloadFiles, GScdaLinPayloadFilesCache);
		return true;
	}

	FString CommonFilename;
	if (!GetScdaLinCommonFilenameFromPath(Filename, CommonFilename))
		return false;
	if (!appFileExists(*CommonFilename))
		return false;

	FFileReader Reader(*CommonFilename, EFileArchiveOptions::OpenWarning);
	if (!Reader.IsOpen())
		return false;

	TArray<byte> LogicalData;
	if (!DecompressScdaLin(&Reader, LogicalData))
		return false;

	TArray<FScdaLinManifestEntry> Manifest;
	if (!ReadScdaLinManifest(LogicalData.GetData(), LogicalData.Num(), Manifest))
		return false;

	GScdaLinCommonCacheFilename = CommonFilename;
	CopyScdaLinArray(GScdaLinCommonManifestCache, Manifest);
	CollectScdaLinPhysicalPayloadFiles(*CommonFilename);
	CopyScdaLinArray(GScdaLinPayloadFilesCache, GScdaLinPayloadFiles);
	GScdaLinCommonCacheLoaded = true;
	if (getenv("SCDA_LIN_DEBUG") || getenv("SCDA_LIN_DEBUG_PACKAGES"))
		appPrintf("SCDA LIN common cache: %s manifest=%d payloadFiles=%d\n",
			*CommonFilename, GScdaLinCommonManifestCache.Num(), GScdaLinPayloadFilesCache.Num());
	return true;
}

static void FindScdaLinMipCandidates(const byte *Data, int DataSize, TArray<FScdaLinMipCandidate>& Candidates)
{
	// SCDA keeps TLazyArray's original absolute SeekPos in each seekless mip body.
	// The body itself is inline in the LIN stream, so this stale offset identifies
	// both the original export range and the body's physical start.
	for (int Tail = 0; Tail + 7 <= DataSize; Tail++)
	{
		uint16 USize, VSize;
		memcpy(&USize, Data + Tail, 2);
		memcpy(&VSize, Data + Tail + 2, 2);
		byte Unknown = Data[Tail + 4];
		byte UBits = Data[Tail + 5];
		byte VBits = Data[Tail + 6];
		if (Unknown != 0 || !IsPowerOfTwo(USize) || !IsPowerOfTwo(VSize) ||
			UBits != GetPowerOfTwoBits(USize) || VBits != GetPowerOfTwoBits(VSize))
			continue;

		int BlocksX = (int(USize) + 3) / 4;
		int BlocksY = (int(VSize) + 3) / 4;
		if (BlocksX < 1) BlocksX = 1;
		if (BlocksY < 1) BlocksY = 1;
		int BlockCount = BlocksX * BlocksY;
		int PixelCount = int(USize) * int(VSize);
		int Lengths[6] =
		{
			BlockCount * 8,	// DXT1
			BlockCount * 16,	// DXT3/DXT5
			PixelCount,		// P8 / one byte per pixel
			PixelCount * 2,	// 16-bit formats
			PixelCount * 3,	// RGB8
			PixelCount * 4	// RGBA8
		};
		for (int LengthIndex = 0; LengthIndex < ARRAY_COUNT(Lengths); LengthIndex++)
		{
			int DataLength = Lengths[LengthIndex];
			bool DuplicateLength = false;
			for (int Prev = 0; Prev < LengthIndex; Prev++)
				if (Lengths[Prev] == DataLength)
					DuplicateLength = true;
			if (DuplicateLength || DataLength <= 0)
				continue;

			// A compact index occupies at most five bytes. Find the exact encoding
			// immediately before the inline mip data.
			for (int CountSize = 1; CountSize <= 5; CountSize++)
			{
				int CountStart = Tail - DataLength - CountSize;
				int SkipField = CountStart - 4;
				if (SkipField < 0)
					continue;
				int CountPos = CountStart;
				int DecodedLength;
				if (!ReadLinCompactIndex(Data, DataSize, CountPos, DecodedLength) ||
					DecodedLength != DataLength || CountPos != Tail - DataLength)
					continue;

				int SkipOffset;
				memcpy(&SkipOffset, Data + SkipField, 4);
				if (SkipOffset <= 0)
					continue;

				FScdaLinMipCandidate& Candidate = Candidates[Candidates.AddDefaulted()];
				Candidate.LogicalSkipField = SkipField;
				Candidate.OriginalSkipField = SkipOffset - 4 - CountSize - DataLength;
				break;
			}
		}
	}
}

static bool IsScdaLinStreamName(const byte *Data, int DataSize, int Pos, int Length)
{
	if (Length <= 1 || Length > 256 || Pos < 0 || Pos + Length > DataSize || Data[Pos + Length - 1] != 0)
		return false;
	for (int i = 0; i < Length - 1; i++)
	{
		byte C = Data[Pos + i];
		if (C < 0x20 || C >= 0x7F)
			return false;
	}
	return true;
}

static bool ReadScdaLinStreamRecord(const byte *Data, int DataSize, int& Pos, FScdaLinTextureStreamEntry& Entry)
{
	int Start = Pos;
	int Length;
	if (!ReadLinCompactIndex(Data, DataSize, Pos, Length) || !IsScdaLinStreamName(Data, DataSize, Pos, Length))
		return false;
	Entry.TextureName = FString(Length - 1, (const char*)Data + Pos);
	Pos += Length;
	if (Pos + 32 > DataSize)
		return false;

	int Fields[8];
	memcpy(Fields, Data + Pos, sizeof(Fields));
	if (Fields[0] < 0 || Fields[1] < 0 || Fields[2] < 0 || Fields[3] != 0 ||
		Fields[4] != -1 || Fields[5] < 0 || Fields[6] != -1 || Fields[7] < 0 ||
		(Fields[1] == 0 && Fields[7] == 0))
		return false;
	Pos += sizeof(Fields);

	Entry.GpuOffset = Fields[0];
	Entry.GpuSize = Fields[1];
	Entry.TextureId = Fields[2];
	Entry.BinOffset = Fields[5];
	Entry.BinSize = Fields[7];
	Entry.LogicalOffset = Start;
	return true;
}

static bool EndsWithScdaTexBin(const FString& Name)
{
	const char *Text = *Name;
	int Length = strlen(Text);
	return Length > 8 && !stricmp(Text + Length - 8, "_tex.bin");
}

static int GetScdaLinDxt1MipSize(int USize, int VSize)
{
	return max(1, (USize + 3) / 4) * max(1, (VSize + 3) / 4) * 8;
}

static int GetScdaLinDxt1ChainSize(int USize, int VSize)
{
	int Size = 0;
	while (true)
	{
		Size += GetScdaLinDxt1MipSize(USize, VSize);
		if (USize <= 4 && VSize <= 4)
			break;
		USize = max(1, USize >> 1);
		VSize = max(1, VSize >> 1);
	}
	return Size;
}

static bool IsScdaLinTextureMetadataCompatible(const FScdaLinTextureStreamEntry& Entry,
	int USize, int VSize, int FormatCode)
{
	if (FormatCode == 1)
		return GetScdaLinDxt1MipSize(USize, VSize) == Entry.BinSize;
	if (FormatCode >= 2 && FormatCode <= 8)
	{
		while (true)
		{
			if (Align(GetScdaLinDxt1ChainSize(USize, VSize), 0x200) == Entry.BinSize)
				return true;
			if (USize <= 1 && VSize <= 1)
				break;
			USize = max(1, USize >> 1);
			VSize = max(1, VSize >> 1);
		}
	}
	return false;
}

static void FindScdaLinTextureMetadata(const byte *Data, int DataSize, TArray<FScdaLinTextureStreamEntry>& Entries)
{
	for (int Pos = 0; Pos + 41 <= DataSize; Pos++)
	{
		if (Data[Pos] != 0x02 || Data[Pos + 1] != 0x22)
			continue;
		int TextureId;
		memcpy(&TextureId, Data + Pos + 2, 4);

		const byte *Meta = Data + Pos + 6;
		if (Meta[0] != 0x07 || Meta[1] != 0x01 || Meta[2] != 0x03 || Meta[3] != 0x03 ||
			Meta[4] != 0x01 || Meta[6] != 0x04 || Meta[7] != 0x01 || Meta[9] != 0x09 ||
			Meta[10] != 0x22 || Meta[15] != 0x08 || Meta[16] != 0x22 ||
			Meta[21] != 0x05 || Meta[22] != 0x22 || Meta[27] != 0x06 || Meta[28] != 0x22 ||
			Meta[33] != 0x01)
			continue;

		int USize, VSize, UClamp, VClamp;
		memcpy(&USize, Meta + 11, 4);
		memcpy(&VSize, Meta + 17, 4);
		memcpy(&UClamp, Meta + 23, 4);
		memcpy(&VClamp, Meta + 29, 4);
		if (!IsPowerOfTwo(USize) || !IsPowerOfTwo(VSize) || UClamp != USize || VClamp != VSize ||
			Meta[5] != GetPowerOfTwoBits(USize) || Meta[8] != GetPowerOfTwoBits(VSize))
			continue;

		for (FScdaLinTextureStreamEntry& Entry : Entries)
		{
			if (Entry.TextureId != TextureId)
				continue;
			int FormatCode = Meta[34];
			bool Compatible = IsScdaLinTextureMetadataCompatible(Entry, USize, VSize, FormatCode);
			bool ExistingCompatible = Entry.USize > 0 &&
				IsScdaLinTextureMetadataCompatible(Entry, Entry.USize, Entry.VSize, Entry.FormatCode);
			if (!Compatible && ExistingCompatible)
				continue;
			Entry.USize = USize;
			Entry.VSize = VSize;
			Entry.FormatCode = FormatCode;
		}
	}
}

static void FindScdaLinTextureStreams(const byte *Data, int DataSize)
{
#if THREADING
	CMutex::ScopedLock Lock(GScdaLinTextureStreamMutex);
#endif
	TArray<FScdaLinTextureStreamEntry> Entries;
	for (int Start = 0; Start < DataSize; Start++)
	{
		int Pos = Start;
		FScdaLinTextureStreamEntry Entry;
		if (!ReadScdaLinStreamRecord(Data, DataSize, Pos, Entry))
			continue;
		Entries.Add(Entry);
		Start = Pos - 1;
	}
	FindScdaLinTextureMetadata(Data, DataSize, Entries);

	for (int Start = 0; Start < DataSize; Start++)
	{
		int NamePos = Start;
		int Length;
		if (!ReadLinCompactIndex(Data, DataSize, NamePos, Length) || !IsScdaLinStreamName(Data, DataSize, NamePos, Length))
			continue;
		FString BinFilename(Length - 1, (const char*)Data + NamePos);
		if (!EndsWithScdaTexBin(BinFilename))
			continue;

		int Added = 0;
		for (FScdaLinTextureStreamEntry& Entry : Entries)
		{
			if (Entry.LogicalOffset >= Start || Start - Entry.LogicalOffset > 0x100000)
				continue;
			Entry.BinFilename = BinFilename;
			bool Duplicate = false;
			for (const FScdaLinTextureStreamEntry& Existing : GScdaLinTextureStreams)
			{
				if (!stricmp(*Existing.TextureName, *Entry.TextureName) &&
					!stricmp(*Existing.BinFilename, *Entry.BinFilename) &&
					Existing.BinOffset == Entry.BinOffset)
				{
					Duplicate = true;
					break;
				}
			}
			if (!Duplicate)
			{
				GScdaLinTextureStreams.Add(Entry);
				Added++;
			}
		}

		if (getenv("SCDA_LIN_DEBUG"))
			appPrintf("    LIN texture stream: %s entries=%d at %X\n", *BinFilename, Added, Start);
	}
}

static int InferScdaLinSquareDxt1SizeFromChain(int ChainSize)
{
	for (int Size = 4096; Size >= 4; Size >>= 1)
	{
		if (Align(GetScdaLinDxt1ChainSize(Size, Size), 0x200) == ChainSize)
			return Size;
	}
	return 0;
}

static int InferScdaLinSquareDxt1SizeFromMip(int MipSize)
{
	for (int Size = 4096; Size >= 4; Size >>= 1)
	{
		if (GetScdaLinDxt1MipSize(Size, Size) == MipSize)
			return Size;
	}
	return 0;
}

static bool InferScdaLinRectDxt1SizeFromChain(int ChainSize, int& USize, int& VSize)
{
	int BestDelta = 0x7FFFFFFF;
	int BestAspect = 0x7FFFFFFF;
	int BestOrientation = 0x7FFFFFFF;
	USize = 0;
	VSize = 0;
	for (int U = 4; U <= 4096; U <<= 1)
	{
		for (int V = 4; V <= 4096; V <<= 1)
		{
			int ExactSize = GetScdaLinDxt1ChainSize(U, V);
			int AlignedSize = Align(ExactSize, 0x200);
			int Delta = min(abs(ChainSize - ExactSize), abs(ChainSize - AlignedSize));
			if (Delta > 0x2000)
				continue;
			int Aspect = abs(GetPowerOfTwoBits(U) - GetPowerOfTwoBits(V));
			int Orientation = U >= V ? 0 : 1;
			if (Delta < BestDelta ||
				(Delta == BestDelta && (Aspect < BestAspect ||
				(Aspect == BestAspect && Orientation < BestOrientation))))
			{
				BestDelta = Delta;
				BestAspect = Aspect;
				BestOrientation = Orientation;
				USize = U;
				VSize = V;
			}
		}
	}
	return USize > 0 && VSize > 0;
}

static bool InferScdaLinRectDxt1SizeFromMip(int MipSize, int& USize, int& VSize)
{
	int BestDelta = 0x7FFFFFFF;
	int BestAspect = 0x7FFFFFFF;
	int BestOrientation = 0x7FFFFFFF;
	USize = 0;
	VSize = 0;
	for (int U = 4; U <= 4096; U <<= 1)
	{
		for (int V = 4; V <= 4096; V <<= 1)
		{
			int ExactSize = GetScdaLinDxt1MipSize(U, V);
			int Delta = abs(MipSize - ExactSize);
			if (Delta > 0x2000)
				continue;
			int Aspect = abs(GetPowerOfTwoBits(U) - GetPowerOfTwoBits(V));
			int Orientation = U >= V ? 0 : 1;
			if (Delta < BestDelta ||
				(Delta == BestDelta && (Aspect < BestAspect ||
				(Aspect == BestAspect && Orientation < BestOrientation))))
			{
				BestDelta = Delta;
				BestAspect = Aspect;
				BestOrientation = Orientation;
				USize = U;
				VSize = V;
			}
		}
	}
	return USize > 0 && VSize > 0;
}

static bool ResolveScdaLinTextureStreamInfo(const FScdaLinTextureStreamEntry& Entry,
	int& USize, int& VSize, int& FormatCode)
{
	USize = Entry.USize;
	VSize = Entry.VSize;
	FormatCode = Entry.FormatCode;
	if (USize > 0 && VSize > 0 && FormatCode >= 0)
		return true;

	int Size = 0;
	if (Entry.GpuSize > 0)
		Size = InferScdaLinSquareDxt1SizeFromChain(Entry.GpuSize);
	if (!Size && Entry.BinSize > 0)
	{
		Size = InferScdaLinSquareDxt1SizeFromChain(Entry.BinSize);
		if (!Size)
			Size = InferScdaLinSquareDxt1SizeFromMip(Entry.BinSize);
	}
	if (!Size)
	{
		int RectU = 0, RectV = 0;
		if (Entry.GpuSize > 0)
			InferScdaLinRectDxt1SizeFromChain(Entry.GpuSize, RectU, RectV);
		if (!RectU && Entry.BinSize > 0)
		{
			InferScdaLinRectDxt1SizeFromChain(Entry.BinSize, RectU, RectV);
			if (!RectU)
				InferScdaLinRectDxt1SizeFromMip(Entry.BinSize, RectU, RectV);
		}
		if (!RectU || !RectV)
			return false;
		USize = RectU;
		VSize = RectV;
		FormatCode = (Entry.BinSize == GetScdaLinDxt1MipSize(RectU, RectV)) ? 1 : 6;
		return true;
	}
	USize = Size;
	VSize = Size;
	FormatCode = (Entry.BinSize == GetScdaLinDxt1MipSize(Size, Size)) ? 1 : 6;
	return true;
}

static void NormalizeScdaLinTextureStreamInfo(FScdaLinTextureStreamEntry& Entry)
{
	int USize, VSize, FormatCode;
	if (!ResolveScdaLinTextureStreamInfo(Entry, USize, VSize, FormatCode))
		return;
	Entry.USize = USize;
	Entry.VSize = VSize;
	Entry.FormatCode = FormatCode;
}

static void GetScdaLinTexturePackageLevelBase(const char *PackageFilename, FString& OutBase)
{
	OutBase.Empty();
	if (!PackageFilename || !PackageFilename[0])
		return;
	const char *Start1 = strrchr(PackageFilename, '/');
	const char *Start2 = strrchr(PackageFilename, '\\');
	const char *Start = (!Start1 || (Start2 && Start2 > Start1)) ? Start2 : Start1;
	Start = Start ? Start + 1 : PackageFilename;
	char Base[MAX_PACKAGE_PATH];
	appStrncpyz(Base, Start, ARRAY_COUNT(Base));
	char *Dot = strrchr(Base, '.');
	if (Dot)
		*Dot = 0;
	const char *Suffix = "_tex";
	int Len = strlen(Base);
	int SuffixLen = strlen(Suffix);
	if (Len > SuffixLen && !stricmp(Base + Len - SuffixLen, Suffix))
		Base[Len - SuffixLen] = 0;
	OutBase = Base;
}

static bool ScdaLinTextureStreamMatchesPackage(const FScdaLinTextureStreamEntry& Entry, const char *PackageFilename)
{
	if (!Entry.PackageFilename.IsEmpty())
		return PackageFilename && !stricmp(*Entry.PackageFilename, PackageFilename);

	FString LevelBase;
	GetScdaLinTexturePackageLevelBase(PackageFilename, LevelBase);
	if (LevelBase.IsEmpty())
		return true;
	char ExpectedPrefix[MAX_PACKAGE_PATH];
	appSprintf(ARRAY_ARG(ExpectedPrefix), "%s/", *LevelBase);
	return !strnicmp(*Entry.BinFilename, ExpectedPrefix, strlen(ExpectedPrefix));
}

bool LoadScdaLinTextureStream(const char *TextureName, TArray<byte>& Data, int& GpuSize, int& TextureId,
	int& USize, int& VSize, int& FormatCode, const char *PackageFilename)
{
	for (const FScdaLinTextureStreamEntry& Entry : GScdaLinTextureStreams)
	{
		if (stricmp(*Entry.TextureName, TextureName))
			continue;
		if (!ScdaLinTextureStreamMatchesPackage(Entry, PackageFilename))
			continue;

		FArchive *Reader = NULL;
		int64 FileSize = 0;
		const CGameFileInfo *File = CGameFileInfo::Find(*Entry.BinFilename);
		if (File)
		{
			FileSize = File->Size;
			Reader = File->CreateReader(true);
		}
		if (!Reader && !GScdaLinManifestFilename.IsEmpty())
		{
			char BaseDir[MAX_PACKAGE_PATH];
			appStrncpyz(BaseDir, *GScdaLinManifestFilename, ARRAY_COUNT(BaseDir));
			char *Slash1 = strrchr(BaseDir, '/');
			char *Slash2 = strrchr(BaseDir, '\\');
			char *Slash = (!Slash1 || (Slash2 && Slash2 > Slash1)) ? Slash2 : Slash1;
			if (Slash)
				*Slash = 0;

			const char *BinName = *Entry.BinFilename;
			if (!strnicmp(BinName, "LMaps/", 6) || !strnicmp(BinName, "LMaps\\", 6))
				BinName += 6;
			char BinPath[MAX_PACKAGE_PATH];
			appSprintf(ARRAY_ARG(BinPath), "%s\\%s", BaseDir, BinName);
			FFileReader *DiskReader = new FFileReader(BinPath, EFileArchiveOptions::NoOpenError);
			if (DiskReader->IsOpen())
			{
				Reader = DiskReader;
				FileSize = Reader->GetFileSize();
			}
			else
			{
				delete DiskReader;
			}
		}
		if (!Reader)
			continue;
		if (Entry.BinOffset < 0 || Entry.BinSize <= 0 || Entry.BinOffset > FileSize - Entry.BinSize)
		{
			delete Reader;
			continue;
		}

		Data.Empty(Entry.BinSize);
		Data.AddUninitialized(Entry.BinSize);
		Reader->Seek(Entry.BinOffset);
		Reader->Serialize(Data.GetData(), Entry.BinSize);
		delete Reader;

		GpuSize = Entry.GpuSize;
		TextureId = Entry.TextureId;
		ResolveScdaLinTextureStreamInfo(Entry, USize, VSize, FormatCode);
		if (getenv("SCDA_LIN_DEBUG"))
			appPrintf("SCDA streamed texture: %s <- %s %X+%X gpuSize=%X id=%X meta=%dx%d fmt=%d\n",
				TextureName, *Entry.BinFilename, Entry.BinOffset, Entry.BinSize, Entry.GpuSize, Entry.TextureId,
				USize, VSize, FormatCode);
		return true;
	}
	return false;
}

bool GetScdaLinTextureStreamInfo(const char *TextureName, int& USize, int& VSize, int& FormatCode,
	const char *PackageFilename)
{
	for (const FScdaLinTextureStreamEntry& Entry : GScdaLinTextureStreams)
	{
		if (stricmp(*Entry.TextureName, TextureName))
			continue;
		if (!ScdaLinTextureStreamMatchesPackage(Entry, PackageFilename))
			continue;
		if (!ResolveScdaLinTextureStreamInfo(Entry, USize, VSize, FormatCode))
			return false;
		return true;
	}
	return false;
}

static bool ValidateScdaLinTaggedProperties(const byte *Data, int BodyEnd, int Pos,
	const TArray<FString>& Names, int& NativeStart, bool RequireProperties);

static bool ValidateScdaLinTextureBody(const byte *Data, int DataSize, int BodyStart, int BodySize,
	const TArray<FString>& Names)
{
	if (BodyStart < 0 || BodySize <= 0 || BodyStart > DataSize - BodySize)
		return false;
	int Pos = BodyStart;
	int BodyEnd = BodyStart + BodySize;
	int NativeStart;

	if (!ValidateScdaLinTaggedProperties(Data, BodyEnd, Pos, Names, NativeStart, true))
		return false;

	Pos = NativeStart;
	int MipCount;
	if (!ReadLinCompactIndex(Data, BodyEnd, Pos, MipCount) || MipCount <= 0 || MipCount > 32)
		return false;
	for (int MipIndex = 0; MipIndex < MipCount; MipIndex++)
	{
		if (Pos + 4 > BodyEnd)
			return false;
		Pos += 4; // stale TLazyArray SeekPos

		int DataLength;
		if (!ReadLinCompactIndex(Data, BodyEnd, Pos, DataLength) || DataLength < 0 || Pos > BodyEnd - DataLength - 7)
			return false;
		Pos += DataLength;

		uint16 USize, VSize;
		memcpy(&USize, Data + Pos, 2);
		memcpy(&VSize, Data + Pos + 2, 2);
		byte Unknown = Data[Pos + 4];
		byte UBits = Data[Pos + 5];
		byte VBits = Data[Pos + 6];
		if (Unknown != 0 || !IsPowerOfTwo(USize) || !IsPowerOfTwo(VSize) ||
			UBits != GetPowerOfTwoBits(USize) || VBits != GetPowerOfTwoBits(VSize))
			return false;
		Pos += 7;
	}
	return Pos == BodyEnd;
}

static bool ValidateScdaLinTaggedProperties(const byte *Data, int BodyEnd, int Pos,
	const TArray<FString>& Names, int& NativeStart, bool RequireProperties)
{
	int PropertyCount = 0;

	while (Pos < BodyEnd)
	{
		int NameIndex;
		if (!ReadLinCompactIndex(Data, BodyEnd, Pos, NameIndex) || NameIndex < 0 || NameIndex >= Names.Num())
			return false;
		if (!stricmp(*Names[NameIndex], "None"))
			break;
		PropertyCount++;

		if (Pos >= BodyEnd)
			return false;
		byte Info = Data[Pos++];
		int Type = Info & 0xF;
		bool IsArray = (Info & 0x80) != 0;
		if (Type <= 0 || Type > 15)
			return false;
		if (Type == 10) // StructProperty
		{
			int StructName;
			if (!ReadLinCompactIndex(Data, BodyEnd, Pos, StructName) || StructName < 0 || StructName >= Names.Num())
				return false;
		}

		int ValueSize;
		switch ((Info >> 4) & 7)
		{
		case 0: ValueSize = 1; break;
		case 1: ValueSize = 2; break;
		case 2: ValueSize = 4; break;
		case 3: ValueSize = 12; break;
		case 4: ValueSize = 16; break;
		case 5:
			if (Pos + 1 > BodyEnd) return false;
			ValueSize = Data[Pos++];
			break;
		case 6:
			if (Pos + 2 > BodyEnd) return false;
			{
				uint16 Size16;
				memcpy(&Size16, Data + Pos, 2);
				ValueSize = Size16;
				Pos += 2;
			}
			break;
		default:
			if (Pos + 4 > BodyEnd) return false;
			memcpy(&ValueSize, Data + Pos, 4);
			Pos += 4;
			break;
		}
		if (ValueSize < 0)
			return false;

		if (Type != 3 && IsArray) // BoolProperty uses the array bit as its value.
		{
			if (Pos >= BodyEnd)
				return false;
			byte Index = Data[Pos++];
			if (Index >= 128)
				Pos += (Index & 0x40) ? 3 : 1;
		}
		if (Pos > BodyEnd - ValueSize)
			return false;
		Pos += ValueSize;
	}
	if ((RequireProperties && PropertyCount <= 0) || Pos >= BodyEnd)
		return false;
	NativeStart = Pos;
	return true;
}

static bool ValidateScdaLinGenericExportBody(const byte *Data, int DataSize, int BodyStart, int BodySize,
	const TArray<FString>& Names)
{
	if (BodyStart < 0 || BodySize <= 0 || BodyStart > DataSize - BodySize)
		return false;
	int NativeStart;
	return ValidateScdaLinTaggedProperties(Data, BodyStart + BodySize, BodyStart, Names, NativeStart, false);
}

static bool ValidateScdaLinTaggedExportBody(const byte *Data, int DataSize, int BodyStart, int BodySize,
	const TArray<FString>& Names)
{
	if (BodyStart < 0 || BodySize <= 0 || BodyStart > DataSize - BodySize)
		return false;
	int NativeStart;
	if (!ValidateScdaLinTaggedProperties(Data, BodyStart + BodySize, BodyStart, Names, NativeStart, true))
		return false;

	// A tagged UObject body alone is not enough to identify a seekless texture
	// export; random inner property blocks can validate and leave a huge native
	// tail. Require either the normal UE2 texture mip layout or a tiny native
	// tail which belongs to non-texture helper exports.
	if (ValidateScdaLinTextureBody(Data, DataSize, BodyStart, BodySize, Names))
		return true;
	return BodyStart + BodySize - NativeStart <= 0x100;
}

static bool ValidateScdaLinTextureTaggedExportBody(const byte *Data, int DataSize, int BodyStart, int BodySize,
	const TArray<FString>& Names)
{
	if (BodyStart < 0 || BodySize <= 0 || BodyStart > DataSize - BodySize)
		return false;

	int Pos = BodyStart;
	int BodyEnd = BodyStart + BodySize;
	bool HasFormat = false;
	bool HasUSize = false;
	bool HasVSize = false;
	int PropertyCount = 0;

	while (Pos < BodyEnd)
	{
		int NameIndex;
		if (!ReadLinCompactIndex(Data, BodyEnd, Pos, NameIndex) || NameIndex < 0 || NameIndex >= Names.Num())
			return false;
		if (!stricmp(*Names[NameIndex], "None"))
			break;
		PropertyCount++;
		if (Pos >= BodyEnd)
			return false;
		byte Info = Data[Pos++];
		int Type = Info & 0xF;
		bool IsArray = (Info & 0x80) != 0;
		if (Type <= 0 || Type > 15)
			return false;

		const FString& PropName = Names[NameIndex];
		if (!stricmp(*PropName, "Format"))
		{
			if (Type != 1)
				return false;
			HasFormat = true;
		}
		else if (!stricmp(*PropName, "UBits") || !stricmp(*PropName, "VBits"))
		{
			if (Type != 1)
				return false;
		}
		else if (!stricmp(*PropName, "USize"))
		{
			if (Type != 2)
				return false;
			HasUSize = true;
		}
		else if (!stricmp(*PropName, "VSize"))
		{
			if (Type != 2)
				return false;
			HasVSize = true;
		}
		else if (!stricmp(*PropName, "UClamp") || !stricmp(*PropName, "VClamp"))
		{
			if (Type != 2)
				return false;
		}
		if (Type == 10)
		{
			int StructName;
			if (!ReadLinCompactIndex(Data, BodyEnd, Pos, StructName) || StructName < 0 || StructName >= Names.Num())
				return false;
		}

		int ValueSize;
		switch ((Info >> 4) & 7)
		{
		case 0: ValueSize = 1; break;
		case 1: ValueSize = 2; break;
		case 2: ValueSize = 4; break;
		case 3: ValueSize = 12; break;
		case 4: ValueSize = 16; break;
		case 5:
			if (Pos + 1 > BodyEnd) return false;
			ValueSize = Data[Pos++];
			break;
		case 6:
			if (Pos + 2 > BodyEnd) return false;
			{
				uint16 Size16;
				memcpy(&Size16, Data + Pos, 2);
				ValueSize = Size16;
				Pos += 2;
			}
			break;
		default:
			if (Pos + 4 > BodyEnd) return false;
			memcpy(&ValueSize, Data + Pos, 4);
			Pos += 4;
			break;
		}
		if (ValueSize < 0)
			return false;
		if (Type != 3 && IsArray)
		{
			if (Pos >= BodyEnd)
				return false;
			byte Index = Data[Pos++];
			if (Index >= 128)
				Pos += (Index & 0x40) ? 3 : 1;
		}
		if (Pos > BodyEnd - ValueSize)
			return false;
		Pos += ValueSize;
	}

	if (PropertyCount <= 0 || !HasFormat || !HasUSize || !HasVSize || Pos >= BodyEnd)
		return false;
	if (ValidateScdaLinTextureBody(Data, DataSize, BodyStart, BodySize, Names))
		return true;
	return BodyEnd - Pos <= 0x100;
}

static bool GetScdaLinTextureBodyOriginalOffset(const byte *Data, int DataSize, int BodyStart, int BodySize,
	const TArray<FString>& Names, int& OriginalOffset)
{
	OriginalOffset = -1;
	if (BodyStart < 0 || BodySize <= 0 || BodyStart > DataSize - BodySize)
		return false;
	int NativeStart;
	if (!ValidateScdaLinTaggedProperties(Data, BodyStart + BodySize, BodyStart, Names, NativeStart, false))
		return false;
	int Pos = NativeStart;
	int BodyEnd = BodyStart + BodySize;
	int MipCount;
	if (!ReadLinCompactIndex(Data, BodyEnd, Pos, MipCount) || MipCount <= 0 || MipCount > 32)
		return false;
	if (Pos + 4 > BodyEnd)
		return false;
	int SeekPos;
	memcpy(&SeekPos, Data + Pos, 4);
	Pos += 4;
	if (SeekPos < 0 || SeekPos > 0x7FFFFFFF - 7)
		return false;
	int DataLength;
	if (!ReadLinCompactIndex(Data, BodyEnd, Pos, DataLength) || DataLength < 0 || Pos > BodyEnd - DataLength - 7)
		return false;
	Pos += DataLength;
	uint16 USize, VSize;
	memcpy(&USize, Data + Pos, 2);
	memcpy(&VSize, Data + Pos + 2, 2);
	byte Unknown = Data[Pos + 4];
	byte UBits = Data[Pos + 5];
	byte VBits = Data[Pos + 6];
	if (Unknown != 0 || !IsPowerOfTwo(USize) || !IsPowerOfTwo(VSize) ||
		UBits != GetPowerOfTwoBits(USize) || VBits != GetPowerOfTwoBits(VSize))
		return false;
	OriginalOffset = SeekPos + 7;
	return true;
}

static bool ShouldMapScdaLinTextureBodies()
{
	const char *TextureBodyFallback = getenv("SCDA_LIN_TEXTURE_BODY_FALLBACK");
	return TextureBodyFallback && stricmp(TextureBodyFallback, "0") != 0;
}

static void MapScdaLinTextureExportsByBody(FScdaLinPackage& Package, const byte *Data, int DataSize,
	const char *SourceFilename, bool Force = false)
{
	if (!Force && !ShouldMapScdaLinTextureBodies())
		return;
	if (!IsScdaLinTexturePackage(Package.Filename))
		return;
	if (!SourceFilename || !stricmp(SourceFilename, "common.lin"))
		return;

	TArray<int> Sizes;
	for (const FScdaLinExportRange& Export : Package.Exports)
	{
		if (Export.SerialSize < 0x100 || Export.SerialSize > DataSize ||
			IsScdaLinRangeMapped(Package, Export.SerialOffset, Export.SerialSize))
			continue;
		bool Seen = false;
		for (int Size : Sizes)
		{
			if (Size == Export.SerialSize)
			{
				Seen = true;
				break;
			}
		}
		if (!Seen)
			Sizes.Add(Export.SerialSize);
	}
	if (!Sizes.Num())
		return;

	struct FBodyCandidate
	{
		int Size;
		int Start;
		int OriginalOffset;
		bool Used;
	};
	TArray<FBodyCandidate> BodyCandidates;
	for (int Size : Sizes)
	{
		for (int Start = 0; Start <= DataSize - Size; Start++)
		{
			int OriginalOffset = -1;
			bool HasOriginalOffset = GetScdaLinTextureBodyOriginalOffset(Data, DataSize, Start, Size,
				Package.Names, OriginalOffset);
			if (!ValidateScdaLinTextureBody(Data, DataSize, Start, Size, Package.Names) &&
				!ValidateScdaLinTextureTaggedExportBody(Data, DataSize, Start, Size, Package.Names) &&
				!HasOriginalOffset)
				continue;
			FBodyCandidate& Candidate = BodyCandidates[BodyCandidates.AddDefaulted()];
			Candidate.Size = Size;
			Candidate.Start = Start;
			Candidate.OriginalOffset = HasOriginalOffset ? OriginalOffset : -1;
			Candidate.Used = false;
		}
	}
	if (!BodyCandidates.Num())
		return;

	int Added = 0;
	for (const FScdaLinExportRange& Export : Package.Exports)
	{
		if (Export.SerialSize <= 0 || IsScdaLinRangeMapped(Package, Export.SerialOffset, Export.SerialSize))
			continue;
		FBodyCandidate *BestCandidate = NULL;
		for (FBodyCandidate& Candidate : BodyCandidates)
		{
			if (Candidate.Used || Candidate.Size != Export.SerialSize)
				continue;
			if (Candidate.OriginalOffset != Export.SerialOffset)
				continue;
			if (!BestCandidate || Candidate.Start < BestCandidate->Start)
				BestCandidate = &Candidate;
		}
		if (!BestCandidate)
		{
			bool HasKeyedCandidateForSize = false;
			for (FBodyCandidate& Candidate : BodyCandidates)
			{
				if (!Candidate.Used && Candidate.Size == Export.SerialSize && Candidate.OriginalOffset >= 0)
				{
					HasKeyedCandidateForSize = true;
					break;
				}
			}
			if (HasKeyedCandidateForSize)
				continue;
			for (FBodyCandidate& Candidate : BodyCandidates)
			{
				if (Candidate.Used || Candidate.Size != Export.SerialSize || Candidate.OriginalOffset >= 0)
					continue;
				if (!BestCandidate || Candidate.Start < BestCandidate->Start)
					BestCandidate = &Candidate;
			}
		}
		if (!BestCandidate)
			continue;
		AddScdaLinSegment(Package, Export.SerialOffset, Data + BestCandidate->Start, Export.SerialSize,
			SourceFilename, BestCandidate->Start);
		BestCandidate->Used = true;
		Added++;
		if (getenv("SCDA_LIN_DEBUG"))
			appPrintf("    LIN texture body fallback: %s %s %X+%X <- %X\n",
				*Package.Filename, *Export.ObjectName, Export.SerialOffset, Export.SerialSize, BestCandidate->Start);
	}
	if (Added && getenv("SCDA_LIN_DEBUG"))
		appPrintf("    LIN texture body fallback mapped %d exports for %s\n", Added, *Package.Filename);
}

static bool IsScdaLinSeeklessNativeExport(const FScdaLinExportRange& Export)
{
	return !stricmp(*Export.ClassName, "SkeletalMesh") || !stricmp(*Export.ClassName, "MeshAnimation") ||
		!stricmp(*Export.ClassName, "StaticMesh");
}

static bool ValidateScdaLinNativeLazyArrays(const byte *Data, int DataSize, int BodyStart,
	const FScdaLinExportRange& Export)
{
	int BodyEnd = BodyStart + Export.SerialSize;
	int Hits = 0;
	for (int Pos = BodyStart; Pos + 8 <= BodyEnd; Pos++)
	{
		int SkipOffset;
		memcpy(&SkipOffset, Data + Pos, 4);
		int OriginalField = Export.SerialOffset + (Pos - BodyStart);
		if (SkipOffset <= OriginalField + 4 || SkipOffset > Export.SerialOffset + Export.SerialSize)
			continue;

		int CountPos = Pos + 4;
		int Count;
		if (!ReadLinCompactIndex(Data, BodyEnd, CountPos, Count) || Count <= 0 || Count > 100000)
			continue;

		int OriginalDataPos = OriginalField + (CountPos - Pos);
		int DataBytes = SkipOffset - OriginalDataPos;
		int PhysicalDataBytes = DataBytes;
		if (PhysicalDataBytes <= 0 || CountPos + PhysicalDataBytes > BodyEnd || PhysicalDataBytes % Count)
			continue;
		int Stride = PhysicalDataBytes / Count;
		if (Stride != 2 && Stride != 4 && Stride != 8 && Stride != 10 && Stride != 12 &&
			Stride != 16 && Stride != 20 && Stride != 24 && Stride != 28 && Stride != 32 &&
			Stride != 36 && Stride != 40 && Stride != 44 && Stride != 48 && Stride != 56)
			continue;
		if (++Hits >= 1)
			return true;
	}
	return false;
}

struct FScdaLinNativeBodyGroup
{
	int ExportIndex;
	int BodyStart;
	int Hits;
};

struct FScdaLinNativeBodyHit
{
	int ExportIndex;
	int BodyStart;
};

static int CompareScdaLinNativeBodyHits(const FScdaLinNativeBodyHit& A, const FScdaLinNativeBodyHit& B)
{
	if (A.ExportIndex != B.ExportIndex)
		return A.ExportIndex - B.ExportIndex;
	return A.BodyStart - B.BodyStart;
}

static void FindBestScdaLinNativeBodyGroup(const TArray<FScdaLinNativeBodyHit>& Hits, int ExportIndex,
	FScdaLinNativeBodyGroup& Best, FScdaLinNativeBodyGroup& Second)
{
	Best.ExportIndex = Second.ExportIndex = ExportIndex;
	Best.BodyStart = Second.BodyStart = 0;
	Best.Hits = Second.Hits = 0;
	for (int i = 0; i < Hits.Num(); )
	{
		const FScdaLinNativeBodyHit& Hit = Hits[i];
		if (Hit.ExportIndex < ExportIndex)
		{
			i++;
			continue;
		}
		if (Hit.ExportIndex > ExportIndex)
			return;
		int BodyStart = Hit.BodyStart;
		int Count = 0;
		while (i < Hits.Num() && Hits[i].ExportIndex == ExportIndex && Hits[i].BodyStart == BodyStart)
		{
			Count++;
			i++;
		}
		if (Count > Best.Hits)
		{
			Second = Best;
			Best.BodyStart = BodyStart;
			Best.Hits = Count;
		}
		else if (Count > Second.Hits)
		{
			Second.BodyStart = BodyStart;
			Second.Hits = Count;
		}
	}
}

static void MapScdaLinNativeExportsBySkipFields(FScdaLinPackage& Package, const byte *Data, int DataSize,
	const char *SourceFilename)
{
	if (!IsScdaLinGenericExportPackage(Package.Filename))
		return;

	const char *DebugObject = getenv("SCDA_LIN_DEBUG_NATIVE_OBJECT");
	const bool bGroupMatch = getenv("SCDA_LIN_NATIVE_GROUP_MATCH") && stricmp(getenv("SCDA_LIN_NATIVE_GROUP_MATCH"), "0");
	TArray<FScdaLinNativeBodyHit> BodyHitsForGroup;
	TArray<int> DebugSkipHits, DebugBodyHits, DebugFieldHits, DebugValidHits;
	if (DebugObject && DebugObject[0])
	{
		DebugSkipHits.AddZeroed(Package.Exports.Num());
		DebugBodyHits.AddZeroed(Package.Exports.Num());
		DebugFieldHits.AddZeroed(Package.Exports.Num());
		DebugValidHits.AddZeroed(Package.Exports.Num());
	}

	for (int Pos = 0; Pos + 4 <= DataSize; Pos++)
	{
		int SkipOffset;
		memcpy(&SkipOffset, Data + Pos, 4);
		if (SkipOffset <= 0)
			continue;

		int Low = 0;
		int High = Package.Exports.Num() - 1;
		int ExportIndex = -1;
		while (Low <= High)
		{
			int Mid = (Low + High) / 2;
			if (Package.Exports[Mid].SerialOffset <= SkipOffset)
			{
				ExportIndex = Mid;
				Low = Mid + 1;
			}
			else
			{
				High = Mid - 1;
			}
		}
		if (ExportIndex < 0)
			continue;

		const FScdaLinExportRange& Export = Package.Exports[ExportIndex];
		if (Export.SerialSize <= 0 || !IsScdaLinSeeklessNativeExport(Export) ||
			IsScdaLinRangeMapped(Package, Export.SerialOffset, Export.SerialSize))
			continue;
		if (SkipOffset <= Export.SerialOffset || SkipOffset > Export.SerialOffset + Export.SerialSize)
			continue;
		if (DebugSkipHits.Num())
			DebugSkipHits[ExportIndex]++;

		int BodyStart = Pos - (SkipOffset - Export.SerialOffset);
		if (BodyStart < 0 || BodyStart > DataSize - Export.SerialSize)
			continue;
		if (DebugBodyHits.Num())
			DebugBodyHits[ExportIndex]++;
		if (bGroupMatch && (!DebugObject || !DebugObject[0] || !stricmp(*Export.ObjectName, DebugObject)))
		{
			FScdaLinNativeBodyHit& Hit = BodyHitsForGroup[BodyHitsForGroup.AddDefaulted()];
			Hit.ExportIndex = ExportIndex;
			Hit.BodyStart = BodyStart;
		}
		int OriginalField = Export.SerialOffset + (Pos - BodyStart);
		if (SkipOffset <= OriginalField + 4)
			continue;
		if (DebugFieldHits.Num())
			DebugFieldHits[ExportIndex]++;
		if (!ValidateScdaLinNativeLazyArrays(Data, DataSize, BodyStart, Export))
			continue;
		if (DebugValidHits.Num())
			DebugValidHits[ExportIndex]++;
		AddScdaLinSegment(Package, Export.SerialOffset, Data + BodyStart, Export.SerialSize,
			SourceFilename, BodyStart);
		if (getenv("SCDA_LIN_DEBUG") || getenv("SCDA_LIN_DEBUG_NATIVE"))
			appPrintf("    LIN native payload: %s %s'%s' %X+%X <- %X skip=%X\n",
				*Package.Filename, *Export.ClassName, *Export.ObjectName,
				Export.SerialOffset, Export.SerialSize, BodyStart, SkipOffset);
	}

	if (bGroupMatch)
	{
		BodyHitsForGroup.Sort(CompareScdaLinNativeBodyHits);
		for (int ExportIndex = 0; ExportIndex < Package.Exports.Num(); ExportIndex++)
		{
			const FScdaLinExportRange& Export = Package.Exports[ExportIndex];
			if (DebugObject && DebugObject[0] && stricmp(*Export.ObjectName, DebugObject))
				continue;
			if (Export.SerialSize <= 0 || !IsScdaLinSeeklessNativeExport(Export) ||
				IsScdaLinRangeMapped(Package, Export.SerialOffset, Export.SerialSize))
				continue;

			FScdaLinNativeBodyGroup Best, Second;
			FindBestScdaLinNativeBodyGroup(BodyHitsForGroup, ExportIndex, Best, Second);
			if (Best.Hits < 3)
				continue;
			if (Second.Hits && Best.Hits < Second.Hits * 2)
				continue;
			if (Best.BodyStart < 0 || Best.BodyStart > DataSize - Export.SerialSize)
				continue;
			AddScdaLinSegment(Package, Export.SerialOffset, Data + Best.BodyStart, Export.SerialSize,
				SourceFilename, Best.BodyStart);
			if (getenv("SCDA_LIN_DEBUG") || getenv("SCDA_LIN_DEBUG_NATIVE") ||
				(DebugObject && !stricmp(*Export.ObjectName, DebugObject)))
				appPrintf("    LIN native grouped payload: %s %s'%s' %X+%X <- %X hits=%d second=%d\n",
					*Package.Filename, *Export.ClassName, *Export.ObjectName,
					Export.SerialOffset, Export.SerialSize, Best.BodyStart, Best.Hits,
					Second.Hits);
		}
	}

	if (DebugObject && DebugObject[0])
	{
		for (int i = 0; i < Package.Exports.Num(); i++)
		{
			const FScdaLinExportRange& Export = Package.Exports[i];
			if (stricmp(*Export.ObjectName, DebugObject))
				continue;
			FScdaLinNativeBodyGroup Best, Second;
			if (bGroupMatch)
			{
				BodyHitsForGroup.Sort(CompareScdaLinNativeBodyHits);
				FindBestScdaLinNativeBodyGroup(BodyHitsForGroup, i, Best, Second);
			}
			else
			{
				Best.ExportIndex = Second.ExportIndex = i;
				Best.BodyStart = Second.BodyStart = 0;
				Best.Hits = Second.Hits = 0;
			}
			appPrintf("    LIN native debug: %s %s'%s' %X+%X mapped=%d skipHits=%d bodyHits=%d fieldHits=%d validHits=%d best=%X/%d second=%X/%d\n",
				*Package.Filename, *Export.ClassName, *Export.ObjectName,
				Export.SerialOffset, Export.SerialSize,
				IsScdaLinRangeMapped(Package, Export.SerialOffset, Export.SerialSize) ? 1 : 0,
				DebugSkipHits.Num() ? DebugSkipHits[i] : 0,
				DebugBodyHits.Num() ? DebugBodyHits[i] : 0,
				DebugFieldHits.Num() ? DebugFieldHits[i] : 0,
				DebugValidHits.Num() ? DebugValidHits[i] : 0,
				Best.BodyStart, Best.Hits,
				Second.BodyStart, Second.Hits);
		}
	}
}

#if THREADING
static CMutex GScdaLinSegmentMutex;
#endif

static void AddScdaLinSegment(FScdaLinPackage& Package, int VirtualOffset, const byte *Data, int Size,
	const char *SourceFilename, int SourceLogicalOffset)
{
#if THREADING
	CMutex::ScopedLock Lock(GScdaLinSegmentMutex);
#endif
	if (VirtualOffset < 0 || Size <= 0 || VirtualOffset > Package.OriginalSize - Size)
		return;
	for (const FScdaLinPackage::FSegment& Segment : Package.Segments)
		if (Segment.VirtualOffset == VirtualOffset)
			return;

	FScdaLinPackage::FSegment& Segment = Package.Segments[Package.Segments.AddDefaulted()];
	Segment.VirtualOffset = VirtualOffset;
	Segment.DataOffset = Package.Data.Num();
	Segment.Size = Size;
	if (SourceFilename)
		Segment.SourceFilename = SourceFilename;
	Segment.SourceLogicalOffset = SourceLogicalOffset;
	Package.Data.AddUninitialized(Size);
	memcpy(Package.Data.GetData() + Segment.DataOffset, Data, Size);
}

static void AddScdaLinZeroSegment(FScdaLinPackage& Package, int VirtualOffset, int Size)
{
#if THREADING
	CMutex::ScopedLock Lock(GScdaLinSegmentMutex);
#endif
	if (VirtualOffset < 0 || Size <= 0 || VirtualOffset > Package.OriginalSize - Size)
		return;
	for (const FScdaLinPackage::FSegment& Segment : Package.Segments)
		if (Segment.VirtualOffset == VirtualOffset)
			return;

	FScdaLinPackage::FSegment& Segment = Package.Segments[Package.Segments.AddDefaulted()];
	Segment.VirtualOffset = VirtualOffset;
	Segment.DataOffset = Package.Data.Num();
	Segment.Size = Size;
	Segment.SourceLogicalOffset = 0;
	Package.Data.AddZeroed(Size);
}

static int WriteScdaLinCompactIndex(byte *Data, int DataSize, int Value)
{
	if (DataSize <= 0 || Value < 0)
		return 0;
	int Pos = 0;
	byte B = Value & 0x3F;
	Value >>= 6;
	if (Value)
		B |= 0x40;
	Data[Pos++] = B;
	while (Value && Pos < DataSize)
	{
		B = Value & 0x7F;
		Value >>= 7;
		if (Value)
			B |= 0x80;
		Data[Pos++] = B;
	}
	return Value ? 0 : Pos;
}

static bool AddScdaLinTextureStubSegment(FScdaLinPackage& Package, const FScdaLinExportRange& Export)
{
	int NoneIndex = -1;
	for (int Index = 0; Index < Package.Names.Num(); Index++)
	{
		if (!stricmp(*Package.Names[Index], "None"))
		{
			NoneIndex = Index;
			break;
		}
	}
	if (NoneIndex < 0 || Export.SerialSize < 8)
		return false;

	TArray<byte> Stub;
	Stub.AddZeroed(Export.SerialSize);
	int Pos = WriteScdaLinCompactIndex(Stub.GetData(), Stub.Num(), NoneIndex);
	if (Pos <= 0 || Pos + 4 > Stub.Num())
		return false;
	AddScdaLinSegment(Package, Export.SerialOffset, Stub.GetData(), Stub.Num());
	return true;
}

static bool AddScdaLinPaletteStubSegment(FScdaLinPackage& Package, const FScdaLinExportRange& Export)
{
	int NoneIndex = -1;
	for (int Index = 0; Index < Package.Names.Num(); Index++)
	{
		if (!stricmp(*Package.Names[Index], "None"))
		{
			NoneIndex = Index;
			break;
		}
	}
	if (NoneIndex < 0 || Export.SerialSize < 8)
		return false;

	TArray<byte> Stub;
	Stub.AddZeroed(Export.SerialSize);
	int Pos = WriteScdaLinCompactIndex(Stub.GetData(), Stub.Num(), NoneIndex);
	if (Pos <= 0)
		return false;
	int CountSize = WriteScdaLinCompactIndex(Stub.GetData() + Pos, Stub.Num() - Pos, 256);
	if (CountSize <= 0)
		return false;
	Pos += CountSize;
	if (Pos + 256 * 4 > Stub.Num())
		return false;
	for (int i = 0; i < 256; i++)
	{
		Stub[Pos + i * 4 + 0] = byte(i);
		Stub[Pos + i * 4 + 1] = byte(i);
		Stub[Pos + i * 4 + 2] = byte(i);
		Stub[Pos + i * 4 + 3] = 255;
	}
	Stub[Pos + 3] = 0;
	AddScdaLinSegment(Package, Export.SerialOffset, Stub.GetData(), Stub.Num());
	return true;
}

static void AddScdaLinSegmentRef(FScdaLinPackage& Package, int VirtualOffset, int Size,
	const char *SourceFilename, int SourceLogicalOffset)
{
	if (VirtualOffset < 0 || Size <= 0 || VirtualOffset > Package.OriginalSize - Size)
		return;
	for (const FScdaLinPackage::FSegment& Segment : Package.Segments)
		if (Segment.VirtualOffset == VirtualOffset)
			return;

	FScdaLinPackage::FSegment& Segment = Package.Segments[Package.Segments.AddDefaulted()];
	Segment.VirtualOffset = VirtualOffset;
	Segment.Size = Size;
	Segment.SourceFilename = SourceFilename;
	Segment.SourceLogicalOffset = SourceLogicalOffset;
}

struct FScdaLinFloatMeshCandidate
{
	int BodyStart;
	int FloatStart;
	int Count;
	float Extent;
	bool Used;
};

static bool IsScdaLinSaneFloat(float Value)
{
	return Value == Value && Value > -10000.0f && Value < 10000.0f;
}

static void GetScdaLinLevelToken(const FString& LevelBase, FString& Token)
{
	Token.Empty();
	const char *Name = *LevelBase;
	const char *Start = strchr(Name, '_');
	if (Start)
	{
		Start++;
		const char *End = strchr(Start, '_');
		if (End && End > Start)
			Token = FString(End - Start, Start);
		else
			Token = Start;
	}
	if (Token.IsEmpty())
		Token = LevelBase;
}

static void FindScdaLinFloatMeshCandidates(const byte *Data, int DataSize, TArray<FScdaLinFloatMeshCandidate>& Candidates)
{
	for (int Pos = 0; Pos <= DataSize - 12; Pos += 4)
	{
		int Count = 0;
		float MinX =  3.4e38f, MinY =  3.4e38f, MinZ =  3.4e38f;
		float MaxX = -3.4e38f, MaxY = -3.4e38f, MaxZ = -3.4e38f;
		for (int P = Pos; P <= DataSize - 12; P += 12)
		{
			float X, Y, Z;
			memcpy(&X, Data + P, 4);
			memcpy(&Y, Data + P + 4, 4);
			memcpy(&Z, Data + P + 8, 4);
			if (!IsScdaLinSaneFloat(X) || !IsScdaLinSaneFloat(Y) || !IsScdaLinSaneFloat(Z))
				break;
			if (fabs(X) < 0.000001f && fabs(Y) < 0.000001f && fabs(Z) < 0.000001f)
				break;
			MinX = min(MinX, X); MaxX = max(MaxX, X);
			MinY = min(MinY, Y); MaxY = max(MaxY, Y);
			MinZ = min(MinZ, Z); MaxZ = max(MaxZ, Z);
			Count++;
			if (Count > 10000)
				break;
		}
		if (Count < 1000 || Count > 8000)
			continue;
		float Extent = (MaxX - MinX) + (MaxY - MinY) + (MaxZ - MinZ);
		if (Extent < 100.0f || Extent > 20000.0f)
			continue;

		FScdaLinFloatMeshCandidate& Candidate = Candidates[Candidates.AddDefaulted()];
		Candidate.FloatStart = Pos;
		Candidate.BodyStart = max(0, Pos - 0x80);
		Candidate.Count = Count;
		Candidate.Extent = Extent;
		Candidate.Used = false;
		Pos += Count * 12 - 4;
	}
}

static void MapScdaLinLevelFloatMeshPayloads(FScdaLinPackage& Package, const byte *Data, int DataSize,
	const FString& LevelBase, const char *SourceFilename)
{
	const char *EnableFallback = getenv("SCDA_LIN_FLOAT_MESH_FALLBACK");
	if (!EnableFallback || !stricmp(EnableFallback, "0"))
		return;

	FString Token;
	GetScdaLinLevelToken(LevelBase, Token);
	if (Token.IsEmpty())
		return;

	TArray<FScdaLinFloatMeshCandidate> Candidates;
	FindScdaLinFloatMeshCandidates(Data, DataSize, Candidates);
	if (!Candidates.Num())
		return;

	for (const FScdaLinExportRange& Export : Package.Exports)
	{
		if (Export.SerialSize <= 0 || stricmp(*Export.ClassName, "SkeletalMesh"))
			continue;
		if (IsScdaLinRangeMapped(Package, Export.SerialOffset, Export.SerialSize))
			continue;
		if (!ScdaLinContainsNoCase(*Export.ObjectName, *Token))
			continue;

		FScdaLinFloatMeshCandidate *Best = NULL;
		for (FScdaLinFloatMeshCandidate& Candidate : Candidates)
		{
			if (Candidate.Used || Candidate.BodyStart > DataSize - Export.SerialSize)
				continue;
			if (Export.SerialSize < Candidate.Count * 12 + 0x1000)
				continue;
			if (!Best || Candidate.Count > Best->Count)
				Best = &Candidate;
		}
		if (!Best)
			continue;
		AddScdaLinSegment(Package, Export.SerialOffset, Data + Best->BodyStart, Export.SerialSize,
			SourceFilename, Best->BodyStart);
		Best->Used = true;
		if (getenv("SCDA_LIN_DEBUG"))
			appPrintf("    LIN float mesh payload: %s %s'%s' %X+%X <- %X verts=%d\n",
				*Package.Filename, *Export.ClassName, *Export.ObjectName,
				Export.SerialOffset, Export.SerialSize, Best->BodyStart, Best->Count);
	}
}

static void InitScdaLinPackageFromHeader(FScdaLinPackage& Package,
	const FString& Filename, int OriginalSize, int PhysicalBase, const FScdaLinHeader& Header, const byte *Data, int DataSize,
	bool DirectMapNativeExports = true, const char *NativeSourceFilename = NULL)
{
	Package.Filename.Empty();
	Package.NativeSourceFilename.Empty();
	Package.Data.Empty();
	Package.Segments.Empty();
	Package.Names.Empty();
	Package.Exports.Empty();
	Package.OriginalSize = 0;
	Package.ImportCount = 0;
	Package.PayloadStreamsMapped = false;
	Package.Filename = Filename;
	if (NativeSourceFilename)
		Package.NativeSourceFilename = NativeSourceFilename;
	int NameTableEnd = Header.NameOffset + (Header.NameEnd - Header.Offset - Header.NameOffset);
	int ImportTableEnd = Header.ImportOffset + (Header.ImportEnd - Header.NameEnd);
	int ExportTableEnd = Header.ExportOffset + (Header.ExportEnd - Header.ImportEnd);
	Package.OriginalSize = max(max(max(OriginalSize, Header.MaxExportEnd), NameTableEnd), max(ImportTableEnd, ExportTableEnd));
	Package.ImportCount = Header.ImportCount;
	Package.Names.Reserve(Header.Names.Num());
	for (const FString& Name : Header.Names)
		Package.Names.Add(Name);
	Package.Exports.Empty(Header.Exports.Num());
	for (const FScdaLinExportRange& Export : Header.Exports)
		Package.Exports.Add(Export);
	AddScdaLinSegment(Package, 0, Data + Header.Offset, Header.NameOffset);
	AddScdaLinSegment(Package, Header.NameOffset, Data + Header.Offset + Header.NameOffset,
		Header.NameEnd - Header.Offset - Header.NameOffset);
	AddScdaLinSegment(Package, Header.ImportOffset, Data + Header.NameEnd,
		Header.ImportEnd - Header.NameEnd);
	AddScdaLinSegment(Package, Header.ExportOffset, Data + Header.ImportEnd,
		Header.ExportEnd - Header.ImportEnd);
	for (const FScdaLinExportRange& Export : Header.Exports)
	{
		if (Export.SerialSize <= 0)
			continue;
		if (IsScdaLinTexturePackage(Package.Filename) && IsScdaLinTextureExportClass(Export.ClassName))
		{
			if (HasScdaLinTextureStream(*Export.ObjectName, *Package.Filename))
				AddScdaLinTextureStubSegment(Package, Export);
			continue;
		}
		if (IsScdaLinTexturePackage(Package.Filename) && IsScdaLinPaletteExportClass(Export.ClassName))
		{
			AddScdaLinPaletteStubSegment(Package, Export);
			continue;
		}
		if (!DirectMapNativeExports && IsScdaLinSeeklessNativeExport(Export))
			continue;
		int PhysicalOffset = PhysicalBase + Export.SerialOffset;
		if (PhysicalOffset < 0 || PhysicalOffset > 0x7FFFFFFF - Export.SerialSize ||
			PhysicalOffset > DataSize - Export.SerialSize)
			continue;
		AddScdaLinSegment(Package, Export.SerialOffset, Data + PhysicalOffset, Export.SerialSize);
	}
}

static FScdaLinPackage& AddScdaLinPackageFromHeader(TArray<FScdaLinPackage>& Packages,
	const FString& Filename, int OriginalSize, int PhysicalBase, const FScdaLinHeader& Header, const byte *Data, int DataSize,
	bool DirectMapNativeExports = true, const char *NativeSourceFilename = NULL)
{
	FScdaLinPackage& Package = *new (Packages) FScdaLinPackage;
	InitScdaLinPackageFromHeader(Package, Filename, OriginalSize, PhysicalBase, Header, Data, DataSize,
		DirectMapNativeExports, NativeSourceFilename);
	return Package;
}

static void MapScdaLinPayloads(FScdaLinPackage& Package, const byte *Data, int DataSize,
	const char *SourceFilename = NULL)
{
	TArray<FScdaLinMipCandidate> MipCandidates;
	FindScdaLinMipCandidates(Data, DataSize, MipCandidates);
	if (getenv("SCDA_LIN_DEBUG"))
		appPrintf("    LIN mip candidates: %d\n", MipCandidates.Num());
	FindScdaLinTextureStreams(Data, DataSize);
	MapScdaLinNativeExportsBySkipFields(Package, Data, DataSize, SourceFilename);
	for (const FScdaLinMipCandidate& Candidate : MipCandidates)
	{
		int Low = 0;
		int High = Package.Exports.Num() - 1;
		int ExportIndex = -1;
		while (Low <= High)
		{
			int Mid = (Low + High) / 2;
			if (Package.Exports[Mid].SerialOffset <= Candidate.OriginalSkipField)
			{
				ExportIndex = Mid;
				Low = Mid + 1;
			}
			else
			{
				High = Mid - 1;
			}
		}
		if (ExportIndex < 0)
			continue;
		const FScdaLinExportRange& Export = Package.Exports[ExportIndex];
		if (Export.SerialSize <= 0 || Candidate.OriginalSkipField >= Export.SerialOffset + Export.SerialSize)
			continue;
		int RelativeSkipField = Candidate.OriginalSkipField - Export.SerialOffset;
		int LogicalBodyStart = Candidate.LogicalSkipField - RelativeSkipField;
		if (LogicalBodyStart < 0 || LogicalBodyStart > DataSize - Export.SerialSize)
			continue;
		const char *GenericExports = getenv("SCDA_LIN_REBUILD_EXPORTS");
		bool ValidTextureBody = ValidateScdaLinTextureBody(Data, DataSize, LogicalBodyStart, Export.SerialSize, Package.Names);
		bool ValidGenericBody = false;
		if (!ValidTextureBody && GenericExports && stricmp(GenericExports, "0") &&
			IsScdaLinGenericExportPackage(Package.Filename))
			ValidGenericBody = ValidateScdaLinGenericExportBody(Data, DataSize, LogicalBodyStart, Export.SerialSize, Package.Names);
		if (!ValidTextureBody && !ValidGenericBody)
			continue;
		AddScdaLinSegment(Package, Export.SerialOffset, Data + LogicalBodyStart, Export.SerialSize,
			SourceFilename, LogicalBodyStart);
		if (getenv("SCDA_LIN_DEBUG"))
			appPrintf("    LIN payload: %s %X+%X <- %X (%s)\n",
				*Package.Filename, Export.SerialOffset, Export.SerialSize, LogicalBodyStart,
				ValidTextureBody ? "texture" : "generic");
	}
	MapScdaLinTextureExportsByBody(Package, Data, DataSize, SourceFilename);
}

static void MapScdaLinPayloads(TArray<FScdaLinPackage>& Packages, const byte *Data, int DataSize,
	const char *SourceFilename = NULL)
{
	for (FScdaLinPackage& Package : Packages)
		MapScdaLinPayloads(Package, Data, DataSize, SourceFilename);
}

static bool DecompressScdaLin(FArchive *Reader, TArray<byte>& LogicalData)
{
	while (!Reader->IsEof())
	{
		int RawSize, CompressedSize;
		*Reader << RawSize << CompressedSize;
		if (RawSize <= 0 || CompressedSize <= 0 || CompressedSize > Reader->GetFileSize() - Reader->Tell())
			return false;
		TArray<byte> Compressed;
		Compressed.AddUninitialized(CompressedSize);
		Reader->Serialize(Compressed.GetData(), CompressedSize);
		int Start = LogicalData.Num();
		LogicalData.AddUninitialized(RawSize);
		appDecompress(Compressed.GetData(), CompressedSize, LogicalData.GetData() + Start, RawSize, COMPRESS_ZLIB);
	}
	return true;
}

static bool HasScdaLinPackage(const TArray<FScdaLinPackage>& Packages, const FString& Filename)
{
	for (const FScdaLinPackage& Package : Packages)
		if (!stricmp(*Package.Filename, *Filename))
			return true;
	return false;
}

static void AddScdaLinLevelPackagesFromData(TArray<FScdaLinPackage>& Packages, const byte *Data, int DataSize,
	const FString& LevelBase, const char *SourceFilename)
{
	const bool bSynthesizeTextures = ShouldSynthesizeScdaLinLevelTexturePackages();
	const bool bSynthesizeSkeletal = ShouldSynthesizeScdaLinLevelSkeletalPackages();
	if (!bSynthesizeTextures && !bSynthesizeSkeletal)
		return;

	FindScdaLinTextureStreams(Data, DataSize);

	bool AddedTexturePackage = false;
	int SkelPackageIndex = 0;
	for (int Pos = 0; Pos + 4 <= DataSize; Pos++)
	{
		int Tag;
		memcpy(&Tag, Data + Pos, 4);
		if (Tag != PACKAGE_FILE_TAG)
			continue;
		FScdaLinHeader Header;
		if (!ReadScdaLinHeader(Data, DataSize, Pos, Header))
			continue;
		if (getenv("SCDA_LIN_DEBUG_SKEL_HEADERS") &&
			(LinHeaderHasName(Header, "SkeletalMesh") || LinHeaderHasName(Header, "MeshAnimation") ||
			LinHeaderHasName(Header, "Skeletal") || LinHeaderHasName(Header, "SkelMesh")))
		{
			appPrintf("  LIN mesh-ish header: %s header=%X names=%d imports=%d exports=%d skel=%d anim=%d\n",
				SourceFilename, Pos, Header.Names.Num(), Header.ImportCount, Header.Exports.Num(),
				LinHeaderHasName(Header, "SkeletalMesh") ? 1 : 0,
				LinHeaderHasName(Header, "MeshAnimation") ? 1 : 0);
			for (const FScdaLinExportRange& Export : Header.Exports)
			{
				if (ScdaLinContainsNoCase(*Export.ClassName, "mesh") ||
					ScdaLinContainsNoCase(*Export.ClassName, "anim") ||
					ScdaLinContainsNoCase(*Export.ObjectName, "sam") ||
					ScdaLinContainsNoCase(*Export.ObjectName, "fisher"))
					appPrintf("    probe %s'%s' %X+%X\n",
						*Export.ClassName, *Export.ObjectName, Export.SerialOffset, Export.SerialSize);
			}
		}

		if (bSynthesizeTextures && !AddedTexturePackage && IsScdaLinTextureHeader(Header))
		{
			char PackageName[MAX_PACKAGE_PATH];
			appSprintf(ARRAY_ARG(PackageName), "DataXb/Textures/%s_tex.utx", *LevelBase);
			FString PackageFilename = PackageName;
			if (!HasScdaLinPackage(Packages, PackageFilename))
			{
				FScdaLinPackage& Package = AddScdaLinPackageFromHeader(Packages, PackageFilename,
					Header.MaxExportEnd, Header.Offset, Header, Data, DataSize, true, SourceFilename);
				if (getenv("SCDA_LIN_DEBUG") || getenv("SCDA_LIN_DEBUG_PACKAGES"))
					appPrintf("  LIN synthesized package: %s from %s header=%X names=%d exports=%d\n",
						*Package.Filename, SourceFilename, Pos, Header.Names.Num(), Header.Exports.Num());
			}
			AddedTexturePackage = true;
			continue;
		}

		if (bSynthesizeSkeletal && IsScdaLinSkeletalHeader(Header))
		{
			char PackageName[MAX_PACKAGE_PATH];
			appSprintf(ARRAY_ARG(PackageName), "DataXb/Animations/%s_skel_%02d.ukx", *LevelBase, SkelPackageIndex++);
			FString PackageFilename = PackageName;
			if (HasScdaLinPackage(Packages, PackageFilename))
				continue;
			FScdaLinPackage& Package = AddScdaLinPackageFromHeader(Packages, PackageFilename,
				Header.MaxExportEnd, Header.Offset, Header, Data, DataSize, true, SourceFilename);
			if (getenv("SCDA_LIN_DEBUG") || getenv("SCDA_LIN_DEBUG_PACKAGES"))
			{
				appPrintf("  LIN synthesized skeletal package: %s from %s header=%X names=%d exports=%d\n",
					*Package.Filename, SourceFilename, Pos, Header.Names.Num(), Header.Exports.Num());
				for (const FScdaLinExportRange& Export : Header.Exports)
				{
					if (!stricmp(*Export.ClassName, "SkeletalMesh") || !stricmp(*Export.ClassName, "MeshAnimation"))
						appPrintf("    %s'%s' %X+%X mapped=%d\n",
							*Export.ClassName, *Export.ObjectName, Export.SerialOffset, Export.SerialSize,
							IsScdaLinRangeMapped(Package, Export.SerialOffset, Export.SerialSize) ? 1 : 0);
				}
			}
		}
	}
}

static void AddScdaLinLevelTexturePackages(TArray<FScdaLinPackage>& Packages)
{
	const bool bSynthesizeTextures = ShouldSynthesizeScdaLinLevelTexturePackages();
	const bool bSynthesizeSkeletal = ShouldSynthesizeScdaLinLevelSkeletalPackages();
	if (!bSynthesizeTextures && !bSynthesizeSkeletal)
		return;

	for (const FScdaLinSourceFile& File : GScdaLinPayloadFiles)
	{
		if (!ScdaLinContainsNoCase(*File.RelativeName, ".lin"))
			continue;
		FString LevelBase;
		GetScdaLinLevelBaseName(*File.RelativeName, LevelBase);
		if (LevelBase.IsEmpty())
			continue;

		FFileReader Reader(*File.Filename, EFileArchiveOptions::OpenWarning);
		if (!Reader.IsOpen())
			continue;
		TArray<byte> LogicalData;
		if (!DecompressScdaLin(&Reader, LogicalData))
			continue;
		AddScdaLinLevelPackagesFromData(Packages, LogicalData.GetData(), LogicalData.Num(), LevelBase, *File.RelativeName);
	}
}

struct FScdaLinPayloadMapContext
{
	FScdaLinPackage& Package;

	FScdaLinPayloadMapContext(FScdaLinPackage& InPackage)
	:	Package(InPackage)
	{}
};

static bool IsScdaLinRangeMapped(const FScdaLinPackage& Package, int Offset, int Size)
{
	for (const FScdaLinPackage::FSegment& Segment : Package.Segments)
		if (Segment.DataOffset >= 0 && Offset >= Segment.VirtualOffset && Offset + Size <= Segment.VirtualOffset + Segment.Size)
			return true;
	return false;
}

static bool HydrateScdaLinCachedSegment(FScdaLinPackage& Package, int Offset, int Size)
{
	const FScdaLinPackage::FSegment *Wanted = NULL;
	for (const FScdaLinPackage::FSegment& Segment : Package.Segments)
	{
		if (Segment.DataOffset < 0 && !Segment.SourceFilename.IsEmpty() &&
			Offset >= Segment.VirtualOffset && Offset + Size <= Segment.VirtualOffset + Segment.Size)
		{
			Wanted = &Segment;
			break;
		}
	}
	if (!Wanted)
		return false;

	const CGameFileInfo *File = CGameFileInfo::Find(*Wanted->SourceFilename);
	if (!File)
		return false;
	FArchive *Reader = File->CreateReader(true);
	if (!Reader)
		return false;
	TArray<byte> LogicalData;
	bool Ok = DecompressScdaLin(Reader, LogicalData);
	delete Reader;
	if (!Ok)
		return false;

	for (FScdaLinPackage::FSegment& Segment : Package.Segments)
	{
		if (Segment.DataOffset >= 0 || stricmp(*Segment.SourceFilename, *Wanted->SourceFilename))
			continue;
		if (Segment.SourceLogicalOffset < 0 || Segment.SourceLogicalOffset > LogicalData.Num() - Segment.Size)
			continue;
		Segment.DataOffset = Package.Data.Num();
		Package.Data.AddUninitialized(Segment.Size);
		memcpy(Package.Data.GetData() + Segment.DataOffset,
			LogicalData.GetData() + Segment.SourceLogicalOffset, Segment.Size);
	}
	if (getenv("SCDA_LIN_DEBUG"))
		appPrintf("  LIN cached payload source: %s\n", *Wanted->SourceFilename);
	return IsScdaLinRangeMapped(Package, Offset, Size);
}

static bool HasScdaLinTextureStream(const char *TextureName, const char *PackageFilename)
{
	for (const FScdaLinTextureStreamEntry& Entry : GScdaLinTextureStreams)
		if (!stricmp(*Entry.TextureName, TextureName) && ScdaLinTextureStreamMatchesPackage(Entry, PackageFilename))
			return true;
	return false;
}

static bool TryMapScdaLinStreamTextureStub(FScdaLinPackage& Package, int Offset, int Size)
{
	if (!IsScdaLinTexturePackage(Package.Filename))
		return false;
	for (const FScdaLinExportRange& Export : Package.Exports)
	{
		if (Export.SerialSize <= 0 || Offset < Export.SerialOffset ||
			Offset + Size > Export.SerialOffset + Export.SerialSize)
			continue;
		if (!HasScdaLinTextureStream(*Export.ObjectName, *Package.Filename))
			return false;
		if (!AddScdaLinTextureStubSegment(Package, Export))
			return false;
		if (getenv("SCDA_LIN_DEBUG"))
			appPrintf("  LIN streamed texture stub: %s %s %X+%X\n",
				*Package.Filename, *Export.ObjectName, Export.SerialOffset, Export.SerialSize);
		return IsScdaLinRangeMapped(Package, Offset, Size);
	}
	return false;
}

static bool MapScdaLinFilePayloads(const FScdaLinSourceFile& File, FScdaLinPayloadMapContext& Context)
{
	FFileReader Reader(*File.Filename, EFileArchiveOptions::OpenWarning);
	if (!Reader.IsOpen())
		return true;
	TArray<byte> LogicalData;
	bool Ok = DecompressScdaLin(&Reader, LogicalData);
	if (!Ok)
		return true;
	if (getenv("SCDA_LIN_DEBUG"))
		appPrintf("  LIN payload stream: %s logical=%X\n", *File.RelativeName, LogicalData.Num());
	MapScdaLinPayloads(Context.Package, LogicalData.GetData(), LogicalData.Num(), *File.RelativeName);
	return true;
}

static bool MapScdaLinNativePayloadsFromHint(FScdaLinPackage& Package)
{
	if (Package.NativeSourceFilename.IsEmpty())
		return false;
	if (IsScdaLinRangeMapped(Package, 0, Package.OriginalSize))
		return true;

	for (const FScdaLinSourceFile& File : GScdaLinPayloadFiles)
	{
		if (stricmp(*File.RelativeName, *Package.NativeSourceFilename))
			continue;

		FFileReader Reader(*File.Filename, EFileArchiveOptions::OpenWarning);
		if (!Reader.IsOpen())
			return false;
		TArray<byte> LogicalData;
		if (!DecompressScdaLin(&Reader, LogicalData))
			return false;
		MapScdaLinNativeExportsBySkipFields(Package, LogicalData.GetData(), LogicalData.Num(), *File.RelativeName);
		if (getenv("SCDA_LIN_DEBUG"))
			appPrintf("  LIN native hinted scan: %s from %s logical=%X\n",
				*Package.Filename, *File.RelativeName, LogicalData.Num());
		return true;
	}
	return false;
}

static void EnsureScdaLinPayload(FScdaLinPackage& Package, int Offset, int Size)
{
	if (IsScdaLinRangeMapped(Package, Offset, Size) || HydrateScdaLinCachedSegment(Package, Offset, Size) ||
		Package.PayloadStreamsMapped)
		return;
	if (MapScdaLinNativePayloadsFromHint(Package) && IsScdaLinRangeMapped(Package, Offset, Size))
		return;
	const TArray<FScdaLinSourceFile>& Files = GScdaLinPayloadFiles;
	if (!Files.Num())
		return;
	if (getenv("SCDA_LIN_DEBUG"))
		appPrintf("  LIN payload scan files: %d\n", Files.Num());
	ParallelFor(Files.Num(), [&Package, &Files](int Index)
	{
		FScdaLinPayloadMapContext Context(Package);
		MapScdaLinFilePayloads(Files[Index], Context);
	});
	Package.PayloadStreamsMapped = true;
}

static bool ShouldScanScdaLinPayloads()
{
	const char *ScanPayloads = getenv("SCDA_LIN_SCAN_PAYLOADS");
	return !ScanPayloads || strcmp(ScanPayloads, "0");
}

static bool ShouldSynthesizeScdaLinLevelTexturePackages()
{
	const char *SynthesizeTextures = getenv("SCDA_LIN_SYNTH_TEXTURE_PACKAGES");
	return !SynthesizeTextures || strcmp(SynthesizeTextures, "0");
}

static bool ShouldSynthesizeScdaLinLevelSkeletalPackages()
{
	const char *SynthesizeSkel = getenv("SCDA_LIN_SYNTH_SKEL_PACKAGES");
	return !SynthesizeSkel || strcmp(SynthesizeSkel, "0");
}

#if THREADING
static CMutex GScdaLinPayloadMutex;
#endif

class FScdaLinInnerFile : public FArchive
{
	DECLARE_ARCHIVE(FScdaLinInnerFile, FArchive);
public:
	FScdaLinInnerFile(FScdaLinPackage *InPackage)
	:	Package(InPackage)
	{
		IsLoading = true;
		Game = GAME_SplinterCell;
		ArStopper = Package->OriginalSize;
	}

	virtual void Seek(int Pos)
	{
		assert(Pos >= 0 && Pos <= GetFileSize());
		ArPos = Pos;
	}

	virtual bool IsEof() const
	{
		return ArPos >= GetFileSize();
	}

	virtual void Serialize(void *Data, int Size)
	{
		if (ArPos + Size > GetFileSize())
			appError("Serializing behind end of LIN package");
#if THREADING
		CMutex::ScopedLock Lock(GScdaLinPayloadMutex);
#endif
		if (!IsScdaLinRangeMapped(*Package, ArPos, Size))
		{
			if (!Package->PayloadStreamsMapped)
			{
				if (!HydrateScdaLinCachedSegment(*Package, ArPos, Size) &&
					!(MapScdaLinNativePayloadsFromHint(*Package) && IsScdaLinRangeMapped(*Package, ArPos, Size)) &&
					ShouldScanScdaLinPayloads())
					EnsureScdaLinPayload(*Package, ArPos, Size);
			}
			if (!IsScdaLinRangeMapped(*Package, ArPos, Size))
				TryMapScdaLinStreamTextureStub(*Package, ArPos, Size);
		}
		memset(Data, 0, Size);
		for (const FScdaLinPackage::FSegment& Segment : Package->Segments)
			CopySegment(Data, Size, Segment.VirtualOffset, Segment.DataOffset, Segment.Size);
		ArPos += Size;
	}

	virtual int GetFileSize() const
	{
		return Package->OriginalSize;
	}

	virtual bool IsRangeAvailable(int Pos, int Size)
	{
#if THREADING
		CMutex::ScopedLock Lock(GScdaLinPayloadMutex);
#endif
		if (!IsScdaLinRangeMapped(*Package, Pos, Size))
		{
			if (!Package->PayloadStreamsMapped)
			{
				if (!HydrateScdaLinCachedSegment(*Package, Pos, Size) &&
					!(MapScdaLinNativePayloadsFromHint(*Package) && IsScdaLinRangeMapped(*Package, Pos, Size)) &&
					ShouldScanScdaLinPayloads())
					EnsureScdaLinPayload(*Package, Pos, Size);
			}
			if (!IsScdaLinRangeMapped(*Package, Pos, Size))
				TryMapScdaLinStreamTextureStub(*Package, Pos, Size);
		}
		return IsScdaLinRangeMapped(*Package, Pos, Size);
	}

protected:
	void CopySegment(void *OutData, int OutSize, int VirtualOffset, int DataOffset, int DataSize)
	{
		if (DataOffset < 0)
			return;
		int Start = ArPos > VirtualOffset ? ArPos : VirtualOffset;
		int End = ArPos + OutSize < VirtualOffset + DataSize ? ArPos + OutSize : VirtualOffset + DataSize;
		if (Start >= End)
			return;
		memcpy(
			(byte*)OutData + Start - ArPos,
			Package->Data.GetData() + DataOffset + Start - VirtualOffset,
			End - Start
		);
	}

	FScdaLinPackage *Package;
};

/*-----------------------------------------------------------------------------
	Splinter Cell Double Agent PC v1 LIN/HLN containers
-----------------------------------------------------------------------------*/

struct FScdaPcLinEntry
{
	FString		Filename;
	int			DataOffset;
	int			OriginalDataOffset;
	int			Size;
	int			PackageTagDelta;
	int			ImportOffset;
	int			ImportSize;
	int			ImportPhysicalOffset;
	int			ExportOffset;
	int			ExportSize;
	int			ExportPhysicalOffset;
};

static void GetScdaPcLinIndexFilename(const char *LinFilename, FString& OutFilename)
{
	char Text[MAX_PACKAGE_PATH];
	appStrncpyz(Text, LinFilename, ARRAY_COUNT(Text));
	char *Dot = strrchr(Text, '.');
	if (Dot)
	{
		if (!stricmp(Dot, ".ulnc") || !stricmp(Dot, ".tlnc"))
			strcpy(Dot, ".hlnc");
		else
			strcpy(Dot, ".hln");
	}
	else
		appStrcatn(Text, ARRAY_COUNT(Text), ".hln");
	OutFilename = Text;
}

static bool ReadScdaPcLinInt32(const byte *Data, int DataSize, int Offset, int& Value)
{
	if (Offset < 0 || Offset + 4 > DataSize)
		return false;
	memcpy(&Value, Data + Offset, 4);
	return true;
}

static bool ReadScdaPcLinName(const byte *Data, int DataSize, int Offset, FString& OutName)
{
	char Name[MAX_PACKAGE_PATH];
	int NameLen = 0;
	for (int Pos = Offset; Pos + 1 < DataSize && Pos < Offset + 0x1FF; Pos += 2)
	{
		byte C = Data[Pos];
		byte Hi = Data[Pos + 1];
		if (C == 0 && Hi == 0)
			break;
		if (Hi != 0 || C < 0x20 || C >= 0x7F || NameLen + 1 >= ARRAY_COUNT(Name))
			return false;
		Name[NameLen++] = char(C);
	}
	if (NameLen < 3)
		return false;
	Name[NameLen] = 0;
	for (int i = 0; i < NameLen; i++)
		if (Name[i] == '\\') Name[i] = '/';
	const char *CleanName = Name;
	while (!strnicmp(CleanName, "../", 3))
		CleanName += 3;
	while (!strnicmp(CleanName, "./", 2))
		CleanName += 2;
	if (!strrchr(CleanName, '.'))
		return false;
	OutName = CleanName;
	return true;
}

static bool IsScdaPcUnrealPackageName(const char *Filename)
{
	const char *Ext = strrchr(Filename, '.');
	if (!Ext)
		return false;
	return !stricmp(Ext, ".u") || !stricmp(Ext, ".unr") || !stricmp(Ext, ".utx") ||
		!stricmp(Ext, ".usx") || !stricmp(Ext, ".ukx") || !stricmp(Ext, ".uax") ||
		!stricmp(Ext, ".ufx");
}

static bool ReadScdaPcLinBytes(FArchive *Reader, int Offset, void *Data, int Size)
{
	if (Offset < 0 || Offset + Size > Reader->GetFileSize())
		return false;
	Reader->Seek(Offset);
	Reader->Serialize(Data, Size);
	return true;
}

static bool LooksLikeScdaPcPackageHeader(const byte *Data, int AvailableSize, int SliceSize)
{
	if (AvailableSize < 0x28)
		return false;
	uint32 Tag;
	memcpy(&Tag, Data, 4);
	if (Tag != PACKAGE_FILE_TAG)
		return false;

	uint32 Version;
	memcpy(&Version, Data + 4, 4);
	const int ArVer = Version & 0xFFFF;
	const int ArLicenseeVer = Version >> 16;
	if (ArVer < 60 || ArVer > 200 || ArLicenseeVer > 255)
		return false;

	int NameCount, NameOffset, ExportCount, ExportOffset, ImportCount, ImportOffset;
	memcpy(&NameCount,   Data + 0x10, 4);
	memcpy(&NameOffset,  Data + 0x14, 4);
	memcpy(&ExportCount, Data + 0x18, 4);
	memcpy(&ExportOffset,Data + 0x1C, 4);
	memcpy(&ImportCount, Data + 0x20, 4);
	memcpy(&ImportOffset,Data + 0x24, 4);

	if (NameCount <= 0 || NameCount > 0x100000 || NameOffset < 0x28 || NameOffset >= SliceSize)
		return false;
	if (ExportCount < 0 || ExportCount > 0x100000 || ImportCount < 0 || ImportCount > 0x100000)
		return false;
	if (ExportOffset < 0 || ExportOffset >= SliceSize || ImportOffset < 0 || ImportOffset >= SliceSize)
		return false;
	return true;
}

static int FindScdaPcNameIndex(const TArray<FString>& Names, const char *Name)
{
	for (int i = 0; i < Names.Num(); i++)
		if (!stricmp(*Names[i], Name))
			return i;
	return INDEX_NONE;
}

static bool ReadScdaPcPackageNames(const byte *Data, int DataSize, TArray<FString>& Names, int& NameEnd)
{
	if (DataSize < 0x28)
		return false;
	int NameCount, NameOffset;
	memcpy(&NameCount, Data + 0x10, 4);
	memcpy(&NameOffset, Data + 0x14, 4);
	if (NameCount <= 0 || NameCount > 100000 || NameOffset < 0x28 || NameOffset >= DataSize)
		return false;

	int Pos = NameOffset;
	Names.Empty(NameCount);
	for (int i = 0; i < NameCount; i++)
	{
		if (Pos >= DataSize)
			return false;
		int Length = Data[Pos++];
		if (Length <= 0 || Length >= 256 || Pos + Length + 5 > DataSize || Data[Pos + Length] != 0)
			return false;
		Names.Add(FString(Length, (const char*)Data + Pos));
		Pos += Length + 1 + 4;
	}
	NameEnd = Pos;
	return true;
}

static bool ReadScdaPcImportRecord(const byte *Data, int DataSize, int& Pos, int NameCount,
	int& ClassPackage, int& ClassName, int& PackageIndex, int& ObjectName)
{
	if (!ReadLinCompactIndex(Data, DataSize, Pos, ClassPackage) ||
		!ReadLinCompactIndex(Data, DataSize, Pos, ClassName) ||
		!ReadLinInt32(Data, DataSize, Pos, PackageIndex) ||
		!ReadLinCompactIndex(Data, DataSize, Pos, ObjectName))
		return false;
	return ClassPackage >= 0 && ClassPackage < NameCount &&
		ClassName >= 0 && ClassName < NameCount &&
		ObjectName >= 0 && ObjectName < NameCount;
}

static bool ReadScdaPcExportRecord(const byte *Data, int DataSize, int& Pos, int NameCount, int& ClassIndex, int& ObjectName)
{
	int Dummy, SerialSize, SerialOffset = 0;
	if (!ReadLinCompactIndex(Data, DataSize, Pos, ClassIndex) ||
		!ReadLinCompactIndex(Data, DataSize, Pos, Dummy) ||
		!ReadLinInt32(Data, DataSize, Pos, Dummy) ||
		!ReadLinCompactIndex(Data, DataSize, Pos, ObjectName) ||
		!ReadLinInt32(Data, DataSize, Pos, Dummy) ||
		!ReadLinCompactIndex(Data, DataSize, Pos, SerialSize))
		return false;
	if (ClassIndex == 0 || ObjectName < 0 || ObjectName >= NameCount || SerialSize < 0)
		return false;
	if (SerialSize && !ReadLinCompactIndex(Data, DataSize, Pos, SerialOffset))
		return false;
	return true;
}

static bool IsScdaPcKnownExportClassName(const char *Name)
{
	return !stricmp(Name, "SkeletalMesh") ||
		!stricmp(Name, "MeshAnimation") ||
		!stricmp(Name, "StaticMesh") ||
		!stricmp(Name, "Texture") ||
		!stricmp(Name, "Sound") ||
		!strnicmp(Name, "AnimNotify", 10);
}

static bool FindScdaPcRelocatedTables(FArchive *Reader, FScdaPcLinEntry& Entry)
{
	if (!IsScdaPcUnrealPackageName(*Entry.Filename) || Entry.Size < 0x100)
		return false;
	if (Entry.DataOffset < 0 || Entry.DataOffset + Entry.Size > Reader->GetFileSize())
		return false;

	TArray<byte> Data;
	Data.AddUninitialized(Entry.Size);
	Reader->Seek(Entry.DataOffset);
	Reader->Serialize(Data.GetData(), Data.Num());

	const byte *Bytes = Data.GetData();
	int NameCount, ExportCount, ExportOffset, ImportCount, ImportOffset;
	memcpy(&NameCount, Bytes + 0x10, 4);
	memcpy(&ExportCount, Bytes + 0x18, 4);
	memcpy(&ExportOffset, Bytes + 0x1C, 4);
	memcpy(&ImportCount, Bytes + 0x20, 4);
	memcpy(&ImportOffset, Bytes + 0x24, 4);
	if (NameCount <= 0 || ExportCount < 0 || ImportCount <= 0)
		return false;

	TArray<FString> Names;
	int NameEnd;
	if (!ReadScdaPcPackageNames(Bytes, Data.Num(), Names, NameEnd))
		return false;

	const int CoreName = FindScdaPcNameIndex(Names, "Core");
	const int PackageName = FindScdaPcNameIndex(Names, "Package");
	const int EngineName = FindScdaPcNameIndex(Names, "Engine");
	const int ClassNameName = FindScdaPcNameIndex(Names, "Class");
	if (CoreName < 0 || PackageName < 0 || EngineName < 0 || ClassNameName < 0)
		return false;

	int Pos = ImportOffset;
	int ClassPackage, ClassName, PackageIndex, ObjectName;
	if (ReadScdaPcImportRecord(Bytes, Data.Num(), Pos, Names.Num(), ClassPackage, ClassName, PackageIndex, ObjectName) &&
		ClassPackage == CoreName && ClassName == PackageName && PackageIndex == 0 && ObjectName == EngineName)
	{
		int ProbePos = Pos;
		if (ReadScdaPcImportRecord(Bytes, Data.Num(), ProbePos, Names.Num(), ClassPackage, ClassName, PackageIndex, ObjectName) &&
			ClassPackage == CoreName && ClassName == ClassNameName && PackageIndex == -1)
		{
			Pos = ImportOffset;
			for (int i = 0; i < ImportCount; i++)
			{
				if (!ReadScdaPcImportRecord(Bytes, Data.Num(), Pos, Names.Num(), ClassPackage, ClassName, PackageIndex, ObjectName))
					goto scan_for_tables;
			}
			if (Pos == ExportOffset)
			{
				for (int i = 0; i < ExportCount; i++)
				{
					int ExportClassIndex, ExportObjectName;
					if (!ReadScdaPcExportRecord(Bytes, Data.Num(), Pos, Names.Num(), ExportClassIndex, ExportObjectName))
						goto scan_for_tables;
				}
				return false;
			}
		}
	}

scan_for_tables:
	int BestScore = -1;
	int BestImportStart = 0;
	int BestImportEnd = 0;
	int BestExportEnd = 0;
	for (int TableStart = 0x28; TableStart + 32 < Data.Num(); TableStart++)
	{
		Pos = TableStart;
		if (!ReadScdaPcImportRecord(Bytes, Data.Num(), Pos, Names.Num(), ClassPackage, ClassName, PackageIndex, ObjectName))
			continue;
		if (ClassPackage != CoreName || ClassName != PackageName || PackageIndex != 0 || ObjectName != EngineName)
			continue;

		int ProbePos = Pos;
		if (!ReadScdaPcImportRecord(Bytes, Data.Num(), ProbePos, Names.Num(), ClassPackage, ClassName, PackageIndex, ObjectName))
			continue;
		if (ClassPackage != CoreName || ClassName != ClassNameName || PackageIndex != -1)
			continue;

		Pos = TableStart;
		TArray<int> ImportObjectNames;
		ImportObjectNames.Empty(ImportCount);
		for (int i = 0; i < ImportCount; i++)
		{
			if (!ReadScdaPcImportRecord(Bytes, Data.Num(), Pos, Names.Num(), ClassPackage, ClassName, PackageIndex, ObjectName))
				goto next_candidate;
			ImportObjectNames.Add(ObjectName);
		}
		const int ImportEnd = Pos;

		int Score = 0;
		for (int i = 0; i < ExportCount; i++)
		{
			int ExportClassIndex, ExportObjectName;
			if (!ReadScdaPcExportRecord(Bytes, Data.Num(), Pos, Names.Num(), ExportClassIndex, ExportObjectName))
				goto next_candidate;
			if (ExportClassIndex < 0)
			{
				const int ImportIndex = -ExportClassIndex - 1;
				if (ImportIndex < 0 || ImportIndex >= ImportObjectNames.Num())
					goto next_candidate;
				if (IsScdaPcKnownExportClassName(*Names[ImportObjectNames[ImportIndex]]))
					Score++;
			}
			else if (ExportClassIndex > ExportCount)
				goto next_candidate;
		}

		if (Score > BestScore)
		{
			BestScore = Score;
			BestImportStart = TableStart;
			BestImportEnd = ImportEnd;
			BestExportEnd = Pos;
			if (Score == ExportCount)
				break;
		}

	next_candidate:
		;
	}

	if (BestScore >= 0)
	{
		Entry.ImportOffset = ImportOffset;
		Entry.ImportSize = BestImportEnd - BestImportStart;
		Entry.ImportPhysicalOffset = Entry.DataOffset + BestImportStart;
		Entry.ExportOffset = ExportOffset;
		Entry.ExportSize = BestExportEnd - BestImportEnd;
		Entry.ExportPhysicalOffset = Entry.DataOffset + BestImportEnd;
		if (getenv("SCDA_PC_LIN_DEBUG") || getenv("SCDA_LIN_DEBUG_PACKAGES"))
			appPrintf("  SCDA PC LIN relocated tables: %s import %X->%X export %X->%X score=%d\n",
				*Entry.Filename, Entry.ImportOffset, BestImportStart, Entry.ExportOffset, BestImportEnd, BestScore);
		return true;
	}
	return false;
}

static bool FindScdaPcPackageTag(FArchive *Reader, FScdaPcLinEntry& Entry)
{
	if (!IsScdaPcUnrealPackageName(*Entry.Filename) || Entry.Size < 0x28)
		return false;

	byte Header[0x40];
	if (!ReadScdaPcLinBytes(Reader, Entry.DataOffset, Header, sizeof(Header)))
		return false;
	if (LooksLikeScdaPcPackageHeader(Header, sizeof(Header), Entry.Size))
		return false;

	const int MaxScan = min(Entry.Size - 4, 16 * 1024 * 1024);
	const int ChunkSize = 64 * 1024;
	TArray<byte> Buffer;
	Buffer.AddUninitialized(ChunkSize + 3);

	int Pos = 0;
	while (Pos < MaxScan)
	{
		const int ReadSize = min(ChunkSize, MaxScan - Pos);
		if (!ReadScdaPcLinBytes(Reader, Entry.DataOffset + Pos, Buffer.GetData(), ReadSize + 3))
			break;
		for (int i = 0; i < ReadSize; i++)
		{
			if (Buffer[i] != 0xC1 || Buffer[i + 1] != 0x83 || Buffer[i + 2] != 0x2A || Buffer[i + 3] != 0x9E)
				continue;
			const int Delta = Pos + i;
			if (!ReadScdaPcLinBytes(Reader, Entry.DataOffset + Delta, Header, sizeof(Header)))
				continue;
			if (!LooksLikeScdaPcPackageHeader(Header, sizeof(Header), Entry.Size))
				continue;

			if (getenv("SCDA_PC_LIN_DEBUG") || getenv("SCDA_LIN_DEBUG_PACKAGES"))
				appPrintf("  SCDA PC LIN aligned package: %s offset=%X -> %X\n",
					*Entry.Filename, Entry.DataOffset, Entry.DataOffset + Delta);
			Entry.DataOffset += Delta;
			Entry.PackageTagDelta += Delta;
			return true;
		}
		Pos += ReadSize;
	}
	return false;
}

class FScdaPcLinInnerFile : public FArchive
{
	DECLARE_ARCHIVE(FScdaPcLinInnerFile, FArchive);
public:
	FScdaPcLinInnerFile(const char *InLinFilename, const FScdaPcLinEntry& InEntry)
	:	LinFilename(InLinFilename)
	,	Entry(InEntry)
	,	Reader(NULL)
	{
		IsLoading = true;
		Game = GAME_SplinterCell;
		Reader = new FFileReader(*LinFilename, EFileArchiveOptions::OpenWarning);
		if (!Reader->IsOpen())
		{
			delete Reader;
			Reader = NULL;
		}
		else
		{
			FindScdaPcPackageTag(Reader, Entry);
			FindScdaPcRelocatedTables(Reader, Entry);
			ArStopper = Entry.Size;
		}
	}

	virtual ~FScdaPcLinInnerFile()
	{
		delete Reader;
	}

	virtual bool IsOpen() const
	{
		return Reader && Reader->IsOpen();
	}

	virtual void Seek(int Pos)
	{
		assert(Pos >= 0 && Pos <= GetFileSize());
		ArPos = Pos;
	}

	virtual bool IsEof() const
	{
		return ArPos >= GetFileSize();
	}

	virtual void Serialize(void *Data, int Size)
	{
		if (!Reader)
			appError("SCDA PC LIN file is not open");
		if (ArPos + Size > GetFileSize())
			appError("Serializing behind end of SCDA PC LIN file");
		Reader->Seek(GetPhysicalOffset(ArPos));
		Reader->Serialize(Data, Size);
		ArPos += Size;
	}

	virtual int GetFileSize() const
	{
		return Entry.Size;
	}

protected:
	int GetPhysicalOffset(int Pos) const
	{
		if (Entry.ImportSize && Pos >= Entry.ImportOffset && Pos < Entry.ImportOffset + Entry.ImportSize)
			return Entry.ImportPhysicalOffset + Pos - Entry.ImportOffset;
		if (Entry.ExportSize && Pos >= Entry.ExportOffset && Pos < Entry.ExportOffset + Entry.ExportSize)
			return Entry.ExportPhysicalOffset + Pos - Entry.ExportOffset;

		return Entry.DataOffset + Pos;
	}

	FString			LinFilename;
	FScdaPcLinEntry	Entry;
	FFileReader		*Reader;
};

class FScdaPcLinVFS : public FVirtualFileSystem
{
public:
	FScdaPcLinVFS(const char *InFilename)
	:	Filename(InFilename)
	{}

	virtual bool AttachReader(FArchive *Reader, FString& Error)
	{
		guard(FScdaPcLinVFS::AttachReader);

		FString IndexFilename;
		GetScdaPcLinIndexFilename(*Filename, IndexFilename);
		if (!appFileExists(*IndexFilename))
		{
			Error = "SCDA PC LIN has no HLN index";
			return false;
		}

		FFileReader IndexReader(*IndexFilename, EFileArchiveOptions::OpenWarning);
		if (!IndexReader.IsOpen() || IndexReader.GetFileSize() <= 0 || IndexReader.GetFileSize() > 64 * 1024 * 1024)
		{
			Error = "Unable to read SCDA PC HLN index";
			return false;
		}
		TArray<byte> IndexData;
		IndexData.AddUninitialized(IndexReader.GetFileSize());
		IndexReader.Serialize(IndexData.GetData(), IndexData.Num());

		int LinSize = Reader->GetFileSize();
		int CandidateCount = 0;
		int RangeRejectCount = 0;
		int NameRejectCount = 0;
		for (int Marker = 4; Marker + 0x224 <= IndexData.Num(); Marker++)
		{
			const byte *Data = IndexData.GetData();
			if (Data[Marker] != 0x58 || Data[Marker + 0x219] != 0x58)
				continue;
			CandidateCount++;

			int FileSize, DataOffset;
			if (!ReadScdaPcLinInt32(Data, IndexData.Num(), Marker - 4, FileSize) ||
				!ReadScdaPcLinInt32(Data, IndexData.Num(), Marker + 0x21A, DataOffset))
				continue;
			if (FileSize <= 0 || DataOffset < 0 || DataOffset > LinSize - FileSize)
			{
				RangeRejectCount++;
				continue;
			}

			FString CleanName;
			if (!ReadScdaPcLinName(Data, IndexData.Num(), Marker + 1, CleanName))
			{
				NameRejectCount++;
				continue;
			}

			FScdaPcLinEntry& Entry = Entries[Entries.AddDefaulted()];
			Entry.Filename = CleanName;
			Entry.DataOffset = DataOffset;
			Entry.OriginalDataOffset = DataOffset;
			Entry.Size = FileSize;
			Entry.PackageTagDelta = 0;
			Entry.ImportOffset = 0;
			Entry.ImportSize = 0;
			Entry.ImportPhysicalOffset = 0;
			Entry.ExportOffset = 0;
			Entry.ExportSize = 0;
			Entry.ExportPhysicalOffset = 0;
		}

		Reserve(Entries.Num());
		for (int i = 0; i < Entries.Num(); i++)
		{
			CRegisterFileInfo Reg;
			Reg.Filename = *Entries[i].Filename;
			Reg.Size = Entries[i].Size;
			Reg.IndexInArchive = i;
			RegisterFile(Reg);
			if (getenv("SCDA_PC_LIN_DEBUG") || getenv("SCDA_LIN_DEBUG_PACKAGES"))
				appPrintf("  SCDA PC LIN file: %s offset=%X size=%X\n",
					*Entries[i].Filename, Entries[i].DataOffset, Entries[i].Size);
		}

		if (getenv("SCDA_PC_LIN_DEBUG") || getenv("SCDA_LIN_DEBUG"))
			appPrintf("SCDA PC LIN %s: index=%s candidates=%d rangeReject=%d nameReject=%d files=%d\n",
				*Filename, *IndexFilename, CandidateCount, RangeRejectCount, NameRejectCount, Entries.Num());
		return Entries.Num() > 0;

		unguardf("%s", *Filename);
	}

	virtual FArchive* CreateReader(int Index)
	{
		if (Index < 0 || Index >= Entries.Num())
			return NULL;
		FScdaPcLinInnerFile *File = new FScdaPcLinInnerFile(*Filename, Entries[Index]);
		if (!File->IsOpen())
		{
			delete File;
			return NULL;
		}
		return File;
	}

protected:
	FString Filename;
	TArray<FScdaPcLinEntry> Entries;
};

struct FScdaV2ManifestPayloadRecord
{
	int		ExportIndex;
	int		VirtualOffset;
	int		Size;
	int		SourceLogicalOffset;
	FString	SourceFilename;
	bool	SyntheticAnimation;

	FScdaV2ManifestPayloadRecord()
	:	ExportIndex(-1)
	,	VirtualOffset(0)
	,	Size(0)
	,	SourceLogicalOffset(0)
	,	SyntheticAnimation(false)
	{}
};

struct FScdaV2ManifestAnimationDataRecord
{
	FString	ObjectName;
	FString	SourceFilename;
	int		SourceLogicalOffset;
	int		Size;
	FString	AnchorName;
	int		AnchorLogicalOffset;
	TArray<byte> Data;

	FScdaV2ManifestAnimationDataRecord()
	:	SourceLogicalOffset(0)
	,	Size(0)
	,	AnchorLogicalOffset(0)
	{}
};

struct FScdaV2ManifestStaticObjectRecord
{
	FString ObjectName;
	FString SourceFilename;
	int SourceNameOffset;
	int SerialOffset;
	int SerialSize;
	int SourceLogicalOffset;

	FScdaV2ManifestStaticObjectRecord()
	:	SourceNameOffset(0)
	,	SerialOffset(0)
	,	SerialSize(0)
	,	SourceLogicalOffset(0)
	{}
};

struct FScdaV2ManifestPackageRecord
{
	FString Filename;
	FString SourceFilename;
	FString Kind;
	int HeaderOffset;
	int OriginalSize;
	bool Loaded;
	bool SyntheticStaticPackage;
	TArray<FScdaV2ManifestPayloadRecord> Payloads;
	TArray<FScdaV2ManifestAnimationDataRecord> AnimationData;
	TArray<FScdaV2ManifestStaticObjectRecord> StaticObjects;
	TArray<FString> SkeletonNames;
	TArray<int> SkeletonParents;

	FScdaV2ManifestPackageRecord()
	:	HeaderOffset(0)
	,	OriginalSize(0)
	,	Loaded(false)
	,	SyntheticStaticPackage(false)
	{}
};

static TArray<FScdaV2ManifestPackageRecord> *GScdaV2ManifestRecords;

bool GetScdaV2ManifestSkeleton(const char *PackageFilename, TArray<FString>& BoneNames,
	TArray<int>& ParentIndices)
{
	BoneNames.Empty();
	ParentIndices.Empty();
	if (!GScdaV2ManifestRecords || !PackageFilename)
		return false;
	for (const FScdaV2ManifestPackageRecord& Record : *GScdaV2ManifestRecords)
	{
		if (stricmp(*Record.Filename, PackageFilename) || !Record.SkeletonNames.Num() ||
			Record.SkeletonNames.Num() != Record.SkeletonParents.Num())
			continue;
		BoneNames.Empty(Record.SkeletonNames.Num());
		ParentIndices.Empty(Record.SkeletonParents.Num());
		for (int i = 0; i < Record.SkeletonNames.Num(); i++)
		{
			BoneNames.Add(Record.SkeletonNames[i]);
			ParentIndices.Add(Record.SkeletonParents[i]);
		}
		return true;
	}
	return false;
}

bool GetScdaV2ManifestAnimationData(const char *PackageFilename, const char *ObjectName,
	TArray<byte>& Data, int& SourceLogicalOffset)
{
	Data.Empty();
	SourceLogicalOffset = 0;
	if (!GScdaV2ManifestRecords || !PackageFilename || !ObjectName)
		return false;
	for (const FScdaV2ManifestPackageRecord& Record : *GScdaV2ManifestRecords)
	{
		if (stricmp(*Record.Filename, PackageFilename))
			continue;
		for (const FScdaV2ManifestAnimationDataRecord& AnimData : Record.AnimationData)
		{
			if (stricmp(*AnimData.ObjectName, ObjectName) || !AnimData.Data.Num())
				continue;
			Data.Empty(AnimData.Data.Num());
			Data.AddUninitialized(AnimData.Data.Num());
			memcpy(Data.GetData(), AnimData.Data.GetData(), AnimData.Data.Num());
			SourceLogicalOffset = AnimData.SourceLogicalOffset;
			return true;
		}
	}
	return false;
}

struct FScdaV2ManifestSourceCache
{
	FString		SourceFilename;
	TArray<byte>	LogicalData;
};

static void GetScdaManifestBaseDir(const char *Filename, FString& OutDir)
{
	char Text[MAX_PACKAGE_PATH];
	appStrncpyz(Text, Filename, ARRAY_COUNT(Text));
	char *Slash1 = strrchr(Text, '/');
	char *Slash2 = strrchr(Text, '\\');
	char *Slash = (!Slash1 || (Slash2 && Slash2 > Slash1)) ? Slash2 : Slash1;
	if (Slash)
		*Slash = 0;
	else
		Text[0] = 0;
	OutDir = Text;
}

static void MakeScdaManifestSiblingPath(const FString& BaseDir, const FString& RelName, FString& OutFilename)
{
	if (RelName.Len() > 2 && RelName[1] == ':')
	{
		OutFilename = RelName;
		return;
	}
	const char *Name = *RelName;
	if (Name[0] == '/' || Name[0] == '\\')
	{
		OutFilename = RelName;
		return;
	}
	OutFilename = BaseDir;
	if (!OutFilename.IsEmpty())
		OutFilename += "/";
	OutFilename += RelName;
}

static bool AddScdaV2ManifestAnimationStub(FScdaLinPackage& Package,
	const FScdaV2ManifestPackageRecord& Record, const FScdaV2ManifestPayloadRecord& Payload)
{
	int NoneIndex = -1;
	for (int Index = 0; Index < Package.Names.Num(); Index++)
	{
		if (!stricmp(*Package.Names[Index], "None"))
		{
			NoneIndex = Index;
			break;
		}
	}
	if (NoneIndex < 0 || Payload.Size < 8 || !Record.SkeletonNames.Num())
		return false;

	TArray<byte> Stub;
	Stub.AddZeroed(Payload.Size);
	int Pos = WriteScdaLinCompactIndex(Stub.GetData(), Stub.Num(), NoneIndex);
	if (Pos <= 0 || Pos + 4 > Stub.Num())
		return false;

	// UObject properties terminate at None. The remaining zero values are:
	// Version, empty RefBones, empty Moves, and empty AnimSeqs. SerializeSC4
	// replaces the empty RefBones with the exact manifest hierarchy.
	AddScdaLinSegment(Package, Payload.VirtualOffset, Stub.GetData(), Stub.Num());
	return true;
}

static void ScdaV2AppendByte(TArray<byte>& Data, byte Value)
{
	Data.Add(Value);
}

static void ScdaV2AppendInt32(TArray<byte>& Data, int Value)
{
	int Pos = Data.Num();
	Data.AddUninitialized(4);
	memcpy(Data.GetData() + Pos, &Value, 4);
}

static void ScdaV2AppendCompactIndex(TArray<byte>& Data, int Value)
{
	bool Negative = Value < 0;
	unsigned Remaining = Negative ? unsigned(-Value) : unsigned(Value);
	byte B = Remaining & 0x3F;
	Remaining >>= 6;
	if (Negative)
		B |= 0x80;
	if (Remaining)
		B |= 0x40;
	Data.Add(B);
	while (Remaining)
	{
		B = Remaining & 0x7F;
		Remaining >>= 7;
		if (Remaining)
			B |= 0x80;
		Data.Add(B);
	}
}

static void ScdaV2PatchInt32(TArray<byte>& Data, int Offset, int Value)
{
	if (Offset < 0 || Offset + 4 > Data.Num())
		return;
	memcpy(Data.GetData() + Offset, &Value, 4);
}

static int ScdaV2FindNameIndex(const TArray<FString>& Names, const char *Name)
{
	for (int i = 0; i < Names.Num(); i++)
		if (!stricmp(*Names[i], Name))
			return i;
	return -1;
}

static void ScdaV2AddUniqueName(TArray<FString>& Names, const FString& Name)
{
	if (ScdaV2FindNameIndex(Names, *Name) < 0)
		Names.Add(Name);
}

static void ScdaV2AddOrUpdateStaticObject(TArray<FScdaV2ManifestStaticObjectRecord>& Objects,
	const FScdaV2ManifestStaticObjectRecord& Object)
{
	for (FScdaV2ManifestStaticObjectRecord& Existing : Objects)
	{
		if (stricmp(*Existing.ObjectName, *Object.ObjectName))
			continue;
		if (Existing.SerialSize <= 0 && Object.SerialSize > 0)
			Existing = Object;
		return;
	}
	Objects.Add(Object);
}

static void ScdaV2AppendNameEntry(TArray<byte>& Data, const FString& Name)
{
	const char *Text = *Name;
	int Length = strlen(Text);
	if (Length > 254)
		Length = 254;
	Data.Add((byte)Length);
	int Pos = Data.Num();
	Data.AddUninitialized(Length + 1 + 4);
	memcpy(Data.GetData() + Pos, Text, Length);
	Data[Pos + Length] = 0;
	memset(Data.GetData() + Pos + Length + 1, 0, 4);
}

static bool BuildScdaV2StaticSidecarPackage(FScdaLinPackage& Package,
	const FScdaV2ManifestPackageRecord& Record)
{
	guard(BuildScdaV2StaticSidecarPackage);
	if (!Record.StaticObjects.Num())
		return false;

	TArray<FString> Names;
	Names.Add("None");
	Names.Add("Core");
	Names.Add("Engine");
	Names.Add("Package");
	Names.Add("Class");
	Names.Add("StaticMesh");
	for (const FScdaV2ManifestStaticObjectRecord& Object : Record.StaticObjects)
		ScdaV2AddUniqueName(Names, Object.ObjectName);

	const int StaticMeshNameIndex = ScdaV2FindNameIndex(Names, "StaticMesh");
	const int EngineNameIndex = ScdaV2FindNameIndex(Names, "Engine");
	const int ClassNameIndex = ScdaV2FindNameIndex(Names, "Class");
	if (StaticMeshNameIndex < 0 || EngineNameIndex < 0 || ClassNameIndex < 0)
		return false;

	TArray<byte> Data;
	Data.AddZeroed(0x98);
	const int Tag = PACKAGE_FILE_TAG;
	const int Version = 100 | (127 << 16);
	ScdaV2PatchInt32(Data, 0x00, Tag);
	ScdaV2PatchInt32(Data, 0x04, Version);
	ScdaV2PatchInt32(Data, 0x08, 0x13);
	ScdaV2PatchInt32(Data, 0x0C, 1);
	ScdaV2PatchInt32(Data, 0x10, Names.Num());
	ScdaV2PatchInt32(Data, 0x14, 0x98);
	ScdaV2PatchInt32(Data, 0x18, Record.StaticObjects.Num());
	ScdaV2PatchInt32(Data, 0x20, 1);
	ScdaV2PatchInt32(Data, 0x28, 0x0FF0ADDE);

	for (const FString& Name : Names)
		ScdaV2AppendNameEntry(Data, Name);

	const int ImportOffset = Data.Num();
	ScdaV2AppendCompactIndex(Data, EngineNameIndex);		// ClassPackage
	ScdaV2AppendCompactIndex(Data, ClassNameIndex);			// ClassName
	ScdaV2AppendInt32(Data, 0);								// PackageIndex
	ScdaV2AppendCompactIndex(Data, StaticMeshNameIndex);	// ObjectName

	const int ExportOffset = Data.Num();
	int MaxSerialEnd = Data.Num();
	int NextSerialOffset = 0x01000000;
	for (const FScdaV2ManifestStaticObjectRecord& Object : Record.StaticObjects)
	{
		const FString& ObjectName = Object.ObjectName;
		int ObjectNameIndex = ScdaV2FindNameIndex(Names, *ObjectName);
		if (ObjectNameIndex < 0)
			continue;
		ScdaV2AppendCompactIndex(Data, -1);					// ClassIndex: import 0 = StaticMesh
		ScdaV2AppendCompactIndex(Data, 0);					// SuperIndex
		ScdaV2AppendInt32(Data, 0);							// PackageIndex
		ScdaV2AppendCompactIndex(Data, ObjectNameIndex);
		ScdaV2AppendInt32(Data, 0);							// ObjectFlags
		const int SerialSize = max(Object.SerialSize, 0);
		ScdaV2AppendCompactIndex(Data, SerialSize);
		if (SerialSize > 0)
		{
			const int SerialOffset = NextSerialOffset;
			ScdaV2AppendCompactIndex(Data, SerialOffset);
			MaxSerialEnd = max(MaxSerialEnd, SerialOffset + SerialSize);
			NextSerialOffset = Align(SerialOffset + SerialSize, 16);
		}
	}

	ScdaV2PatchInt32(Data, 0x1C, ExportOffset);
	ScdaV2PatchInt32(Data, 0x24, ImportOffset);

	Package.Filename = Record.Filename;
	Package.NativeSourceFilename = Record.SourceFilename;
	Package.OriginalSize = max(Data.Num(), MaxSerialEnd);
	Package.ImportCount = 1;
	Package.Names.Empty(Names.Num());
	for (const FString& Name : Names)
		Package.Names.Add(Name);
	Package.Exports.Empty(Record.StaticObjects.Num());
	NextSerialOffset = 0x01000000;
	for (const FScdaV2ManifestStaticObjectRecord& Object : Record.StaticObjects)
	{
		const FString& ObjectName = Object.ObjectName;
		FScdaLinExportRange& Export = Package.Exports[Package.Exports.AddDefaulted()];
		Export.SerialOffset = Object.SerialSize > 0 ? NextSerialOffset : 0;
		Export.SerialSize = max(Object.SerialSize, 0);
		Export.ClassIndex = -1;
		Export.ObjectNameIndex = ScdaV2FindNameIndex(Names, *ObjectName);
		Export.ObjectName = ObjectName;
		Export.ClassName = "StaticMesh";
		if (Object.SerialSize > 0)
			NextSerialOffset = Align(NextSerialOffset + Object.SerialSize, 16);
	}
	Package.PayloadStreamsMapped = true;
	AddScdaLinSegment(Package, 0, Data.GetData(), Data.Num(), *Record.SourceFilename, 0);
	return true;
	unguard;
}

class FScdaV2ManifestVFS : public FVirtualFileSystem
{
public:
	FScdaV2ManifestVFS(const char *InFilename)
	:	Filename(InFilename)
	{}
	virtual ~FScdaV2ManifestVFS()
	{
		if (GScdaV2ManifestRecords == &Records)
			GScdaV2ManifestRecords = NULL;
	}

	virtual bool AttachReader(FArchive *Reader, FString& Error)
	{
		guard(FScdaV2ManifestVFS::AttachReader);

		if (!Reader || Reader->GetFileSize() <= 0 || Reader->GetFileSize() > 256 * 1024 * 1024)
		{
			Error = "Invalid SCDA V2 manifest";
			return false;
		}

		GScdaLinTextureStreams.Empty();
		GScdaLinPayloadFiles.Empty();
		GScdaLinCommonSourceFile = FScdaLinSourceFile();
		GScdaLinCommonSourceFileValid = false;
		GScdaLinPackages = &Packages;
		GScdaLinManifestFilename = Filename;
		GetScdaManifestBaseDir(*Filename, BaseDir);

		TArray<char> Text;
		Text.AddUninitialized(Reader->GetFileSize() + 1);
		Reader->Serialize(Text.GetData(), Reader->GetFileSize());
		Text[Text.Num() - 1] = 0;

		bool HeaderSeen = false;
		for (char *Line = strtok(Text.GetData(), "\r\n"); Line; Line = strtok(NULL, "\r\n"))
		{
			if (!Line[0] || Line[0] == '#')
				continue;
			int Version;
			if (sscanf(Line, "UMODEL_SCDA_V2_LEVEL_MANIFEST %d", &Version) == 1)
			{
				if (Version != 1)
				{
					Error = "Unsupported SCDA V2 manifest version";
					return false;
				}
				HeaderSeen = true;
				continue;
			}

			char Name1[MAX_PACKAGE_PATH], Name2[MAX_PACKAGE_PATH], Name3[MAX_PACKAGE_PATH], Name4[MAX_PACKAGE_PATH], Kind[64];
			unsigned HeaderOffset, OriginalSize;
			unsigned ExportIndex, SerialOffset, SerialSize, SourceLogicalOffset;
			if (sscanf(Line, "package \"%511[^\"]\" source \"%511[^\"]\" kind %63s header %x size %x",
				Name1, Name2, Kind, &HeaderOffset, &OriginalSize) == 5)
			{
				FScdaV2ManifestPackageRecord& Record = Records[Records.AddDefaulted()];
				Record.Filename = Name1;
				Record.SourceFilename = Name2;
				Record.Kind = Kind;
				Record.HeaderOffset = HeaderOffset;
				Record.OriginalSize = OriginalSize;

				FScdaLinPackage& Package = Packages[Packages.AddDefaulted()];
				Package.Filename = Record.Filename;
				Package.NativeSourceFilename = Record.SourceFilename;
				Package.OriginalSize = Record.OriginalSize;
				Package.PayloadStreamsMapped = true;
				continue;
			}
			if (sscanf(Line, "staticpackage \"%511[^\"]\" source \"%511[^\"]\" header %x size %x",
				Name1, Name2, &HeaderOffset, &OriginalSize) == 4 ||
				sscanf(Line, "staticpackage \"%511[^\"]\" source \"%511[^\"]\"",
				Name1, Name2) == 2)
			{
				bool Exists = false;
				for (FScdaV2ManifestPackageRecord& Existing : Records)
					if (!stricmp(*Existing.Filename, Name1))
					{
						Exists = true;
						break;
					}
				if (Exists)
					continue;
				FScdaV2ManifestPackageRecord& Record = Records[Records.AddDefaulted()];
				Record.Filename = Name1;
				Record.SourceFilename = Name2;
				Record.Kind = "sidecar_static";
				if (strstr(Line, " header "))
				{
					Record.HeaderOffset = HeaderOffset;
					Record.OriginalSize = OriginalSize;
				}
				else
				{
					Record.HeaderOffset = 0;
					Record.OriginalSize = 0x98;
				}
				Record.SyntheticStaticPackage = true;

				FScdaLinPackage& Package = Packages[Packages.AddDefaulted()];
				Package.Filename = Record.Filename;
				Package.NativeSourceFilename = Record.SourceFilename;
				Package.OriginalSize = Record.OriginalSize;
				Package.PayloadStreamsMapped = true;
				continue;
			}
			unsigned NameOffset;
			if (sscanf(Line, "staticobject \"%511[^\"]\" object \"%511[^\"]\" source \"%511[^\"]\" name_offset %x serial %x size %x logical %x",
				Name1, Name2, Name3, &NameOffset, &SerialOffset, &SerialSize, &SourceLogicalOffset) == 7 ||
				sscanf(Line, "staticobject \"%511[^\"]\" object \"%511[^\"]\" source \"%511[^\"]\" name_offset %x",
				Name1, Name2, Name3, &NameOffset) == 4)
			{
				for (FScdaV2ManifestPackageRecord& Record : Records)
				{
					if (stricmp(*Record.Filename, Name1))
						continue;
					FScdaV2ManifestStaticObjectRecord Object;
					Object.ObjectName = Name2;
					Object.SourceFilename = Name3;
					Object.SourceNameOffset = NameOffset;
					if (strstr(Line, " serial "))
					{
						Object.SerialOffset = SerialOffset;
						Object.SerialSize = SerialSize;
						Object.SourceLogicalOffset = SourceLogicalOffset;
					}
					ScdaV2AddOrUpdateStaticObject(Record.StaticObjects, Object);
					if (Record.SourceFilename.IsEmpty())
						Record.SourceFilename = Name3;
					Record.SyntheticStaticPackage = true;
					break;
				}
				continue;
			}

			unsigned SkeletonCount, BoneIndex;
			int ParentIndex;
			if (sscanf(Line, "skeleton \"%511[^\"]\" count %u signature %511s",
				Name1, &SkeletonCount, Name2) == 3)
			{
				for (FScdaV2ManifestPackageRecord& Record : Records)
				if (!stricmp(*Record.Filename, Name1) && SkeletonCount > 0 && SkeletonCount <= 512)
				{
					Record.SkeletonNames.Empty(SkeletonCount);
					Record.SkeletonNames.AddDefaulted(SkeletonCount);
					Record.SkeletonParents.Empty(SkeletonCount);
					Record.SkeletonParents.AddZeroed(SkeletonCount);
					break;
				}
				continue;
			}
			if (sscanf(Line, "bone \"%511[^\"]\" index %u name \"%511[^\"]\" parent %d",
				Name1, &BoneIndex, Name2, &ParentIndex) == 4)
			{
				for (FScdaV2ManifestPackageRecord& Record : Records)
					if (!stricmp(*Record.Filename, Name1) &&
						BoneIndex < (unsigned)Record.SkeletonNames.Num())
					{
						Record.SkeletonNames[BoneIndex] = Name2;
						Record.SkeletonParents[BoneIndex] = ParentIndex;
						break;
					}
				continue;
			}
			if (sscanf(Line,
				"animation \"%511[^\"]\" index %u object \"%511[^\"]\" serial %x size %x skeleton %511s",
				Name1, &ExportIndex, Name2, &SerialOffset, &SerialSize, Name3) == 6)
			{
				for (FScdaV2ManifestPackageRecord& Record : Records)
				{
					if (stricmp(*Record.Filename, Name1))
						continue;
					FScdaV2ManifestPayloadRecord& Payload = Record.Payloads[Record.Payloads.AddDefaulted()];
					Payload.ExportIndex = ExportIndex;
					Payload.VirtualOffset = SerialOffset;
					Payload.Size = SerialSize;
					Payload.SyntheticAnimation = true;
					break;
				}
				continue;
			}
			if (sscanf(Line,
				"payload \"%511[^\"]\" index %u class \"%511[^\"]\" object \"%511[^\"]\" serial %x size %x source \"%511[^\"]\" logical %x",
				Name1, &ExportIndex, Name2, Name3, &SerialOffset, &SerialSize, Name4, &SourceLogicalOffset) == 8)
			{
				for (FScdaV2ManifestPackageRecord& Record : Records)
				{
					if (stricmp(*Record.Filename, Name1))
						continue;
					FScdaV2ManifestPayloadRecord& Payload = Record.Payloads[Record.Payloads.AddDefaulted()];
					Payload.ExportIndex = ExportIndex;
					Payload.VirtualOffset = SerialOffset;
					Payload.Size = SerialSize;
					Payload.SourceFilename = Name4;
					Payload.SourceLogicalOffset = SourceLogicalOffset;
					break;
				}
				continue;
			}
			unsigned AnimDataSize, AnchorLogicalOffset;
			if (sscanf(Line,
				"animdata \"%511[^\"]\" object \"%511[^\"]\" source \"%511[^\"]\" logical %x size %x",
				Name1, Name2, Name3, &SourceLogicalOffset, &AnimDataSize) == 5)
			{
				for (FScdaV2ManifestPackageRecord& Record : Records)
				{
					if (stricmp(*Record.Filename, Name1))
						continue;
					FScdaV2ManifestAnimationDataRecord& AnimData = Record.AnimationData[Record.AnimationData.AddDefaulted()];
					AnimData.ObjectName = Name2;
					AnimData.SourceFilename = Name3;
					AnimData.SourceLogicalOffset = SourceLogicalOffset;
					AnimData.Size = AnimDataSize;
					break;
				}
				continue;
			}
			if (sscanf(Line,
				"animdata \"%511[^\"]\" object \"%511[^\"]\" source \"%511[^\"]\" logical %x size %x anchor \"%511[^\"]\" anchor_logical %x",
				Name1, Name2, Name3, &SourceLogicalOffset, &AnimDataSize, Name4, &AnchorLogicalOffset) == 7)
			{
				for (FScdaV2ManifestPackageRecord& Record : Records)
				{
					if (stricmp(*Record.Filename, Name1))
						continue;
					FScdaV2ManifestAnimationDataRecord& AnimData = Record.AnimationData[Record.AnimationData.AddDefaulted()];
					AnimData.ObjectName = Name2;
					AnimData.SourceFilename = Name3;
					AnimData.SourceLogicalOffset = SourceLogicalOffset;
					AnimData.Size = AnimDataSize;
					AnimData.AnchorName = Name4;
					AnimData.AnchorLogicalOffset = AnchorLogicalOffset;
					break;
				}
				continue;
			}

			unsigned LogicalOffset, GpuOffset, GpuSize, TextureId, BinOffset, BinSize, USize, VSize;
			int FormatCode, Valid;
			if (sscanf(Line,
				"texstream \"%511[^\"]\" package \"%511[^\"]\" source \"%511[^\"]\" bin \"%511[^\"]\" logical %x gpu_offset %x gpu_size %x texture_id %u bin_offset %x bin_size %x usize %u vsize %u format %d valid %d",
				Name1, Name2, Name3, Name4, &LogicalOffset, &GpuOffset, &GpuSize, &TextureId, &BinOffset, &BinSize,
				&USize, &VSize, &FormatCode, &Valid) == 14)
			{
				if (!Valid)
					continue;
				FScdaLinTextureStreamEntry& Entry = GScdaLinTextureStreams[GScdaLinTextureStreams.AddDefaulted()];
				Entry.TextureName = Name1;
				Entry.PackageFilename = Name2;
				Entry.BinFilename = Name4;
				Entry.LogicalOffset = LogicalOffset;
				Entry.GpuOffset = GpuOffset;
				Entry.GpuSize = GpuSize;
				Entry.TextureId = TextureId;
				Entry.BinOffset = BinOffset;
				Entry.BinSize = BinSize;
				Entry.USize = USize;
				Entry.VSize = VSize;
				Entry.FormatCode = FormatCode;
				NormalizeScdaLinTextureStreamInfo(Entry);
				continue;
			}
			if (sscanf(Line,
				"texstream \"%511[^\"]\" source \"%511[^\"]\" bin \"%511[^\"]\" logical %x gpu_offset %x gpu_size %x texture_id %u bin_offset %x bin_size %x usize %u vsize %u format %d valid %d",
				Name1, Name2, Name3, &LogicalOffset, &GpuOffset, &GpuSize, &TextureId, &BinOffset, &BinSize,
				&USize, &VSize, &FormatCode, &Valid) == 13)
			{
				if (!Valid)
					continue;
				FScdaLinTextureStreamEntry& Entry = GScdaLinTextureStreams[GScdaLinTextureStreams.AddDefaulted()];
				Entry.TextureName = Name1;
				Entry.BinFilename = Name3;
				Entry.LogicalOffset = LogicalOffset;
				Entry.GpuOffset = GpuOffset;
				Entry.GpuSize = GpuSize;
				Entry.TextureId = TextureId;
				Entry.BinOffset = BinOffset;
				Entry.BinSize = BinSize;
				Entry.USize = USize;
				Entry.VSize = VSize;
				Entry.FormatCode = FormatCode;
				NormalizeScdaLinTextureStreamInfo(Entry);
				continue;
			}
		}

		if (!HeaderSeen || !Records.Num())
		{
			Error = "SCDA V2 manifest has no package records";
			return false;
		}
		GScdaV2ManifestRecords = &Records;

		Reserve(Packages.Num());
		for (int i = 0; i < Packages.Num(); i++)
		{
			CRegisterFileInfo Reg;
			Reg.Filename = *Packages[i].Filename;
			Reg.Size = Packages[i].OriginalSize;
			Reg.IndexInArchive = i;
			RegisterFile(Reg);
		}

		int PayloadCount = 0;
		for (const FScdaV2ManifestPackageRecord& Record : Records)
			PayloadCount += Record.Payloads.Num();
		appPrintf("Loaded SCDA V2 level manifest: %s (%d packages, %d payloads, %d texture streams)\n",
			*Filename, Records.Num(), PayloadCount, GScdaLinTextureStreams.Num());
		return true;

		unguardf("%s", *Filename);
	}

	virtual FArchive* CreateReader(int Index)
	{
		if (Index < 0 || Index >= Records.Num())
			return NULL;
		if (!LoadPackage(Index))
			return NULL;
		return new FScdaLinInnerFile(&Packages[Index]);
	}

protected:
	const TArray<byte>* GetSourceData(const FString& SourceFilename)
	{
		for (const FScdaV2ManifestSourceCache& Cache : SourceCache)
			if (!stricmp(*Cache.SourceFilename, *SourceFilename))
				return &Cache.LogicalData;

		FString SourcePath;
		MakeScdaManifestSiblingPath(BaseDir, SourceFilename, SourcePath);
		FFileReader Reader(*SourcePath, EFileArchiveOptions::OpenWarning);
		if (!Reader.IsOpen())
			return NULL;

		FScdaV2ManifestSourceCache& Cache = SourceCache[SourceCache.AddDefaulted()];
		Cache.SourceFilename = SourceFilename;
		const char *Ext = strrchr(*SourceFilename, '.');
		bool Ok;
		if (Ext && !stricmp(Ext, ".bin"))
		{
			Cache.LogicalData.AddUninitialized(Reader.GetFileSize());
			Reader.Serialize(Cache.LogicalData.GetData(), Cache.LogicalData.Num());
			Ok = true;
		}
		else
		{
			Ok = DecompressScdaLin(&Reader, Cache.LogicalData);
		}
		if (!Ok)
		{
			SourceCache.RemoveAt(SourceCache.Num() - 1);
			return NULL;
		}
		if (getenv("SCDA_LIN_DEBUG") || getenv("SCDA_LIN_DEBUG_PACKAGES"))
			appPrintf("  SCDA V2 manifest source: %s logical=%X\n", *SourceFilename, Cache.LogicalData.Num());
		return &SourceCache.Last().LogicalData;
	}

	bool LoadPackage(int Index)
	{
		FScdaV2ManifestPackageRecord& Record = Records[Index];
		if (Record.Loaded)
			return true;
		if (Record.SyntheticStaticPackage)
		{
			if (Record.HeaderOffset > 0 && Record.OriginalSize > 0)
			{
				const TArray<byte> *LogicalData = GetSourceData(Record.SourceFilename);
				if (!LogicalData)
					return false;
				FScdaLinHeader Header;
				if (!ReadScdaLinHeader(LogicalData->GetData(), LogicalData->Num(), Record.HeaderOffset, Header))
					return false;
				InitScdaLinPackageFromHeader(Packages[Index], Record.Filename, Record.OriginalSize, Header.Offset,
					Header, LogicalData->GetData(), LogicalData->Num(), true, *Record.SourceFilename);
				Record.OriginalSize = Packages[Index].OriginalSize;
				Record.Loaded = true;
				if (getenv("SCDA_LIN_DEBUG") || getenv("SCDA_LIN_DEBUG_PACKAGES"))
					appPrintf("  SCDA V2 inline static package: %s source=%s header=%X objects=%d size=%X\n",
						*Record.Filename, *Record.SourceFilename, Record.HeaderOffset, Record.StaticObjects.Num(),
						Packages[Index].OriginalSize);
				return true;
			}
			if (!BuildScdaV2StaticSidecarPackage(Packages[Index], Record))
				return false;
			for (int StaticIndex = 0; StaticIndex < Record.StaticObjects.Num(); StaticIndex++)
			{
				const FScdaV2ManifestStaticObjectRecord& Object = Record.StaticObjects[StaticIndex];
				if (Object.SerialSize <= 0 || Object.SourceLogicalOffset < 0)
					continue;
				if (StaticIndex >= Packages[Index].Exports.Num())
					continue;
				const FScdaLinExportRange& Export = Packages[Index].Exports[StaticIndex];
				const TArray<byte> *LogicalData = GetSourceData(Object.SourceFilename.IsEmpty() ? Record.SourceFilename : Object.SourceFilename);
				if (!LogicalData)
					continue;
				if (Object.SourceLogicalOffset < 0 || Object.SourceLogicalOffset > LogicalData->Num() - Object.SerialSize)
				{
					if (getenv("SCDA_LIN_DEBUG") || getenv("SCDA_LIN_DEBUG_PACKAGES"))
						appPrintf("  SCDA V2 static payload skipped: %s object=%s logical=%X size=%X source_size=%X\n",
							*Record.Filename, *Object.ObjectName, Object.SourceLogicalOffset, Object.SerialSize,
							LogicalData->Num());
					continue;
				}
				AddScdaLinSegment(Packages[Index], Export.SerialOffset,
					LogicalData->GetData() + Object.SourceLogicalOffset, Object.SerialSize,
					*(Object.SourceFilename.IsEmpty() ? Record.SourceFilename : Object.SourceFilename),
					Object.SourceLogicalOffset);
			}
			Record.OriginalSize = Packages[Index].OriginalSize;
			Record.Loaded = true;
			if (getenv("SCDA_LIN_DEBUG") || getenv("SCDA_LIN_DEBUG_PACKAGES"))
				appPrintf("  SCDA V2 static sidecar package: %s source=%s objects=%d size=%X\n",
					*Record.Filename, *Record.SourceFilename, Record.StaticObjects.Num(),
					Packages[Index].OriginalSize);
			return true;
		}

		const TArray<byte> *LogicalData = GetSourceData(Record.SourceFilename);
		if (!LogicalData)
			return false;

		FScdaLinHeader Header;
		if (!ReadScdaLinHeader(LogicalData->GetData(), LogicalData->Num(), Record.HeaderOffset, Header))
			return false;
		InitScdaLinPackageFromHeader(Packages[Index], Record.Filename, Record.OriginalSize, Header.Offset,
			Header, LogicalData->GetData(), LogicalData->Num(), false, *Record.SourceFilename);
		for (const FScdaV2ManifestPayloadRecord& Payload : Record.Payloads)
		{
			if (Payload.SyntheticAnimation)
			{
				for (int SegmentIndex = Packages[Index].Segments.Num() - 1; SegmentIndex >= 0; SegmentIndex--)
				{
					if (Packages[Index].Segments[SegmentIndex].VirtualOffset == Payload.VirtualOffset)
						Packages[Index].Segments.RemoveAt(SegmentIndex);
				}
				AddScdaV2ManifestAnimationStub(Packages[Index], Record, Payload);
				continue;
			}
			const TArray<byte> *PayloadData = LogicalData;
			if (stricmp(*Payload.SourceFilename, *Record.SourceFilename))
			{
				PayloadData = GetSourceData(Payload.SourceFilename);
				if (!PayloadData)
					continue;
			}
			if (Payload.SourceLogicalOffset < 0 || Payload.Size <= 0 ||
				Payload.SourceLogicalOffset > PayloadData->Num() - Payload.Size)
				continue;
			// Explicit manifest payloads are authoritative. Package initialization
			// may have installed a provisional direct-map or synthetic stub at the
			// same virtual export offset; replace it instead of silently keeping it.
			for (int SegmentIndex = Packages[Index].Segments.Num() - 1; SegmentIndex >= 0; SegmentIndex--)
			{
				if (Packages[Index].Segments[SegmentIndex].VirtualOffset == Payload.VirtualOffset)
					Packages[Index].Segments.RemoveAt(SegmentIndex);
			}
			AddScdaLinSegment(Packages[Index], Payload.VirtualOffset,
				PayloadData->GetData() + Payload.SourceLogicalOffset, Payload.Size,
				*Payload.SourceFilename, Payload.SourceLogicalOffset);
		}
		for (FScdaV2ManifestAnimationDataRecord& AnimData : Record.AnimationData)
		{
			const TArray<byte> *AnimSourceData = LogicalData;
			if (stricmp(*AnimData.SourceFilename, *Record.SourceFilename))
			{
				AnimSourceData = GetSourceData(AnimData.SourceFilename);
				if (!AnimSourceData)
					continue;
			}
			if (AnimData.SourceLogicalOffset < 0 || AnimData.Size <= 0 ||
				AnimData.SourceLogicalOffset > AnimSourceData->Num() - AnimData.Size)
				continue;
			AnimData.Data.Empty(AnimData.Size);
			AnimData.Data.AddUninitialized(AnimData.Size);
			memcpy(AnimData.Data.GetData(),
				AnimSourceData->GetData() + AnimData.SourceLogicalOffset,
				AnimData.Size);
			if (getenv("SCDA_LIN_DEBUG") || getenv("SCDA_DEBUG_ANIM"))
				appPrintf("  SCDA V2 animdata: %s %s <- %s %X+%X anchor=%s@%X\n",
					*Record.Filename, *AnimData.ObjectName, *AnimData.SourceFilename,
					AnimData.SourceLogicalOffset, AnimData.Size,
					*AnimData.AnchorName, AnimData.AnchorLogicalOffset);
		}
		Packages[Index].PayloadStreamsMapped = true;
		Record.Loaded = true;
		if (getenv("SCDA_LIN_DEBUG") || getenv("SCDA_LIN_DEBUG_PACKAGES"))
			appPrintf("  SCDA V2 manifest package: %s source=%s header=%X size=%X names=%d imports=%d exports=%d payloads=%d\n",
				*Record.Filename, *Record.SourceFilename, Record.HeaderOffset, Record.OriginalSize,
				Packages[Index].Names.Num(), Packages[Index].ImportCount, Packages[Index].Exports.Num(),
				Record.Payloads.Num());
		return true;
	}

	FString Filename;
	FString BaseDir;
	TArray<FScdaV2ManifestPackageRecord> Records;
	TArray<FScdaV2ManifestSourceCache> SourceCache;
	TArray<FScdaLinPackage> Packages;
};

class FScdaLinVFS : public FVirtualFileSystem
{
public:
	FScdaLinVFS(const char *InFilename)
	:	Filename(InFilename)
	{}

	virtual bool AttachReader(FArchive *Reader, FString& Error)
	{
		guard(FScdaLinVFS::AttachReader);

		GScdaLinTextureStreams.Empty();
		GScdaLinPayloadFiles.Empty();
		GScdaLinCommonSourceFile = FScdaLinSourceFile();
		GScdaLinCommonSourceFileValid = false;
		GScdaLinPackages = NULL;
		GetScdaLinManifestFilename(*Filename, GScdaLinManifestFilename);

		const char *ShortName = strrchr(*Filename, '/');
		const char *ShortName2 = strrchr(*Filename, '\\');
		if (!ShortName || (ShortName2 && ShortName2 > ShortName))
			ShortName = ShortName2;
		ShortName = ShortName ? ShortName + 1 : *Filename;

		TArray<byte> LogicalData;
		if (!DecompressScdaLin(Reader, LogicalData))
		{
			Error = "Invalid SCDA LIN compression block";
			return false;
		}

		if (stricmp(ShortName, "common.lin"))
		{
			EnsureScdaLinCommonCache(*Filename);
			FString LevelBase;
			GetScdaLinLevelBaseName(ShortName, LevelBase);
			GScdaLinPackages = &Packages;
			AddScdaLinLevelPackagesFromData(Packages, LogicalData.GetData(), LogicalData.Num(), LevelBase, ShortName);

			Reserve(Packages.Num());
			for (int i = 0; i < Packages.Num(); i++)
			{
				CRegisterFileInfo Reg;
				Reg.Filename = *Packages[i].Filename;
				Reg.Size = Packages[i].OriginalSize;
				Reg.IndexInArchive = i;
				RegisterFile(Reg);
				if (getenv("SCDA_LIN_DEBUG") || getenv("SCDA_LIN_DEBUG_PACKAGES"))
					appPrintf("  LIN level package: %s size=%X names=%d imports=%d exports=%d\n",
						*Packages[i].Filename, Packages[i].OriginalSize, Packages[i].Names.Num(),
						Packages[i].ImportCount, Packages[i].Exports.Num());
			}

			if (getenv("SCDA_LIN_DEBUG"))
				appPrintf("SCDA LIN level %s: packages=%d logical=%X\n", *Filename, Packages.Num(), LogicalData.Num());
			if (!Packages.Num())
			{
				Error = "SCDA LIN has no supported inline packages";
				return false;
			}
			return true;
		}

		TArray<FScdaLinManifestEntry> Manifest;
		if (!ReadScdaLinManifest(LogicalData.GetData(), LogicalData.Num(), Manifest))
		{
			Error = "SCDA LIN has no package manifest";
			return false;
		}
		GScdaLinCommonCacheFilename = Filename;
		CopyScdaLinArray(GScdaLinCommonManifestCache, Manifest);
		GScdaLinCommonCacheLoaded = true;

		TArray<FScdaLinHeader> Headers;
		Headers.Reserve(1024);
		for (int Pos = 0; Pos + 4 <= LogicalData.Num(); Pos++)
		{
			int Tag;
			memcpy(&Tag, LogicalData.GetData() + Pos, 4);
			if (Tag != PACKAGE_FILE_TAG)
				continue;
			int HeaderIndex = Headers.Num();
			FScdaLinHeader& Header = *new (Headers) FScdaLinHeader;
			if (!ReadScdaLinHeader(LogicalData.GetData(), LogicalData.Num(), Pos, Header))
				Headers.RemoveAt(Headers.Num() - 1);
		}

		TArray<byte> UsedHeaders;
		UsedHeaders.AddZeroed(Headers.Num());
		Packages.Reserve(Manifest.Num());
		for (const FScdaLinManifestEntry& Entry : Manifest)
		{
			int BestHeader = -1;
			int BestTail = 0x7FFFFFFF;
			for (int HeaderIndex = 0; HeaderIndex < Headers.Num(); HeaderIndex++)
			{
				if (UsedHeaders[HeaderIndex])
					continue;
				const FScdaLinHeader& Header = Headers[HeaderIndex];
				if (!LinHeaderHasName(Header, *Entry.PackageName))
					continue;
				if (Header.MaxExportEnd <= 0 || Header.MaxExportEnd > Entry.OriginalSize)
					continue;
				int Tail = Entry.OriginalSize - Header.MaxExportEnd;
				if (Tail < BestTail)
				{
					BestTail = Tail;
					BestHeader = HeaderIndex;
				}
			}
			if (BestHeader < 0)
				continue;

			UsedHeaders[BestHeader] = 1;
			const FScdaLinHeader& Header = Headers[BestHeader];
			if (getenv("SCDA_LIN_DEBUG_SAM") &&
				(ScdaLinContainsNoCase(*Entry.Filename, "ingredients_skel") ||
				ScdaLinContainsNoCase(*Entry.Filename, "esam")))
			{
				appPrintf("  LIN package bind: %s manifestBase=%X header=%X size=%X maxExport=%X\n",
					*Entry.Filename, Entry.OriginalOffset, Header.Offset, Entry.OriginalSize, Header.MaxExportEnd);
			}
			AddScdaLinPackageFromHeader(Packages, Entry.Filename, Entry.OriginalSize, Entry.OriginalOffset, Header, LogicalData.GetData(), LogicalData.Num());
		}

		GScdaLinPackages = &Packages;
		CollectScdaLinPhysicalPayloadFiles(*Filename);
		CopyScdaLinArray(GScdaLinPayloadFilesCache, GScdaLinPayloadFiles);
		if (!ReadScdaLinPayloadManifest() && ShouldScanScdaLinPayloads())
			MapScdaLinPayloads(Packages, LogicalData.GetData(), LogicalData.Num(), "common.lin");

		Reserve(Packages.Num());
		for (int i = 0; i < Packages.Num(); i++)
		{
			CRegisterFileInfo Reg;
			Reg.Filename = *Packages[i].Filename;
			Reg.Size = Packages[i].OriginalSize;
			Reg.IndexInArchive = i;
			RegisterFile(Reg);
			if (getenv("SCDA_LIN_DEBUG") || getenv("SCDA_LIN_DEBUG_PACKAGES"))
				appPrintf("  LIN package: %s size=%X names=%d imports=%d exports=%d\n",
					*Packages[i].Filename, Packages[i].OriginalSize, Packages[i].Names.Num(),
					Packages[i].ImportCount, Packages[i].Exports.Num());
			if (getenv("SCDA_LIN_DEBUG_SAM"))
			{
				if (ScdaLinContainsNoCase(*Packages[i].Filename, "ingredients_skel") ||
					ScdaLinContainsNoCase(*Packages[i].Filename, "esam"))
				{
					for (const FScdaLinPackage::FSegment& Segment : Packages[i].Segments)
						appPrintf("    LIN segment: %s %X+%X data=%X source=%s:%X\n",
							*Packages[i].Filename, Segment.VirtualOffset, Segment.Size,
							Segment.DataOffset, *Segment.SourceFilename, Segment.SourceLogicalOffset);
				}
				for (const FScdaLinExportRange& Export : Packages[i].Exports)
				{
					if (ScdaLinContainsNoCase(*Export.ObjectName, "sam") ||
						ScdaLinContainsNoCase(*Export.ClassName, "mesh") ||
						ScdaLinContainsNoCase(*Export.ClassName, "anim"))
						appPrintf("    LIN export: %s %s'%s' %X+%X mapped=%d\n",
							*Packages[i].Filename, *Export.ClassName, *Export.ObjectName, Export.SerialOffset, Export.SerialSize,
							IsScdaLinRangeMapped(Packages[i], Export.SerialOffset, Export.SerialSize) ? 1 : 0);
				}
			}
		}

		if (getenv("SCDA_LIN_DEBUG"))
			appPrintf("SCDA LIN %s: manifest=%d headers=%d packages=%d\n", *Filename, Manifest.Num(), Headers.Num(), Packages.Num());
		return Packages.Num() > 0;

		unguardf("%s", *Filename);
	}

	virtual FArchive* CreateReader(int Index)
	{
		if (Index < 0 || Index >= Packages.Num())
			return NULL;
		return new FScdaLinInnerFile(&Packages[Index]);
	}

protected:
	FString Filename;
	TArray<FScdaLinPackage> Packages;
};

FVirtualFileSystem* CreateScdaLinVFS(const char *Filename)
{
	FString PcIndexFilename;
	GetScdaPcLinIndexFilename(Filename, PcIndexFilename);
	if (appFileExists(*PcIndexFilename))
	return new FScdaPcLinVFS(Filename);
	return new FScdaLinVFS(Filename);
}

FVirtualFileSystem* CreateScdaV2ManifestVFS(const char *Filename)
{
	return new FScdaV2ManifestVFS(Filename);
}

FVirtualFileSystem* CreateScdaPcLinVFS(const char *Filename)
{
	return new FScdaPcLinVFS(Filename);
}

#endif // SPLINTER_CELL


/*-----------------------------------------------------------------------------
	LEAD Engine archive file reader
-----------------------------------------------------------------------------*/

#if LEAD

#define LEAD_FILE_TAG				0x4B435045		// 'EPCK'

// Archive flags
#define LEAD_PKG_COMPRESS_ZLIB		0x01			// use ZLib compression
#define LEAD_PKG_COMPRESS_XMEM		0x02			// use XBox 360 LZX compression
#define LEAD_PKG_CHECK_CRC			0x30			// really this is 2 separate flags
#define LEAD_PKG_COMPRESS_TABLES	0x80			// archive page table is compressed

FArchive* CreateUMDReader(FArchive *File);
static void ReplaceExtension(char *Filename, int FilenameSize, const char *NewExt);
static bool ShouldLeadUseAssStreams();


struct FLeadArcPage
{
	int						CompressedPos;
	int						CompressedSize;
	byte					Flags;					// LEAD_PKG_COMPRESS_...
	unsigned				CompressedCrc;
	unsigned				UncompressedCrc;
};


class FLeadArchiveReader : public FArchive
{
public:
	FArchive				*Reader;
	// compression parameters
	int						BufferSize;
	int						FileSize;
	int						DirectoryOffset;
	byte					Flags;					// LEAD_PKG_...
	TArray<FLeadArcPage>	Pages;
	// own file positions, overriding FArchive's one (because parent class is
	// used for compressed data)
	int						Stopper;
	int						Position;
	// decompression buffer
	byte					*Buffer;
	int						BufferStart;
	int						BufferEnd;

	FLeadArchiveReader(FArchive *File)
	:	Reader(File)
	,	Buffer(NULL)
	,	BufferStart(0)
	,	BufferEnd(0)
	{
		guard(FLeadArchiveReader::FLeadArchiveReader);
		SetupFrom(*File);
		ReadArchiveHeaders();
		ArVer = 128;		// something UE2
		IsLoading = true;
		unguard;
	}

	~FLeadArchiveReader()
	{
		if (Buffer) delete Buffer;
		if (Reader) delete Reader;
	}

	void ReadArchiveHeaders()
	{
		guard(FLeadArchiveReader::ReadArchiveHeaders);
		*Reader << BufferSize << FileSize << DirectoryOffset << Flags;
		int DataStart = Reader->Tell();
		if (getenv("SC_CONV_UMD_DEBUG"))
			appPrintf("buf=%X file=%X dir=%X F=%X\n", BufferSize, FileSize, DirectoryOffset, Flags);
		if (Flags & LEAD_PKG_COMPRESS_TABLES)
		{
			int DataSkip, TableSizeUncompr, TableSizeCompr;
			*Reader << DataSkip << TableSizeUncompr << TableSizeCompr;
			byte* PageTableUncompr = new byte[TableSizeUncompr];
			if (getenv("SC_CONV_UMD_DEBUG"))
				appPrintf("compr tables: skip=%X uncompTblSize=%X compSize=%X\n", DataSkip, TableSizeUncompr, TableSizeCompr);
			if (TableSizeCompr)
			{
				byte* PageTableCompr = new byte[TableSizeCompr];
				Reader->Serialize(PageTableCompr, TableSizeCompr);
				int len = appDecompress(PageTableCompr, TableSizeCompr, PageTableUncompr, TableSizeUncompr, COMPRESS_ZLIB);
				assert(len == TableSizeUncompr);
				delete[] PageTableCompr;
			}
			else
			{
				Reader->Serialize(PageTableUncompr, TableSizeUncompr);
			}
			FMemReader Mem(PageTableUncompr, TableSizeUncompr);
			Mem.SetupFrom(*Reader);
			ReadPageTable(Mem, DataStart + DataSkip + 12, (Flags & LEAD_PKG_CHECK_CRC));
			assert(Mem.Tell() == TableSizeUncompr);
			delete[] PageTableUncompr;
		}
		else
		{
			appNotify("#1: LEAD package with uncompressed tables");	//!! UNTESTED
			Reader->Seek(DirectoryOffset - 4);
			ReadPageTable(*Reader, DataStart, (Flags & LEAD_PKG_CHECK_CRC));
		}
		unguard;
	}

	void ReadPageTable(FArchive& Ar, int DataOffset, bool checkCrc)
	{
		guard(FLeadArchiveReader::ReadPageTable);
		int NumPages = (FileSize + BufferSize - 1) / BufferSize;
		Pages.AddZeroed(NumPages);
		int Remaining = FileSize;
		int NumPages2;
		Ar << AR_INDEX(NumPages2);	// unused
		assert(NumPages == NumPages2);
		for (int i = 0; i < NumPages; i++)
		{
			FLeadArcPage& P = Pages[i];
			P.CompressedPos = DataOffset;
			Ar << AR_INDEX(P.CompressedSize) << P.Flags;
			if (checkCrc)
			{
				Ar << P.CompressedCrc << P.UncompressedCrc;
			}
			if (!P.CompressedSize)
				P.CompressedSize = min(Remaining, BufferSize);	// size of uncompressed page
			// advance pointers
			DataOffset += P.CompressedSize;
			Remaining  -= BufferSize;
		}
		unguard;
	}

	// this function is taken from FUE3ArchiveReader
	virtual void Serialize(void *data, int size)
	{
		guard(FLeadArchiveReader::Serialize);

		if (Stopper > 0 && Position + size > Stopper)
			appError("Serializing behind stopper (%X+%X > %X)", Position, size, Stopper);

		while (true)
		{
			// check for valid buffer
			if (Position >= BufferStart && Position < BufferEnd)
			{
				int ToCopy = BufferEnd - Position;						// available size
				if (ToCopy > size) ToCopy = size;						// shrink by required size
				memcpy(data, Buffer + Position - BufferStart, ToCopy);	// copy data
				// advance pointers/counters
				Position += ToCopy;
				size     -= ToCopy;
				data     = OffsetPointer(data, ToCopy);
				if (!size) return;										// copied enough
			}
			// here: data/size points outside of loaded Buffer
			PrepareBuffer(Position);
			assert(Position >= BufferStart && Position < BufferEnd);	// validate PrepareBuffer()
		}

		unguard;
	}

	void PrepareBuffer(int Pos)
	{
		guard(FLeadArchiveReader::PrepareBuffer);

		int Page = Pos / BufferSize;
		assert(Page >= 0 && Page < Pages.Num());
		const FLeadArcPage& P = Pages[Page];
		if (!Buffer) Buffer = new byte[BufferSize];

		// read page
		Reader->Seek(P.CompressedPos);
		int DstSize = (Page < Pages.Num() - 1) ? BufferSize : FileSize % BufferSize;
		int SrcSize = P.CompressedSize;
		if (!SrcSize) SrcSize = DstSize;

		byte* CompressedBuffer = new byte[SrcSize];
		Reader->Serialize(CompressedBuffer, SrcSize);

		if (!P.Flags)
		{
			assert(SrcSize <= BufferSize);
			memcpy(Buffer, CompressedBuffer, SrcSize);
		}
		else
		{
			assert(SrcSize == P.CompressedSize);
			assert(P.Flags & (LEAD_PKG_COMPRESS_ZLIB | LEAD_PKG_COMPRESS_XMEM));
			appDecompress(
				CompressedBuffer, SrcSize, Buffer, DstSize,
				(P.Flags & LEAD_PKG_COMPRESS_ZLIB) ? COMPRESS_ZLIB : COMPRESS_LZX
			);
		}

		delete[] CompressedBuffer;

		BufferStart = Page * BufferSize;
		BufferEnd   = BufferStart + BufferSize;

		unguard;
	}

	// position controller
	virtual void Seek(int Pos)
	{
		Position = Pos;
	}
	virtual int Tell() const
	{
		return Position;
	}
	virtual int GetFileSize() const
	{
		return FileSize;
	}
	virtual void SetStopper(int Pos)
	{
		Stopper = Pos;
	}
	virtual int  GetStopper() const
	{
		return Stopper;
	}
};


struct FLeadDirEntry
{
	int						id;						// file identifier (hash of filename?)
	// the following data are originally packed as separate structure
	FString					ShortFilename;			// short filename
	FString					Filename;				// full filename with path
	int						FileSize;

	friend FArchive& operator<<(FArchive &Ar, FLeadDirEntry &D)
	{
		return Ar << D.id << D.ShortFilename << D.Filename << AR_INDEX(D.FileSize);
	}
};


struct FLeadArcChunk								// file chunk
{
	int						OriginalOffset;			// offset in original file
	int						PackedOffset;			// position in uncompressed UMD (excluding aligned archive directory size!! align=0x20000)
	int						ChunkSize;				// length of the data

	friend FArchive& operator<<(FArchive &Ar, FLeadArcChunk &C)
	{
		return Ar << AR_INDEX(C.OriginalOffset) << AR_INDEX(C.PackedOffset) << AR_INDEX(C.ChunkSize);
	}
};


struct FLeadArcChunkList
{
	int						id;						// file identifier
	TArray<FLeadArcChunk>	chunks;

	friend FArchive& operator<<(FArchive &Ar, FLeadArcChunkList& L)
	{
		return Ar << L.id << L.chunks;
	}
};


struct FLeadArcHdr
{
	FString					id;						// package tag
	TArray<FLeadDirEntry>	dir;					// file list
	TArray<FLeadArcChunkList> chunkList;			// file chunk list

	friend FArchive& operator<<(FArchive &Ar, FLeadArcHdr &H)
	{
		return Ar << H.id << H.dir << H.chunkList;
	}
};


class FLeadUmdFile : public FLeadArchiveReader
{
public:
	FLeadArcHdr				Hdr;
	int						DataStart;

	FLeadUmdFile(FArchive *File)
	:	FLeadArchiveReader(File)
	{
		// load embedded data
		Seek(0);
		*(FArchive*)this << Hdr;
		DataStart = Align(Tell(), 0x20000);		//?? align with BlockSize?
	}

	void PrintHeaders()
	{
		//!! dump embedded data
		appPrintf("DataStart: %08X\n", DataStart);
		int i;
		appPrintf("ID: %s\n", *Hdr.id);
		for (i = 0; i < Hdr.dir.Num(); i++)
		{
			const FLeadDirEntry& Dir = Hdr.dir[i];
			appPrintf("[%d] %08X - (%s) %s / %d (%08X)\n", i, Dir.id, *Dir.ShortFilename, *Dir.Filename, Dir.FileSize, Dir.FileSize);
		}
		for (i = 0; i < Hdr.chunkList.Num(); i++)
		{
			const FLeadArcChunkList& List = Hdr.chunkList[i];
			appPrintf("[%d] %08X (%d)\n", i, List.id, List.chunks.Num());
			for (int j = 0; j < List.chunks.Num(); j++)
			{
				const FLeadArcChunk &Chunk = List.chunks[j];
				appPrintf("       %08X  %08X  %08X\n", Chunk.OriginalOffset, Chunk.PackedOffset, Chunk.ChunkSize);
			}
		}
	}

	bool LoadFileBytes(const char *RelativeName, int Offset, int Size, byte *OutData)
	{
		guard(FLeadUmdFile::LoadFileBytes);

		char WantName[512];
		appStrncpyz(WantName, RelativeName, ARRAY_COUNT(WantName));
		for (char *s = WantName; *s; s++)
			if (*s == '\\') *s = '/';

		int FileId = 0;
		for (int i = 0; i < Hdr.dir.Num(); i++)
		{
			char HaveName[512];
			appStrncpyz(HaveName, *Hdr.dir[i].Filename, ARRAY_COUNT(HaveName));
			for (char *s = HaveName; *s; s++)
				if (*s == '\\') *s = '/';
			if (!stricmp(HaveName, WantName))
			{
				FileId = Hdr.dir[i].id;
				break;
			}
		}
		if (!FileId)
			return false;

		bool bCopiedAny = false;
		const int EndOffset = Offset + Size;
		if (getenv("SC_CONV_TEX_DEBUG"))
			appPrintf("SCConv UMD file hit: %s offset=%08X size=%X id=%08X\n", RelativeName, Offset, Size, FileId);
		for (int i = 0; i < Hdr.chunkList.Num(); i++)
		{
			const FLeadArcChunkList &List = Hdr.chunkList[i];
			if (List.id != FileId)
				continue;

			for (int j = 0; j < List.chunks.Num(); j++)
			{
				const FLeadArcChunk &Chunk = List.chunks[j];
				const int ChunkStart = Chunk.OriginalOffset;
				const int ChunkEnd = Chunk.OriginalOffset + Chunk.ChunkSize;
				if (ChunkEnd <= Offset || ChunkStart >= EndOffset)
					continue;

				const int CopyStart = max(Offset, ChunkStart);
				const int CopyEnd = min(EndOffset, ChunkEnd);
				const int CopySize = CopyEnd - CopyStart;
				const int SkipInChunk = CopyStart - ChunkStart;

				Seek(DataStart + Chunk.PackedOffset + SkipInChunk);
				Serialize(OutData + CopyStart - Offset, CopySize);
				for (int k = 0; k < CopySize; k++)
					OutData[CopyStart - Offset + k] ^= 0xB7;
				if (getenv("SC_CONV_TEX_DEBUG"))
					appPrintf("SCConv UMD chunk: %08X-%08X copied %08X-%08X\n", ChunkStart, ChunkEnd, CopyStart, CopyEnd);
				bCopiedAny = true;
			}
		}

		return bCopiedAny;

		unguard;
	}

	bool LoadFileBytesByDirIndex(int DirIndex, int Offset, int Size, byte *OutData)
	{
		guard(FLeadUmdFile::LoadFileBytesByDirIndex);

		if (!OutData || DirIndex < 0 || DirIndex >= Hdr.dir.Num() || Offset < 0 || Size < 0)
			return false;

		memset(OutData, 0, Size);
		const int FileSize = Hdr.dir[DirIndex].FileSize;
		if (Offset >= FileSize)
			return true;
		if (Offset + Size > FileSize)
			Size = FileSize - Offset;

		const int FileId = Hdr.dir[DirIndex].id;
		bool bCopiedAny = false;
		const int EndOffset = Offset + Size;

		for (int i = 0; i < Hdr.chunkList.Num(); i++)
		{
			const FLeadArcChunkList &List = Hdr.chunkList[i];
			if (List.id != FileId)
				continue;

			for (int j = 0; j < List.chunks.Num(); j++)
			{
				const FLeadArcChunk &Chunk = List.chunks[j];
				const int ChunkStart = Chunk.OriginalOffset;
				const int ChunkEnd = Chunk.OriginalOffset + Chunk.ChunkSize;
				if (ChunkEnd <= Offset || ChunkStart >= EndOffset)
					continue;

				const int CopyStart = max(Offset, ChunkStart);
				const int CopyEnd = min(EndOffset, ChunkEnd);
				const int CopySize = CopyEnd - CopyStart;
				const int SkipInChunk = CopyStart - ChunkStart;

				Seek(DataStart + Chunk.PackedOffset + SkipInChunk);
				Serialize(OutData + CopyStart - Offset, CopySize);
				for (int k = 0; k < CopySize; k++)
					OutData[CopyStart - Offset + k] ^= 0xB7;
				bCopiedAny = true;
			}
		}

		return bCopiedAny;

		unguard;
	}

	bool HasChunkAtDirOffset(int DirIndex, int Offset) const
	{
		guard(FLeadUmdFile::HasChunkAtDirOffset);

		if (DirIndex < 0 || DirIndex >= Hdr.dir.Num())
			return false;

		const int FileId = Hdr.dir[DirIndex].id;
		for (int i = 0; i < Hdr.chunkList.Num(); i++)
		{
			const FLeadArcChunkList &List = Hdr.chunkList[i];
			if (List.id != FileId)
				continue;
			for (int j = 0; j < List.chunks.Num(); j++)
			{
				const FLeadArcChunk &Chunk = List.chunks[j];
				if (Offset >= Chunk.OriginalOffset && Offset < Chunk.OriginalOffset + Chunk.ChunkSize)
					return true;
			}
		}

		return false;

		unguard;
	}

	// test function
	void SaveEmbeddedFile(const char *FileName)
	{
		appPrintf("Extracting ...\n");
		byte* mem = new byte[FileSize];
		Seek(0);
		Serialize(mem, FileSize);
		for (int i = DataStart; i < FileSize; i++)	// data XOR-ed from DataStart offset
			mem[i] ^= 0xB7;
		appMakeDirectoryForFile(FileName);
		FArchive *OutAr = new FFileWriter(FileName);
		OutAr->Serialize(mem, FileSize);
		delete[] mem;
		delete OutAr;
		appPrintf("Extraction done.\n");
	}


	bool Extract(const char *OutDir)
	{
		guard(FLeadUmdFile::Extract);

		// extra bytes to allow oversize (bug in LEAD engine?)
#define ALLOW_EXTRA_BYTES	1
//		PrintHeaders();

		// precache all file contents
		byte *Data = new byte[FileSize + ALLOW_EXTRA_BYTES];
		guard(Precache);
		Seek(DataStart);
		Serialize(Data, FileSize - DataStart);
		for (int i = 0; i < FileSize - DataStart; i++)	// data XOR-ed from DataStart offset
			Data[i] ^= 0xB7;
		unguard;

		int i, j;
		for (i = 0; i < Hdr.chunkList.Num(); i++)
		{
			const FLeadArcChunkList& List = Hdr.chunkList[i];

			// find file name for this chunk list
			const char *FileName = NULL;
			guard(GetFilename);
			for (j = 0; j < Hdr.dir.Num(); j++)
			{
				const FLeadDirEntry& Dir = Hdr.dir[j];
				if (Dir.id == List.id)
				{
					FileName = *Dir.Filename;
					break;
				}
			}
			unguard;
			if (!FileName)
			{
				// should be found
//				PrintHeaders();
				appError("Chunk #%d: unable to find file for id %08X", i, List.id);
			}
			appPrintf("... %s\n", FileName);

			// open a file
			char FullFileName[512];
			appSprintf(ARRAY_ARG(FullFileName), "%s/%s", OutDir, FileName);
			appMakeDirectoryForFile(FullFileName);
			FILE *f = fopen(FullFileName, "rb+");				// open for update
			if (!f) f = fopen(FullFileName, "wb");				// open for writing
			if (!f)
			{
				appPrintf("ERROR: unable to open file \"%s\"\n", FullFileName);
				continue;
			}

			// extract all chunks in a list
			guard(ExtractChunks);
			for (j = 0; j < List.chunks.Num(); j++)
			{
				const FLeadArcChunk &Chunk = List.chunks[j];
				fseek(f, Chunk.OriginalOffset, SEEK_SET);
//				appPrintf("PackedOffset=%08X  ChunkSize=%08X  FileSize=%08X  DataStart=%08X\n", Chunk.PackedOffset, Chunk.ChunkSize, FileSize, DataStart);
				assert(Chunk.PackedOffset + Chunk.ChunkSize <= FileSize - DataStart + ALLOW_EXTRA_BYTES);
				fwrite(Data + Chunk.PackedOffset, Chunk.ChunkSize, 1, f);
			}
			unguardf("list %d/%d, chunk %d/%d", i, Hdr.chunkList.Num(), j, List.chunks.Num());

			fclose(f);
		}

		delete[] Data;

		return true;

		unguard;
	}
};


class FLeadUmdInnerFile : public FArchive
{
	DECLARE_ARCHIVE(FLeadUmdInnerFile, FArchive);
public:
	FLeadUmdInnerFile(class FLeadUmdVFS *InOwner, int InDirIndex);
	virtual void Seek(int Pos);
	virtual bool IsEof() const;
	virtual void Serialize(void *data, int size);
	virtual int GetFileSize() const;

protected:
	FLeadUmdFile* GetUmd() const;

	FLeadUmdVFS		*Owner;
	int				DirIndex;
};


class FLeadUmdVFS : public FVirtualFileSystem
{
public:
	FLeadUmdVFS(const char *InFilename)
	:	Umd(NULL)
	,	Ass(NULL)
	,	Filename(InFilename)
	{}

	virtual ~FLeadUmdVFS()
	{
		delete Ass;
		delete Umd;
	}

	virtual bool AttachReader(FArchive *reader, FString& error)
	{
		guard(FLeadUmdVFS::AttachReader);

		FArchive *Archive = CreateUMDReader(reader);
		if (!Archive)
		{
			error = "Not a LEAD UMD archive";
			return false;
		}

		Umd = (FLeadUmdFile*)Archive;

		if (ShouldLeadUseAssStreams())
		{
			char AssName[MAX_PACKAGE_PATH];
			appStrncpyz(AssName, *Filename, ARRAY_COUNT(AssName));
			ReplaceExtension(ARRAY_ARG(AssName), ".ass");
			FArchive *AssDiskFile = new FFileReader(AssName, EFileArchiveOptions::NoOpenError);
			if (AssDiskFile->IsOpen())
			{
				int Tag = 0;
				*AssDiskFile << Tag;
				byte AssFlags = 0;
				AssDiskFile->Seek(16);
				*AssDiskFile << AssFlags;
				if (Tag == LEAD_FILE_TAG && (AssFlags & LEAD_PKG_COMPRESS_TABLES))
				{
					AssDiskFile->Seek(4);
					Ass = new FLeadArchiveReader(AssDiskFile);
				}
				else
				{
					delete AssDiskFile;
				}
			}
			else
			{
				delete AssDiskFile;
			}
		}

		Reserve(Umd->Hdr.dir.Num());

		for (int i = 0; i < Umd->Hdr.dir.Num(); i++)
		{
			const FLeadDirEntry& Dir = Umd->Hdr.dir[i];
			if (!Dir.Filename.Len() || Dir.FileSize <= 0)
				continue;
			if (!Umd->HasChunkAtDirOffset(i, 0))
				continue;

			FStaticString<MAX_PACKAGE_PATH> FileName;
			FileName = *Dir.Filename;
			for (char& c : FileName.GetDataArray())
				if (c == '\\') c = '/';

			CRegisterFileInfo reg;
			reg.Filename = *FileName;
			reg.Size = Dir.FileSize;
			reg.IndexInArchive = i;
			RegisterFile(reg);
		}

		return true;

		unguardf("%s", *Filename);
	}

	virtual FArchive* CreateReader(int index)
	{
		guard(FLeadUmdVFS::CreateReader);
		if (!Umd || index < 0 || index >= Umd->Hdr.dir.Num())
			return NULL;
		return new FLeadUmdInnerFile(this, index);
		unguard;
	}

	virtual bool LoadStreamBytes(int index, int offset, int size, byte *data)
	{
		guard(FLeadUmdVFS::LoadStreamBytes);
		if (!Ass || index < 0 || index >= Umd->Hdr.dir.Num() || offset < 0 || size <= 0)
			return false;
		if (offset + size > Ass->GetFileSize())
			return false;

		Ass->Seek(offset);
		Ass->Serialize(data, size);
		if (!getenv("SC_CONV_ASS_NO_XOR"))
		{
			for (int i = 0; i < size; i++)
				data[i] ^= 0xB7;
		}
		return true;

		unguard;
	}

	bool LoadBytes(int DirIndex, int Offset, int Size, byte *Data)
	{
		guard(FLeadUmdVFS::LoadBytes);
		memset(Data, 0, Size);
		return Umd->LoadFileBytesByDirIndex(DirIndex, Offset, Size, Data);
		unguard;
	}

	FLeadUmdFile* GetUmd() const
	{
		return Umd;
	}

protected:
	FLeadUmdFile	*Umd;
	FLeadArchiveReader *Ass;
	FString			Filename;
};

static bool ShouldLeadUseAssStreams()
{
	return getenv("SC_CONV_ENABLE_ASS_SEARCH") != NULL;
}


FLeadUmdFile* FLeadUmdInnerFile::GetUmd() const
{
	return Owner->GetUmd();
}

FLeadUmdInnerFile::FLeadUmdInnerFile(FLeadUmdVFS *InOwner, int InDirIndex)
:	Owner(InOwner)
,	DirIndex(InDirIndex)
{
	IsLoading = true;
	ArStopper = GetFileSize();
}

void FLeadUmdInnerFile::Seek(int Pos)
{
	guard(FLeadUmdInnerFile::Seek);
	assert(Pos >= 0 && Pos <= GetFileSize());
	ArPos = Pos;
	unguard;
}

bool FLeadUmdInnerFile::IsEof() const
{
	return ArPos >= GetFileSize();
}

void FLeadUmdInnerFile::Serialize(void *data, int size)
{
	guard(FLeadUmdInnerFile::Serialize);
	if (ArStopper > 0 && ArPos + size > ArStopper)
		appError("Serializing behind stopper (%X+%X > %X)", ArPos, size, ArStopper);
	if (ArPos + size > GetFileSize())
		appError("Serializing behind end of UMD file");
	if (!Owner->LoadBytes(DirIndex, ArPos, size, (byte*)data))
		memset(data, 0, size);
	ArPos += size;
	unguard;
}

int FLeadUmdInnerFile::GetFileSize() const
{
	return GetUmd()->Hdr.dir[DirIndex].FileSize;
}


/*-----------------------------------------------------------------------------
	External interface functions (CHANGE!! MOVE FLeadUmdFile to .h)
-----------------------------------------------------------------------------*/

FArchive* CreateUMDReader(FArchive *File)
{
	guard(CreateUMDReader);

	int Tag;
	*File << Tag;
	if (Tag != LEAD_FILE_TAG) return NULL;

	FLeadUmdFile *Ar = new FLeadUmdFile(File);	// note: File points to offset 4
//	Ar->PrintHeaders();
	return Ar;

	unguard;
}


FVirtualFileSystem* CreateLeadUmdVFS(const char *Filename)
{
	guard(CreateLeadUmdVFS);
	return new FLeadUmdVFS(Filename);
	unguard;
}


bool ExtractUMDArchive(FArchive *UmdFile, const char *OutDir)
{
	guard(ExtractUMDArchive);

	FLeadUmdFile *File = (FLeadUmdFile*)UmdFile;
	return File->Extract(OutDir);

	unguard;
}


void SaveUMDArchive(FArchive *UmdFile, const char *OutName)
{
	guard(SaveUMDArchive);

	FLeadUmdFile *File = (FLeadUmdFile*)UmdFile;
	return File->SaveEmbeddedFile(OutName);

	unguard;
}


bool LoadLeadVfsFileBytes(const CGameFileInfo *FileInfo, int Offset, int Size, byte *Data)
{
	guard(LoadLeadVfsFileBytes);
	if (!FileInfo || !FileInfo->FileSystem || Size <= 0 || !Data)
		return false;
	return FileInfo->FileSystem->LoadStreamBytes(FileInfo->IndexInVfs, Offset, Size, Data);
	unguard;
}


static bool IsPathSeparator(char c)
{
	return c == '/' || c == '\\';
}


static void GetLeadUmdSearchDir(char *OutDir, int OutSize)
{
	guard(GetLeadUmdSearchDir);

	const char *Root = appGetRootDirectory();
	if (!Root)
	{
		OutDir[0] = 0;
		return;
	}

	appStrncpyz(OutDir, Root, OutSize);
	int Len = strlen(OutDir);
	while (Len > 0 && IsPathSeparator(OutDir[Len - 1]))
		OutDir[--Len] = 0;

	const char *LastSep = strrchr(OutDir, '/');
	const char *LastSep2 = strrchr(OutDir, '\\');
	if (!LastSep || (LastSep2 && LastSep2 > LastSep))
		LastSep = LastSep2;
	if (LastSep && !stricmp(LastSep + 1, "unpacked"))
		OutDir[LastSep - OutDir] = 0;

	unguard;
}


static bool TryLoadLeadPackageBytesFromFile(const char *UmdName, const char *RelativeName, int Offset, int Size, byte *Data)
{
	guard(TryLoadLeadPackageBytesFromFile);

	FArchive *DiskFile = new FFileReader(UmdName, EFileArchiveOptions::NoOpenError);
	if (!DiskFile->IsOpen())
	{
		delete DiskFile;
		return false;
	}

	FArchive *UmdFile = CreateUMDReader(DiskFile);
	if (!UmdFile)
	{
		delete DiskFile;
		return false;
	}

	bool bOk = ((FLeadUmdFile*)UmdFile)->LoadFileBytes(RelativeName, Offset, Size, Data);
	delete UmdFile;
	return bOk;

	unguard;
}


static bool TryLoadLeadAssBytesFromFile(const char *AssName, int Offset, int Size, byte *Data)
{
	guard(TryLoadLeadAssBytesFromFile);

	FArchive *DiskFile = new FFileReader(AssName, EFileArchiveOptions::NoOpenError);
	if (!DiskFile->IsOpen())
	{
		delete DiskFile;
		return false;
	}

	byte AssFlags = 0;
	DiskFile->Seek(16);
	*DiskFile << AssFlags;
	if (!(AssFlags & LEAD_PKG_COMPRESS_TABLES))
	{
		delete DiskFile;
		return false;
	}

	DiskFile->Seek(4);
	FLeadArchiveReader *AssFile = new FLeadArchiveReader(DiskFile);
	bool bOk = false;
	if (Offset >= 0 && Offset + Size <= AssFile->GetFileSize())
	{
		AssFile->Seek(Offset);
		AssFile->Serialize(Data, Size);
		for (int i = 0; i < Size; i++)
			Data[i] ^= 0xB7;
		bOk = true;
	}
	delete AssFile;
	return bOk;

	unguard;
}


static void ReplaceExtension(char *Filename, int FilenameSize, const char *NewExt)
{
	char *Dot = strrchr(Filename, '.');
	if (!Dot)
		Dot = Filename + strlen(Filename);
	appStrncpyz(Dot, NewExt, FilenameSize - (Dot - Filename));
}


bool LoadLeadUmdFileBytes(const char *RelativeName, int Offset, int Size, byte *Data)
{
	guard(LoadLeadUmdFileBytes);
	if (!ShouldLeadUseAssStreams())
		return false;

	char UmdDir[512];
	GetLeadUmdSearchDir(ARRAY_ARG(UmdDir));
	if (!UmdDir[0])
		return false;

	bool bLoadedPackage = false;
	bool bLoadedStream = false;
	const bool bUseAssStreams = ShouldLeadUseAssStreams();

#if _WIN32
	char Pattern[512];
	appSprintf(ARRAY_ARG(Pattern), "%s/*.umd", UmdDir);
	_finddatai64_t Found;
	intptr_t Find = _findfirsti64(Pattern, &Found);
	if (Find != -1)
	{
		do
		{
			char UmdName[512];
			appSprintf(ARRAY_ARG(UmdName), "%s/%s", UmdDir, Found.name);
			if (TryLoadLeadPackageBytesFromFile(UmdName, RelativeName, Offset, Size, Data))
			{
				bLoadedPackage = true;
				if (bUseAssStreams)
				{
					char AssName[512];
					appStrncpyz(AssName, UmdName, ARRAY_COUNT(AssName));
					ReplaceExtension(ARRAY_ARG(AssName), ".ass");
					if (TryLoadLeadAssBytesFromFile(AssName, Offset, Size, Data))
						bLoadedStream = true;
				}
			}
		} while (_findnexti64(Find, &Found) != -1);
		_findclose(Find);
	}

	if (bUseAssStreams && !bLoadedStream)
	{
		appSprintf(ARRAY_ARG(Pattern), "%s/*.ass", UmdDir);
		Find = _findfirsti64(Pattern, &Found);
		if (Find != -1)
		{
			do
			{
				char AssName[512];
				appSprintf(ARRAY_ARG(AssName), "%s/%s", UmdDir, Found.name);
				if (TryLoadLeadAssBytesFromFile(AssName, Offset, Size, Data))
					bLoadedStream = true;
			} while (_findnexti64(Find, &Found) != -1);
			_findclose(Find);
		}
	}
#endif

	return bLoadedPackage || bLoadedStream;

	unguardf("%s", RelativeName);
}


#endif // LEAD
