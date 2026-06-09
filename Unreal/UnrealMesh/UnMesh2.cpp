#include "Core.h"
#include "UnCore.h"
#include "UnObject.h"
#include "UnMesh2.h"
#include "UnMeshTypes.h"
#include "UnrealPackage/UnPackage.h"

#include "UnrealMaterial/UnMaterial.h"
#include "UnrealMaterial/UnMaterial2.h" // for UMaterial* serialization

#include "Mesh/SkeletalMesh.h"
#include "Mesh/StaticMesh.h"
#include "TypeConvert.h"

//#define DEBUG_SKELMESH		1
//#define DEBUG_STATICMESH		1

/*-----------------------------------------------------------------------------
	ULodMesh class
-----------------------------------------------------------------------------*/

#if LOCO

struct FLocoUnk1
{
	FName		f0;
	int			f4;

	friend FArchive& operator<<(FArchive &Ar, FLocoUnk1 &V)
	{
		return Ar << V.f0 << V.f4;
	}
};

struct FLocoUnk2
{
	FString		f0;
	FName		f1;
	FVector		f2;
	FRotator	f3;
	int			f4, f5;
	float		f6;
	FVector		f7;
	int			f8, f9;

	friend FArchive& operator<<(FArchive &Ar, FLocoUnk2 &V)
	{
		return Ar << V.f0 << V.f1 << V.f2 << V.f3 << V.f4 << V.f5 << V.f6 << V.f7 << V.f8 << V.f9;
	}
};

#endif // LOCO

#if VANGUARD

struct FVanguardSkin
{
	TArray<UMaterial*> Textures;
	FName		Name;

	friend FArchive& operator<<(FArchive &Ar, FVanguardSkin &S)
	{
		return Ar << S.Textures << S.Name;
	}
};

#endif // VANGUARD

void ULodMesh::Serialize(FArchive &Ar)
{
	guard(ULodMesh::Serialize);
	Super::Serialize(Ar);

	Ar << Version << VertexCount;
	const bool isPandoraTomorrowOnline = (Ar.Game == GAME_SplinterCell && (Ar.ArVer == 164 || Ar.ArVer == 171 || Ar.ArVer == 172) && Ar.ArLicenseeVer == 0);
	const bool isDoubleAgentOnline = (Ar.Game == GAME_SplinterCell && Ar.ArVer >= 173 && Ar.ArVer <= 275 && Ar.ArLicenseeVer == 0);
	const bool debugDoubleAgent = isDoubleAgentOnline && getenv("SC4_DEBUG_MESH");
	if (debugDoubleAgent)
		appPrintf("SC4 ULodMesh start %s pos=%08X version=%d vertexCount=%d\n", Name, Ar.Tell(), Version, VertexCount);

#if LINEAGE2
	if (Ar.Game == GAME_Lineage2 && Ar.ArVer >= 133)
	{
		TArray<FVector> NewVerts;
		Ar << NewVerts;
		int Count = NewVerts.Num();
		Verts.AddUninitialized(Count);
		// Convert FVector to FMeshVert (with losing precision)
		for (int i = 0; i < Count; i++)
		{
			FMeshVert& V = Verts[i];
			const FVector& SV = NewVerts[i];
			V.X = SV.X;
			V.Y = SV.Y;
			V.Z = SV.Z;
		}
		goto post_verts;
	}
#endif // LINEAGE2

	Ar << Verts;
	if (debugDoubleAgent)
		appPrintf("SC4 after Verts pos=%08X verts=%d\n", Ar.Tell(), Verts.Num());
post_verts:

	if (Version <= 1 || (Ar.Game == GAME_SplinterCell && !isPandoraTomorrowOnline && !isDoubleAgentOnline))
	{
		// skip FMeshTri2 section
		TArray<FMeshTri2> tmp;
		Ar << tmp;
		if (debugDoubleAgent)
			appPrintf("SC4 after FMeshTri2 pos=%08X tmp=%d\n", Ar.Tell(), tmp.Num());
	}

#if VANGUARD
	if (Ar.Game == GAME_Vanguard && Ar.ArLicenseeVer >= 9)
	{
		TArray<FVanguardSkin> Skins;
		int unk74;
		Ar << Skins << unk74;
		if (Skins.Num())
			CopyArray(Textures, Skins[0].Textures);
		goto after_textures;
	}
#endif // VANGUARD
	if (Ar.Game == GAME_SplinterCell && Ar.ArVer == 100 && Ar.ArLicenseeVer == 124 && getenv("SCCT_SKIP_MATERIAL_IMPORTS"))
	{
		int NumTextures;
		Ar << AR_INDEX(NumTextures);
		Textures.Empty(NumTextures);
		Textures.AddZeroed(NumTextures);
		for (int i = 0; i < NumTextures; i++)
		{
			int TextureRef;
			Ar << AR_INDEX(TextureRef);
		}
	}
	else
	{
		Ar << Textures;
	}
	if (debugDoubleAgent)
		appPrintf("SC4 after Textures pos=%08X textures=%d\n", Ar.Tell(), Textures.Num());
after_textures:

#if DEBUG_SKELMESH
	for (int i = 0; i < Textures.Num(); i++) appPrintf("Tex[%d] = %s\n", i, Textures[i] ? Textures[i]->Name : "None");
#endif

#if SPLINTER_CELL
	if (Ar.Game == GAME_SplinterCell && Version >= 3 && !isPandoraTomorrowOnline && !isDoubleAgentOnline)
	{
		if (Ar.ArVer == 100 && Ar.ArLicenseeVer == 124 && getenv("SCCT_SKIP_MATERIAL_IMPORTS"))
		{
			int NumObjects;
			Ar << AR_INDEX(NumObjects);
			for (int i = 0; i < NumObjects; i++)
			{
				int ObjectRef;
				Ar << AR_INDEX(ObjectRef);
			}
		}
		else
		{
			TArray<UObject*> unk80;
			Ar << unk80;
		}
	}
#endif // SPLINTER_CELL
#if LOCO
	if (Ar.Game == GAME_Loco)
	{
		int               unk7C;
		TArray<FName>     unk80;
		TArray<FLocoUnk2> unk8C;
		TArray<FLocoUnk1> unk98;
		if (Version >= 5) Ar << unk98;
		if (Version >= 6) Ar << unk80;
		if (Version >= 7) Ar << unk8C << unk7C;
	}
#endif // LOCO
	Ar << MeshScale << MeshOrigin << RotOrigin;

#if DEBUG_SKELMESH
	appPrintf("Scale: %g %g %g\nOrigin: %g %g %g\nRotation: %d %d %d\n", VECTOR_ARG(MeshScale), VECTOR_ARG(MeshOrigin), FROTATOR_ARG(RotOrigin));
#endif

	if (Version <= 1 || (Ar.Game == GAME_SplinterCell && !isPandoraTomorrowOnline && !isDoubleAgentOnline))
	{
		// skip 2nd obsolete section
		TArray<uint16> tmp;
		Ar << tmp;
	}

	Ar << FaceLevel << Faces << CollapseWedgeThus << Wedges << Materials;
	Ar << MeshScaleMax;
	if (debugDoubleAgent)
		appPrintf("SC4 base buffers pos=%08X faceLevel=%d faces=%d collapse=%d wedges=%d materials=%d\n",
			Ar.Tell(), FaceLevel.Num(), Faces.Num(), CollapseWedgeThus.Num(), Wedges.Num(), Materials.Num());

#if EOS
	if (Ar.Game == GAME_EOS && Ar.ArLicenseeVer >= 42) goto lod_fields2;
#endif
	Ar << LODHysteresis;
lod_fields2:
	Ar << LODStrength << LODMinVerts << LODMorph << LODZDisplace;

#if SPLINTER_CELL
	if (Ar.Game == GAME_SplinterCell && !isPandoraTomorrowOnline) return;
#endif

	if (Version >= 3)
	{
		Ar << HasImpostor << SpriteMaterial;
		Ar << ImpLocation << ImpRotation << ImpScale << ImpColor;
		Ar << ImpSpaceMode << ImpDrawMode << ImpLightMode;
	}

	if (Version >= 4)
	{
		Ar << SkinTesselationFactor;
	}
#if LINEAGE2
	if (Ar.Game == GAME_Lineage2 && Version >= 5)
	{
		int unk;
		Ar << unk;
		// From forum discussion: version 6 has 1 more byte here
		if (Version >= 6)
			Ar.Seek(Ar.Tell()+1);
	}
#endif // LINEAGE2
#if BATTLE_TERR
	if (Ar.Game == GAME_BattleTerr && Version >= 5)
	{
		TArray<int> unk;
		Ar << unk;
	}
#endif // BATTLE_TERR
#if SWRC
	if (Ar.Game == GAME_RepCommando && Version >= 7)
	{
		int unkD4, unkD8, unkDC, unkE0;
		Ar << unkD4 << unkD8 << unkDC << unkE0;
	}
#endif // SWRC

	unguard;
}


/*-----------------------------------------------------------------------------
	UVertMesh class
-----------------------------------------------------------------------------*/

void UVertMesh::BuildNormals()
{
	// UE1 meshes have no stored normals, should build them
	// This function is similar to BuildNormals() from SkelMeshInstance.cpp
	int numVerts = Verts.Num();
	int i;
	Normals.Empty(numVerts);
	Normals.AddZeroed(numVerts);
	TArray<CVec3> tmpVerts, tmpNormals;
	tmpVerts.AddZeroed(numVerts);
	tmpNormals.AddZeroed(numVerts);
	// convert verts
	for (i = 0; i < numVerts; i++)
	{
		const FMeshVert &SV = Verts[i];
		CVec3           &DV = tmpVerts[i];
		DV[0] = SV.X * MeshScale.X;
		DV[1] = SV.Y * MeshScale.Y;
		DV[2] = SV.Z * MeshScale.Z;
	}
	// iterate faces
	for (i = 0; i < Faces.Num(); i++)
	{
		const FMeshFace &F = Faces[i];
		// get vertex indices
		int i1 = Wedges[F.iWedge[0]].iVertex;
		int i2 = Wedges[F.iWedge[2]].iVertex;		// note: reverse order in comparison with SkeletalMesh
		int i3 = Wedges[F.iWedge[1]].iVertex;
		// iterate all frames
		for (int j = 0; j < FrameCount; j++)
		{
			int base = VertexCount * j;
			// compute edges
			const CVec3 &V1 = tmpVerts[base + i1];
			const CVec3 &V2 = tmpVerts[base + i2];
			const CVec3 &V3 = tmpVerts[base + i3];
			CVec3 D1, D2, D3;
			VectorSubtract(V2, V1, D1);
			VectorSubtract(V3, V2, D2);
			VectorSubtract(V1, V3, D3);
			// compute normal
			CVec3 norm;
			cross(D2, D1, norm);
			norm.Normalize();
			// compute angles
			D1.Normalize();
			D2.Normalize();
			D3.Normalize();
			float angle1 = acos(-dot(D1, D3));
			float angle2 = acos(-dot(D1, D2));
			float angle3 = acos(-dot(D2, D3));
			// add normals for triangle verts
			VectorMA(tmpNormals[base + i1], angle1, norm);
			VectorMA(tmpNormals[base + i2], angle2, norm);
			VectorMA(tmpNormals[base + i3], angle3, norm);
		}
	}
	// normalize and convert computed normals
	for (i = 0; i < numVerts; i++)
	{
		CVec3 &SN     = tmpNormals[i];
		FMeshNorm &DN = Normals[i];
		SN.Normalize();
		DN.X = appRound(SN[0] * 511 + 512);
		DN.Y = appRound(SN[1] * 511 + 512);
		DN.Z = appRound(SN[2] * 511 + 512);
	}
}


void UVertMesh::Serialize(FArchive &Ar)
{
	guard(UVertMesh::Serialize);

#if UNREAL1
	if (Ar.Engine() == GAME_UE1)
	{
		SerializeVertMesh1(Ar);
		RotOrigin.Roll = -RotOrigin.Roll;	//??
		return;
	}
#endif // UNREAL1

	Super::Serialize(Ar);
	RotOrigin.Roll = -RotOrigin.Roll;		//??

	Ar << AnimMeshVerts << StreamVersion; // FAnimMeshVertexStream: may skip this (simply seek archive)
	Ar << Verts2 << f150;
	Ar << AnimSeqs << Normals;
	Ar << VertexCount << FrameCount;
	Ar << BoundingBoxes << BoundingSpheres;

	unguard;
}


/*-----------------------------------------------------------------------------
	USkeletalMesh class
-----------------------------------------------------------------------------*/

// Implement constructor in cpp to avoid inlining (it's large enough).
// It's useful to declare TArray<> structures as forward declarations in header file.
USkeletalMesh::USkeletalMesh()
{}


USkeletalMesh::~USkeletalMesh()
{
	delete ConvertedMesh;
}

#if LEAD

static bool IsReasonableSCConvPosition(const FVector &V)
{
	return V.X == V.X && V.Y == V.Y && V.Z == V.Z &&
		fabs(V.X) < 1000000.0f && fabs(V.Y) < 1000000.0f && fabs(V.Z) < 1000000.0f;
}

struct FSCConvLeadMeshLod
{
	int VertexHeader;
	int VertexStart;
	int VertexStride;
	int VertexBytes;
	int IndexHeader;
	int IndexCount;
};

static bool FindSCConvLeadMeshLod(FArchive &Ar, int Start, int Stop, FSCConvLeadMeshLod &Lod)
{
	guard(FindSCConvLeadMeshLod);

	memset(&Lod, 0, sizeof(Lod));
	Lod.VertexHeader = -1;
	Lod.IndexHeader = -1;

	for (int Pos = Start; Pos + 24 < Stop; Pos++)
	{
		int Stride, ByteSize;
		Ar.Seek(Pos);
		Ar << Stride << ByteSize;
		if (Stride != 40 || ByteSize <= 0 || (ByteSize % Stride) != 0)
			continue;
		int NumVerts = ByteSize / Stride;
		if (NumVerts <= 0 || NumVerts >= 65535 || Pos + 16 + ByteSize >= Stop)
			continue;
		Ar.Seek(Pos + 20);
		FVector P;
		Ar << P;
		if (!IsReasonableSCConvPosition(P))
			continue;
		Lod.VertexHeader = Pos;
		Lod.VertexStride = Stride;
		Lod.VertexBytes = ByteSize;
		break;
	}
	if (Lod.VertexHeader < 0)
		return false;

	Lod.VertexStart = Lod.VertexHeader + 16;
	const int NumVerts = Lod.VertexBytes / Lod.VertexStride;
	const int VertexEnd = Lod.VertexStart + Lod.VertexBytes;

	for (int Pos = VertexEnd; Pos + 64 < Stop; Pos++)
	{
		int ByteSize;
		Ar.Seek(Pos);
		Ar << ByteSize;
		if (ByteSize <= 0 || (ByteSize & 1) || ByteSize > 600000 || Pos + 16 + ByteSize > Stop)
			continue;
		int Count = ByteSize / 2;
		if (Count <= 0 || (Count % 3) != 0)
			continue;
		bool bGood = true;
		Ar.Seek(Pos + 16);
		for (int i = 0; i < Count; i++)
		{
			uint16 Index;
			Ar << Index;
			if (Index >= NumVerts)
			{
				bGood = false;
				break;
			}
		}
		if (!bGood)
			continue;
		Lod.IndexHeader = Pos;
		Lod.IndexCount = Count;
		return true;
	}

	return false;

	unguard;
}

static bool ReadSCConvLeadMeshPayload(USkeletalMesh &Mesh, FArchive &Ar, int Stop)
{
	guard(ReadSCConvLeadMeshPayload);

	// LeadMesh starts with normal UObject property data. Most mesh payloads seen so far
	// have no serialized properties here, i.e. the first FName is "None".
	int PayloadStart = Ar.Tell();
	FName FirstProp;
	Ar << FirstProp;
	if (stricmp(*FirstProp, "None"))
		Ar.Seek(PayloadStart);

	const int PositionOffset = 4;

	TArray<FSCConvLeadMeshLod> SourceLods;
	int SearchPos = Ar.Tell();
	while (SearchPos + 64 < Stop)
	{
		FSCConvLeadMeshLod Lod;
		if (!FindSCConvLeadMeshLod(Ar, SearchPos, Stop, Lod))
			break;
		SourceLods.Add(Lod);
		SearchPos = Lod.IndexHeader + 16 + Lod.IndexCount * 2;
	}
	if (!SourceLods.Num())
		return false;

	Mesh.MeshScale.Set(1, 1, 1);
	Mesh.MeshOrigin.Set(0, 0, 0);
	Mesh.RotOrigin.Set(0, 0, 0);

	Mesh.RefSkeleton.Empty(1);
	Mesh.RefSkeleton.AddZeroed(1);
	FMeshBone &RootBone = Mesh.RefSkeleton[0];
	RootBone.Name = "B";
	RootBone.Flags = 0;
	RootBone.BonePos.Orientation.Set(0, 0, 0, 1);
	RootBone.BonePos.Position.Set(0, 0, 0);
	RootBone.BonePos.Length = 0;
	RootBone.BonePos.Size.Set(1, 1, 1);
	RootBone.ParentIndex = 0;
	RootBone.NumChildren = 0;
	Mesh.SkeletalDepth = 1;

	delete Mesh.ConvertedMesh;
	Mesh.ConvertedMesh = new CSkeletalMesh(&Mesh);
	CSkeletalMesh *OutMesh = Mesh.ConvertedMesh;
	OutMesh->MeshScale.Set(1, 1, 1);
	OutMesh->MeshOrigin.Set(0, 0, 0);
	OutMesh->RotOrigin.Set(0, 0, 0);
	OutMesh->RefSkeleton.Empty(1);
	CSkelMeshBone *DstBone = new (OutMesh->RefSkeleton) CSkelMeshBone;
	DstBone->Name = RootBone.Name;
	DstBone->ParentIndex = RootBone.ParentIndex;
	DstBone->Position.Set(0, 0, 0);
	DstBone->Orientation.Set(0, 0, 0, 1);
#if !BAKE_BONE_SCALES
	DstBone->Scale.Set(1, 1, 1);
#endif

	int TotalVerts = 0;
	int TotalIndices = 0;
	for (int PieceIndex = 0; PieceIndex < SourceLods.Num(); PieceIndex++)
	{
		const FSCConvLeadMeshLod &SrcPiece = SourceLods[PieceIndex];
		TotalVerts += SrcPiece.VertexBytes / SrcPiece.VertexStride;
		TotalIndices += SrcPiece.IndexCount;
	}

	CSkelMeshLod *Lod = new (OutMesh->Lods) CSkelMeshLod;
	Lod->NumTexCoords = 1;
	Lod->HasNormals = false;
	Lod->HasTangents = false;
	Lod->AllocateVerts(TotalVerts);
	if (TotalVerts < 65536)
		Lod->Indices.Indices16.AddZeroed(TotalIndices);
	else
		Lod->Indices.Indices32.AddZeroed(TotalIndices);

	int VertexBase = 0;
	int IndexBase = 0;
	for (int PieceIndex = 0; PieceIndex < SourceLods.Num(); PieceIndex++)
	{
		const FSCConvLeadMeshLod &SrcPiece = SourceLods[PieceIndex];
		const int NumVerts = SrcPiece.VertexBytes / SrcPiece.VertexStride;
		for (int i = 0; i < NumVerts; i++)
		{
			Ar.Seek(SrcPiece.VertexStart + i * SrcPiece.VertexStride + PositionOffset);
			FVector P;
			Ar << P;
			CSkelMeshVertex &V = Lod->Verts[VertexBase + i];
			memset(&V, 0, sizeof(V));
			V.Position = CVT(P);
			V.PackedWeights = 0xFF;
			V.Bone[0] = 0;
			V.Bone[1] = V.Bone[2] = V.Bone[3] = -1;
			V.UV.U = 0;
			V.UV.V = 0;
		}

		CMeshSection *Section = new (Lod->Sections) CMeshSection;
		memset(Section, 0, sizeof(*Section));
		Section->FirstIndex = IndexBase;
		Section->NumFaces = SrcPiece.IndexCount / 3;

		Ar.Seek(SrcPiece.IndexHeader + 16);
		for (int i = 0; i < SrcPiece.IndexCount; i++)
		{
			uint16 Index;
			Ar << Index;
			if (TotalVerts < 65536)
				Lod->Indices.Indices16[IndexBase + i] = VertexBase + Index;
			else
				Lod->Indices.Indices32[IndexBase + i] = VertexBase + Index;
		}

		appPrintf("SCConv LeadMesh piece%d: verts=%d indices=%d vertex=%08X index=%08X\n",
			PieceIndex, NumVerts, SrcPiece.IndexCount, SrcPiece.VertexHeader, SrcPiece.IndexHeader);

		VertexBase += NumVerts;
		IndexBase += SrcPiece.IndexCount;
	}
	appPrintf("SCConv LeadMesh combined: pieces=%d verts=%d indices=%d\n",
		SourceLods.Num(), TotalVerts, TotalIndices);

	OutMesh->FinalizeMesh();
	return true;

	unguard;
}

static void GetSCConvLeadFamilyName(const char *Name, char *Out, int OutSize)
{
	appStrncpyz(Out, Name, OutSize);
	char *L3d = strstr(Out, "_l3d");
	if (L3d && !L3d[4])
		*L3d = 0;

	char *LastUnderscore = strrchr(Out, '_');
	if (LastUnderscore)
	{
		if (!strnicmp(LastUnderscore, "_M", 2) && isdigit((unsigned char)LastUnderscore[2]))
			*LastUnderscore = 0;
		else if (!stricmp(LastUnderscore, "_B"))
			*LastUnderscore = 0;
	}

	int Len = strlen(Out);
	while (Len > 0 && isdigit((unsigned char)Out[Len - 1]))
		Out[--Len] = 0;
}

