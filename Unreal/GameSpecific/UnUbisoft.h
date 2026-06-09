#ifndef __UNUBISOFT_H__
#define __UNUBISOFT_H__

#if SPLINTER_CELL

FVirtualFileSystem* CreateScdaLinVFS(const char *Filename);
bool LoadScdaLinTextureStream(const char *TextureName, TArray<byte>& Data, int& GpuSize, int& TextureId,
	int& USize, int& VSize, int& FormatCode);
bool GetScdaLinTextureStreamInfo(const char *TextureName, int& USize, int& VSize, int& FormatCode);

#endif // SPLINTER_CELL

#if LEAD


class FLeadUmdFile;


FArchive* CreateUMDReader(FArchive *File);
FVirtualFileSystem* CreateLeadUmdVFS(const char *Filename);
bool ExtractUMDArchive(FArchive *UmdFile, const char *OutDir);
void SaveUMDArchive(FArchive *UmdFile, const char *OutName);
bool LoadLeadVfsFileBytes(const CGameFileInfo *FileInfo, int Offset, int Size, byte *Data);
bool LoadLeadUmdFileBytes(const char *RelativeName, int Offset, int Size, byte *Data);


#endif // LEAD

#endif // __UNUBISOFT_H__