static bool ReadSCConvSiblingLeadMesh(USkeletalMesh &Mesh, FArchive &Ar)
{
	guard(ReadSCConvSiblingLeadMesh);

	if (!Mesh.Package)
		return false;

	auto TryLeadExport = [&Mesh, &Ar](int ExportIndex) -> bool
	{
		const FObjectExport &Exp = Mesh.Package->GetExport(ExportIndex);
		int OldPos = Ar.Tell();
		int OldStopper = Ar.GetStopper();
		Ar.Seek(Exp.SerialOffset);
		Ar.SetStopper(Exp.SerialOffset + Exp.SerialSize);
		TArray<byte> SerialData;
		SerialData.AddUninitialized(Exp.SerialSize);
		Ar.Serialize(SerialData.GetData(), Exp.SerialSize);
		FMemReader Mem(SerialData.GetData(), Exp.SerialSize);
		Mem.SetupFrom(Ar);
		bool bOk = ReadSCConvLeadMeshPayload(Mesh, Mem, Exp.SerialSize);
		Ar.SetStopper(OldStopper);
		Ar.Seek(OldPos);
		return bOk;
	};

	char LeadName[256];
	appSprintf(ARRAY_ARG(LeadName), "%s_l3d", Mesh.Name);
	int ExportIndex = Mesh.Package->FindExport(LeadName);
	if (ExportIndex >= 0 && TryLeadExport(ExportIndex))
		return true;

	char MeshFamily[256];
	GetSCConvLeadFamilyName(Mesh.Name, ARRAY_ARG(MeshFamily));
	TArray<int> TriedExports;
	if (ExportIndex >= 0)
		TriedExports.Add(ExportIndex);
	while (true)
	{
		int BestIndex = INDEX_NONE;
		int BestScore = 0;
		for (int i = 0; i < Mesh.Package->Summary.ExportCount; i++)
		{
			bool bTried = false;
			for (int j = 0; j < TriedExports.Num(); j++)
			{
				if (TriedExports[j] == i)
				{
					bTried = true;
					break;
				}
			}
			if (bTried)
				continue;

			const FObjectExport &Exp = Mesh.Package->GetExport(i);
			if (stricmp(Mesh.Package->GetClassNameFor(Exp), "LeadMesh"))
				continue;
			if (Exp.SerialSize <= 0)
				continue;

			const char *CandidateName = *Exp.ObjectName;
			if (!strstr(CandidateName, "_l3d"))
				continue;

			char CandidateFamily[256];
			GetSCConvLeadFamilyName(CandidateName, ARRAY_ARG(CandidateFamily));

			int Score = 0;
			if (!stricmp(CandidateFamily, MeshFamily))
				Score = 1000;
			else if (!strnicmp(CandidateFamily, MeshFamily, strlen(MeshFamily)))
				Score = 500 + strlen(MeshFamily);
			else if (!strnicmp(MeshFamily, CandidateFamily, strlen(CandidateFamily)))
				Score = 250 + strlen(CandidateFamily);

			if (!strnicmp(MeshFamily, "SK_", 3))
			{
				const char *AccessoryName = MeshFamily + 3;
				if (AccessoryName[0] && strstr(CandidateFamily, AccessoryName))
					Score = max(Score, 700 + (int)strlen(AccessoryName));
				else if (!strnicmp(CandidateFamily, "Sam_", 4))
					Score = max(Score, 25);
			}

			if (Score > BestScore)
			{
				BestScore = Score;
				BestIndex = i;
			}
		}

		if (BestIndex == INDEX_NONE)
			break;

		if (TryLeadExport(BestIndex))
		{
			appPrintf("SCConv LeadMesh remap: %s -> %s\n", Mesh.Name, *Mesh.Package->GetExport(BestIndex).ObjectName);
			return true;
		}
		TriedExports.Add(BestIndex);
	}

	return false;

	unguard;
}

#endif // LEAD


#if SWRC

struct FAttachSocketSWRC
{
	FName		Alias;
	FName		BoneName;
	FMatrix		Matrix;

	friend FArchive& operator<<(FArchive &Ar, FAttachSocketSWRC &S)
	{
		return Ar << S.Alias << S.BoneName << S.Matrix;
	}
};

struct FMeshAnimLinkSWRC
{
	int			Flags;
	UMeshAnimation *Anim;

	friend FArchive& operator<<(FArchive &Ar, FMeshAnimLinkSWRC &S)
	{
		if (Ar.ArVer >= 151)
			Ar << S.Flags;
		else
			S.Flags = 1;
		return Ar << S.Anim;
	}
};

#endif // SWRC


#if SPLINTER_CELL
static int FindPandoraLazyArray(FArchive &Ar, int Start, int Stop, int ElementSize, int ExpectedCount = 0, int MinCount = 1)
{
	guard(FindPandoraLazyArray);
	for (int Pos = Start; Pos < Stop - 8; Pos++)
	{
		Ar.Seek(Pos);
		int SkipPos;
		Ar << SkipPos;
		if (SkipPos <= Pos + 4 || SkipPos > Stop) continue;

		int Count;
		Ar << AR_INDEX(Count);
		if (Count < MinCount) continue;
		if (ExpectedCount && Count != ExpectedCount) continue;
		if (Ar.Tell() + Count * ElementSize != SkipPos) continue;

		return Pos;
	}
	return 0;
	unguard;
}

static int FindNextPandoraLazyArray(FArchive &Ar, int Start, int Stop, int ElementSize, int &OutCount, int MinCount = 1)
{
	guard(FindNextPandoraLazyArray);
	for (int Pos = Start; Pos < Stop - 8; Pos++)
	{
		Ar.Seek(Pos);
		int SkipPos;
		Ar << SkipPos;
		if (SkipPos <= Pos + 4 || SkipPos > Stop) continue;

		int Count;
		Ar << AR_INDEX(Count);
		if (Count < MinCount) continue;
		if (Ar.Tell() + Count * ElementSize != SkipPos) continue;

		OutCount = Count;
		return Pos;
	}
	OutCount = 0;
	return 0;
	unguard;
}

static void DumpSC4LazyArrays(FArchive &Ar, int Start, int Stop)
{
	guard(DumpSC4LazyArrays);
	appPrintf("SC4 lazy arrays between %08X and %08X:\n", Start, Stop);
	for (int Pos = Start; Pos < Stop - 8; Pos++)
	{
		Ar.Seek(Pos);
		int SkipPos;
		Ar << SkipPos;
		if (SkipPos <= Pos + 4 || SkipPos > Stop)
			continue;

		int Count;
		Ar << AR_INDEX(Count);
		if (Count < 1 || Count > 1000000)
			continue;

		int DataPos = Ar.Tell();
		int DataSize = SkipPos - DataPos;
		if (DataSize < 0 || DataSize % Count)
			continue;

		appPrintf("  %08X -> %08X count=%d stride=%d\n", Pos, SkipPos, Count, DataSize / Count);
	}
	unguard;
}

static bool IsSaneSCDAFloat(float Value)
{
	return Value == Value && Value > -50000.0f && Value < 50000.0f;
}

static uint16 ReadSCDAUInt16At(FArchive &Ar, int Pos, bool BigEndian)
{
	byte B[2];
	Ar.Seek(Pos);
	Ar.Serialize(B, 2);
	return BigEndian ? (uint16)((B[0] << 8) | B[1]) : (uint16)(B[0] | (B[1] << 8));
}

static float ReadSCDAFloatAt(FArchive &Ar, int Pos, bool BigEndian)
{
	byte B[4];
	Ar.Seek(Pos);
	Ar.Serialize(B, 4);
	unsigned V = BigEndian
		? ((unsigned)B[0] << 24) | ((unsigned)B[1] << 16) | ((unsigned)B[2] << 8) | B[3]
		: ((unsigned)B[3] << 24) | ((unsigned)B[2] << 16) | ((unsigned)B[1] << 8) | B[0];
	float F;
	memcpy(&F, &V, 4);
	return F;
}

static bool ReadSCDACompactIndex(FArchive &Ar, int Stop, int& Value)
{
	if (Ar.Tell() >= Stop)
		return false;
	byte B0;
	Ar << B0;
	int Sign = B0 & 0x80;
	Value = B0 & 0x3F;
	int Shift = 6;
	if (B0 & 0x40)
	{
		for (int i = 0; i < 4; i++)
		{
			if (Ar.Tell() >= Stop)
				return false;
			byte B;
			Ar << B;
			Value |= (B & 0x7F) << Shift;
			Shift += 7;
			if (!(B & 0x80))
				break;
		}
	}
	if (Sign)
		Value = -Value;
	return true;
}

static void DumpSCDAMeshCompactNames(FArchive &Ar, UnPackage *Package, int Start, int Stop)
{
	if (!getenv("SCDA_MESH_COMPACT_DEBUG"))
		return;
	int SavePos = Ar.Tell();
	Ar.Seek(Start);
	appPrintf("SCDA compact stream at %08X:\n", Start);
	for (int i = 0; i < 80 && Ar.Tell() < Stop; i++)
	{
		int Pos = Ar.Tell();
		int Value;
		if (!ReadSCDACompactIndex(Ar, Stop, Value))
			break;
		const char *NameText = "";
		if (Value >= 0 && Package && Value < Package->Summary.NameCount)
			NameText = Package->GetName(Value);
		appPrintf("  [%02d] %08X = %d %s\n", i, Pos, Value, NameText);
	}
	Ar.Seek(SavePos);
}

static void DumpSCDAPackedVertexCandidates(FArchive &Ar, int Start, int Stop)
{
	if (!getenv("SCDA_MESH_PACKED_DEBUG"))
		return;
	int SavePos = Ar.Tell();
	appPrintf("SCDA packed candidates at %08X-%08X:\n", Start, Stop);
	for (int Stride = 8; Stride <= 64; Stride++)
	{
		int BestPos = 0;
		int BestCount = 0;
		for (int Base = Start; Base < Start + Stride && Base <= Stop - 6; Base++)
		{
			int Count = 0;
			int LastX = 0, LastY = 0, LastZ = 0;
			for (int Pos = Base; Pos <= Stop - 6; Pos += Stride)
			{
				int16 X, Y, Z;
				Ar.Seek(Pos);
				Ar << X << Y << Z;
				if (X == -32768 || Y == -32768 || Z == -32768)
					break;
				if (abs((int)X) > 30000 || abs((int)Y) > 30000 || abs((int)Z) > 30000)
					break;
				if (Count > 0 &&
					(abs((int)X - LastX) > 10000 || abs((int)Y - LastY) > 10000 || abs((int)Z - LastZ) > 10000))
					break;
				LastX = X; LastY = Y; LastZ = Z;
				Count++;
				if (Count > 20000)
					break;
			}
			if (Count > BestCount)
			{
				BestCount = Count;
				BestPos = Base;
			}
		}
		if (BestCount >= 32)
			appPrintf("  stride=%d pos=%08X count=%d\n", Stride, BestPos, BestCount);
	}
	Ar.Seek(SavePos);
}

static bool FindSCDARawVectorBlock(FArchive &Ar, int Start, int Stop, TArray<FVector>& OutPoints, int& OutPos)
{
	guard(FindSCDARawVectorBlock);
	if (getenv("SC4_DEBUG_MESH"))
		appPrintf("SCDA raw vector scan: %08X-%08X\n", Start, Stop);
	if (getenv("SCDA_RAW_CANDIDATE_DEBUG") && Start + 0x8C <= Stop)
	{
		float X = ReadSCDAFloatAt(Ar, Start + 0x80, false);
		float Y = ReadSCDAFloatAt(Ar, Start + 0x84, false);
		float Z = ReadSCDAFloatAt(Ar, Start + 0x88, false);
		appPrintf("SCDA raw vector sample @+80 LE=(%g,%g,%g)\n", X, Y, Z);
	}
	int BestPos = 0;
	int BestCount = 0;
	float BestExtent = 0;
	bool BestBigEndian = false;
	const int EndianCount = getenv("SCDA_RAW_ALLOW_BIG_ENDIAN") ? 2 : 1;
	for (int Endian = 0; Endian < EndianCount; Endian++)
	{
		const bool BigEndian = Endian != 0;
		for (int Pos = Start; Pos <= Stop - 12; Pos += 4)
		{
			int Count = 0;
			FVector Min, Max;
			Min.Set(3.4e38f, 3.4e38f, 3.4e38f);
			Max.Set(-3.4e38f, -3.4e38f, -3.4e38f);
			for (int P = Pos; P <= Stop - 12; P += 12)
			{
				float X = ReadSCDAFloatAt(Ar, P + 0, BigEndian);
				float Y = ReadSCDAFloatAt(Ar, P + 4, BigEndian);
				float Z = ReadSCDAFloatAt(Ar, P + 8, BigEndian);
				if (!IsSaneSCDAFloat(X) || !IsSaneSCDAFloat(Y) || !IsSaneSCDAFloat(Z))
					break;
				if (fabs(X) < 0.000001f && fabs(Y) < 0.000001f && fabs(Z) < 0.000001f)
					break;
				Min.X = min(Min.X, X); Min.Y = min(Min.Y, Y); Min.Z = min(Min.Z, Z);
				Max.X = max(Max.X, X); Max.Y = max(Max.Y, Y); Max.Z = max(Max.Z, Z);
				Count++;
				if (Count > 20000)
					break;
			}
			if (Count < 128)
				continue;
			float SizeX = Max.X - Min.X;
			float SizeY = Max.Y - Min.Y;
			float SizeZ = Max.Z - Min.Z;
			float MaxAxis = max(SizeX, max(SizeY, SizeZ));
			float MinAxis = max(0.001f, min(SizeX, min(SizeY, SizeZ)));
			if (!getenv("SCDA_RAW_ALLOW_ELONGATED") && MaxAxis / MinAxis > 12.0f)
			{
				if (getenv("SC4_DEBUG_MESH"))
					appPrintf("SCDA raw vector block rejected: pos=%08X count=%d axisRatio=%g size=(%g,%g,%g)\n",
						Pos, Count, MaxAxis / MinAxis, SizeX, SizeY, SizeZ);
				Pos += max(0, Count * 12 - 4);
				continue;
			}
			float Extent = (Max.X - Min.X) + (Max.Y - Min.Y) + (Max.Z - Min.Z);
			if (Extent < 1.0f || Extent > 20000.0f)
			{
				if (getenv("SC4_DEBUG_MESH"))
					appPrintf("SCDA raw vector block rejected: pos=%08X count=%d extent=%g size=(%g,%g,%g)\n",
						Pos, Count, Extent, SizeX, SizeY, SizeZ);
				continue;
			}
			if (getenv("SCDA_RAW_CANDIDATE_DEBUG"))
				appPrintf("SCDA raw vector candidate: pos=%08X count=%d endian=%s extent=%g axisRatio=%g size=(%g,%g,%g)\n",
					Pos, Count, BigEndian ? "BE" : "LE", Extent, MaxAxis / MinAxis, SizeX, SizeY, SizeZ);
			if (Count > BestCount || (Count == BestCount && Extent > BestExtent))
			{
				BestPos = Pos;
				BestCount = Count;
				BestExtent = Extent;
				BestBigEndian = BigEndian;
			}
			Pos += max(0, Count * 12 - 4);
		}
	}
	if (!BestCount)
	{
		if (getenv("SC4_DEBUG_MESH"))
			appPrintf("SCDA raw vector scan: no candidate\n");
		return false;
	}
	OutPoints.Empty(BestCount);
	OutPoints.AddUninitialized(BestCount);
	for (int i = 0; i < BestCount; i++)
	{
		const int Pos = BestPos + i * 12;
		OutPoints[i].X = ReadSCDAFloatAt(Ar, Pos + 0, BestBigEndian);
		OutPoints[i].Y = ReadSCDAFloatAt(Ar, Pos + 4, BestBigEndian);
		OutPoints[i].Z = ReadSCDAFloatAt(Ar, Pos + 8, BestBigEndian);
	}
	OutPos = BestPos;
	if (getenv("SC4_DEBUG_MESH"))
	{
		appPrintf("SCDA raw vector block: pos=%08X count=%d endian=%s extent=%g first=(%g,%g,%g)\n",
			BestPos, BestCount, BestBigEndian ? "BE" : "LE", BestExtent,
			OutPoints[0].X, OutPoints[0].Y, OutPoints[0].Z);
	}
	return true;
	unguard;
}

static bool ReadSCDARawFaceRecords(FArchive &Ar, int Pos, int Stop, int Count, int PointCount, bool BigEndian, TArray<VTriangle>& OutTriangles)
{
	guard(ReadSCDARawFaceRecords);
	if (Count < 1 || Pos < 0 || Pos + Count * 8 > Stop)
		return false;
	OutTriangles.Empty(Count);
	OutTriangles.AddZeroed(Count);
	for (int i = 0; i < Count; i++)
	{
		int P = Pos + i * 8;
		uint16 A = ReadSCDAUInt16At(Ar, P + 0, BigEndian);
		uint16 B = ReadSCDAUInt16At(Ar, P + 2, BigEndian);
		uint16 C = ReadSCDAUInt16At(Ar, P + 4, BigEndian);
		uint16 Aux = ReadSCDAUInt16At(Ar, P + 6, BigEndian);
		if (A >= PointCount || B >= PointCount || C >= PointCount || Aux > 255)
		{
			OutTriangles.Empty();
			return false;
		}
		VTriangle &T = OutTriangles[i];
		T.WedgeIndex[0] = A;
		T.WedgeIndex[1] = B;
		T.WedgeIndex[2] = C;
		T.MatIndex = Aux & 0xFF;
		T.AuxMatIndex = 0;
		T.SmoothingGroups = 0;
	}
	return true;
	unguard;
}

static bool FindSCDARawIndexBlock(FArchive &Ar, int Start, int Stop, const TArray<FVector>& Points, int PointsPos, TArray<VTriangle>& OutTriangles, int& OutPos)
{
	guard(FindSCDARawIndexBlock);
	int PointCount = Points.Num();
	if (PointCount <= 0 || PointCount > 65535)
		return false;

	if (PointsPos >= Start + 8)
	{
		const int HeaderFaceCount1 = ReadSCDAUInt16At(Ar, PointsPos - 4, false);
		const int HeaderFaceCount2 = ReadSCDAUInt16At(Ar, PointsPos - 2, false);
		const int HeaderFaceCount = max(HeaderFaceCount1, HeaderFaceCount2);
		if (HeaderFaceCount >= 300 && HeaderFaceCount < 20000)
		{
			const int PointsEnd = PointsPos + PointCount * 12;
			for (int Delta = 0; Delta < 32; Delta++)
			{
				int Pos = PointsEnd + Delta;
				if (ReadSCDARawFaceRecords(Ar, Pos, Stop, HeaderFaceCount, PointCount, false, OutTriangles))
				{
					OutPos = Pos;
					if (getenv("SC4_DEBUG_MESH"))
						appPrintf("SCDA counted face records: pos=%08X triangles=%d endian=LE stride=8 delta=%d\n",
							Pos, OutTriangles.Num(), Delta);
					return true;
				}
			}
		}
	}

	int BestRecordPos = 0;
	int BestRecordCount = 0;
	bool BestRecordBigEndian = false;
	for (int Endian = 0; Endian < 2; Endian++)
	{
		const bool BigEndian = Endian != 0;
		for (int Pos = Start; Pos <= Stop - 8; Pos++)
		{
			int Count = 0;
			for (int P = Pos; P <= Stop - 8; P += 8)
			{
				uint16 A = ReadSCDAUInt16At(Ar, P + 0, BigEndian);
				uint16 B = ReadSCDAUInt16At(Ar, P + 2, BigEndian);
				uint16 C = ReadSCDAUInt16At(Ar, P + 4, BigEndian);
				uint16 Aux = ReadSCDAUInt16At(Ar, P + 6, BigEndian);
				if (A >= PointCount || B >= PointCount || C >= PointCount || Aux > 255)
					break;
				if (A == B || A == C || B == C)
					break;
				float ABx = Points[B].X - Points[A].X;
				float ABy = Points[B].Y - Points[A].Y;
				float ABz = Points[B].Z - Points[A].Z;
				float ACx = Points[C].X - Points[A].X;
				float ACy = Points[C].Y - Points[A].Y;
				float ACz = Points[C].Z - Points[A].Z;
				float CX = ABy * ACz - ABz * ACy;
				float CY = ABz * ACx - ABx * ACz;
				float CZ = ABx * ACy - ABy * ACx;
				float Area = CX * CX + CY * CY + CZ * CZ;
				if (Area < 0.000001f || Area > 1000000000.0f)
					break;
				Count++;
				if (Count > 100000)
					break;
			}
			if (Count > BestRecordCount)
			{
				BestRecordPos = Pos;
				BestRecordCount = Count;
				BestRecordBigEndian = BigEndian;
			}
			if (Count >= 300)
				Pos += Count * 8 - 1;
		}
	}
	if (BestRecordCount >= 300)
	{
		if (!ReadSCDARawFaceRecords(Ar, BestRecordPos, Stop, BestRecordCount, PointCount, BestRecordBigEndian, OutTriangles))
			return false;
		OutPos = BestRecordPos;
		if (getenv("SC4_DEBUG_MESH"))
			appPrintf("SCDA raw face records: pos=%08X triangles=%d endian=%s stride=8\n",
				BestRecordPos, OutTriangles.Num(), BestRecordBigEndian ? "BE" : "LE");
		return true;
	}

	int BestPos = 0;
	int BestTriangleCount = 0;
	float BestArea = 0;
	bool BestBigEndian = false;
	bool BestStrip = false;

	for (int Endian = 0; Endian < 2; Endian++)
	{
		const bool BigEndian = Endian != 0;
		for (int Mode = 0; Mode < 2; Mode++)
		{
			const bool Strip = Mode != 0;
			for (int Pos = Start; Pos <= Stop - (Strip ? 8 : 6); Pos += 2)
			{
				int TriangleCount = 0;
				int IndexCount = 0;
				float AreaScore = 0;
				uint16 Prev2 = 0, Prev1 = 0;
				for (int P = Pos; P <= Stop - 2; P += 2)
				{
					const uint16 I = ReadSCDAUInt16At(Ar, P, BigEndian);
					if (I >= PointCount)
						break;
					if (!Strip)
					{
						IndexCount++;
						if ((IndexCount % 3) != 0)
							continue;
					}
					else
					{
						IndexCount++;
						if (IndexCount < 3)
						{
							Prev2 = Prev1;
							Prev1 = I;
							continue;
						}
					}

					uint16 A, B, C;
					if (!Strip)
					{
						A = ReadSCDAUInt16At(Ar, P - 4, BigEndian);
						B = ReadSCDAUInt16At(Ar, P - 2, BigEndian);
						C = I;
					}
					else if (IndexCount & 1)
					{
						A = Prev2; B = Prev1; C = I;
					}
					else
					{
						A = Prev1; B = Prev2; C = I;
					}
					Prev2 = Prev1;
					Prev1 = I;

					if (A == B || A == C || B == C)
						continue;
					float ABx = Points[B].X - Points[A].X;
					float ABy = Points[B].Y - Points[A].Y;
					float ABz = Points[B].Z - Points[A].Z;
					float ACx = Points[C].X - Points[A].X;
					float ACy = Points[C].Y - Points[A].Y;
					float ACz = Points[C].Z - Points[A].Z;
					float CX = ABy * ACz - ABz * ACy;
					float CY = ABz * ACx - ABx * ACz;
					float CZ = ABx * ACy - ABy * ACx;
					float Area = CX * CX + CY * CY + CZ * CZ;
					if (Area < 0.000001f || Area > 100000000.0f)
					{
						if (!Strip)
							break;
						continue;
					}
					AreaScore += min(Area, 1000000.0f);
					TriangleCount++;
					if (IndexCount > 200000)
						break;
				}
				if (TriangleCount < 100)
					continue;
				if (TriangleCount > BestTriangleCount || (TriangleCount == BestTriangleCount && AreaScore > BestArea))
				{
					BestPos = Pos;
					BestTriangleCount = TriangleCount;
					BestArea = AreaScore;
					BestBigEndian = BigEndian;
					BestStrip = Strip;
				}
				if (!Strip)
					Pos += max(0, TriangleCount * 6 - 2);
				else
					Pos += max(0, (TriangleCount + 2) * 2 - 2);
			}
		}
	}
	if (!BestTriangleCount)
		return false;
	OutTriangles.Empty(BestTriangleCount);
	OutTriangles.AddZeroed(BestTriangleCount);
	int OutIndex = 0;
	uint16 Prev2 = 0, Prev1 = 0;
	int IndexCount = 0;
	for (int P = BestPos; P <= Stop - 2 && OutIndex < BestTriangleCount; P += 2)
	{
		const uint16 I = ReadSCDAUInt16At(Ar, P, BestBigEndian);
		if (I >= PointCount)
			break;
		IndexCount++;
		uint16 A, B, C;
		if (!BestStrip)
		{
			if ((IndexCount % 3) != 0)
				continue;
			A = ReadSCDAUInt16At(Ar, P - 4, BestBigEndian);
			B = ReadSCDAUInt16At(Ar, P - 2, BestBigEndian);
			C = I;
		}
		else
		{
			if (IndexCount < 3)
			{
				Prev2 = Prev1;
				Prev1 = I;
				continue;
			}
			if (IndexCount & 1)
			{
				A = Prev2; B = Prev1; C = I;
			}
			else
			{
				A = Prev1; B = Prev2; C = I;
			}
			Prev2 = Prev1;
			Prev1 = I;
		}
		if (A == B || A == C || B == C)
			continue;
		OutTriangles[OutIndex].WedgeIndex[0] = A;
		OutTriangles[OutIndex].WedgeIndex[1] = B;
		OutTriangles[OutIndex].WedgeIndex[2] = C;
		OutIndex++;
	}
	OutTriangles.RemoveAt(OutIndex, OutTriangles.Num() - OutIndex);
	OutPos = BestPos;
	if (getenv("SC4_DEBUG_MESH"))
		appPrintf("SCDA raw index block: pos=%08X triangles=%d endian=%s mode=%s\n",
			BestPos, OutTriangles.Num(), BestBigEndian ? "BE" : "LE", BestStrip ? "strip" : "list");
	return true;
	unguard;
}

static bool FindSCDAPacked17TriangleSoup(FArchive &Ar, int Start, int Stop,
	TArray<FVector>& OutPoints, TArray<VTriangle>& OutTriangles, int& OutPos)
{
	guard(FindSCDAPacked17TriangleSoup);
	const int Stride = 17;
	int BestPos = 0;
	int BestCount = 0;
	int BestExtent = 0;
	for (int Base = Start; Base < Start + Stride && Base <= Stop - Stride; Base++)
	{
		int Count = 0;
		int MinX =  0x7FFFFFFF, MinY =  0x7FFFFFFF, MinZ =  0x7FFFFFFF;
		int MaxX = -0x7FFFFFFF, MaxY = -0x7FFFFFFF, MaxZ = -0x7FFFFFFF;
		for (int Pos = Base; Pos <= Stop - Stride; Pos += Stride)
		{
			int16 X, Y, Z;
			Ar.Seek(Pos);
			Ar << X << Y << Z;
			if (X == -32768 || Y == -32768 || Z == -32768)
				break;
			if (abs((int)X) > 20000 || abs((int)Y) > 20000 || abs((int)Z) > 20000)
				break;
			MinX = min(MinX, (int)X); MaxX = max(MaxX, (int)X);
			MinY = min(MinY, (int)Y); MaxY = max(MaxY, (int)Y);
			MinZ = min(MinZ, (int)Z); MaxZ = max(MaxZ, (int)Z);
			Count++;
			if (Count > 50000)
				break;
		}
		int Extent = (MaxX - MinX) + (MaxY - MinY) + (MaxZ - MinZ);
		if (Count >= 96 && (Count > BestCount || (Count == BestCount && Extent > BestExtent)))
		{
			BestPos = Base;
			BestCount = Count;
			BestExtent = Extent;
		}
	}
	if (BestCount < 96)
		return false;
	BestCount -= BestCount % 3;
	OutPoints.Empty(BestCount);
	OutPoints.AddUninitialized(BestCount);
	Ar.Seek(BestPos);
	for (int i = 0; i < BestCount; i++)
	{
		int16 X, Y, Z;
		Ar.Seek(BestPos + i * Stride);
		Ar << X << Y << Z;
		OutPoints[i].Set(X / 32.0f, Y / 32.0f, Z / 32.0f);
	}
	int TriangleCount = BestCount / 3;
	OutTriangles.Empty(TriangleCount);
	OutTriangles.AddZeroed(TriangleCount);
	for (int i = 0; i < TriangleCount; i++)
	{
		OutTriangles[i].WedgeIndex[0] = i * 3 + 0;
		OutTriangles[i].WedgeIndex[1] = i * 3 + 1;
		OutTriangles[i].WedgeIndex[2] = i * 3 + 2;
	}
	OutPos = BestPos;
	return true;
	unguard;
}

static int FindSC4WedgeArray(FArchive &Ar, int Start, int FacesPos, int MinCount, int &OutCount)
{
	guard(FindSC4WedgeArray);
	const int Stride = sizeof(uint16) + sizeof(float) * 9;
	for (int Pos = Start; Pos < FacesPos - 8; Pos++)
	{
		Ar.Seek(Pos);
		int SkipPos;
		Ar << SkipPos;
		if (SkipPos != FacesPos) continue;

		int Count;
		Ar << AR_INDEX(Count);
		if (Count < MinCount) continue;
		if (Ar.Tell() + Count * Stride != SkipPos) continue;

		OutCount = Count;
		return Pos;
	}
	OutCount = 0;
	return 0;
	unguard;
}

static bool ReadSC4Wedges(FArchive &Ar, int Pos, int Stop, int PointCount, TArray<FMeshWedge> &OutWedges)
{
	guard(ReadSC4Wedges);
	const int Stride = sizeof(uint16) + sizeof(float) * 9;
	Ar.Seek(Pos);
	int SkipPos;
	int Count;
	Ar << SkipPos << AR_INDEX(Count);
	if (Count < 1 || SkipPos > Stop || Ar.Tell() + Count * Stride != SkipPos)
		return false;

	OutWedges.Empty(Count);
	OutWedges.AddZeroed(Count);
	for (int i = 0; i < Count; i++)
	{
		int RecordEnd = Ar.Tell() + Stride;
		FMeshWedge &W = OutWedges[i];
		Ar << W.iVertex << W.TexUV;
		if (W.iVertex >= PointCount || W.TexUV.U != W.TexUV.U || W.TexUV.V != W.TexUV.V)
			return false;
		Ar.Seek(RecordEnd);
	}
	return true;
	unguard;
}

static int ReadSC4RefSkeleton(FArchive &Ar, UnPackage *Package, int Pos, int Stop, TArray<FMeshBone> &OutBones)
{
	guard(ReadSC4RefSkeleton);
	int SavePos = Ar.Tell();
	Ar.Seek(Pos);

	int BoneCount;
	Ar << AR_INDEX(BoneCount);
	if (BoneCount < 2 || BoneCount > 256)
	{
		Ar.Seek(SavePos);
		return 0;
	}

	TArray<FMeshBone> Bones;
	Bones.AddZeroed(BoneCount);
	int ParentCounts[256];
	memset(ParentCounts, 0, sizeof(ParentCounts));
	int ValidQuats = 0;
	for (int i = 0; i < BoneCount; i++)
	{
		int NameIndex, ExtraIndex;
		unsigned Flags;
		FQuat Orientation;
		FVector Position;
		float Length;
		FVector Size;
		int NumChildren, ParentIndex;

		Ar << AR_INDEX(NameIndex) << AR_INDEX(ExtraIndex);
		if (!Package || unsigned(NameIndex) >= Package->Summary.NameCount || Ar.Tell() + 56 > Stop)
		{
			Ar.Seek(SavePos);
			return 0;
		}
		Ar << Flags << Orientation << Position << Length << Size << NumChildren << ParentIndex;

		float QuatLen = Orientation.X * Orientation.X + Orientation.Y * Orientation.Y +
			Orientation.Z * Orientation.Z + Orientation.W * Orientation.W;
		if (ParentIndex < 0 || ParentIndex >= BoneCount || (i > 0 && ParentIndex >= i) ||
			QuatLen < 0.5f || QuatLen > 1.5f ||
			Position.X != Position.X || Position.Y != Position.Y || Position.Z != Position.Z ||
			fabs(Position.X) > 10000 || fabs(Position.Y) > 10000 || fabs(Position.Z) > 10000)
		{
			Ar.Seek(SavePos);
			return 0;
		}
		if (QuatLen > 0.8f && QuatLen < 1.2f)
			ValidQuats++;
		if (i > 0)
			ParentCounts[ParentIndex]++;

		FMeshBone &B = Bones[i];
		B.Name = Package->GetName(NameIndex);
		B.Flags = Flags;
		B.BonePos.Orientation = Orientation;
		B.BonePos.Position = Position;
		B.BonePos.Length = Length;
		B.BonePos.Size = Size;
		B.NumChildren = NumChildren;
		B.ParentIndex = ParentIndex;
	}

	int MatchingChildren = 0;
	for (int i = 0; i < BoneCount; i++)
	{
		if (Bones[i].NumChildren == ParentCounts[i])
			MatchingChildren++;
		Bones[i].NumChildren = ParentCounts[i];
	}

	CopyArray(OutBones, Bones);
	Ar.Seek(SavePos);
	return BoneCount * 100 + MatchingChildren * 25 + ValidQuats * 5;
	unguard;
}

static bool FindSC4RefSkeleton(FArchive &Ar, UnPackage *Package, int Start, int Stop, TArray<FMeshBone> &OutBones)
{
	guard(FindSC4RefSkeleton);
	int BestPos = 0;
	int BestScore = 0;
	TArray<FMeshBone> BestBones;
	for (int Pos = Start; Pos < Stop - 128; Pos++)
	{
		TArray<FMeshBone> Bones;
		int Score = ReadSC4RefSkeleton(Ar, Package, Pos, Stop, Bones);
		if (Score <= BestScore)
			continue;
		BestScore = Score;
		BestPos = Pos;
		CopyArray(BestBones, Bones);
		int PerfectScore = Bones.Num() * (100 + 25 + 5);
		if (Bones.Num() && BestScore >= PerfectScore)
			break;
	}
	if (!BestScore)
	{
		OutBones.Empty();
		return false;
	}

	CopyArray(OutBones, BestBones);
	appPrintf("SC4 mesh RefSkeleton block: %08X bones=%d score=%d\n", BestPos, OutBones.Num(), BestScore);
	return true;
	unguard;
}

static int FindSC4InfluenceArray(FArchive &Ar, int Start, int WedgesPos, int PointCount, int BoneCount)
{
	guard(FindSC4InfluenceArray);
	for (int Pos = Start; Pos < WedgesPos - 8; Pos++)
	{
		Ar.Seek(Pos);
		int SkipPos;
		Ar << SkipPos;
		if (SkipPos != WedgesPos) continue;

		int Count;
		Ar << AR_INDEX(Count);
		if (Count < PointCount || Ar.Tell() + Count * sizeof(FVertInfluence) != SkipPos)
			continue;

		int SaveDataPos = Ar.Tell();
		bool Valid = true;
		for (int i = 0; i < Count; i++)
		{
			FVertInfluence I;
			Ar << I;
			if (I.Weight != I.Weight || I.Weight < 0 || I.Weight > 1.001f ||
				I.PointIndex >= PointCount || I.BoneIndex >= BoneCount)
			{
				Valid = false;
				break;
			}
		}
		Ar.Seek(SaveDataPos);
		if (Valid)
			return Pos;
	}
	return 0;
	unguard;
}
#endif // SPLINTER_CELL


void USkeletalMesh::Serialize(FArchive &Ar)
{
	guard(USkeletalMesh::Serialize);

	assert(Ar.Game < GAME_UE3);

#if UNREAL1
	if (Ar.Engine() == GAME_UE1)
	{
		SerializeSkelMesh1(Ar);
		return;
	}
#endif
#if BIOSHOCK
	if (Ar.Game == GAME_Bioshock)
	{
		SerializeBioshockMesh(Ar);
		return;
	}
#endif
#if LEAD
	if (Ar.Game == GAME_SplinterCellConv)
	{
		UObject::Serialize(Ar);
		if (!ReadSCConvSiblingLeadMesh(*this, Ar))
			appPrintf("WARNING: Unable to locate Conviction LeadMesh payload for %s\n", Name);
		DROP_REMAINING_DATA(Ar);
		return;
	}
#endif
#if SPLINTER_CELL
	if (Ar.Game == GAME_SplinterCell && Ar.ArVer == 100 && Ar.ArLicenseeVer >= 127)
	{
		SerializeSCell(Ar);
		return;
	}
#endif

	Super::Serialize(Ar);
#if SPLINTER_CELL
	if (Ar.Game == GAME_SplinterCell && !((Ar.ArVer == 164 || Ar.ArVer == 171 || Ar.ArVer == 172) && Ar.ArLicenseeVer == 0))
	{
		SerializeSCell(Ar);
		return;
	}
#endif // SPLINTER_CELL
#if TRIBES3
	TRIBES_HDR(Ar, 4);
#endif

#if DEBUG_SKELMESH
	appPrintf("Version: %d\n", Version);
#endif

	Ar << Points2;
	const bool isPandoraTomorrowOnline = (Ar.Game == GAME_SplinterCell && (Ar.ArVer == 164 || Ar.ArVer == 171 || Ar.ArVer == 172) && Ar.ArLicenseeVer == 0);
#if BATTLE_TERR
	if (Ar.Game == GAME_BattleTerr && Ar.ArVer >= 134)
	{
		TArray<FVector> Points3;
		Ar << Points3;
	}
#endif // BATTLE_TERR

	Ar << RefSkeleton;
#if DEBUG_SKELMESH
	appPrintf("RefSkeleton: %d bones\n", RefSkeleton.Num());
	for (int i1 = 0; i1 < RefSkeleton.Num(); i1++)
		appPrintf("  [%d] n=%s p=%d\n", i1, *RefSkeleton[i1].Name, RefSkeleton[i1].ParentIndex);
#endif // DEBUG_SKELMESH

#if SWRC
	if (Ar.Game == GAME_RepCommando && Ar.ArVer >= 142)
	{
		for (int i = 0; i < RefSkeleton.Num(); i++)
		{
			FMeshBone &B = RefSkeleton[i];
			B.BonePos.Orientation.X *= -1;
			B.BonePos.Orientation.Y *= -1;
			B.BonePos.Orientation.Z *= -1;
		}
	}
	if (Ar.Game == GAME_RepCommando && Version >= 5)
	{
		TArray<FMeshAnimLinkSWRC> Anims;
		Ar << Anims;
		if (Anims.Num() >= 1) Animation = Anims[0].Anim;
	}
	else
#endif // SWRC
		Ar << Animation;
#if AA2
	if (Ar.Game == GAME_AA2 && Ar.ArLicenseeVer >= 22)
	{
		TArray<UObject*> unk230;
		Ar << unk230;
	}
#endif // AA2
	Ar << SkeletalDepth << WeightIndices << BoneInfluences;
#if SWRC
	if (Ar.Game == GAME_RepCommando && Ar.ArVer >= 140)
	{
		TArray<FAttachSocketSWRC> Sockets;
		Ar << Sockets;	//?? convert
	}
	else
#endif // SWRC
	{
		Ar << AttachAliases << AttachBoneNames << AttachCoords;
	}
	if (isPandoraTomorrowOnline)
	{
		int SearchStart = Ar.Tell();
		int Stop = Ar.GetStopper();

		int FacesPos = FindPandoraLazyArray(Ar, SearchStart, Stop, sizeof(FMeshFace), FaceLevel.Num(), 1);
		int WedgeCount = 0;
		int PointCount = 0;
		int WedgesPos = FindNextPandoraLazyArray(Ar, SearchStart, FacesPos, sizeof(uint16) + sizeof(FMeshUVFloat), WedgeCount, 1);
		int PointsPos = FindNextPandoraLazyArray(Ar, FacesPos + 1, Stop, sizeof(FVector), PointCount, 1);
		if (!FacesPos || !WedgesPos || !PointsPos)
			appError("Unable to locate Pandora Tomorrow skeletal mesh buffers");

		TLazyArray<FMeshFace> RawFaces;
		Ar.Seek(FacesPos);
		Ar << RawFaces;

		int MaxWedgeIndex = 0;
		for (int i = 0; i < RawFaces.Num(); i++)
		{
			const FMeshFace &F = RawFaces[i];
			MaxWedgeIndex = max(MaxWedgeIndex, (int)F.iWedge[0]);
			MaxWedgeIndex = max(MaxWedgeIndex, (int)F.iWedge[1]);
			MaxWedgeIndex = max(MaxWedgeIndex, (int)F.iWedge[2]);
		}

		if (MaxWedgeIndex >= WedgeCount)
			appError("Pandora Tomorrow face buffer does not match wedge buffer");

		int InfluencesPos = FindPandoraLazyArray(Ar, SearchStart, WedgesPos, sizeof(FVertInfluence), 0, PointCount);
		if (!WedgesPos || !InfluencesPos)
			appError("Unable to locate Pandora Tomorrow wedge/influence buffers");

		Ar.Seek(PointsPos);
		Ar << Points;
		Ar.Seek(WedgesPos);
		Ar << Wedges;
		Ar.Seek(InfluencesPos);
		Ar << VertInfluences;

		Triangles.Empty(RawFaces.Num());
		Triangles.AddUninitialized(RawFaces.Num());
		for (int i = 0; i < RawFaces.Num(); i++)
		{
			const FMeshFace &F = RawFaces[i];
			VTriangle &T = Triangles[i];
			T.WedgeIndex[0] = F.iWedge[0];
			T.WedgeIndex[1] = F.iWedge[1];
			T.WedgeIndex[2] = F.iWedge[2];
			T.MatIndex = F.MaterialIndex;
			T.AuxMatIndex = 0;
			T.SmoothingGroups = 0;
		}

		LODModels.Empty();
		goto skip_remaining;
	}
	if (Version <= 1)
	{
//		appNotify("SkeletalMesh of version %d\n", Version);
		TArray<FLODMeshSection> tmp1, tmp2;
		TArray<uint16> tmp3;
		Ar << tmp1 << tmp2 << tmp3;
		// copy and convert data from old mesh format
		UpgradeMesh();
	}
	else
	{
#if UC2
		if (Ar.Engine() == GAME_UE2X && Ar.ArVer >= 136)
		{
			int f338;
			Ar << f338;
		}
#endif // UC2
#if SWRC
		if (Ar.Game == GAME_RepCommando)
		{
			int f1C4;
			if (Version >= 6) Ar << f1C4;
			Ar << LODModels;
			if (Version < 5) Ar << f224;
			Ar << Points << Wedges << Triangles << VertInfluences;
			Ar << CollapseWedge << f1C8;
			goto skip_remaining;
		}
#endif // SWRC
#if EOS
		if (Ar.Game == GAME_EOS)
		{
			int unk1;
			UObject* unk2;
			UObject* unk3;
			if (Version >= 6) Ar << unk1 << unk2;
			if (Version >= 7) Ar << unk3;
			Ar << LODModels;
			goto skip_remaining;
		}
#endif // EOS
#if 0
		// Shui Hu Q Zhuan 2 Online
		if (Ar.ArVer == 126 && Ar.ArLicenseeVer == 1)
		{
			// skip LOD models
			int Num;
			Ar << AR_INDEX(Num);
			for (int i = 0; i < Num; i++)
			{
				int Pos;
				Ar << Pos;
				Ar.Seek(Ar.Tell() + Pos - 4);
			}
			goto after_lods;
		}
#endif
		Ar << LODModels;
	after_lods:
		Ar << f224 << Points;
#if BATTLE_TERR
		if (Ar.Game == GAME_BattleTerr && Ar.ArVer >= 134)
		{
			TLazyArray<int>	unk15C;
			Ar << unk15C;
		}
#endif // BATTLE_TERR
		Ar << Wedges << Triangles << VertInfluences;
		Ar << CollapseWedge << f1C8;
	}

#if TRIBES3
	if ((Ar.Game == GAME_Tribes3 || Ar.Game == GAME_Swat4) && t3_hdrSV >= 3)
	{
	#if 0
		// it looks like format of following data was changed since
		// data was prepared, and game executable does not load these
		// LazyArrays (otherwise error should occur) -- so we are
		// simply skipping these arrays
		TLazyArray<FT3Unk1>    unk1;
		TLazyArray<FMeshWedge> unk2;
		TLazyArray<uint16>     unk3;
		Ar << unk1 << unk2 << unk3;
	#else
		SkipLazyArray(Ar);
		SkipLazyArray(Ar);
		SkipLazyArray(Ar);
	#endif
		// nothing interesting below ...
		goto skip_remaining;
	}
#endif // TRIBES3
#if BATTLE_TERR
	if (Ar.Game == GAME_BattleTerr) goto skip_remaining;
#endif
#if UC2
	if (Ar.Engine() == GAME_UE2X) goto skip_remaining;
#endif

#if LINEAGE2
	if (Ar.Game == GAME_Lineage2)
	{
		int unk1, unk3, unk4;
		TArray<float> unk2;
		if (Ar.ArVer >= 118 && Ar.ArLicenseeVer >= 3)
			Ar << unk1;
		if (Ar.ArVer >= 123 && Ar.ArLicenseeVer >= 0x12)
			Ar << unk2;
		if (Ar.ArVer >= 120)
			Ar << unk3;		// AuthKey ?
		if (Ar.ArLicenseeVer >= 0x23)
			Ar << unk4;
		ConvertMesh();
		return;
	}
#endif // LINEAGE2

	if (Ar.ArVer >= 120)
	{
		Ar << AuthKey;
	}

#if LOCO
	if (Ar.Game == GAME_Loco) goto skip_remaining;	// Loco codepath is similar to UT2004, but sometimes has different version switches
#endif

#if UT2
	if (Ar.Game == GAME_UT2)
	{
		// UT2004 has branched version of UE2, which is slightly different
		// in comparison with generic UE2, which is used in all other UE2 games.
		if (Ar.ArVer >= 122)
			Ar << KarmaProps << BoundingSpheres << BoundingBoxes << f32C;
		if (Ar.ArVer >= 127)
			Ar << CollisionMesh;
		ConvertMesh();
		return;
	}
#endif // UT2

	// generic UE2 code
	if (Ar.ArVer >= 124)
		Ar << KarmaProps << BoundingSpheres << BoundingBoxes;
	if (Ar.ArVer >= 125)
		Ar << f32C;

#if XIII
	if (Ar.Game == GAME_XIII) goto skip_remaining;
#endif
#if RAGNAROK2
	if (Ar.Game == GAME_Ragnarok2 && Ar.ArVer >= 131)
	{
		float unk1, unk2;
		Ar << unk1 << unk2;
	}
#endif // RAGNAROK2

	if (Ar.ArLicenseeVer && (Ar.Tell() != Ar.GetStopper()))
	{
		appPrintf("Serializing SkeletalMesh'%s' of unknown game: %d unreal bytes\n", Name, Ar.GetStopper() - Ar.Tell());
	skip_remaining:
		DROP_REMAINING_DATA(Ar);
	}

	ConvertMesh();

	unguard;
}


void USkeletalMesh::PostLoad()
{
#if BIOSHOCK
	if (GetGame() == GAME_Bioshock)
		PostLoadBioshockMesh();		// should be called after loading of all used objects
#endif // BIOSHOCK
}

#if LEAD
void ULeadMesh::Serialize(FArchive &Ar)
{
	guard(ULeadMesh::Serialize);

	if (Ar.Game == GAME_SplinterCellConv)
	{
		if (!ReadSCConvLeadMeshPayload(*this, Ar, Ar.GetStopper()) && !ReadSCConvSiblingLeadMesh(*this, Ar))
			appPrintf("WARNING: Unable to locate Conviction LeadMesh payload for %s\n", Name);
		DROP_REMAINING_DATA(Ar);
		return;
	}

	Super::Serialize(Ar);

	unguard;
}
#endif // LEAD


void USkeletalMesh::UpgradeFaces()
{
	guard(UpgradeFaces);
	// convert 'FMeshFace Faces' to 'VTriangle Triangles'
	if (Faces.Num() && !Triangles.Num())
	{
		Triangles.Empty(Faces.Num());
		Triangles.AddUninitialized(Faces.Num());
		for (int i = 0; i < Faces.Num(); i++)
		{
			const FMeshFace &F = Faces[i];
			VTriangle &T = Triangles[i];
			T.WedgeIndex[0] = F.iWedge[0];
			T.WedgeIndex[1] = F.iWedge[1];
			T.WedgeIndex[2] = F.iWedge[2];
			T.MatIndex      = F.MaterialIndex;
		}
	}
	unguard;
}

void USkeletalMesh::UpgradeMesh()
{
	guard(USkeletalMesh.UpgradeMesh);

	int i;
	CopyArray(Points, Points2);
	CopyArray(Wedges, Super::Wedges);
	UpgradeFaces();
	// convert VBoneInfluence and VWeightIndex to FVertInfluence
	// count total influences
	int numInfluences = 0;
	for (i = 0; i < WeightIndices.Num(); i++)
		numInfluences += WeightIndices[i].BoneInfIndices.Num() * (i + 1);
	VertInfluences.Empty(numInfluences);
	VertInfluences.AddZeroed(numInfluences);
	int vIndex = 0;
	for (i = 0; i < WeightIndices.Num(); i++)				// loop by influence count per vertex
	{
		const VWeightIndex &WI = WeightIndices[i];
		int index = WI.StartBoneInf;
		for (int j = 0; j < WI.BoneInfIndices.Num(); j++)	// loop by vertices
		{
			int iVertex = WI.BoneInfIndices[j];
			for (int k = 0; k <= i; k++)					// enumerate all bones per vertex
			{
				const VBoneInfluence &BI = BoneInfluences[index++];
				FVertInfluence &I = VertInfluences[vIndex++];
				I.Weight     = BI.BoneWeight / 65535.0f;
				I.BoneIndex  = BI.BoneIndex;
				I.PointIndex = iVertex;
			}
		}
	}

	unguard;
}


void USkeletalMesh::ConvertMesh()
{
	guard(USkeletalMesh::ConvertMesh);

	CSkeletalMesh *Mesh = new CSkeletalMesh(this);
	ConvertedMesh = Mesh;
	Mesh->BoundingBox    = BoundingBox;
	Mesh->BoundingSphere = BoundingSphere;

	Mesh->RotOrigin  = RotOrigin;
	Mesh->MeshScale  = CVT(MeshScale);
	Mesh->MeshOrigin = CVT(MeshOrigin);

	Mesh->Lods.Empty(LODModels.Num());

#if DEBUG_SKELMESH
	appPrintf("  Base : Points[%d] Wedges[%d] Influences[%d] Faces[%d]\n",
		Points.Num(), Wedges.Num(), VertInfluences.Num(), Triangles.Num()
	);
#endif

	// some games has troubles with LOD models ...
#if TRIBES3
	if (GetGame() == GAME_Tribes3) goto base_mesh;
#endif
#if SWRC
	if (GetGame() == GAME_RepCommando) goto base_mesh;
#endif

	if (!LODModels.Num())
	{
	base_mesh:
		guard(ConvertBaseMesh);

		// create CSkelMeshLod from base mesh
		CSkelMeshLod *Lod = new (Mesh->Lods) CSkelMeshLod;
		Lod->NumTexCoords = 1;
		Lod->HasNormals   = false;
		Lod->HasTangents  = false;

		if (Points.Num() && Wedges.Num() && VertInfluences.Num())
		{
			InitSections(*Lod);
			ConvertWedges(*Lod, Points, Wedges, VertInfluences);
			BuildIndices(*Lod);
		}
		else
		{
			appPrintf("ERROR: bad base mesh\n");
		}
		goto skeleton;

		unguard;
	}

	// convert LODs
	for (int lod = 0; lod < LODModels.Num(); lod++)
	{
		guard(ConvertLod);

		const FStaticLODModel &SrcLod = LODModels[lod];

#if DEBUG_SKELMESH
		appPrintf("  Lod %d: Points[%d] Wedges[%d] Influences[%d] Faces[%d]  Rigid(Sec[%d] Indices[%d] Verts[%d])  Soft(Sec[%d] Indices[%d] Verts[%d] Stream[%d])\n",
			lod, SrcLod.Points.Num(), SrcLod.Wedges.Num(), SrcLod.VertInfluences.Num(), SrcLod.Faces.Num(),
			SrcLod.RigidSections.Num(), SrcLod.RigidIndices.Indices.Num(), SrcLod.VertexStream.Verts.Num(),
			SrcLod.SoftSections.Num(), SrcLod.SoftIndices.Indices.Num(), SrcLod.SkinPoints.Num(), SrcLod.SkinningData.Num()
		);
#endif
//		if (SrcLod.Faces.Num() == 0 && SrcLod.SoftSections.Num() > 0)
//			continue;

		CSkelMeshLod *Lod = new (Mesh->Lods) CSkelMeshLod;
		Lod->NumTexCoords = 1;
		Lod->HasNormals   = false;
		Lod->HasTangents  = false;

		if (IsCorrectLOD(SrcLod))
		{
			InitSections(*Lod);
			ConvertWedges(*Lod, SrcLod.Points, SrcLod.Wedges, SrcLod.VertInfluences);
			BuildIndicesForLod(*Lod, SrcLod);
		}
		else
		{
			appPrintf("WARNING: bad LOD#%d mesh, switching to base\n", lod);
			if (lod == 0)
			{
				Mesh->Lods.Empty();
				goto base_mesh;
			}
			else
			{
				Mesh->Lods.RemoveAt(lod);
				break;
			}
		}

		unguard;
	}

skeleton:
	// copy skeleton
	guard(ProcessSkeleton);
	Mesh->RefSkeleton.Empty(RefSkeleton.Num());
	for (int i = 0; i < RefSkeleton.Num(); i++)
	{
		const FMeshBone &B = RefSkeleton[i];
		CSkelMeshBone *Dst = new (Mesh->RefSkeleton) CSkelMeshBone;
		Dst->Name        = B.Name;
		Dst->ParentIndex = B.ParentIndex;
		Dst->Position    = CVT(B.BonePos.Position);
		Dst->Orientation = CVT(B.BonePos.Orientation);
#if !BAKE_BONE_SCALES
		Dst->Scale.Set(1, 1, 1);
#endif
	}
	unguard; // ProcessSkeleton

	if (Animation && Animation->ConvertedAnim && Animation->ConvertedAnim->TrackBoneNames.Num() == Mesh->RefSkeleton.Num())
	{
		CAnimSet *AnimSet = Animation->ConvertedAnim;
		bool bMatchesSkeleton = true;
		for (int i = 0; i < Mesh->RefSkeleton.Num(); i++)
		{
			if (stricmp(*AnimSet->TrackBoneNames[i], *Mesh->RefSkeleton[i].Name))
			{
				bMatchesSkeleton = false;
				break;
			}
		}

		if (bMatchesSkeleton)
		{
			AnimSet->BonePositions.Empty(Mesh->RefSkeleton.Num());
			for (int i = 0; i < Mesh->RefSkeleton.Num(); i++)
			{
				CSkeletonBonePosition BonePosition;
				BonePosition.Position    = Mesh->RefSkeleton[i].Position;
				BonePosition.Orientation = Mesh->RefSkeleton[i].Orientation;
				AnimSet->BonePositions.Add(BonePosition);
			}

			for (int SeqIndex = 0; SeqIndex < AnimSet->Sequences.Num(); SeqIndex++)
			{
				CAnimSequence *Seq = AnimSet->Sequences[SeqIndex];
				for (int BoneIndex = 0; BoneIndex < Seq->Tracks.Num() && BoneIndex < Mesh->RefSkeleton.Num(); BoneIndex++)
				{
					CAnimTrack *Track = Seq->Tracks[BoneIndex];
					if (!Track->KeyPos.Num())
						Track->KeyPos.Add(Mesh->RefSkeleton[BoneIndex].Position);
					if (!Track->KeyQuat.Num())
						Track->KeyQuat.Add(Mesh->RefSkeleton[BoneIndex].Orientation);
				}
			}
		}
	}

	// copy sockets
	int NumSockets = AttachAliases.Num();
	Mesh->Sockets.Empty(NumSockets);
	for (int i = 0; i < NumSockets; i++)
	{
		CSkelMeshSocket *DS = new (Mesh->Sockets) CSkelMeshSocket;
		DS->Name      = AttachAliases[i];
		DS->Bone      = AttachBoneNames[i];
		DS->Transform = CVT(AttachCoords[i]);
	}

	Mesh->FinalizeMesh();

	unguard;
}


void USkeletalMesh::InitSections(CSkelMeshLod &Lod)
{
	// allocate sections and set CMeshSection.Material
	Lod.Sections.AddZeroed(Materials.Num());
	for (int sec = 0; sec < Materials.Num(); sec++)
	{
		const FMeshMaterial &M = Materials[sec];
		CMeshSection &Sec = Lod.Sections[sec];
		int TexIndex  = M.TextureIndex;
		UUnrealMaterial *Mat = (TexIndex < Textures.Num()) ? Textures[TexIndex] : NULL;
#if RENDERING
		Sec.Material = UMaterialWithPolyFlags::Create(Mat, M.PolyFlags);
#endif
	}
}

void USkeletalMesh::ConvertWedges(CSkelMeshLod &Lod, const TArray<FVector> &MeshPoints, const TArray<FMeshWedge> &MeshWedges, const TArray<FVertInfluence> &VertInfluences)
{
	guard(USkeletalMesh::ConvertWedges);

	struct CVertInfo
	{
		int		NumInfs;		// may be higher than NUM_INFLUENCES
		int		Bone[NUM_INFLUENCES];
		float	Weight[NUM_INFLUENCES];
	};

	int i, j;

	CVertInfo *Verts = new CVertInfo[MeshPoints.Num()];
	memset(Verts, 0, MeshPoints.Num() * sizeof(CVertInfo));

	// collect influences per vertex
	for (i = 0; i < VertInfluences.Num(); i++)
	{
		const FVertInfluence &Inf = VertInfluences[i];
		CVertInfo &V = Verts[Inf.PointIndex];
		int NumInfs = V.NumInfs++;
		int idx = NumInfs;
		if (NumInfs >= NUM_INFLUENCES)
		{
			// overflow
			// find smallest weight smaller than current
			float w = Inf.Weight;
			idx = -1;
			for (j = 0; j < NUM_INFLUENCES; j++)
			{
				if (V.Weight[j] < w)
				{
					w = V.Weight[j];
					idx = j;
					// continue - may be other weight will be even smaller
				}
			}
			if (idx < 0) continue;	// this weight is smaller than other
		}
		// add influence
		V.Bone[idx]   = Inf.BoneIndex;
		V.Weight[idx] = Inf.Weight;
	}

	// normalize influences
	for (i = 0; i < MeshPoints.Num(); i++)
	{
		CVertInfo &V = Verts[i];
		if (V.NumInfs == 0)
		{
			appPrintf("WARNING: Vertex %d has 0 influences\n", i);
			V.NumInfs = 1;
			V.Bone[0] = 0;
			V.Weight[0] = 1.0f;
			continue;
		}
		if (V.NumInfs <= NUM_INFLUENCES) continue;	// no normalization is required
		float s = 0;
		for (j = 0; j < NUM_INFLUENCES; j++)		// count sum
			s += V.Weight[j];
		s = 1.0f / s;
		for (j = 0; j < NUM_INFLUENCES; j++)		// adjust weights
			V.Weight[j] *= s;
	}

	// create vertices
	Lod.AllocateVerts(MeshWedges.Num());
	for (i = 0; i < MeshWedges.Num(); i++)
	{
		const FMeshWedge &SW = MeshWedges[i];
		CSkelMeshVertex  &DW = Lod.Verts[i];
		DW.Position = CVT(MeshPoints[SW.iVertex]);
		DW.UV = CVT(SW.TexUV);
		// DW.Normal and DW.Tangent are unset
		// setup Bone[] and Weight[]
		const CVertInfo &V = Verts[SW.iVertex];
		unsigned PackedWeights = 0;
		for (j = 0; j < V.NumInfs; j++)
		{
			DW.Bone[j]   = V.Bone[j];
			PackedWeights |= appRound(V.Weight[j] * 255) << (j * 8);
		}
		DW.PackedWeights = PackedWeights;
		for (/* continue */; j < NUM_INFLUENCES; j++)	// place end marker and zero weight
		{
			DW.Bone[j] = -1;
		}
	}

	delete[] Verts;

	unguard;
}


void USkeletalMesh::BuildIndices(CSkelMeshLod &Lod)
{
	guard(USkeletalMesh::BuildIndices);

	int i;
	int NumSections = Lod.Sections.Num();

	// 1st pass: count Lod.Sections[i].NumFaces
	// 2nd pass: set Lod.Sections[i].FirstIndex, allocate and fill indices array
	for (int pass = 0; pass < 2; pass++)
	{
		int NumIndices = 0;
		for (i = 0; i < NumSections; i++)
		{
			CMeshSection &Sec = Lod.Sections[i];
			if (pass == 1)
			{
				int SecIndices = Sec.NumFaces * 3;
				Sec.FirstIndex = NumIndices;
				NumIndices += SecIndices;
			}
			Sec.NumFaces = 0;
		}

		// allocate index buffer
		if (pass == 1)
		{
			Lod.Indices.Indices16.AddZeroed(NumIndices);
		}

		for (i = 0; i < Triangles.Num(); i++)
		{
			const VTriangle &Face = Triangles[i];
			int MatIndex = Face.MatIndex;
			// if section does not exist - add it
			if (MatIndex >= NumSections)
			{
				Lod.Sections.AddZeroed(MatIndex - NumSections + 1);
				NumSections = MatIndex + 1;
			}
			CMeshSection &Sec = Lod.Sections[MatIndex];

			if (pass == 1)	// on 1st pass count NumFaces, on 2nd pass fill indices
			{
				uint16 *idx = &Lod.Indices.Indices16[Sec.FirstIndex + Sec.NumFaces * 3];
				for (int j = 0; j < 3; j++)
					*idx++ = Face.WedgeIndex[j];
			}

			Sec.NumFaces++;
		}
	}

	unguard;
}


/* TODO:
 * The most correct way of converting FStaticLODModel to CSkelMeshLod is using soft and rigid sections.
 *   RigidSection: RigidIndices -> VertexStream (contains position and UV)
 *   SoftSection: SoftIndices -> SkinningData (encoded: contains index in SkinPoints plus UV and influences)
 * NOTE: there's no common vertex stream here, these vertex and index buffers are entirely separate.
 *
 * Current implementation is not correct, and it is failed when mesh has both rigid and soft sections (or
 * when TLazyArray part of FStaticLODModel erased). Should be fixed ConvertWedges (separate function for LOD
 * model) and BuildIndicesForLod. ConvertWedges should process SkinningData, BuildIndicesForLod should
 * use index buffers for all sections (i.e. do not use Faces for soft sections).
 * Possible reason of this: it seems when SkinningData generated by the engine, it reorders vertices so
 * soft vertices goes before rigid.
 */
void USkeletalMesh::BuildIndicesForLod(CSkelMeshLod &Lod, const FStaticLODModel &SrcLod)
{
	guard(USkeletalMesh::BuildIndicesForLod);

	int i;
	int NumSections = Lod.Sections.Num();

	// 1st pass: count Lod.Sections[i].NumFaces
	// 2nd pass: set Lod.Sections[i].FirstIndex, allocate and fill indices array
	for (int pass = 0; pass < 2; pass++)
	{
		int NumIndices = 0;
		for (i = 0; i < NumSections; i++)
		{
			CMeshSection &Sec = Lod.Sections[i];
			if (pass == 1)
			{
				int SecIndices = Sec.NumFaces * 3;
				Sec.FirstIndex = NumIndices;
				NumIndices += SecIndices;
			}
			Sec.NumFaces = 0;
		}

		// allocate index buffer
		if (pass == 1)
		{
			Lod.Indices.Indices16.AddZeroed(NumIndices);
		}

		int s;

		// soft sections (influence count >= 2)
		for (s = 0; s < SrcLod.SoftSections.Num(); s++)
		{
			const FSkelMeshSection &ms = SrcLod.SoftSections[s];
			int MatIndex = ms.MaterialIndex;
			// if section does not exist - add it
			if (MatIndex >= NumSections)
			{
				Lod.Sections.AddZeroed(MatIndex - NumSections + 1);
				NumSections = MatIndex + 1;
			}
			CMeshSection &Sec = Lod.Sections[MatIndex];

			if (pass == 1)
			{
				uint16 *idx = &Lod.Indices.Indices16[Sec.FirstIndex + Sec.NumFaces * 3];
//				printf("sidx[%d]: %d + %d (nf=%d) -> %d\n", MatIndex, Sec.FirstIndex + Sec.NumFaces * 3, ms.FirstFace * 3, ms.NumFaces, Sec.FirstIndex + Sec.NumFaces * 3 + ms.NumFaces * 3);
				for (i = 0; i < ms.NumFaces; i++)
				{
					const FMeshFace &F = SrcLod.Faces[ms.FirstFace + i];
					//?? ignore F.MaterialIndex - may be any
//					assert(F.MaterialIndex == ms.MaterialIndex);
					for (int j = 0; j < 3; j++)
						*idx++ = F.iWedge[j];
				}
			}

			Sec.NumFaces += ms.NumFaces;
		}
		// rigid sections (influence count == 1)
		// code is similar to the block above
		for (s = 0; s < SrcLod.RigidSections.Num(); s++)
		{
			const FSkelMeshSection &ms = SrcLod.RigidSections[s];
			int MatIndex = ms.MaterialIndex;
			// if section does not exist - add it
			if (MatIndex >= NumSections)
			{
				Lod.Sections.AddZeroed(MatIndex - NumSections + 1);
				NumSections = MatIndex + 1;
			}
			CMeshSection &Sec  = Lod.Sections[MatIndex];

			if (pass == 1)
			{
				uint16 *idx = &Lod.Indices.Indices16[Sec.FirstIndex + Sec.NumFaces * 3];
				uint16 firstIndex = ms.FirstFace * 3;
//				printf("ridx[%d]: %d + %d * 3 (nf=%d) -> %d\n", MatIndex, Sec.FirstIndex + Sec.NumFaces * 3, ms.FirstFace * 3, ms.NumFaces, Sec.FirstIndex + Sec.NumFaces * 3 + ms.NumFaces * 3);
				for (i = 0; i < ms.NumFaces * 3; i++)
					*idx++ = SrcLod.RigidIndices.Indices[firstIndex + i];
			}

			Sec.NumFaces += ms.NumFaces;
		}
	}

	unguard;
}


bool USkeletalMesh::IsCorrectLOD(const FStaticLODModel &Lod) const
{
	if (!Lod.Points.Num() || !Lod.Wedges.Num() || !Lod.VertInfluences.Num())
		return false;
	if (!(Lod.RigidIndices.Indices.Num() + Lod.SoftIndices.Indices.Num()) && !Lod.Faces.Num())
		return false;

	if ((Lod.RigidSections.Num() != 0) && (Lod.SoftSections.Num() != 0))
		return false; //!! we can't reliably support this kind of mesh - see note before BuildIndicesForLod()

	//?? really any mesh with rigid sections should be considered as "bad" (we can't work with such LODs)

	int i;
	int NumPoints = Lod.Points.Num();
	int NumWedges = Lod.Wedges.Num();

	// verify influences
	for (i = 0; i < Lod.VertInfluences.Num(); i++)
	{
		int PointIndex = Lod.VertInfluences[i].PointIndex;
		if (PointIndex < 0 || PointIndex >= NumPoints) return false;
	}

	// verify indices (only RigidIndices, SoftIndices aren't used)
	for (i = 0; i < Lod.RigidIndices.Indices.Num(); i++)
	{
		int WedgeIndex = Lod.RigidIndices.Indices[i];
		if (WedgeIndex < 0 || WedgeIndex >= NumWedges) return false;
	}

	int TotalFaces = 0;

	// soft sections (influence count >= 2)
	int s;
	for (s = 0; s < Lod.SoftSections.Num(); s++)
	{
		const FSkelMeshSection &ms = Lod.SoftSections[s];
		TotalFaces += ms.NumFaces;
//		int MatIndex = ms.MaterialIndex;
//		if (MatIndex < 0 || MatIndex >= ... //??
	}
	// rigid sections (influence count == 1)
	for (s = 0; s < Lod.RigidSections.Num(); s++)
	{
		const FSkelMeshSection &ms = Lod.RigidSections[s];
		TotalFaces += ms.NumFaces;
//		int MatIndex = ms.MaterialIndex; ... //??
	}

	if (!TotalFaces) return false;

	return true;
}


#if SPLINTER_CELL

struct FSCellUnk1
{
	int				f0, f4, f8, fC;

	friend FArchive& operator<<(FArchive &Ar, FSCellUnk1 &S)
	{
		return Ar << S.f0 << S.f4 << S.f8 << S.fC;
	}
};

SIMPLE_TYPE(FSCellUnk1, int)

struct FSCellUnk2
{
	int				f0, f4, f8, fC, f10;

	friend FArchive& operator<<(FArchive &Ar, FSCellUnk2 &S)
	{
		return Ar << S.f0 << S.f4 << S.f10 << S.f8 << S.fC;
	}
};

struct FSCellUnk3
{
	int				f0, f4, f8, fC;

	friend FArchive& operator<<(FArchive &Ar, FSCellUnk3 &S)
	{
		return Ar << S.f0 << S.fC << S.f4 << S.f8;
	}
};

struct FSCellUnk4a
{
	FVector			f0;
	FVector			fC;
	int				f18;					// float?

	friend FArchive& operator<<(FArchive &Ar, FSCellUnk4a &S)
	{
		return Ar << S.f0 << S.fC << S.f18;
	}
};

SIMPLE_TYPE(FSCellUnk4a, float)

struct FSCellUnk4
{
	int				Size;
	int				Count;
	TArray<FString>	BoneNames;				// BoneNames.Num() == Count
	FString			f14;
	FSCellUnk4a**	Data;					// (FSCellUnk4a*)[Count][Size]

	FSCellUnk4()
	:	Data(NULL)
	{}
	~FSCellUnk4()
	{
		// cleanup data
		if (Data)
		{
			for (int i = 0; i < Count; i++)
				delete Data[i];
			delete Data;
		}
	}

	friend FArchive& operator<<(FArchive &Ar, FSCellUnk4 &S)
	{
		int i, j;

		// serialize scalars
		Ar << S.Size << S.Count << S.BoneNames << S.f14;

		if (Ar.IsLoading)
		{
			// allocate array of pointers
			S.Data = new FSCellUnk4a* [S.Count];
			// allocate arrays
			for (i = 0; i < S.Count; i++)
				S.Data[i] = new FSCellUnk4a [S.Size];
		}
		// serialize arrays
		for (i = 0; i < S.Count; i++)
		{
			FSCellUnk4a* Ptr = S.Data[i];
			for (j = 0; j < S.Size; j++, Ptr++)
				Ar << *Ptr;
		}
		return Ar;
	}
};

static int ReadSCCTMeshRefSkeleton(FArchive &Ar, UnPackage *Package, int Pos, int Stop, TArray<FMeshBone> &Bones)
{
	guard(ReadSCCTMeshRefSkeleton);
	int SavePos = Ar.Tell();
	Ar.Seek(Pos);

	int BoneCount;
	Ar << AR_INDEX(BoneCount);
	if (BoneCount < 2 || BoneCount > 256)
	{
		Ar.Seek(SavePos);
		return 0;
	}

	TArray<FMeshBone> TmpBones;
	TmpBones.AddZeroed(BoneCount);
	int ParentCounts[256];
	memset(ParentCounts, 0, sizeof(ParentCounts));
	int ValidQuats = 0;
	for (int i = 0; i < BoneCount; i++)
	{
		int NameIndex;
		unsigned Flags;
		FQuat Orientation;
		FVector Position;
		float Length;
		FVector Size;
		int NumChildren;
		int ParentIndex;

		Ar << AR_INDEX(NameIndex);
		if (!Package || unsigned(NameIndex) >= Package->Summary.NameCount || Ar.Tell() + 56 > Stop)
		{
			Ar.Seek(SavePos);
			return 0;
		}
		Ar << Flags << Orientation << Position << Length << Size << NumChildren << ParentIndex;
		if (ParentIndex < 0 || ParentIndex >= BoneCount || (i > 0 && ParentIndex >= i))
		{
			Ar.Seek(SavePos);
			return 0;
		}
		float QuatLen = Orientation.X * Orientation.X + Orientation.Y * Orientation.Y + Orientation.Z * Orientation.Z + Orientation.W * Orientation.W;
		if (QuatLen < 0.5f || QuatLen > 1.5f || fabs(Position.X) > 1000 || fabs(Position.Y) > 1000 || fabs(Position.Z) > 1000 ||
			Position.X != Position.X || Position.Y != Position.Y || Position.Z != Position.Z)
		{
			Ar.Seek(SavePos);
			return 0;
		}
		if (QuatLen > 0.8f && QuatLen < 1.2f)
			ValidQuats++;
		if (i > 0)
			ParentCounts[ParentIndex]++;

		FMeshBone &B = TmpBones[i];
		B.Name = Package->GetName(NameIndex);
		B.Flags = Flags;
		B.BonePos.Orientation = Orientation;
		B.BonePos.Position = Position;
		B.BonePos.Length = Length;
		B.BonePos.Size = Size;
		B.NumChildren = NumChildren;
		B.ParentIndex = ParentIndex;
	}

	int MatchingChildren = 0;
	for (int i = 0; i < BoneCount; i++)
	{
		if (TmpBones[i].NumChildren == ParentCounts[i])
			MatchingChildren++;
		TmpBones[i].NumChildren = ParentCounts[i];
	}

	CopyArray(Bones, TmpBones);
	Ar.Seek(SavePos);
	return BoneCount * 100 + MatchingChildren * 25 + ValidQuats * 5;
	unguard;
}

static bool FindSCCTMeshRefSkeleton(FArchive &Ar, UnPackage *Package, int Start, int Stop, TArray<FMeshBone> &Bones)
{
	guard(FindSCCTMeshRefSkeleton);
	int BestPos = 0;
	int BestScore = 0;
	TArray<FMeshBone> BestBones;
	for (int Pos = Start; Pos < Stop - 128; Pos++)
	{
		TArray<FMeshBone> TmpBones;
		int Score = ReadSCCTMeshRefSkeleton(Ar, Package, Pos, Stop, TmpBones);
		if (Score > BestScore)
		{
			BestScore = Score;
			BestPos = Pos;
			CopyArray(BestBones, TmpBones);
			int PerfectScore = TmpBones.Num() * (100 + 25 + 5);
			if (TmpBones.Num() && BestScore >= PerfectScore)
				break;
		}
	}
	if (BestScore)
	{
		CopyArray(Bones, BestBones);
		appPrintf("SCCT mesh RefSkeleton block: %08X bones=%d score=%d\n", BestPos, Bones.Num(), BestScore);
		return true;
	}
	Bones.Empty();
	return false;
	unguard;
}

struct FSCCTBonePalette
{
	int		FirstWedge;
	int		LastWedge;
	byte	Map[256];

	void Clear()
	{
		FirstWedge = LastWedge = 0;
		memset(Map, 0xFF, sizeof(Map));
	}
};

static bool ReadSCCTBonePalette(FArchive &Ar, int Pos, int Stop, int BoneCount, FSCCTBonePalette &Palette)
{
	guard(ReadSCCTBonePalette);
	if (Pos + 1 >= Stop)
		return false;

	int SavePos = Ar.Tell();
	Ar.Seek(Pos);
	byte Count;
	Ar << Count;
	if (Count < 1 || Count > 64 || Pos + 1 + Count * 2 > Stop)
	{
		Ar.Seek(SavePos);
		return false;
	}

	Palette.Clear();
	for (int i = 0; i < Count; i++)
	{
		byte GlobalBone, LocalBone;
		Ar << GlobalBone << LocalBone;
		if (GlobalBone >= BoneCount)
		{
			Ar.Seek(SavePos);
			return false;
		}
		Palette.Map[LocalBone] = GlobalBone;
	}

	Ar.Seek(SavePos);
	return true;
	unguard;
}

static bool BuildSCCTPaletteChain(const TArray<FSCCTBonePalette> &Candidates, int CurrentWedge, int WedgeCount, TArray<int> &Chain)
{
	if (CurrentWedge == WedgeCount)
		return true;
	for (int i = 0; i < Candidates.Num(); i++)
	{
		const FSCCTBonePalette &P = Candidates[i];
		if (P.FirstWedge != CurrentWedge || P.LastWedge < P.FirstWedge)
			continue;
		new (Chain) int(i);
		if (BuildSCCTPaletteChain(Candidates, P.LastWedge + 1, WedgeCount, Chain))
			return true;
		Chain.RemoveAt(Chain.Num() - 1);
	}
	return false;
}

static bool FindSCCTBonePalettes(FArchive &Ar, int Start, int Stop, int WedgeCount, int BoneCount, TArray<FSCCTBonePalette> &Palettes)
{
	guard(FindSCCTBonePalettes);
	Palettes.Empty();
	TArray<FSCCTBonePalette> Candidates;
	int SavePos = Ar.Tell();

	for (int Pos = Start + 24; Pos < Stop - 128; Pos++)
	{
		FSCCTBonePalette Palette;
		if (!ReadSCCTBonePalette(Ar, Pos, Stop, BoneCount, Palette))
			continue;

		Ar.Seek(Pos - 18);
		uint16 SectionIndex, Unknown, FirstWedge, LastWedge, WedgeSpan;
		Ar << SectionIndex << Unknown << FirstWedge << LastWedge << WedgeSpan;
		if (SectionIndex > 128 || FirstWedge > LastWedge || LastWedge >= WedgeCount || WedgeSpan != LastWedge - FirstWedge + 1)
			continue;

		Palette.FirstWedge = FirstWedge;
		Palette.LastWedge = LastWedge;
		new (Candidates) FSCCTBonePalette(Palette);
	}

	TArray<int> Chain;
	if (BuildSCCTPaletteChain(Candidates, 0, WedgeCount, Chain))
	{
		for (int i = 0; i < Chain.Num(); i++)
		{
			const FSCCTBonePalette &P = Candidates[Chain[i]];
			new (Palettes) FSCCTBonePalette(P);
		}
		appPrintf("SCCT bone palettes: %d\n", Palettes.Num());
	}

	Ar.Seek(SavePos);
	return Palettes.Num() > 0;
	unguard;
}

static const FSCCTBonePalette* FindSCCTPaletteForWedge(const TArray<FSCCTBonePalette> &Palettes, int WedgeIndex)
{
	for (int i = 0; i < Palettes.Num(); i++)
	{
		if (WedgeIndex >= Palettes[i].FirstWedge && WedgeIndex <= Palettes[i].LastWedge)
			return &Palettes[i];
	}
	return NULL;
}

static bool ReadSCCTWedgeInfluences(FArchive &Ar, int Pos, int Stop, const TArray<FVector> &Points, const TArray<FMeshWedge> &Wedges, int BoneCount, const TArray<FSCCTBonePalette> &Palettes, TArray<FVertInfluence> &Influences, TArray<FVector> *OutPoints = NULL, int *OutScore = NULL)
{
	guard(ReadSCCTWedgeInfluences);
	const int Stride = 36;
	int WedgeCount = Wedges.Num();
	if (!WedgeCount || Pos + WedgeCount * Stride > Stop)
		return false;

	int SavePos = Ar.Tell();
	int PosMatches = 0;
	int ValidWeights = 0;
	int Tested = 0;
	for (int i = 0; i < WedgeCount; i++)
	{
		Ar.Seek(Pos + i * Stride);
		FVector Position;
		Ar << Position;
		if (Position.X != Position.X || Position.Y != Position.Y || Position.Z != Position.Z ||
			fabs(Position.X) > 10000 || fabs(Position.Y) > 10000 || fabs(Position.Z) > 10000)
		{
			Ar.Seek(SavePos);
			return false;
		}
		const FMeshWedge &W = Wedges[i];
		if (Points.IsValidIndex(W.iVertex))
		{
			const FVector &P = Points[W.iVertex];
			float DX = Position.X - P.X;
			float DY = Position.Y - P.Y;
			float DZ = Position.Z - P.Z;
			float DistSq = DX * DX + DY * DY + DZ * DZ;
			if (DistSq < 0.01f)
				PosMatches++;
		}

		byte Bone[4], Weight[4];
		Ar.Seek(Pos + i * Stride + 28);
		for (int j = 0; j < 4; j++)
			Ar << Bone[j];
		int WeightSum = 0;
		bool bValidBones = true;
		for (int j = 0; j < 4; j++)
		{
			Ar << Weight[j];
			WeightSum += Weight[j];
			const FSCCTBonePalette *Palette = FindSCCTPaletteForWedge(Palettes, i);
			int BoneIndex = (Palette && Palette->Map[Bone[j]] != 0xFF) ? Palette->Map[Bone[j]] : Bone[j];
			if (Weight[j] && BoneIndex >= BoneCount)
				bValidBones = false;
		}
		if (bValidBones && WeightSum == 255)
			ValidWeights++;
		Tested++;
	}

	if (PosMatches < WedgeCount * 9 / 10 || ValidWeights < WedgeCount * 9 / 10)
	{
		Ar.Seek(SavePos);
		return false;
	}

	if (!OutPoints && !OutScore)
	{
		Ar.Seek(SavePos);
		return true;
	}
	if (OutScore && !OutPoints)
	{
		*OutScore = PosMatches + ValidWeights;
		Ar.Seek(SavePos);
		return true;
	}

	Influences.Empty(WedgeCount * 4);
	if (OutPoints)
	{
		OutPoints->Empty(WedgeCount);
		OutPoints->AddZeroed(WedgeCount);
	}
	for (int i = 0; i < WedgeCount; i++)
	{
		if (OutPoints)
		{
			Ar.Seek(Pos + i * Stride);
			Ar << (*OutPoints)[i];
		}
		byte Bone[4], Weight[4];
		Ar.Seek(Pos + i * Stride + 28);
		for (int j = 0; j < 4; j++)
			Ar << Bone[j];
		for (int j = 0; j < 4; j++)
			Ar << Weight[j];
		for (int j = 0; j < 4; j++)
		{
			if (!Weight[j])
				continue;
			const FSCCTBonePalette *Palette = FindSCCTPaletteForWedge(Palettes, i);
			int BoneIndex = (Palette && Palette->Map[Bone[j]] != 0xFF) ? Palette->Map[Bone[j]] : Bone[j];
			FVertInfluence *I = new (Influences) FVertInfluence;
			I->Weight = Weight[j] / 255.0f;
			I->PointIndex = i;
			I->BoneIndex = BoneIndex;
		}
	}

	if (OutScore)
		*OutScore = PosMatches + ValidWeights;
	Ar.Seek(SavePos);
	return true;
	unguard;
}

static bool FindSCCTWedgeInfluences(FArchive &Ar, int Start, int Stop, const TArray<FVector> &Points, const TArray<FMeshWedge> &Wedges, int BoneCount, const TArray<FSCCTBonePalette> &Palettes, TArray<FVertInfluence> &Influences, TArray<FVector> &WedgePoints)
{
	guard(FindSCCTWedgeInfluences);
	const int Stride = 36;
	int WedgeCount = Wedges.Num();
	int BestPos = 0;
	int BestScore = 0;
	int PerfectScore = WedgeCount * 2;
	for (int Pass = 0; Pass < 2 && !BestScore; Pass++)
	{
		int Step = (Pass == 0) ? 4 : 1;
		for (int Pos = Start; Pos <= Stop - WedgeCount * Stride; Pos += Step)
		{
			if (Pass == 1 && ((Pos - Start) & 3) == 0)
				continue;
			int Score = 0;
			if (ReadSCCTWedgeInfluences(Ar, Pos, Stop, Points, Wedges, BoneCount, Palettes, Influences, NULL, &Score) && Score > BestScore)
			{
				BestScore = Score;
				BestPos = Pos;
				if (BestScore >= PerfectScore)
					break;
			}
		}
	}
	if (BestScore)
	{
		if (!ReadSCCTWedgeInfluences(Ar, BestPos, Stop, Points, Wedges, BoneCount, Palettes, Influences, &WedgePoints, NULL))
			return false;
		appPrintf("SCCT wedge influences: %08X wedges=%d influences=%d score=%d\n", BestPos, Wedges.Num(), Influences.Num(), BestScore);
		return true;
	}
	Influences.Empty();
	return false;
	unguard;
}

void USkeletalMesh::SerializeSCell(FArchive &Ar)
{
	const bool isDoubleAgentMeshLayout =
		(Ar.ArVer >= 173 && Ar.ArVer <= 275 && Ar.ArLicenseeVer == 0) ||
		(Ar.ArVer == 100 && Ar.ArLicenseeVer >= 127);
	const bool debugDoubleAgent = (isDoubleAgentMeshLayout && getenv("SC4_DEBUG_MESH"));
	if (debugDoubleAgent)
	{
		appPrintf("SC4 SerializeSCell start %s pos=%08X version=%d\n", Name, Ar.Tell(), Version);
		int SavePos = Ar.Tell();
		byte Bytes[64];
		int Count = min(ARRAY_COUNT(Bytes), Ar.GetStopper() - SavePos);
		appPrintf("SC4 mesh byte count=%d stopper=%08X\n", Count, Ar.GetStopper());
		if (Count > 0)
		{
			Ar.Serialize(Bytes, Count);
			appPrintf("SC4 mesh bytes:");
			for (int i = 0; i < Count; i++)
				appPrintf(" %02X", Bytes[i]);
			appPrintf("\n");
			Ar.Seek(SavePos);
		}
		if (getenv("SCDA_DUMP_MESH_RAW"))
		{
			char Filename[256];
			appSprintf(ARRAY_ARG(Filename), "scda_%s_raw.bin", Name);
			FILE *F = fopen(Filename, "wb");
			if (F)
			{
				int Size = Ar.GetStopper() - SavePos;
				TArray<byte> Raw;
				Raw.AddUninitialized(Size);
				Ar.Serialize(Raw.GetData(), Size);
				fwrite(Raw.GetData(), 1, Size, F);
				fclose(F);
				appPrintf("SCDA dumped raw mesh export: %s size=%d\n", Filename, Size);
				Ar.Seek(SavePos);
			}
		}
	}
	if (isDoubleAgentMeshLayout)
	{
		int ScanStart = Ar.Tell();
		int Stop = Ar.GetStopper();
		DumpSCDAMeshCompactNames(Ar, Package, ScanStart, Stop);
		DumpSCDAPackedVertexCandidates(Ar, ScanStart, Stop);

		int FacesPos = FindPandoraLazyArray(Ar, ScanStart, Stop, sizeof(FMeshFace), FaceLevel.Num(), 1);
		if (getenv("SCDA_FORCE_RAW_MESH"))
			FacesPos = 0;
		if (!FacesPos)
		{
			if (debugDoubleAgent)
				DumpSC4LazyArrays(Ar, ScanStart, Stop);

			int RawPointsPos = 0;
			int RawIndicesPos = 0;
			TArray<FVector> RawPoints;
			TArray<VTriangle> RawTriangles;
			bool bHaveRawMesh = false;
			TArray<byte> RawExportData;
			FArchive *RawScanAr = &Ar;
			int RawScanStart = ScanStart;
			int RawScanStop = Stop;
			FMemReader *RawMemReader = NULL;
			if (getenv("SCDA_FORCE_RAW_MESH"))
			{
				int SavePos = Ar.Tell();
				Ar.Seek(ScanStart);
				RawExportData.AddUninitialized(Stop - ScanStart);
				Ar.Serialize(RawExportData.GetData(), RawExportData.Num());
				Ar.Seek(SavePos);
				RawMemReader = new FMemReader(RawExportData.GetData(), RawExportData.Num());
				RawScanAr = RawMemReader;
				RawScanStart = 0;
				RawScanStop = RawExportData.Num();
			}
			if (FindSCDARawVectorBlock(*RawScanAr, RawScanStart, RawScanStop, RawPoints, RawPointsPos))
			{
				bHaveRawMesh = FindSCDARawIndexBlock(*RawScanAr, RawScanStart, RawScanStop, RawPoints, RawPointsPos, RawTriangles, RawIndicesPos);
				if (!bHaveRawMesh && RawPoints.Num() >= 96)
				{
					int PointCount = RawPoints.Num() - RawPoints.Num() % 3;
					RawPoints.RemoveAt(PointCount, RawPoints.Num() - PointCount);
					int TriangleCount = PointCount / 3;
					RawTriangles.Empty(TriangleCount);
					RawTriangles.AddZeroed(TriangleCount);
					for (int i = 0; i < TriangleCount; i++)
					{
						RawTriangles[i].WedgeIndex[0] = i * 3 + 0;
						RawTriangles[i].WedgeIndex[1] = i * 3 + 1;
						RawTriangles[i].WedgeIndex[2] = i * 3 + 2;
					}
					bHaveRawMesh = true;
				}
			}
			if (!bHaveRawMesh)
				bHaveRawMesh = FindSCDAPacked17TriangleSoup(*RawScanAr, RawScanStart, RawScanStop, RawPoints, RawTriangles, RawPointsPos);
			delete RawMemReader;
			if (bHaveRawMesh)
			{
				CopyArray(Points, RawPoints);
				CopyArray(Triangles, RawTriangles);
				Wedges.Empty(Points.Num());
				Wedges.AddZeroed(Points.Num());
				for (int i = 0; i < Wedges.Num(); i++)
					Wedges[i].iVertex = i;
				RefSkeleton.Empty(1);
				RefSkeleton.AddZeroed(1);
				RefSkeleton[0].Name = "B";
				RefSkeleton[0].BonePos.Orientation.W = 1.0f;
				RefSkeleton[0].BonePos.Size.Set(1.0f, 1.0f, 1.0f);
				VertInfluences.Empty(Points.Num());
				VertInfluences.AddZeroed(Points.Num());
				for (int i = 0; i < VertInfluences.Num(); i++)
				{
					VertInfluences[i].Weight = 1.0f;
					VertInfluences[i].PointIndex = i;
					VertInfluences[i].BoneIndex = 0;
				}
				if (debugDoubleAgent)
					appPrintf("SCDA raw mesh fallback: points=%d triangles=%d pointsPos=%08X indicesPos=%08X\n",
						Points.Num(), Triangles.Num(), RawPointsPos, RawIndicesPos);
				DROP_REMAINING_DATA(Ar);
				ConvertMesh();
				return;
			}

			int PointsPos = FindPandoraLazyArray(Ar, ScanStart, Stop, sizeof(FVector), VertexCount, 1);
			if (PointsPos)
			{
				Ar.Seek(PointsPos);
				Ar << Points;

				TArray<FMeshBone> MeshBones;
				if (FindSC4RefSkeleton(Ar, Package, ScanStart, Stop, MeshBones))
					CopyArray(RefSkeleton, MeshBones);

				appPrintf("WARNING: Double Agent skeletal mesh %s has no serialized face buffer; loaded points and skeleton only\n", Name);
				DROP_REMAINING_DATA(Ar);
				ConvertMesh();
				return;
			}

			appError("Unable to locate Double Agent skeletal mesh face buffer");
		}

		TLazyArray<FMeshFace> RawFaces;
		Ar.Seek(FacesPos);
		Ar << RawFaces;

		int MaxWedgeIndex = 0;
		for (int i = 0; i < RawFaces.Num(); i++)
		{
			const FMeshFace &F = RawFaces[i];
			MaxWedgeIndex = max(MaxWedgeIndex, (int)F.iWedge[0]);
			MaxWedgeIndex = max(MaxWedgeIndex, (int)F.iWedge[1]);
			MaxWedgeIndex = max(MaxWedgeIndex, (int)F.iWedge[2]);
		}

		int WedgeCount = 0;
		int ExpandedWedgesPos = FindSC4WedgeArray(Ar, ScanStart, FacesPos, MaxWedgeIndex + 1, WedgeCount);
		int StandardWedgesPos = FindPandoraLazyArray(Ar, ScanStart, FacesPos, sizeof(uint16) + sizeof(float) * 2, 0, MaxWedgeIndex + 1);
		bool ExpandedWedges = ExpandedWedgesPos != 0;
		int WedgesPos = ExpandedWedges ? ExpandedWedgesPos : StandardWedgesPos;
		int PointCount = 0;
		int PointsPos = FindNextPandoraLazyArray(Ar, FacesPos + 1, Stop, sizeof(FVector), PointCount, 1);
		if (!WedgesPos || !PointsPos || !PointCount)
		{
			if (debugDoubleAgent)
				DumpSC4LazyArrays(Ar, ScanStart, Stop);
			appError("Unable to locate Double Agent skeletal mesh vertex buffers");
		}

		TArray<FMeshBone> MeshBones;
		if (!FindSC4RefSkeleton(Ar, Package, ScanStart, WedgesPos, MeshBones) || !MeshBones.Num())
			appError("Unable to locate Double Agent skeletal mesh bind skeleton");
		CopyArray(RefSkeleton, MeshBones);

		int InfluencesPos = FindSC4InfluenceArray(Ar, ScanStart, WedgesPos, PointCount, RefSkeleton.Num());
		if (!InfluencesPos)
			appError("Unable to locate Double Agent skeletal mesh influence buffer");

		Ar.Seek(PointsPos);
		Ar << Points;
		Ar.Seek(WedgesPos);
		if (!ExpandedWedges)
		{
			Ar << Wedges;
		}
		else if (!ReadSC4Wedges(Ar, WedgesPos, Stop, Points.Num(), Wedges))
		{
			appError("Unable to decode Double Agent skeletal mesh wedge buffer");
		}
		Ar.Seek(InfluencesPos);
		Ar << VertInfluences;

		Triangles.Empty(RawFaces.Num());
		Triangles.AddUninitialized(RawFaces.Num());
		for (int i = 0; i < RawFaces.Num(); i++)
		{
			const FMeshFace &F = RawFaces[i];
			VTriangle &T = Triangles[i];
			T.WedgeIndex[0] = F.iWedge[0];
			T.WedgeIndex[1] = F.iWedge[1];
			T.WedgeIndex[2] = F.iWedge[2];
			T.MatIndex = F.MaterialIndex;
			T.AuxMatIndex = 0;
			T.SmoothingGroups = 0;
		}

		if (debugDoubleAgent)
			appPrintf("SC4 mesh buffers: faces=%d wedges=%d points=%d bones=%d influences=%d\n",
				Triangles.Num(), Wedges.Num(), Points.Num(), RefSkeleton.Num(), VertInfluences.Num());

		DROP_REMAINING_DATA(Ar);
		ConvertMesh();
		return;
	}

	if (Version >= 2) Ar << Version;		// interesting code
	Ar << Points2;
	if (debugDoubleAgent)
		appPrintf("SC4 after Points2 pos=%08X version=%d points2=%d\n", Ar.Tell(), Version, Points2.Num());
	if (Ar.ArVer == 100 && Ar.ArLicenseeVer == 124)
	{
		// Chaos Theory keeps a heavily modified skeletal tail after the base LodMesh data.
		int ScanStart = Ar.Tell();
		TArray<FMeshBone> MeshBones;
		if (FindSCCTMeshRefSkeleton(Ar, Package, ScanStart, Ar.GetStopper(), MeshBones) && MeshBones.Num())
		{
			CopyArray(Points, Points2);
			CopyArray(Wedges, Super::Wedges);
			UpgradeFaces();

			VertInfluences.Empty(Wedges.Num());
			TArray<FVector> WedgePoints;
			TArray<FSCCTBonePalette> BonePalettes;
			FindSCCTBonePalettes(Ar, ScanStart, Ar.GetStopper(), Wedges.Num(), MeshBones.Num(), BonePalettes);
			if (!FindSCCTWedgeInfluences(Ar, ScanStart, Ar.GetStopper(), Points, Wedges, MeshBones.Num(), BonePalettes, VertInfluences, WedgePoints) || !VertInfluences.Num())
			{
				appPrintf("WARNING: Unable to locate SCCT skeletal mesh influence data for %s, using serialized influences\n", Name);
				DROP_REMAINING_DATA(Ar);
				ConvertMesh();
				return;
			}
			CopyArray(Points, WedgePoints);
			for (int i = 0; i < Wedges.Num(); i++)
				Wedges[i].iVertex = i;

			RefSkeleton.Empty(MeshBones.Num());
			RefSkeleton.AddZeroed(MeshBones.Num());
			for (int i = 0; i < RefSkeleton.Num(); i++)
			{
				RefSkeleton[i] = MeshBones[i];
			}
			DROP_REMAINING_DATA(Ar);
			ConvertMesh();
			return;
		}
		else
		{
			appPrintf("WARNING: Unable to locate SCCT skeletal mesh bind pose for %s, using rigid base-mesh fallback\n", Name);
			CopyArray(Points, Points2);
			CopyArray(Wedges, Super::Wedges);
			UpgradeFaces();

			RefSkeleton.Empty(1);
			RefSkeleton.AddZeroed(1);
			FMeshBone &RootBone = RefSkeleton[0];
			RootBone.Name = "B";
			RootBone.Flags = 0;
			RootBone.BonePos.Orientation.Set(0, 0, 0, 1);
			RootBone.BonePos.Position.Set(0, 0, 0);
			RootBone.BonePos.Length = 0;
			RootBone.BonePos.Size.Set(1, 1, 1);
			RootBone.ParentIndex = 0;
			RootBone.NumChildren = 0;

			VertInfluences.Empty(Points.Num());
			for (int PointIndex = 0; PointIndex < Points.Num(); PointIndex++)
			{
				FVertInfluence *Inf = new (VertInfluences) FVertInfluence;
				Inf->Weight = 1.0f;
				Inf->PointIndex = PointIndex;
				Inf->BoneIndex = 0;
			}

			DROP_REMAINING_DATA(Ar);
			ConvertMesh();
			return;
		}
	}
	if (Ar.ArLicenseeVer >= 48)
	{
		TArray<FVector> unk1;
		Ar << unk1;
	}
	if (Ar.ArLicenseeVer >= 49 && Ar.ArLicenseeVer < 67)
	{
		TArray<byte> unk2;
		Ar << unk2;
	}
	Ar << RefSkeleton;
	if (debugDoubleAgent)
		appPrintf("SC4 after RefSkeleton pos=%08X bones=%d\n", Ar.Tell(), RefSkeleton.Num());
	Ar << Animation;
	if (Ar.ArLicenseeVer >= 155)
	{
		UObject* unk218;
		Ar << unk218;
	}
	Ar << SkeletalDepth << WeightIndices << BoneInfluences;
	if (debugDoubleAgent)
		appPrintf("SC4 after influences pos=%08X depth=%d weights=%d influences=%d\n",
			Ar.Tell(), SkeletalDepth, WeightIndices.Num(), BoneInfluences.Num());
	Ar << AttachAliases << AttachBoneNames << AttachCoords;
	DROP_REMAINING_DATA(Ar);
	UpgradeMesh();

	ConvertMesh();

/*	TArray<FSCellUnk1> tmp1;
	TArray<FSCellUnk2> tmp2;
	TArray<FSCellUnk3> tmp3;
	TArray<FLODMeshSection> tmp4, tmp5;
	TArray<uint16> tmp6;
	FSCellUnk4 complex;
	Ar << tmp1 << tmp2 << tmp3 << tmp4 << tmp5 << tmp6 << complex; */
}

#endif // SPLINTER_CELL


#if LINEAGE2

void FStaticLODModel::RestoreLineageMesh()
{
	guard(FStaticLODModel::RestoreLineageMesh);

	if (Wedges.Num()) return;			// nothing to restore
	appPrintf("Converting Lineage2 LODModel to standard LODModel ...\n");
	if (SoftSections.Num() && RigidSections.Num())
		appNotify("have soft & rigid sections");

	int i, j, k;
	int NumWedges = LineageWedges.Num() + VertexStream.Verts.Num(); // one of them is zero (ensured by assert below)
	if (!NumWedges)
	{
		appNotify("Cannot restore mesh: no wedges");
		return;
	}
	assert(LineageWedges.Num() == 0 || VertexStream.Verts.Num() == 0);

	Wedges.Empty(NumWedges);
	Points.Empty(NumWedges);			// really, should be a smaller count
	VertInfluences.Empty(NumWedges);	// min count = NumVerts
	Faces.Empty((SoftIndices.Indices.Num() + RigidIndices.Indices.Num()) / 3);
	TArray<FVector> PointNormals;
	PointNormals.Empty(NumWedges);

	// remap bones and build faces
	TArray<const FSkelMeshSection*> WedgeSection;
	WedgeSection.Empty(NumWedges);
	WedgeSection.AddZeroed(NumWedges);
	// soft sections
	guard(SoftWedges);
	for (k = 0; k < SoftSections.Num(); k++)
	{
		const FSkelMeshSection &ms = SoftSections[k];
		for (i = 0; i < ms.NumFaces; i++)
		{
			FMeshFace *F = new (Faces) FMeshFace;
			F->MaterialIndex = ms.MaterialIndex;
			for (j = 0; j < 3; j++)
			{
				int WedgeIndex = SoftIndices.Indices[(ms.FirstFace + i) * 3 + j];
				assert(WedgeSection[WedgeIndex] == NULL || WedgeSection[WedgeIndex] == &ms);
				WedgeSection[WedgeIndex] = &ms;
				F->iWedge[j] = WedgeIndex;
			}
		}
	}
	unguard;
	guard(RigidWedges);
	// and the same code for rigid sections
	for (k = 0; k < RigidSections.Num(); k++)
	{
		const FSkelMeshSection &ms = RigidSections[k];
		for (i = 0; i < ms.NumFaces; i++)
		{
			FMeshFace *F = new (Faces) FMeshFace;
			F->MaterialIndex = ms.MaterialIndex;
			for (j = 0; j < 3; j++)
			{
				int WedgeIndex = RigidIndices.Indices[(ms.FirstFace + i) * 3 + j];
				assert(WedgeSection[WedgeIndex] == NULL || WedgeSection[WedgeIndex] == &ms);
				WedgeSection[WedgeIndex] = &ms;
				F->iWedge[j] = WedgeIndex;
			}
		}
	}
	unguard;

	// process wedges

	// convert LineageWedges (soft sections)
	guard(BuildSoftWedges);
	for (i = 0; i < LineageWedges.Num(); i++)
	{
		const FLineageWedge &LW = LineageWedges[i];
		FVector VPos = LW.Point;
		// find the same point in previous items
		int PointIndex = -1;
		while (true)
		{
			PointIndex = Points.FindItem(VPos, PointIndex + 1);
			if (PointIndex == INDEX_NONE) break;
			if (PointNormals[PointIndex] == LW.Normal) break;
		}
		if (PointIndex == INDEX_NONE)
		{
			// point was not found - create it
			PointIndex = Points.AddUninitialized();
			Points[PointIndex] = LW.Point;
			PointNormals.Add(LW.Normal);
			// build influences
			const FSkelMeshSection *ms = WedgeSection[i];
			assert(ms);
			for (j = 0; j < 4; j++)
			{
				if (LW.Bones[j] == 255) continue;	// no bone assigned
				float Weight = LW.Weights[j];
				if (Weight < 0.000001f) continue;	// zero weight
				FVertInfluence *Inf = new (VertInfluences) FVertInfluence;
				Inf->Weight     = Weight;
				Inf->BoneIndex  = ms->LineageBoneMap[LW.Bones[j]];
				Inf->PointIndex = PointIndex;
			}
		}
		// create wedge
		FMeshWedge *W = new (Wedges) FMeshWedge;
		W->iVertex = PointIndex;
		W->TexUV   = LW.Tex;
	}
	unguard;
	// similar code for VertexStream (rigid sections)
	guard(BuildRigidWedges);
	for (i = 0; i < VertexStream.Verts.Num(); i++)
	{
		const FAnimMeshVertex &LW = VertexStream.Verts[i];
		FVector VPos = LW.Pos;
		// find the same point in previous items
		int PointIndex = -1;
		while (true)
		{
			PointIndex = Points.FindItem(VPos, PointIndex + 1);
			if (PointIndex == INDEX_NONE) break;
			if (LW.Norm == PointNormals[PointIndex]) break;
		}
		if (PointIndex == INDEX_NONE)
		{
			// point was not found - create it
			PointIndex = Points.AddUninitialized();
			Points[PointIndex] = LW.Pos;
			PointNormals.Add(LW.Norm);
			// build influences
			const FSkelMeshSection *ms = WedgeSection[i];
			assert(ms);
			FVertInfluence *Inf = new (VertInfluences) FVertInfluence;
			Inf->Weight     = 1.0f;
			Inf->BoneIndex  = /*VertexStream.Revision; //??*/ ms->BoneIndex; //-- equals 0 in Lineage2 ...
			Inf->PointIndex = PointIndex;
		}
		// create wedge
		FMeshWedge *W = new (Wedges) FMeshWedge;
		W->iVertex = PointIndex;
		W->TexUV   = LW.Tex;
	}
	unguard;

	unguard;
}


#endif // LINEAGE2


/*-----------------------------------------------------------------------------
	UStaticMesh class
-----------------------------------------------------------------------------*/

struct FStaticMeshTriangleUnk
{
	float					unk1[2];
	float					unk2[2];
	float					unk3[2];
};

// complex FStaticMeshTriangle structure
struct FStaticMeshTriangle
{
	FVector					f0;
	FVector					fC;
	FVector					f18;
	FStaticMeshTriangleUnk	f24[8];
	byte					fE4[12];
	int						fF0;
	int						fF4;
	int						fF8;

	friend FArchive& operator<<(FArchive &Ar, FStaticMeshTriangle &T)
	{
		guard(FStaticMeshTriangle<<);
		int i;

		assert(Ar.ArVer >= 112);
		Ar << T.f0 << T.fC << T.f18;
		Ar << T.fF8;
		assert(T.fF8 <= ARRAY_COUNT(T.f24));
		for (i = 0; i < T.fF8; i++)
		{
			FStaticMeshTriangleUnk &V = T.f24[i];
			Ar << V.unk1[0] << V.unk1[1] << V.unk2[0] << V.unk2[1] << V.unk3[0] << V.unk3[1];
		}
		for (i = 0; i < 12; i++)
			Ar << T.fE4[i];			// UT2 has strange order of field serialization: [2,1,0,3] x 3 times
		Ar << T.fF0 << T.fF4;
		// extra fields for older version (<= 111)

		return Ar;
		unguard;
	}
};

struct FkDOPNode
{
	float					unk1[3];
	float					unk2[3];
	int						unk3;		//?? index * 32 ?
	int16					unk4;
	int16					unk5;

	friend FArchive& operator<<(FArchive &Ar, FkDOPNode &N)
	{
		guard(FkDOPNode<<);
		int i;
		for (i = 0; i < 3; i++)
			Ar << N.unk1[i];
		for (i = 0; i < 3; i++)
			Ar << N.unk2[i];
		Ar << N.unk3 << N.unk4 << N.unk5;
		return Ar;
		unguard;
	}
};

RAW_TYPE(FkDOPNode)

struct FkDOPCollisionTriangle
{
	int16					v[4];

	friend FArchive& operator<<(FArchive &Ar, FkDOPCollisionTriangle &T)
	{
		return Ar << T.v[0] << T.v[1] << T.v[2] << T.v[3];
	}
};

SIMPLE_TYPE(FkDOPCollisionTriangle, int16)

struct FStaticMeshCollisionNode
{
	int						f1[4];
	float					f2[6];
	byte					f3;

	friend FArchive& operator<<(FArchive &Ar, FStaticMeshCollisionNode &N)
	{
		int i;
		for (i = 0; i < 4; i++) Ar << AR_INDEX(N.f1[i]);
		for (i = 0; i < 6; i++) Ar << N.f2[i];
		Ar << N.f3;
		return Ar;
	}
};

struct FStaticMeshCollisionTriangle
{
	float					f1[16];
	int						f2[4];

	friend FArchive& operator<<(FArchive &Ar, FStaticMeshCollisionTriangle &T)
	{
		int i;
		for (i = 0; i < 16; i++) Ar << T.f1[i];
		for (i = 0; i <  4; i++) Ar << AR_INDEX(T.f2[i]);
		return Ar;
	}
};

static bool ReadSCDAStaticCompactIndex(const byte* Data, int DataSize, int& Pos, int& Value)
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

static float ReadSCDAStaticFloat(const byte* Data, int Pos)
{
	float Value;
	memcpy(&Value, Data + Pos, sizeof(Value));
	return Value;
}

static uint16 ReadSCDAStaticBE16(const byte* Data, int Pos)
{
	return (uint16)((Data[Pos] << 8) | Data[Pos + 1]);
}

static uint16 ReadSCDAStaticLE16(const byte* Data, int Pos)
{
	return (uint16)(Data[Pos] | (Data[Pos + 1] << 8));
}

static FMeshUVFloat ReadSCDAStaticHalfUV(const byte* Data, int Pos)
{
	FMeshUVHalf Half;
	Half.U = ReadSCDAStaticLE16(Data, Pos + 0);
	Half.V = ReadSCDAStaticLE16(Data, Pos + 2);
	return Half;
}

static FMeshUVFloat ReadSCDAStaticFloatUV(const byte* Data, int Pos)
{
	FMeshUVFloat UV;
	UV.U = ReadSCDAStaticFloat(Data, Pos + 0);
	UV.V = ReadSCDAStaticFloat(Data, Pos + 4);
	return UV;
}

static void SCDAStaticNormalize(FVector& V)
{
	float LenSq = V.X * V.X + V.Y * V.Y + V.Z * V.Z;
	if (LenSq > 0.000001f)
	{
		float InvLen = 1.0f / sqrt(LenSq);
		V.X *= InvLen;
		V.Y *= InvLen;
		V.Z *= InvLen;
	}
	else
	{
		V.Set(0, 0, 1);
	}
}

static FVector SCDAStaticCross(const FVector& A, const FVector& B)
{
	FVector R;
	R.X = A.Y * B.Z - A.Z * B.Y;
	R.Y = A.Z * B.X - A.X * B.Z;
	R.Z = A.X * B.Y - A.Y * B.X;
	return R;
}

struct FSCDAStaticVertexLayout
{
	int Start;
	int Count;
	int Stride;
	float Score;
};

struct FSCDAStaticSectionInfo
{
	int FirstIndex;
	int FirstVertex;
	int LastVertex;
	int NumFaces;
};

static bool SCDAStaticDebugEnabled()
{
	const char* DebugStatic = getenv("SC4_DEBUG_STATIC");
	return DebugStatic && strcmp(DebugStatic, "0") != 0;
}

static bool ScoreSCDAStaticVertexLayout(const byte* Data, int DataSize, int Start, int Count, int Stride, FSCDAStaticVertexLayout& Best)
{
	if (Start < 0 || Count < 3 || Count > 200000 || Stride < 12 || Start + Count * Stride > DataSize)
		return false;

	float Min[3] = {  1.0e30f,  1.0e30f,  1.0e30f };
	float Max[3] = { -1.0e30f, -1.0e30f, -1.0e30f };
	float NormalScore = 0.0f;
	for (int i = 0; i < Count; i++)
	{
		const int Pos = Start + i * Stride;
		float V[3] =
		{
			ReadSCDAStaticFloat(Data, Pos + 0),
			ReadSCDAStaticFloat(Data, Pos + 4),
			ReadSCDAStaticFloat(Data, Pos + 8)
		};
		for (int j = 0; j < 3; j++)
		{
			if (V[j] != V[j] || fabs(V[j]) > 100000.0f)
				return false;
			if (V[j] < Min[j]) Min[j] = V[j];
			if (V[j] > Max[j]) Max[j] = V[j];
		}
		if (Stride >= 24)
		{
			float N[3] =
			{
				ReadSCDAStaticFloat(Data, Pos + 12),
				ReadSCDAStaticFloat(Data, Pos + 16),
				ReadSCDAStaticFloat(Data, Pos + 20)
			};
			const float LenSq = N[0] * N[0] + N[1] * N[1] + N[2] * N[2];
			if (LenSq > 0.25f && LenSq < 2.25f)
				NormalScore += 1.0f;
		}
	}

	const float Extent = (Max[0] - Min[0]) + (Max[1] - Min[1]) + (Max[2] - Min[2]);
	if (Extent <= 0.01f)
		return false;

	const float FormatBias = (Stride == 40) ? 40.0f : ((Stride == 24) ? 2.0f : 0.0f);
	const float NormalRatio = NormalScore / Count;
	if (Stride >= 24 && NormalRatio < 0.5f)
		return false;

	const float Score = Count * 0.5f + Extent * 0.01f + NormalScore * 0.75f + FormatBias - Start * 0.0001f;
	if (SCDAStaticDebugEnabled() && Score > Best.Score * 0.75f)
		appPrintf("SC4 Static candidate start=%X count=%d stride=%d extent=%g normal=%g score=%g\n",
			Start, Count, Stride, Extent, NormalRatio, Score);
	if (Score > Best.Score)
	{
		Best.Start = Start;
		Best.Count = Count;
		Best.Stride = Stride;
		Best.Score = Score;
	}
	return true;
}

static bool FindSCDAStaticVertexLayout(const byte* Data, int DataSize, FSCDAStaticVertexLayout& Layout)
{
	memset(&Layout, 0, sizeof(Layout));
	Layout.Score = -1.0f;
	if (DataSize < 64)
		return false;

	int SectionCount = 0;
	memcpy(&SectionCount, Data, 4);
	if (SectionCount < 1 || SectionCount > 256)
		return false;

	int SectionVertexCount = 0;
	if (DataSize >= 15)
		SectionVertexCount = ReadSCDAStaticLE16(Data, 9) + 1;
	for (int i = 1; i < SectionCount; i++)
	{
		const int Pos = 0x13 + (i - 1) * 14;
		if (Pos + 6 > DataSize)
			break;
		SectionVertexCount = max(SectionVertexCount, (int)ReadSCDAStaticLE16(Data, Pos + 4) + 1);
	}
	if (SectionVertexCount < 3 || SectionVertexCount >= 200000)
		SectionVertexCount = 0;

	int MaxBE = 0;
	for (int i = 0; i < SectionCount; i++)
	{
		const int SectionPos = 4 + i * 12;
		if (SectionPos + 12 > DataSize)
			break;
		for (int j = 0; j < 6; j++)
		{
			int V = ReadSCDAStaticBE16(Data, SectionPos + j * 2);
			if (V > MaxBE && V < 200000)
				MaxBE = V;
		}
	}

	int Counts[16];
	int NumCounts = 0;
	if (SectionVertexCount)
		Counts[NumCounts++] = SectionVertexCount;

	int Starts[24];
	int NumStarts = 0;
	const int StartA = 4 + SectionCount * 12;
	const int StartB = 1 + SectionCount * 12;
	Starts[NumStarts++] = StartA;
	Starts[NumStarts++] = (StartA + 3) & ~3;
	Starts[NumStarts++] = StartB;
	Starts[NumStarts++] = (StartB + 3) & ~3;
	for (int Pos = 0; Pos < min(DataSize - 12, 128) && NumStarts < ARRAY_COUNT(Starts); Pos++)
	{
		float X = ReadSCDAStaticFloat(Data, Pos);
		float Y = ReadSCDAStaticFloat(Data, Pos + 4);
		float Z = ReadSCDAStaticFloat(Data, Pos + 8);
		if (X == X && Y == Y && Z == Z && fabs(X) < 100000.0f && fabs(Y) < 100000.0f && fabs(Z) < 100000.0f &&
			(fabs(X) + fabs(Y) + fabs(Z)) > 1.0f)
			Starts[NumStarts++] = Pos;
	}

	const int Strides[] = { 40, 24, 28, 32, 36, 44, 48 };
	for (int i = 0; i < NumStarts; i++)
		for (int j = 0; j < NumCounts; j++)
			for (int k = 0; k < ARRAY_COUNT(Strides); k++)
				ScoreSCDAStaticVertexLayout(Data, DataSize, Starts[i], Counts[j], Strides[k], Layout);

	if (Layout.Score > 0.0f)
		return true;

	NumCounts = 0;
	if (SectionCount == 1 && SectionVertexCount)
		Counts[NumCounts++] = SectionVertexCount;
	if (SectionCount > 1 && MaxBE >= 3)
	{
		Counts[NumCounts++] = MaxBE;
		if (MaxBE >= 16 && MaxBE + 1 < 200000)
			Counts[NumCounts++] = MaxBE + 1;
	}
	for (int Pos = 0; Pos < min(DataSize, 256) && NumCounts < ARRAY_COUNT(Counts); Pos++)
	{
		int Tmp = Pos;
		int Value;
		if (ReadSCDAStaticCompactIndex(Data, DataSize, Tmp, Value) && Value >= 3 && Value < 200000)
			Counts[NumCounts++] = Value;
	}

	for (int i = 0; i < NumStarts; i++)
		for (int j = 0; j < NumCounts; j++)
			for (int k = 0; k < ARRAY_COUNT(Strides); k++)
				ScoreSCDAStaticVertexLayout(Data, DataSize, Starts[i], Counts[j], Strides[k], Layout);

	return Layout.Score > 0.0f;
}

static float SCDAStaticVertexDistanceSq(const byte* Data, const FSCDAStaticVertexLayout& Layout, int A, int B)
{
	const int PosA = Layout.Start + A * Layout.Stride;
	const int PosB = Layout.Start + B * Layout.Stride;
	const float Ax = ReadSCDAStaticFloat(Data, PosA + 0);
	const float Ay = ReadSCDAStaticFloat(Data, PosA + 4);
	const float Az = ReadSCDAStaticFloat(Data, PosA + 8);
	const float Bx = ReadSCDAStaticFloat(Data, PosB + 0);
	const float By = ReadSCDAStaticFloat(Data, PosB + 4);
	const float Bz = ReadSCDAStaticFloat(Data, PosB + 8);
	const float Dx = Ax - Bx;
	const float Dy = Ay - By;
	const float Dz = Az - Bz;
	return Dx * Dx + Dy * Dy + Dz * Dz;
}

static bool ReadSCDAStaticSections(const byte* Data, int DataSize, int SectionCount, int NumVerts, TArray<FSCDAStaticSectionInfo>& Sections)
{
	if (SectionCount < 1 || SectionCount > 256 || DataSize < 16)
		return false;

	Sections.Empty(SectionCount);
	Sections.AddZeroed(SectionCount);

	FSCDAStaticSectionInfo& First = Sections[0];
	First.FirstIndex  = 0;
	First.FirstVertex = ReadSCDAStaticLE16(Data, 5);
	First.LastVertex  = ReadSCDAStaticLE16(Data, 9);
	First.NumFaces    = ReadSCDAStaticLE16(Data, 11);
	const int FirstNumFaces2 = ReadSCDAStaticLE16(Data, 13);
	if (FirstNumFaces2 == First.NumFaces)
	{
		// Good, this is the common SCDA layout.
	}
	else if (First.NumFaces <= 0 && FirstNumFaces2 > 0)
	{
		First.NumFaces = FirstNumFaces2;
	}

	for (int i = 1; i < SectionCount; i++)
	{
		const int Pos = 0x13 + (i - 1) * 14;
		if (Pos + 10 > DataSize)
			return false;

		FSCDAStaticSectionInfo& S = Sections[i];
		S.FirstIndex  = ReadSCDAStaticLE16(Data, Pos + 0);
		S.FirstVertex = ReadSCDAStaticLE16(Data, Pos + 2);
		S.LastVertex  = ReadSCDAStaticLE16(Data, Pos + 4);
		S.NumFaces    = ReadSCDAStaticLE16(Data, Pos + 6);
		const int NumFaces2 = ReadSCDAStaticLE16(Data, Pos + 8);
		if (NumFaces2 && NumFaces2 == S.NumFaces)
		{
			// repeated by format, keep it
		}
		else if (S.NumFaces <= 0 && NumFaces2 > 0)
		{
			S.NumFaces = NumFaces2;
		}
	}

	int RunningFirstIndex = 0;
	for (int i = 0; i < SectionCount; i++)
	{
		FSCDAStaticSectionInfo& S = Sections[i];
		if (S.NumFaces < 0 || S.NumFaces > 200000)
			return false;
		if (S.FirstVertex < 0 || S.FirstVertex >= NumVerts)
			S.FirstVertex = 0;
		if (S.LastVertex < S.FirstVertex || S.LastVertex >= NumVerts)
			S.LastVertex = NumVerts - 1;
		if (i == 0)
			S.FirstIndex = 0;
		else if (S.FirstIndex != RunningFirstIndex)
			S.FirstIndex = RunningFirstIndex;
		RunningFirstIndex += S.NumFaces * 3;
	}

	if (SCDAStaticDebugEnabled())
	{
		for (int i = 0; i < Sections.Num(); i++)
		{
			const FSCDAStaticSectionInfo& S = Sections[i];
			appPrintf("SC4 Section[%d] firstIdx=%d firstVert=%d lastVert=%d faces=%d\n",
				i, S.FirstIndex, S.FirstVertex, S.LastVertex, S.NumFaces);
		}
	}
	return true;
}

static bool FindSCDAStaticIndexBlock(const byte* Data, int DataSize, int Start, const FSCDAStaticVertexLayout& Layout, int ExpectedIndexCount, TArray<uint16>& Indices, int& OutIndexPos)
{
	int BestPos = -1;
	int BestCount = 0;
	float BestScore = -1.0e30f;
	OutIndexPos = 0;
	const int NumVerts = Layout.Count;
	for (int Pos = Start; Pos < DataSize - 8; Pos++)
	{
		int CountPos = Pos;
		int Count;
		if (!ReadSCDAStaticCompactIndex(Data, DataSize, CountPos, Count))
			continue;
		if (ExpectedIndexCount > 0 && Count != ExpectedIndexCount)
			continue;
		if (Count < 3 || Count > 200000 || (Count % 3) != 0 || CountPos + Count * 2 > DataSize)
			continue;

		bool bValid = true;
		int GoodFaces = 0;
		float EdgeSum = 0.0f;
		int EdgeSamples = 0;
		for (int i = 0; i < Count; i += 3)
		{
			uint16 A = ReadSCDAStaticLE16(Data, CountPos + (i + 0) * 2);
			uint16 B = ReadSCDAStaticLE16(Data, CountPos + (i + 1) * 2);
			uint16 C = ReadSCDAStaticLE16(Data, CountPos + (i + 2) * 2);
			if (A >= NumVerts || B >= NumVerts || C >= NumVerts)
			{
				bValid = false;
				break;
			}
			if (A != B && B != C && C != A)
			{
				GoodFaces++;
				if (EdgeSamples < 1024)
				{
					EdgeSum += SCDAStaticVertexDistanceSq(Data, Layout, A, B);
					EdgeSum += SCDAStaticVertexDistanceSq(Data, Layout, B, C);
					EdgeSum += SCDAStaticVertexDistanceSq(Data, Layout, C, A);
					EdgeSamples += 3;
				}
			}
		}
		if (bValid && GoodFaces > 0)
		{
			const float AvgEdge = EdgeSamples ? EdgeSum / EdgeSamples : 1.0e30f;
			const float FaceCount = Count / 3.0f;
			const float Score = FaceCount * 100.0f - sqrt(AvgEdge) * FaceCount * 0.25f - CountPos * 0.0001f;
			if (SCDAStaticDebugEnabled() && Count >= 30)
				appPrintf("SC4 Index candidate pos=%X count=%d faces=%d avgEdge=%g score=%g\n",
					CountPos, Count, GoodFaces, sqrt(AvgEdge), Score);
			if (Score > BestScore)
			{
				BestPos = CountPos;
				BestCount = Count;
				BestScore = Score;
			}
		}
	}

	if (BestPos < 0 || BestCount < 3)
		return false;

	Indices.Empty(BestCount);
	Indices.AddZeroed(BestCount);
	for (int i = 0; i < BestCount; i++)
		Indices[i] = ReadSCDAStaticLE16(Data, BestPos + i * 2);
	if (SCDAStaticDebugEnabled())
		appPrintf("SC4 Index selected pos=%X count=%d score=%g\n", BestPos, BestCount, BestScore);
	OutIndexPos = BestPos;
	return true;
}

static float SCDAStaticUVEdgeSq(const FMeshUVFloat& A, const FMeshUVFloat& B)
{
	const float DU = A.U - B.U;
	const float DV = A.V - B.V;
	return DU * DU + DV * DV;
}

static bool FindSCDAStaticUVBlock(const byte* Data, int DataSize, int Start, int End, const FSCDAStaticVertexLayout& Layout, const TArray<uint16>& Indices, FStaticMeshUVStream& UV, int& OutGoodUVs)
{
	OutGoodUVs = 0;
	const int Count = Layout.Count;
	if (Count <= 0 || Start < 0 || End > DataSize || Start >= End)
		return false;

	int BestPos = -1;
	float BestScore = -1.0e30f;
	for (int Pos = Start; Pos + Count * 8 <= End; Pos++)
	{
		int Valid = 0;
		int NonZero = 0;
		float MinU = 1.0e30f, MinV = 1.0e30f;
		float MaxU = -1.0e30f, MaxV = -1.0e30f;
		for (int i = 0; i < Count; i++)
		{
			FMeshUVFloat TestUV = ReadSCDAStaticFloatUV(Data, Pos + i * 8);
			if (TestUV.U != TestUV.U || TestUV.V != TestUV.V || fabs(TestUV.U) > 64.0f || fabs(TestUV.V) > 64.0f)
				break;
			if (fabs(TestUV.U) + fabs(TestUV.V) > 0.0001f)
				NonZero++;
			MinU = min(MinU, TestUV.U);
			MaxU = max(MaxU, TestUV.U);
			MinV = min(MinV, TestUV.V);
			MaxV = max(MaxV, TestUV.V);
			Valid++;
		}
		if (Valid != Count || NonZero < Count / 2)
			continue;

		const float RangeU = MaxU - MinU;
		const float RangeV = MaxV - MinV;
		if (RangeU < 0.01f || RangeV < 0.01f)
			continue;

		float UVDistortion = 0.0f;
		int UVEdgeSamples = 0;
		for (int i = 0; i + 2 < Indices.Num() && UVEdgeSamples < 1536; i += 3)
		{
			int A = Indices[i + 0];
			int B = Indices[i + 1];
			int C = Indices[i + 2];
			if (A < 0 || A >= Count || B < 0 || B >= Count || C < 0 || C >= Count)
				continue;

			FMeshUVFloat UVA = ReadSCDAStaticFloatUV(Data, Pos + A * 8);
			FMeshUVFloat UVB = ReadSCDAStaticFloatUV(Data, Pos + B * 8);
			FMeshUVFloat UVC = ReadSCDAStaticFloatUV(Data, Pos + C * 8);
			const float GeoAB = sqrt(SCDAStaticVertexDistanceSq(Data, Layout, A, B));
			const float GeoBC = sqrt(SCDAStaticVertexDistanceSq(Data, Layout, B, C));
			const float GeoCA = sqrt(SCDAStaticVertexDistanceSq(Data, Layout, C, A));
			const float UVAB = sqrt(SCDAStaticUVEdgeSq(UVA, UVB));
			const float UVBC = sqrt(SCDAStaticUVEdgeSq(UVB, UVC));
			const float UVCA = sqrt(SCDAStaticUVEdgeSq(UVC, UVA));

			if (GeoAB > 0.001f && UVAB < 0.000001f) UVDistortion += 25.0f;
			if (GeoBC > 0.001f && UVBC < 0.000001f) UVDistortion += 25.0f;
			if (GeoCA > 0.001f && UVCA < 0.000001f) UVDistortion += 25.0f;
			if (UVAB > 25.0f || UVBC > 25.0f || UVCA > 25.0f) UVDistortion += 5.0f;
			UVDistortion += min(25.0f, UVAB + UVBC + UVCA) * 0.01f;
			UVEdgeSamples += 3;
		}

		float Score = NonZero * 0.05f + min(RangeU + RangeV, 16.0f) - UVDistortion;
		if ((Pos & 3) == 2)
			Score += 0.25f; // SCDA's float UV stream often starts after a small unaligned header.
		if (Score > BestScore)
		{
			BestScore = Score;
			BestPos = Pos;
			OutGoodUVs = NonZero;
		}
	}

	if (BestPos < 0)
		return false;

	for (int i = 0; i < Count; i++)
		UV.Data[i] = ReadSCDAStaticFloatUV(Data, BestPos + i * 8);

	if (SCDAStaticDebugEnabled())
		appPrintf("SC4 UV selected pos=%X count=%d score=%g\n", BestPos, Count, BestScore);
	return true;
}

static bool SerializeDoubleAgentStaticMesh(UStaticMesh* Mesh, FArchive& Ar)
{
	const int Start = Ar.Tell();
	const int Size = Ar.GetStopper() - Start;
	if (Size <= 0)
		return false;

	TArray<byte> Raw;
	Raw.AddUninitialized(Size);
	Ar.Serialize(Raw.GetData(), Size);
	Ar.Seek(Start);

	FSCDAStaticVertexLayout Layout;
	if (!FindSCDAStaticVertexLayout(Raw.GetData(), Size, Layout))
		return false;

	int SectionCount = 0;
	memcpy(&SectionCount, Raw.GetData(), 4);
	TArray<FSCDAStaticSectionInfo> SCDASections;
	ReadSCDAStaticSections(Raw.GetData(), Size, SectionCount, Layout.Count, SCDASections);
	int ExpectedIndexCount = 0;
	for (int i = 0; i < SCDASections.Num(); i++)
		ExpectedIndexCount += SCDASections[i].NumFaces * 3;

	Mesh->VertexStream.Vert.Empty(Layout.Count);
	Mesh->VertexStream.Vert.AddZeroed(Layout.Count);
	FStaticMeshUVStream* UV = new (Mesh->UVStream) FStaticMeshUVStream;
	UV->Data.Empty(Layout.Count);
	UV->Data.AddZeroed(Layout.Count);

	int GoodNormals = 0;
	int GoodUVs = 0;
	for (int i = 0; i < Layout.Count; i++)
	{
		const int Pos = Layout.Start + i * Layout.Stride;
		FStaticMeshVertex& V = Mesh->VertexStream.Vert[i];
		V.Pos.X = ReadSCDAStaticFloat(Raw.GetData(), Pos + 0);
		V.Pos.Y = ReadSCDAStaticFloat(Raw.GetData(), Pos + 4);
		V.Pos.Z = ReadSCDAStaticFloat(Raw.GetData(), Pos + 8);
		if (Layout.Stride >= 24)
		{
			V.Normal.X = ReadSCDAStaticFloat(Raw.GetData(), Pos + 12);
			V.Normal.Y = ReadSCDAStaticFloat(Raw.GetData(), Pos + 16);
			V.Normal.Z = ReadSCDAStaticFloat(Raw.GetData(), Pos + 20);
			const float LenSq = V.Normal.X * V.Normal.X + V.Normal.Y * V.Normal.Y + V.Normal.Z * V.Normal.Z;
			if (LenSq > 0.25f && LenSq < 2.25f)
				GoodNormals++;
			else
				V.Normal.Set(0, 0, 1);
		}
		else
		{
			V.Normal.Set(0, 0, 1);
		}
	}

	int IndexPos = 0;
	FindSCDAStaticIndexBlock(Raw.GetData(), Size, Layout.Start + Layout.Count * Layout.Stride, Layout, ExpectedIndexCount, Mesh->IndexStream1.Indices, IndexPos);
	const int VertexEnd = Layout.Start + Layout.Count * Layout.Stride;
	if (IndexPos > VertexEnd)
		FindSCDAStaticUVBlock(Raw.GetData(), Size, VertexEnd, IndexPos, Layout, Mesh->IndexStream1.Indices, *UV, GoodUVs);

	for (int s = 0; s < SCDASections.Num(); s++)
	{
		const FSCDAStaticSectionInfo& S = SCDASections[s];
		const int FirstIndex = S.FirstIndex;
		const int IndexCount = S.NumFaces * 3;
		if (FirstIndex < 0 || FirstIndex + IndexCount > Mesh->IndexStream1.Indices.Num())
			continue;

		int MaxIndex = 0;
		for (int i = 0; i < IndexCount; i++)
			MaxIndex = max(MaxIndex, (int)Mesh->IndexStream1.Indices[FirstIndex + i]);

		const int LocalRange = S.LastVertex - S.FirstVertex;
		if (S.FirstVertex > 0 && MaxIndex <= LocalRange)
		{
			for (int i = 0; i < IndexCount; i++)
				Mesh->IndexStream1.Indices[FirstIndex + i] += S.FirstVertex;
			if (SCDAStaticDebugEnabled())
				appPrintf("SC4 Section[%d] applied local vertex base %d\n", s, S.FirstVertex);
		}
	}

	if (GoodNormals < Layout.Count * 3 / 4 && Mesh->IndexStream1.Indices.Num() >= 3)
	{
		for (int i = 0; i < Layout.Count; i++)
			Mesh->VertexStream.Vert[i].Normal.Set(0, 0, 0);

		for (int i = 0; i + 2 < Mesh->IndexStream1.Indices.Num(); i += 3)
		{
			int A = Mesh->IndexStream1.Indices[i + 0];
			int B = Mesh->IndexStream1.Indices[i + 1];
			int C = Mesh->IndexStream1.Indices[i + 2];
			if (A < 0 || A >= Layout.Count || B < 0 || B >= Layout.Count || C < 0 || C >= Layout.Count)
				continue;

			const FVector& PA = Mesh->VertexStream.Vert[A].Pos;
			const FVector& PB = Mesh->VertexStream.Vert[B].Pos;
			const FVector& PC = Mesh->VertexStream.Vert[C].Pos;
			FVector AB, AC;
			AB.Set(PB.X - PA.X, PB.Y - PA.Y, PB.Z - PA.Z);
			AC.Set(PC.X - PA.X, PC.Y - PA.Y, PC.Z - PA.Z);
			FVector N = SCDAStaticCross(AB, AC);
			Mesh->VertexStream.Vert[A].Normal.Add(N);
			Mesh->VertexStream.Vert[B].Normal.Add(N);
			Mesh->VertexStream.Vert[C].Normal.Add(N);
		}

		for (int i = 0; i < Layout.Count; i++)
			SCDAStaticNormalize(Mesh->VertexStream.Vert[i].Normal);

		if (SCDAStaticDebugEnabled())
			appPrintf("SC4 rebuilt normals from faces: goodEmbedded=%d/%d\n", GoodNormals, Layout.Count);
	}

	const char* DebugStatic = getenv("SC4_DEBUG_STATIC");
	if (DebugStatic && strcmp(DebugStatic, "0") != 0)
		appPrintf("SC4 StaticMesh %s layout start=%X count=%d stride=%d indices=%d score=%g normals=%d uvs=%d\n",
			Mesh->Name, Layout.Start, Layout.Count, Layout.Stride, Mesh->IndexStream1.Indices.Num(), Layout.Score, GoodNormals, GoodUVs);

	if (Mesh->IndexStream1.Indices.Num() >= 3 && SCDASections.Num() > 0)
	{
		for (int i = 0; i < SCDASections.Num(); i++)
		{
			const FSCDAStaticSectionInfo& Src = SCDASections[i];
			if (Src.NumFaces <= 0)
				continue;
			FStaticMeshSection* Section = new (Mesh->Sections) FStaticMeshSection;
			memset(Section, 0, sizeof(FStaticMeshSection));
			Section->FirstIndex  = Src.FirstIndex;
			Section->FirstVertex = Src.FirstVertex;
			Section->LastVertex  = Src.LastVertex;
			Section->fE          = Src.NumFaces;
			Section->NumFaces    = Src.NumFaces;
		}
	}
	else if (Mesh->IndexStream1.Indices.Num() >= 3)
	{
		FStaticMeshSection* Section = new (Mesh->Sections) FStaticMeshSection;
		memset(Section, 0, sizeof(FStaticMeshSection));
		Section->FirstIndex  = 0;
		Section->FirstVertex = 0;
		Section->LastVertex  = Layout.Count - 1;
		Section->fE          = Mesh->IndexStream1.Indices.Num();
		Section->NumFaces    = Mesh->IndexStream1.Indices.Num() / 3;
	}

	DROP_REMAINING_DATA(Ar);
	Mesh->ConvertMesh();
	return true;
}


// Implement constructor in cpp to avoid inlining (it's large enough).
// It's useful to declare TArray<> structures as forward declarations in header file.
UStaticMesh::UStaticMesh()
{}

UStaticMesh::~UStaticMesh()
{
	delete ConvertedMesh;
}

void UStaticMesh::Serialize(FArchive &Ar)
{
	guard(UStaticMesh::Serialize);

	assert(Ar.Game < GAME_UE3);

#if BIOSHOCK
	if (Ar.Game == GAME_Bioshock)
	{
		SerializeBioshockMesh(Ar);
		return;
	}
#endif // BIOSHOCK

#if VANGUARD
	if (Ar.Game == GAME_Vanguard && Ar.ArVer >= 128 && Ar.ArLicenseeVer >= 25)
	{
		SerializeVanguardMesh(Ar);
		return;
	}
#endif // VANGUARD

	if (Ar.ArVer < 112 && Ar.Game != GAME_SplinterCell)
	{
		appNotify("StaticMesh of old version %d/%d has been found", Ar.ArVer, Ar.ArLicenseeVer);
	skip_remaining:
		DROP_REMAINING_DATA(Ar);
		ConvertMesh();
		return;
	}

	//!! copy common part inside BIOSHOCK and UC2 code paths
	//!! separate BIOSHOCK and UC2 code paths to cpps
	//!! separate specific data type declarations into cpps
	//!! UC2 code: can integrate LoadExternalUC2Data() into serializer
	Super::Serialize(Ar);

#if SPLINTER_CELL
	const bool isDoubleAgentOnlineStatic = (Ar.Game == GAME_SplinterCell && Ar.ArVer >= 173 && Ar.ArVer <= 275 && Ar.ArLicenseeVer == 0);
	const char* DebugStaticEnv = getenv("SC4_DEBUG_STATIC");
	const bool debugDoubleAgentStatic = isDoubleAgentOnlineStatic && DebugStaticEnv && strcmp(DebugStaticEnv, "0") != 0;
	if (debugDoubleAgentStatic)
	{
		int SavePos = Ar.Tell();
		int Stop = Ar.GetStopper();
		byte Bytes[96];
		int Count = min(ARRAY_COUNT(Bytes), Stop - SavePos);
		appPrintf("SC4 StaticMesh start %s pos=%08X stopper=%08X size=%X\n", Name, SavePos, Stop, Stop - SavePos);
		if (Count > 0)
		{
			Ar.Serialize(Bytes, Count);
			appPrintf("SC4 StaticMesh bytes:");
			for (int i = 0; i < Count; i++)
				appPrintf(" %02X", Bytes[i]);
			appPrintf("\n");
			Ar.Seek(SavePos);
		}
		if (getenv("SCDA_DUMP_STATIC_RAW"))
		{
			char Filename[256];
			appSprintf(ARRAY_ARG(Filename), "scda_static_%s_raw.bin", Name);
			FILE *F = fopen(Filename, "wb");
			if (F)
			{
				int Size = Stop - SavePos;
				TArray<byte> Raw;
				Raw.AddUninitialized(Size);
				Ar.Serialize(Raw.GetData(), Size);
				fwrite(Raw.GetData(), 1, Size, F);
				fclose(F);
				appPrintf("SCDA dumped raw static mesh export: %s size=%d\n", Filename, Size);
				Ar.Seek(SavePos);
			}
		}
		if (getenv("SCDA_STATIC_PROBE_ONLY"))
		{
			DROP_REMAINING_DATA(Ar);
			ConvertMesh();
			return;
		}
	}

	if (isDoubleAgentOnlineStatic)
	{
		if (SerializeDoubleAgentStaticMesh(this, Ar))
			return;
		appNotify("Unable to decode Double Agent static mesh %s, skipping native data", Name);
		DROP_REMAINING_DATA(Ar);
		ConvertMesh();
		return;
	}

	if (Ar.Game == GAME_SplinterCell && Ar.ArVer == 100 && Ar.ArLicenseeVer >= 120)
	{
		int NumVerts;
		Ar << AR_INDEX(NumVerts);
		if (NumVerts < 0 || NumVerts > 0x100000)
			appError("Splinter Cell StaticMesh %s has invalid vertex count %d", Name, NumVerts);

		VertexStream.Vert.Empty(NumVerts);
		VertexStream.Vert.AddZeroed(NumVerts);

		FStaticMeshUVStream* UV = new (UVStream) FStaticMeshUVStream;
		UV->Data.Empty(NumVerts);
		UV->Data.AddZeroed(NumVerts);

		for (int i = 0; i < NumVerts; i++)
		{
			FPackedNormal PackedNormals[3];
			byte PackedUV[4];
			FStaticMeshVertex& V = VertexStream.Vert[i];
			Ar << V.Pos << PackedNormals[0] << PackedNormals[1] << PackedNormals[2];
			Ar << PackedUV[0] << PackedUV[1] << PackedUV[2] << PackedUV[3];
			FPackedNormal PackedNormal;
			PackedNormal.Data = uint32(PackedUV[0])
				| (uint32(PackedUV[1]) << 8)
				| (uint32(PackedUV[2]) << 16)
				| (uint32(PackedUV[3]) << 24);
			FVector Normal = PackedNormal;
			Normal.Y = -Normal.Y;
			V.Normal = Normal;
			if (getenv("SCCT_SM_DEBUG") && i < 8)
			{
				appPrintf("SCCT StaticMesh %s: vert %d uvraw=%02X %02X %02X %02X n0=%08X n1=%08X n2=%08X\n",
					Name, i, PackedUV[0], PackedUV[1], PackedUV[2], PackedUV[3],
					PackedNormals[0].Data, PackedNormals[1].Data, PackedNormals[2].Data);
			}

			// SCCT stores wedge UVs in the first packed-normal slot as two
			// little-endian 16-bit fixed point values. The following 2 packed
			// normals are the real basis vectors.
			UV->Data[i].U = (PackedNormals[0].Data & 0xFFFF) / 2048.0f;
			UV->Data[i].V = (PackedNormals[0].Data >> 16) / 2048.0f;
		}
		Ar << VertexStream.Revision;
		Ar << IndexStream1;
		Ar << IndexStream2;
		if (getenv("SCCT_SM_DEBUG"))
		{
			appPrintf("SCCT StaticMesh %s: verts=%d idx1=%d idx2=%d\n",
				Name, NumVerts, IndexStream1.Indices.Num(), IndexStream2.Indices.Num());
			if (IndexStream1.Indices.Num())
			{
				appPrintf("SCCT StaticMesh %s: idx1 first", Name);
				for (int i = 0; i < IndexStream1.Indices.Num() && i < 32; i++)
					appPrintf(" %04X", IndexStream1.Indices[i]);
				appPrintf("\n");
			}
			if (IndexStream2.Indices.Num())
			{
				appPrintf("SCCT StaticMesh %s: idx2 first", Name);
				for (int i = 0; i < IndexStream2.Indices.Num() && i < 32; i++)
					appPrintf(" %04X", IndexStream2.Indices[i]);
				appPrintf("\n");
			}
		}

		bool bHaveByteFaces = false;
		if (!getenv("SCCT_SM_BYTEFACES") && IndexStream1.Indices.Num() >= 3 && (IndexStream1.Indices.Num() % 3) == 0)
		{
			bHaveByteFaces = true;
			if (getenv("SCCT_SM_DEBUG"))
				appPrintf("SCCT StaticMesh %s: idx1 triangle list faces=%d verts=%d\n",
					Name, IndexStream1.Indices.Num() / 3, NumVerts);
		}
		const bool bUseStrip1 = !getenv("SCCT_SM_BYTEFACES") && (getenv("SCCT_SM_STRIP1") || (IndexStream1.Indices.Num() % 3) != 0);
		const bool bUseStrip2 = getenv("SCCT_SM_STRIP2") != NULL;
		if (!bHaveByteFaces && (bUseStrip1 || bUseStrip2) && (bUseStrip2 ? IndexStream2.Indices.Num() : IndexStream1.Indices.Num()) >= 3)
		{
			TArray<uint16>& StripIndices = bUseStrip2 ? IndexStream2.Indices : IndexStream1.Indices;
			TArray<uint16> StripFaces;
			for (int i = 2; i < StripIndices.Num(); i++)
			{
				const int A = StripIndices[i - 2] & 0x7FFF;
				const int B = StripIndices[i - 1] & 0x7FFF;
				const int C = StripIndices[i] & 0x7FFF;
				if (A >= NumVerts || B >= NumVerts || C >= NumVerts)
					continue;
				if (A == B || B == C || C == A)
					continue;

				const int Base = StripFaces.Num();
				StripFaces.AddZeroed(3);
				if (i & 1)
				{
					StripFaces[Base + 0] = B;
					StripFaces[Base + 1] = A;
					StripFaces[Base + 2] = C;
				}
				else
				{
					StripFaces[Base + 0] = A;
					StripFaces[Base + 1] = B;
					StripFaces[Base + 2] = C;
				}
			}
			if (StripFaces.Num() >= 3)
			{
				IndexStream1.Indices.Empty(StripFaces.Num());
				IndexStream1.Indices.AddZeroed(StripFaces.Num());
				for (int i = 0; i < StripFaces.Num(); i++)
					IndexStream1.Indices[i] = StripFaces[i];
				bHaveByteFaces = true;
				if (getenv("SCCT_SM_DEBUG"))
					appPrintf("SCCT StaticMesh %s: strip%s faces=%d verts=%d stripIndices=%d\n",
						Name, bUseStrip2 ? "2" : "1", StripFaces.Num() / 3, NumVerts, StripIndices.Num());
			}
		}

		const int TailStart = Ar.Tell();
		const int TailSize = Ar.GetStopper() - TailStart;
		if (!bHaveByteFaces && getenv("SCCT_SM_BYTEFACES") && TailSize > 0)
		{
			TArray<byte> Tail;
			Tail.Empty(TailSize);
			Tail.AddZeroed(TailSize);
			Ar.Serialize(Tail.GetData(), TailSize);

			int BestOffset = -1;
			int BestFaces = 0;
			float BestArea = 0.0f;
			const int MaxMaterialIndex = Materials.Num() > 8 ? Materials.Num() + 8 : 16;
			for (int Offset = 0; Offset + 11 < TailSize; Offset++)
			{
				int NumFaces = 0;
				float AreaSum = 0.0f;
				for (int Pos = Offset; Pos + 3 < TailSize; Pos += 4)
				{
					const int I0  = Tail[Pos + 0];
					const int I1  = Tail[Pos + 1];
					const int I2  = Tail[Pos + 2];
					const int Mat = Tail[Pos + 3];
					if (I0 >= NumVerts || I1 >= NumVerts || I2 >= NumVerts || Mat > MaxMaterialIndex)
						break;
					if (I0 == I1 || I1 == I2 || I2 == I0)
						break;

					const FVector& A = VertexStream.Vert[I0].Pos;
					const FVector& B = VertexStream.Vert[I1].Pos;
					const FVector& C = VertexStream.Vert[I2].Pos;
					const float Ux = B.X - A.X;
					const float Uy = B.Y - A.Y;
					const float Uz = B.Z - A.Z;
					const float Vx = C.X - A.X;
					const float Vy = C.Y - A.Y;
					const float Vz = C.Z - A.Z;
					const float Cx = Uy * Vz - Uz * Vy;
					const float Cy = Uz * Vx - Ux * Vz;
					const float Cz = Ux * Vy - Uy * Vx;
					const float Area = Cx * Cx + Cy * Cy + Cz * Cz;
					if (Area >= 0.000001f)
						AreaSum += Area;
					NumFaces++;
				}
				if (NumFaces > BestFaces || (NumFaces == BestFaces && AreaSum > BestArea))
				{
					BestOffset = Offset;
					BestFaces = NumFaces;
					BestArea = AreaSum;
				}
			}

			if (BestFaces >= 8 && BestFaces * 3 > IndexStream1.Indices.Num())
			{
				IndexStream1.Indices.Empty(BestFaces * 3);
				IndexStream1.Indices.AddZeroed(BestFaces * 3);
				for (int i = 0; i < BestFaces; i++)
				{
					const int Pos = BestOffset + i * 4;
					IndexStream1.Indices[i * 3 + 0] = Tail[Pos + 0];
					IndexStream1.Indices[i * 3 + 1] = Tail[Pos + 1];
					IndexStream1.Indices[i * 3 + 2] = Tail[Pos + 2];
				}
				bHaveByteFaces = true;
				if (getenv("SCCT_SM_DEBUG"))
				{
					appPrintf("SCCT StaticMesh %s: byte face block at %08X (+%X) faces=%d verts=%d tail=%d\n",
						Name, TailStart + BestOffset, BestOffset, BestFaces, NumVerts, TailSize);
				}
			}
		}

		if (!bHaveByteFaces && getenv("SCCT_SM_WEDGEMAP") && IndexStream1.Indices.Num() && (IndexStream1.Indices.Num() % 3) == 0 && !getenv("SCCT_SM_POINT_INDICES"))
		{
			TArray<FStaticMeshVertex> PointVerts;
			PointVerts.Empty(NumVerts);
			PointVerts.AddZeroed(NumVerts);
			TArray<FMeshUVFloat> PointUVs;
			PointUVs.Empty(NumVerts);
			PointUVs.AddZeroed(NumVerts);
			for (int i = 0; i < NumVerts; i++)
			{
				PointVerts[i] = VertexStream.Vert[i];
				PointUVs[i] = UV->Data[i];
			}

			const int NumWedges = IndexStream1.Indices.Num();
			bool bValidWedgeMap = true;
			for (int i = 0; i < NumWedges; i++)
			{
				if (IndexStream1.Indices[i] >= NumVerts)
				{
					bValidWedgeMap = false;
					break;
				}
			}

			if (bValidWedgeMap)
			{
				VertexStream.Vert.Empty(NumWedges);
				VertexStream.Vert.AddZeroed(NumWedges);
				UV->Data.Empty(NumWedges);
				UV->Data.AddZeroed(NumWedges);
				for (int i = 0; i < NumWedges; i++)
				{
					const int PointIndex = IndexStream1.Indices[i];
					VertexStream.Vert[i] = PointVerts[PointIndex];
					UV->Data[i] = PointUVs[PointIndex];
					IndexStream1.Indices[i] = i;
				}
				NumVerts = NumWedges;
			}
		}

		const int NumIndices = IndexStream1.Indices.Num();
		if (NumVerts && NumIndices >= 3)
		{
			FStaticMeshSection* Section = new (Sections) FStaticMeshSection;
			memset(Section, 0, sizeof(FStaticMeshSection));
			Section->FirstIndex  = 0;
			Section->FirstVertex = 0;
			Section->LastVertex  = NumVerts - 1;
			Section->fE          = NumIndices;
			Section->NumFaces    = NumIndices / 3;
		}

		DROP_REMAINING_DATA(Ar);
		ConvertMesh();
		return;
	}
#endif // SPLINTER_CELL

#if TRIBES3
	TRIBES_HDR(Ar, 3);
#endif
#if VANGUARD
	if (Ar.Game == GAME_Vanguard) GUseNewVanguardStaticMesh = false; // in game code InternalVersion is analyzed before serialization
#endif
	Ar << Sections;
	Ar << BoundingBox;			// UPrimitive field, serialized twice ...
#if UC2
	if (Ar.Engine() == GAME_UE2X)
	{
		FVector f120, VectorScale, VectorBase;	// defaults: vec(1.0), Scale=vec(1.0), Base=vec(0.0)
		int     f154, f158, f15C, f160;
		if (Ar.ArVer >= 135)
		{
			Ar << f120 << VectorScale << f154 << f158 << f15C << f160;
			if (Ar.ArVer >= 137) Ar << VectorBase;
		}
		GUC2VectorScale = VectorScale;
		GUC2VectorBase  = VectorBase;
		Ar << VertexStream << ColorStream << AlphaStream << UVStream << IndexStream1;
		if (Ar.ArLicenseeVer != 1) Ar << IndexStream2;
		//!!!!!
//		appPrintf("v:%d c1:%d c2:%d uv:%d idx1:%d\n", VertexStream.Vert.Num(), ColorStream.Color.Num(), AlphaStream.Color.Num(),
//			UVStream.Num() ? UVStream[0].Data.Num() : -1, IndexStream1.Indices.Num());
		Ar << f108;

		LoadExternalUC2Data();

		// skip collision information
		goto skip_remaining;
	}
#endif // UC2
#if SWRC
	if (Ar.Game == GAME_RepCommando)
	{
		int f164, f160;
		Ar << VertexStream;
		if (Ar.ArVer >= 155) Ar << f164;
		if (Ar.ArVer >= 149) Ar << f160;
		Ar << ColorStream << AlphaStream << UVStream << IndexStream1 << IndexStream2 << f108;
		goto skip_remaining;
	}
#endif // SWRC
	Ar << VertexStream << ColorStream << AlphaStream << UVStream << IndexStream1 << IndexStream2 << f108;

#if 1
	// UT2 and UE2Runtime has very different collision structures
	// We don't need it, so don't bother serializing it
	goto skip_remaining;
#else
	if (Ar.ArVer < 126)
	{
		assert(Ar.ArVer >= 112);
		TArray<FStaticMeshCollisionTriangle> CollisionFaces;
		TArray<FStaticMeshCollisionNode>     CollisionNodes;
		Ar << CollisionFaces << CollisionNodes;
	}
	else
	{
		// this is an UT2 code, UE2Runtime has different structures
		Ar << kDOPNodes << kDOPCollisionFaces;
	}
	if (Ar.ArVer < 114)
		Ar << f124 << f128 << f12C;

	Ar << Faces;					// can skip this array
	Ar << InternalVersion;

#if UT2
	if (Ar.Game == GAME_UT2)
	{
		//?? check for generic UE2
		if (Ar.ArLicenseeVer == 22)
		{
			float unk;				// predecessor of f150
			Ar << unk;
		}
		else if (Ar.ArLicenseeVer >= 23)
			Ar << f150;
		Ar << f16C;
		if (Ar.ArVer >= 120)
			Ar << f15C;
	}
#endif // UT2
#endif // 0

	ConvertMesh();

	unguard;
}

#if UC2

void UStaticMesh::LoadExternalUC2Data()
{
	guard(UStaticMesh::LoadExternalUC2Data);

	int i, Size;
	void *Data;

	//?? S.NumFaces is used as NumIndices, but it is not a multiply of 3
	//?? (sometimes is is N*3, sometimes = N*3+1, sometimes - N*3+2 ...)
	//?? May be UC2 uses triangle strips instead of triangles?
	assert(IndexStream1.Indices.Num() == 0);	//???
/*	int NumIndices = 0;
	for (i = 0; i < Sections.Num(); i++)
	{
		FStaticMeshSection &S = Sections[i];
		int idx = S.FirstIndex + S.NumFaces;
	} */
	/*
		FindXprData will return block in following format:
			dword	unk
			dword	itemSize (6 for index stream = 3 verts * sizeof(int16))
					(or unknown meaning in a case of triangle strips)
			dword	unk
			data[]
			dword*5	unk (may be include padding?)
	*/

	for (i = 0; i < Sections.Num(); i++)
	{
//		FStaticMeshSection &S = Sections[i];
		Data = FindXprData(va("%s_%d_pb", Name, i), &Size);
		if (!Data)
		{
			appNotify("Missing external index stream for mesh %s", Name);
			return;
		}
		//!! use
//		appPrintf("...[%d] f4=%d FirstIndex=%d FirstVertex=%d LastVertex=%d fE=%d NumFaces=%d\n", i, S.f4, S.FirstIndex, S.FirstVertex, S.LastVertex, S.fE, S.NumFaces);
		appFree(Data);
	}

	if (!VertexStream.Vert.Num())
	{
		Data = FindXprData(va("%s_VS", Name), &Size);
		if (!Data)
		{
			appNotify("Missing external vertex stream for mesh %s", Name);
			return;
		}
		//!! use
		appFree(Data);
	}

	// other streams:
	//	ColorStream = CS
	//	AlphaStream = AS

	for (i = 0; i < UVStream.Num(); i++)
	{
		if (UVStream[i].Data.Num()) continue;
		Data = FindXprData(va("%s_UV%d", Name, i), &Size);
		if (!Data)
		{
			appNotify("Missing external UV stream for mesh %s", Name);
			return;
		}
		//!! use
		appFree(Data);
	}

	unguard;
}

#endif // UC2


#if VANGUARD

struct FVanguardBasisVector
{
	FVector					v1, v2;

	friend FArchive& operator<<(FArchive &Ar, FVanguardBasisVector &V)
	{
		return Ar << V.v1 << V.v2;
	}
};

SIMPLE_TYPE(FVanguardBasisVector, float)

struct FVanguardUTangentStream
{
	TArray<FVanguardBasisVector> Data;
	int						Version;

	friend FArchive& operator<<(FArchive &Ar, FVanguardUTangentStream &S)
	{
		return Ar << S.Data << S.Version;
	}
};


bool GUseNewVanguardStaticMesh;

void UStaticMesh::SerializeVanguardMesh(FArchive &Ar)
{
	guard(UStaticMesh::SerializeVanguardMesh);

	Super::Serialize(Ar);

	Ar.Seek(Ar.Tell() + 236);		// skip header
	Ar << InternalVersion;
	GUseNewVanguardStaticMesh = (InternalVersion >= 13);

	int		unk1CC, unk134;
	UObject	*unk198, *unk1DC;
	float	unk194, unk19C, unk1A0;
	byte	unk1A4;
	TArray<FVanguardUTangentStream> BasisStream;
	TArray<int> unk144, unk150;
	TArray<byte> unk200;

	Ar << unk1CC << unk134 << f108 << unk198 << unk194 << unk19C;
#if DEBUG_STATICMESH
	appPrintf("Version: %d\n", InternalVersion);
#endif
	if (InternalVersion > 11)
		Ar << unk1A0;
	Ar << unk1A4;
	Ar << unk1DC;

	Ar << BoundingBox;
#if DEBUG_STATICMESH
	appPrintf("Bounds: %g %g %g - %g %g %g (%d)\n", VECTOR_ARG(BoundingBox.Min), VECTOR_ARG(BoundingBox.Max), BoundingBox.IsValid);
#endif

	Ar << Sections;

	TArray<FVanguardSkin> Skins;
	Ar << Skins;
	if (Skins.Num() && !Materials.Num())
	{
		const FVanguardSkin &S = Skins[0];
		Materials.AddZeroed(S.Textures.Num());
		for (int i = 0; i < S.Textures.Num(); i++)
			Materials[i].Material = S.Textures[i];
	}

	Ar << Faces << UVStream << BasisStream;
	Ar << unk144 << unk150 << unk200;

	Ar << VertexStream << ColorStream << AlphaStream << IndexStream1 << IndexStream2;

	// skip the remaining data
	Ar.Seek(Ar.GetStopper());

	ConvertMesh();

	unguard;
}

#endif // VANGUARD

void UStaticMesh::ConvertMesh()
{
	guard(UStaticMesh::ConvertMesh);

	int i;

	CStaticMesh *Mesh = new CStaticMesh(this);
	ConvertedMesh = Mesh;
	Mesh->BoundingBox    = BoundingBox;
	Mesh->BoundingSphere = BoundingSphere;

	CStaticMeshLod *Lod = new (Mesh->Lods) CStaticMeshLod;
	Lod->HasNormals  = true;
	Lod->HasTangents = false;

	// convert sections
	Lod->Sections.AddZeroed(Sections.Num());
	for (i = 0; i < Sections.Num(); i++)
	{
		CMeshSection &Dst = Lod->Sections[i];
		const FStaticMeshSection &Src = Sections[i];
		Dst.Material   = (i < Materials.Num()) ? Materials[i].Material : NULL;
		Dst.FirstIndex = Src.FirstIndex;
		Dst.NumFaces   = Src.NumFaces;
	}

	// convert vertices
	int NumVerts = VertexStream.Vert.Num();
	int NumTexCoords = UVStream.Num();
	if (NumTexCoords > MAX_MESH_UV_SETS)
	{
		appNotify("StaticMesh has %d UV sets", NumTexCoords);
		NumTexCoords = MAX_MESH_UV_SETS;
	}
	Lod->NumTexCoords = NumTexCoords;

	Lod->AllocateVerts(NumVerts);
	if (ColorStream.Color.Num() && UseVertexColor)
		Lod->AllocateVertexColorBuffer();

	bool PrintedWarning = false;
	for (i = 0; i < NumVerts; i++)
	{
		CStaticMeshVertex &V = Lod->Verts[i];
		const FStaticMeshVertex &SV = VertexStream.Vert[i];

		V.Position = CVT(SV.Pos);
		Pack(V.Normal, CVT(SV.Normal));
		if (Lod->VertexColors)
			Lod->VertexColors[i] = ColorStream.Color[i];

		for (int j = 0; j < NumTexCoords; j++)
		{
			if (i < UVStream[j].Data.Num())		// Lineage2 has meshes with UVStream[i>1].Data size less than NumVerts
			{
				const FMeshUVFloat &SUV = UVStream[j].Data[i];
				if (j == 0)
				{
					V.UV = CVT(SUV);
				}
				else
				{
					Lod->ExtraUV[j-1][i] = CVT(SUV);
				}
			}
			else if (!PrintedWarning)
			{
				appPrintf("WARNING: StaticMesh UV#%d has %d vertices (should be %d)\n", j, UVStream[j].Data.Num(), NumVerts);
				PrintedWarning = true;
			}
		}
	}

	// copy indices
	Lod->Indices.Initialize(&IndexStream1.Indices);

	Mesh->FinalizeMesh();

	unguard;
}
