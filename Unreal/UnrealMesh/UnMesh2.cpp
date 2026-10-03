#include "Core.h"
#include "UnCore.h"
#include "UnObject.h"
#include "UnMesh2.h"
#include "UnMeshTypes.h"
#include "UnrealPackage/UnPackage.h"
#include "GameSpecific/UnUbisoft.h"

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

static unsigned ReadSCDAUInt32At(FArchive &Ar, int Pos, bool BigEndian)
{
	byte B[4];
	Ar.Seek(Pos);
	Ar.Serialize(B, 4);
	return BigEndian
		? ((unsigned)B[0] << 24) | ((unsigned)B[1] << 16) | ((unsigned)B[2] << 8) | B[3]
		: ((unsigned)B[0]) | ((unsigned)B[1] << 8) | ((unsigned)B[2] << 16) | ((unsigned)B[3] << 24);
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

static bool IsSCDAMaterialClassName(const char *ClassName)
{
	return ClassName &&
		(!stricmp(ClassName, "SCX_basic_material") ||
		!stricmp(ClassName, "SCX_glow_material") ||
		!stricmp(ClassName, "SCX_unlit_material") ||
		!stricmp(ClassName, "SCX_glass_material") ||
		!stricmp(ClassName, "SCX_refractiveglass_material") ||
		!stricmp(ClassName, "Unreal3Material") ||
		!stricmp(ClassName, "BasicModernMaterial") ||
		!stricmp(ClassName, "TranspBasicModernMaterial") ||
		!stricmp(ClassName, "EmissiveModernMaterial") ||
		!stricmp(ClassName, "TranspEmissiveModernMaterial") ||
		!stricmp(ClassName, "EvolvedModernMaterial") ||
		!stricmp(ClassName, "Material") ||
		!stricmp(ClassName, "Material3") ||
		!stricmp(ClassName, "MaterialInstance") ||
		!stricmp(ClassName, "MaterialInstanceConstant"));
}

static void ScanSCDAPcV1MaterialRefs(FArchive &Ar, UnPackage *Package, int Start, int Stop, TArray<int>& OutRefs)
{
	guard(ScanSCDAPcV1MaterialRefs);
	OutRefs.Empty();
	if (!Package)
		return;

	int SavePos = Ar.Tell();
	for (int Pos = Start; Pos < Stop; Pos++)
	{
		Ar.Seek(Pos);
		int Ref = 0;
		if (!ReadSCDACompactIndex(Ar, Stop, Ref) || Ref >= 0)
			continue;
		const int ImportIndex = -Ref - 1;
		if (unsigned(ImportIndex) >= Package->Summary.ImportCount)
			continue;
		const FObjectImport& Imp = Package->GetImport(ImportIndex);
		if (!IsSCDAMaterialClassName(*Imp.ClassName))
			continue;

		bool Duplicate = false;
		for (int Existing : OutRefs)
			if (Existing == Ref)
				Duplicate = true;
		if (!Duplicate)
			OutRefs.Add(Ref);

		if (getenv("SCDA_DEBUG_MATERIALS"))
		{
			const char *PackageName = Package->GetObjectPackageName(Imp.PackageIndex);
			appPrintf("SCDA v1 material ref at %08X: ref=%d import=%d %s'%s' package=%s\n",
				Pos, Ref, ImportIndex, *Imp.ClassName, *Imp.ObjectName, PackageName ? PackageName : "");
		}
		Pos = Ar.Tell() - 1;
	}
	Ar.Seek(SavePos);
	unguard;
}

static bool ReadSCDAPcV1HeaderMaterialRefs(FArchive &Ar, UnPackage *Package, int Start, int Stop, TArray<int>& OutRefs)
{
	guard(ReadSCDAPcV1HeaderMaterialRefs);
	OutRefs.Empty();
	if (!Package)
		return false;

	const int SavePos = Ar.Tell();
	const int ProbeStop = min(Start + 16, Stop - 0x34);
	for (int Header = Start; Header <= ProbeStop; Header++)
	{
		if (ReadSCDAUInt32At(Ar, Header + 0x00, false) != 0xC32CD01C ||
			ReadSCDAUInt32At(Ar, Header + 0x0C, false) != 0x432CD01C)
			continue;

		Ar.Seek(Header + 0x33);
		int MaterialCount = 0;
		Ar << AR_INDEX(MaterialCount);
		if (MaterialCount < 1 || MaterialCount > 64)
			continue;

		TArray<int> Refs;
		Refs.Empty(MaterialCount);
		bool bValid = true;
		for (int i = 0; i < MaterialCount; i++)
		{
			int Ref = 0;
			Ar << AR_INDEX(Ref);
			if (Ref >= 0)
			{
				bValid = false;
				break;
			}
			const int ImportIndex = -Ref - 1;
			if (unsigned(ImportIndex) >= Package->Summary.ImportCount)
			{
				bValid = false;
				break;
			}
			const FObjectImport& Imp = Package->GetImport(ImportIndex);
			if (!IsSCDAMaterialClassName(*Imp.ClassName))
			{
				bValid = false;
				break;
			}
			Refs.Add(Ref);
		}
		if (!bValid || Refs.Num() != MaterialCount)
			continue;

		CopyArray(OutRefs, Refs);
		if (getenv("SCDA_DEBUG_MATERIALS"))
		{
			appPrintf("SCDA v1 header material refs at %08X: count=%d\n", Header, OutRefs.Num());
			for (int i = 0; i < OutRefs.Num(); i++)
			{
				const FObjectImport& Imp = Package->GetImport(-OutRefs[i] - 1);
				const char *PackageName = Package->GetObjectPackageName(Imp.PackageIndex);
				appPrintf("  mat[%d] ref=%d import=%d %s'%s' package=%s\n",
					i, OutRefs[i], -OutRefs[i] - 1, *Imp.ClassName, *Imp.ObjectName, PackageName ? PackageName : "");
			}
		}
		Ar.Seek(SavePos);
		return true;
	}

	Ar.Seek(SavePos);
	return false;
	unguard;
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

struct FSCDAInlineTriangleStreamCandidate
{
	int Start;
	int Stride;
	int VertexCount;
	float Score;
};

static bool ScoreSCDAInlineTriangleStream(FArchive &Ar, int Start, int Stop, int Pos, int Stride, FSCDAInlineTriangleStreamCandidate& Best)
{
	guard(ScoreSCDAInlineTriangleStream);
	if (Stride < 12 || Pos < Start || Pos + Stride * 9 > Stop)
		return false;

	const int MaxVertexCount = min((Stop - Pos) / Stride, 20000);
	if (MaxVertexCount < 9)
		return false;

	int ValidCount = 0;
	int ZeroCount = 0;
	int SmallTailScore = 0;
	float MinX = 0, MinY = 0, MinZ = 0;
	float MaxX = 0, MaxY = 0, MaxZ = 0;
	bool bHaveBounds = false;

	for (int i = 0; i < MaxVertexCount; i++)
	{
		int VPos = Pos + i * Stride;
		float X = ReadSCDAFloatAt(Ar, VPos + 0, false);
		float Y = ReadSCDAFloatAt(Ar, VPos + 4, false);
		float Z = ReadSCDAFloatAt(Ar, VPos + 8, false);
		if (!IsSaneSCDAFloat(X) || !IsSaneSCDAFloat(Y) || !IsSaneSCDAFloat(Z))
			break;
		float Sum = fabs(X) + fabs(Y) + fabs(Z);
		if (Sum < 0.0001f)
			ZeroCount++;
		else
		{
			if (!bHaveBounds)
			{
				MinX = MaxX = X;
				MinY = MaxY = Y;
				MinZ = MaxZ = Z;
				bHaveBounds = true;
			}
			else
			{
				if (X < MinX) MinX = X; if (X > MaxX) MaxX = X;
				if (Y < MinY) MinY = Y; if (Y > MaxY) MaxY = Y;
				if (Z < MinZ) MinZ = Z; if (Z > MaxZ) MaxZ = Z;
			}
		}

		if (Stride >= 24)
		{
			unsigned A = ReadSCDAUInt32At(Ar, VPos + 12, false);
			unsigned B = ReadSCDAUInt32At(Ar, VPos + 16, false);
			unsigned C = ReadSCDAUInt32At(Ar, VPos + 20, false);
			if (A <= 32) SmallTailScore++;
			if (B <= 4) SmallTailScore++;
			if (C <= 16) SmallTailScore++;
		}
		ValidCount++;
	}

	int VertexCount = ValidCount - (ValidCount % 3);
	if (VertexCount < 9)
		return false;
	if (!bHaveBounds || (MaxX - MinX) + (MaxY - MinY) + (MaxZ - MinZ) < 1.0f)
		return false;
	if (ZeroCount > VertexCount / 2)
		return false;

	float Score = (float)VertexCount;
	Score += (float)SmallTailScore * 0.25f;
	Score -= (float)(Pos - Start) * 0.01f;
	Score -= (float)Stride * 0.1f;
	if (Score <= Best.Score)
		return false;

	Best.Start = Pos;
	Best.Stride = Stride;
	Best.VertexCount = VertexCount;
	Best.Score = Score;
	return true;
	unguard;
}

static bool FindSCDAInlineTriangleStream(FArchive &Ar, int Start, int Stop,
	TArray<FVector>& OutPoints, TArray<FMeshWedge>& OutWedges, TArray<VTriangle>& OutTriangles,
	int& OutPointsPos)
{
	guard(FindSCDAInlineTriangleStream);
	if (Stop <= Start || Stop - Start > 0x30000)
		return false;

	FSCDAInlineTriangleStreamCandidate Best;
	memset(&Best, 0, sizeof(Best));
	Best.Start = -1;
	Best.Score = 0.0f;

	static const int Strides[] = { 24, 28, 32, 36, 40, 44, 48, 60 };
	for (int s = 0; s < ARRAY_COUNT(Strides); s++)
	{
		int Stride = Strides[s];
		for (int Pos = Start; Pos < Start + Stride && Pos + Stride * 9 <= Stop; Pos++)
			ScoreSCDAInlineTriangleStream(Ar, Start, Stop, Pos, Stride, Best);
	}

	if (Best.Start < 0 || Best.VertexCount < 9)
		return false;

	OutPoints.Empty(Best.VertexCount);
	OutPoints.AddZeroed(Best.VertexCount);
	OutWedges.Empty(Best.VertexCount);
	OutWedges.AddZeroed(Best.VertexCount);
	for (int i = 0; i < Best.VertexCount; i++)
	{
		int VPos = Best.Start + i * Best.Stride;
		OutPoints[i].X = ReadSCDAFloatAt(Ar, VPos + 0, false);
		OutPoints[i].Y = ReadSCDAFloatAt(Ar, VPos + 4, false);
		OutPoints[i].Z = ReadSCDAFloatAt(Ar, VPos + 8, false);
		OutWedges[i].iVertex = i;
	}

	int TriangleCount = Best.VertexCount / 3;
	OutTriangles.Empty(TriangleCount);
	OutTriangles.AddZeroed(TriangleCount);
	for (int i = 0; i < TriangleCount; i++)
	{
		OutTriangles[i].WedgeIndex[0] = i * 3 + 0;
		OutTriangles[i].WedgeIndex[1] = i * 3 + 1;
		OutTriangles[i].WedgeIndex[2] = i * 3 + 2;
		OutTriangles[i].MatIndex = 0;
	}

	OutPointsPos = Best.Start;
	if (getenv("SC4_DEBUG_MESH"))
		appPrintf("SCDA inline triangle stream: pos=%08X stride=%d verts=%d tris=%d score=%g\n",
			Best.Start, Best.Stride, Best.VertexCount, TriangleCount, Best.Score);
	return true;
	unguard;
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

static bool IsSaneSCDATriangleArea(float Area)
{
	return Area >= 0.000001f && Area <= 1.0e14f;
}

static void PrepareSCDASkeletalMeshForView(USkeletalMesh& Mesh)
{
	guard(PrepareSCDASkeletalMeshForView);
	Mesh.MeshScale.Set(1.0f, 1.0f, 1.0f);
	Mesh.MeshOrigin.Set(0.0f, 0.0f, 0.0f);
	Mesh.RotOrigin.Set(0, 0, 0);
	if (!Mesh.Points.Num())
		return;

	FVector Min, Max;
	Min.Set(3.4e38f, 3.4e38f, 3.4e38f);
	Max.Set(-3.4e38f, -3.4e38f, -3.4e38f);
	for (int i = 0; i < Mesh.Points.Num(); i++)
	{
		const FVector& P = Mesh.Points[i];
		Min.X = min(Min.X, P.X); Min.Y = min(Min.Y, P.Y); Min.Z = min(Min.Z, P.Z);
		Max.X = max(Max.X, P.X); Max.Y = max(Max.Y, P.Y); Max.Z = max(Max.Z, P.Z);
	}

	Mesh.BoundingBox.Min = Min;
	Mesh.BoundingBox.Max = Max;
	Mesh.BoundingBox.IsValid = 1;
	Mesh.BoundingSphere.X = (Min.X + Max.X) * 0.5f;
	Mesh.BoundingSphere.Y = (Min.Y + Max.Y) * 0.5f;
	Mesh.BoundingSphere.Z = (Min.Z + Max.Z) * 0.5f;
	float RadiusSq = 1.0f;
	for (int i = 0; i < Mesh.Points.Num(); i++)
	{
		const FVector& P = Mesh.Points[i];
		const float DX = P.X - Mesh.BoundingSphere.X;
		const float DY = P.Y - Mesh.BoundingSphere.Y;
		const float DZ = P.Z - Mesh.BoundingSphere.Z;
		RadiusSq = max(RadiusSq, DX * DX + DY * DY + DZ * DZ);
	}
	Mesh.BoundingSphere.R = sqrt(RadiusSq);
	unguard;
}

static float GetSCDATriangleAreaSq(const TArray<FVector>& Points, int A, int B, int C)
{
	const float ABx = Points[B].X - Points[A].X;
	const float ABy = Points[B].Y - Points[A].Y;
	const float ABz = Points[B].Z - Points[A].Z;
	const float ACx = Points[C].X - Points[A].X;
	const float ACy = Points[C].Y - Points[A].Y;
	const float ACz = Points[C].Z - Points[A].Z;
	const float CX = ABy * ACz - ABz * ACy;
	const float CY = ABz * ACx - ABx * ACz;
	const float CZ = ABx * ACy - ABy * ACx;
	return CX * CX + CY * CY + CZ * CZ;
}

static bool GetSCDAWedgeTriangleAreaSq(const TArray<FVector>& Points, const TArray<FMeshWedge>& Wedges, int A, int B, int C, float& OutArea)
{
	if (A < 0 || B < 0 || C < 0 || A >= Wedges.Num() || B >= Wedges.Num() || C >= Wedges.Num())
		return false;
	const int PA = Wedges[A].iVertex;
	const int PB = Wedges[B].iVertex;
	const int PC = Wedges[C].iVertex;
	if (PA < 0 || PB < 0 || PC < 0 || PA >= Points.Num() || PB >= Points.Num() || PC >= Points.Num())
		return false;
	if (PA == PB || PA == PC || PB == PC)
		return false;
	OutArea = GetSCDATriangleAreaSq(Points, PA, PB, PC);
	return true;
}

static bool GetSCDAWedgeTriangleMetrics(const TArray<FVector>& Points, const TArray<FMeshWedge>& Wedges, int A, int B, int C, float& OutArea, float& OutMaxEdge)
{
	if (A < 0 || B < 0 || C < 0 || A >= Wedges.Num() || B >= Wedges.Num() || C >= Wedges.Num())
		return false;
	const int PA = Wedges[A].iVertex;
	const int PB = Wedges[B].iVertex;
	const int PC = Wedges[C].iVertex;
	if (PA < 0 || PB < 0 || PC < 0 || PA >= Points.Num() || PB >= Points.Num() || PC >= Points.Num())
		return false;
	if (PA == PB || PA == PC || PB == PC)
		return false;
	const FVector& APos = Points[PA];
	const FVector& BPos = Points[PB];
	const FVector& CPos = Points[PC];
	float DX = APos.X - BPos.X;
	float DY = APos.Y - BPos.Y;
	float DZ = APos.Z - BPos.Z;
	const float E0 = sqrt(DX * DX + DY * DY + DZ * DZ);
	DX = BPos.X - CPos.X;
	DY = BPos.Y - CPos.Y;
	DZ = BPos.Z - CPos.Z;
	const float E1 = sqrt(DX * DX + DY * DY + DZ * DZ);
	DX = CPos.X - APos.X;
	DY = CPos.Y - APos.Y;
	DZ = CPos.Z - APos.Z;
	const float E2 = sqrt(DX * DX + DY * DY + DZ * DZ);
	OutMaxEdge = max(E0, max(E1, E2));
	OutArea = GetSCDATriangleAreaSq(Points, PA, PB, PC);
	return true;
}

static bool FindSCDAPackedVectorBlock(FArchive &Ar, int Start, int Stop, TArray<FVector>& OutPoints, int& OutPos)
{
	guard(FindSCDAPackedVectorBlock);
	int BestPos = 0;
	int BestCount = 0;
	int BestStride = 0;
	int BestExtent = 0;
	for (int Stride = 20; Stride <= 48; Stride += 4)
	{
		for (int Base = Start; Base < Start + Stride && Base <= Stop - 6; Base++)
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
				if (abs((int)X) > 30000 || abs((int)Y) > 30000 || abs((int)Z) > 30000)
					break;
				MinX = min(MinX, (int)X); MaxX = max(MaxX, (int)X);
				MinY = min(MinY, (int)Y); MaxY = max(MaxY, (int)Y);
				MinZ = min(MinZ, (int)Z); MaxZ = max(MaxZ, (int)Z);
				Count++;
				if (Count > 100000)
					break;
			}
			int Extent = (MaxX - MinX) + (MaxY - MinY) + (MaxZ - MinZ);
			if (Count < 64 || Extent < 256)
				continue;
			if (Count > BestCount || (Count == BestCount && Extent > BestExtent))
			{
				BestPos = Base;
				BestCount = Count;
				BestStride = Stride;
				BestExtent = Extent;
			}
		}
	}
	if (!BestCount)
		return false;

	OutPoints.Empty(BestCount);
	OutPoints.AddUninitialized(BestCount);
	for (int i = 0; i < BestCount; i++)
	{
		int16 X, Y, Z;
		Ar.Seek(BestPos + i * BestStride);
		Ar << X << Y << Z;
		OutPoints[i].Set(X / 32.0f, Y / 32.0f, Z / 32.0f);
	}
	OutPos = BestPos;
	if (getenv("SC4_DEBUG_MESH"))
		appPrintf("SCDA packed vector block: pos=%08X count=%d stride=%d extent=%d first=(%g,%g,%g)\n",
			BestPos, BestCount, BestStride, BestExtent,
			OutPoints[0].X, OutPoints[0].Y, OutPoints[0].Z);
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

struct FSCDANativeMeshHeader
{
	int MaterialCount;
	int MaterialRefsPos;
	int TransformPos;
	int TopologyCountPos;
	int TopologyDataPos;
	int TopologyWordCount;
	int FirstMarkerWord;
	int SecondMarkerWord;
	int FacePos;
	int FaceCount;
	int MaxFaceIndex;
};

static bool ReadSCDANativeMeshHeader(FArchive &Ar, int Start, int Stop, FSCDANativeMeshHeader& H, TArray<int> *MaterialRefs = NULL)
{
	guard(ReadSCDANativeMeshHeader);
	memset(&H, 0, sizeof(H));
	if (Start < 0 || Start + 0x34 > Stop)
		return false;
	// Foliage uses its own primitive bounds, unlike the fixed character box.
	for (int Axis = 0; Axis < 3; Axis++)
	{
		float Low = ReadSCDAFloatAt(Ar, Start + Axis * 4, false);
		float High = ReadSCDAFloatAt(Ar, Start + 12 + Axis * 4, false);
		if (!IsSaneSCDAFloat(Low) || !IsSaneSCDAFloat(High) || Low > High)
			return false;
	}
	Ar.Seek(Start + 24);
	byte ValidBox;
	Ar << ValidBox;
	const int NativeVersion = ReadSCDAUInt32At(Ar, Start + 41, false);
	if (ValidBox != 1 || (NativeVersion != 3 && NativeVersion != 4))
		return false;

	Ar.Seek(Start + 0x33);
	Ar << AR_INDEX(H.MaterialCount);
	if (H.MaterialCount < 1 || H.MaterialCount > 64)
		return false;
	H.MaterialRefsPos = Ar.Tell();
	if (MaterialRefs)
		MaterialRefs->Empty(H.MaterialCount);
	for (int i = 0; i < H.MaterialCount; i++)
	{
		int Ref;
		Ar << AR_INDEX(Ref);
		if (MaterialRefs)
			MaterialRefs->Add(Ref);
	}

	// SCDA stores a second, optional material-reference array before the
	// fixed transform block. Donald uses it; Sam's meshes serialize it empty.
	int SecondaryMaterialCount;
	Ar << AR_INDEX(SecondaryMaterialCount);
	if (SecondaryMaterialCount < 0 || SecondaryMaterialCount > 64)
		return false;
	for (int i = 0; i < SecondaryMaterialCount; i++)
	{
		int Ref;
		Ar << AR_INDEX(Ref);
	}

	H.TransformPos = Ar.Tell();
	if (H.TransformPos + 36 > Stop ||
		ReadSCDAUInt32At(Ar, H.TransformPos + 0, false) != 0x3F800000 ||
		ReadSCDAUInt32At(Ar, H.TransformPos + 4, false) != 0x3F800000 ||
		ReadSCDAUInt32At(Ar, H.TransformPos + 8, false) != 0x3F800000)
		return false;
	H.TopologyCountPos = H.TransformPos + 36;
	if (H.TopologyCountPos + 4 > Stop)
		return false;
	H.TopologyWordCount = ReadSCDAUInt32At(Ar, H.TopologyCountPos, false);
	H.TopologyDataPos = H.TopologyCountPos + 4;
	if (H.TopologyWordCount < 16 || H.TopologyWordCount > 100000 ||
		H.TopologyDataPos + H.TopologyWordCount * 2 > Stop)
		return false;

	H.FirstMarkerWord = -1;
	H.SecondMarkerWord = -1;
	uint16 Marker = 0;
	for (int i = 0; i < H.TopologyWordCount; i++)
	{
		const uint16 Value = ReadSCDAUInt16At(Ar, H.TopologyDataPos + i * 2, false);
		if (!(Value & 0xE000))
			continue;
		if (H.FirstMarkerWord < 0)
		{
			H.FirstMarkerWord = i;
			Marker = Value;
		}
		else
		{
			if (Value != Marker)
				return false;
			H.SecondMarkerWord = i;
			break;
		}
	}
	if (H.FirstMarkerWord < 0 || H.SecondMarkerWord <= H.FirstMarkerWord + 1)
		return false;

	H.FaceCount = H.SecondMarkerWord - H.FirstMarkerWord - 1;
	H.FacePos = H.TopologyDataPos + (H.SecondMarkerWord + 1) * 2;
	if (H.FaceCount < 1 || H.FaceCount > 100000 || H.FacePos + H.FaceCount * 8 > Stop)
		return false;

	H.MaxFaceIndex = 0;
	for (int i = 0; i < H.FaceCount; i++)
	{
		const int Pos = H.FacePos + i * 8;
		const uint16 A = ReadSCDAUInt16At(Ar, Pos + 0, false);
		const uint16 B = ReadSCDAUInt16At(Ar, Pos + 2, false);
		const uint16 C = ReadSCDAUInt16At(Ar, Pos + 4, false);
		const uint16 Material = ReadSCDAUInt16At(Ar, Pos + 6, false);
		if (A == B || A == C || B == C || Material >= H.MaterialCount)
			return false;
		H.MaxFaceIndex = max(H.MaxFaceIndex, max((int)A, max((int)B, (int)C)));
	}
	if (getenv("SC4_DEBUG_MESH"))
		appPrintf("SCDA native mesh header: materials=%d transform=%08X topologyCount=%08X words=%d markers=%d,%d faces=%08X count=%d maxIndex=%d\n",
			H.MaterialCount, H.TransformPos, H.TopologyCountPos, H.TopologyWordCount,
			H.FirstMarkerWord, H.SecondMarkerWord, H.FacePos, H.FaceCount, H.MaxFaceIndex);
	return true;
	unguard;
}

static bool FindSCDANativeMeshHeader(FArchive &Ar, int Start, int Stop, FSCDANativeMeshHeader& H, TArray<int> *MaterialRefs = NULL)
{
	guard(FindSCDANativeMeshHeader);
	const int ProbeStop = min(Start + 16, Stop - 0x34);
	for (int Pos = Start; Pos <= ProbeStop; Pos++)
	{
		if (ReadSCDANativeMeshHeader(Ar, Pos, Stop, H, MaterialRefs))
			return true;
	}
	return false;
	unguard;
}

static bool ReadSCDANativeVertexStream(FArchive &Ar, int Start, int Stop, int VertexCount,
	TArray<FVector>& OutPoints, TArray<FMeshWedge>& OutWedges, TArray<FVertInfluence>& OutInfluences,
	int& OutDataPos, int& OutBoneCount)
{
	guard(ReadSCDANativeVertexStream);
	if (VertexCount < 1 || VertexCount > 65535)
		return false;

	int FoundDataPos = 0;
	int CandidateCount = 0;
	for (int CountPos = Start; CountPos < Stop; CountPos++)
	{
		int Pos = CountPos;
		byte B = 0;
		if (Pos >= Stop)
			break;
		Ar.Seek(Pos++);
		Ar << B;
		const bool Negative = (B & 0x80) != 0;
		int Count = B & 0x3F;
		int Shift = 6;
		if (B & 0x40)
		{
			do
			{
				if (Pos >= Stop || Shift > 27)
				goto next_count_pos;
				Ar.Seek(Pos++);
				Ar << B;
				Count |= (B & 0x7F) << Shift;
				Shift += 7;
			} while (B & 0x80);
		}
		if (Negative)
			Count = -Count;
		const int DataPos = Pos;
		if (Count != VertexCount || DataPos + VertexCount * 36 > Stop)
			continue;

		FVector Min, Max;
		Min.Set(3.4e38f, 3.4e38f, 3.4e38f);
		Max.Set(-3.4e38f, -3.4e38f, -3.4e38f);
		bool Valid = true;
		for (int i = 0; i < VertexCount; i++)
		{
			const int Pos = DataPos + i * 36;
			const float X = ReadSCDAFloatAt(Ar, Pos + 0, false);
			const float Y = ReadSCDAFloatAt(Ar, Pos + 4, false);
			const float Z = ReadSCDAFloatAt(Ar, Pos + 8, false);
			if (!IsSaneSCDAFloat(X) || !IsSaneSCDAFloat(Y) || !IsSaneSCDAFloat(Z))
			{
				Valid = false;
				break;
			}
			Min.X = min(Min.X, X); Min.Y = min(Min.Y, Y); Min.Z = min(Min.Z, Z);
			Max.X = max(Max.X, X); Max.Y = max(Max.Y, Y); Max.Z = max(Max.Z, Z);
		}
		if (!Valid)
			continue;
		const float Extent = (Max.X - Min.X) + (Max.Y - Min.Y) + (Max.Z - Min.Z);
		if (Extent < 10.0f || Extent > 5000.0f)
			continue;
		FoundDataPos = DataPos;
		CandidateCount++;
	next_count_pos:;
	}
	if (CandidateCount != 1)
	{
		if (getenv("SC4_DEBUG_MESH"))
			appPrintf("SCDA native vertex stream declaration is not unique: count=%d candidates=%d\n", VertexCount, CandidateCount);
		return false;
	}

	OutPoints.Empty(VertexCount);
	OutPoints.AddUninitialized(VertexCount);
	OutWedges.Empty(VertexCount);
	OutWedges.AddZeroed(VertexCount);
	OutInfluences.Empty(VertexCount * 2);
	OutBoneCount = 0;
	for (int i = 0; i < VertexCount; i++)
	{
		const int Pos = FoundDataPos + i * 36;
		OutPoints[i].Set(
			ReadSCDAFloatAt(Ar, Pos + 0, false),
			ReadSCDAFloatAt(Ar, Pos + 4, false),
			ReadSCDAFloatAt(Ar, Pos + 8, false)
		);
		OutWedges[i].iVertex = i;
		OutWedges[i].TexUV.U = ReadSCDAUInt16At(Ar, Pos + 12, false) / 2048.0f;
		OutWedges[i].TexUV.V = ReadSCDAUInt16At(Ar, Pos + 14, false) / 2048.0f;
		int WeightSum = 0;
		for (int j = 0; j < 4; j++)
		{
			const byte Bone = ReadSCDAUInt16At(Ar, Pos + 28 + j, false) & 0xFF;
			const byte Weight = ReadSCDAUInt16At(Ar, Pos + 32 + j, false) & 0xFF;
			WeightSum += Weight;
			if (!Weight)
				continue;
			FVertInfluence& Influence = OutInfluences[OutInfluences.AddDefaulted()];
			Influence.Weight = Weight / 255.0f;
			Influence.PointIndex = i;
			Influence.BoneIndex = Bone;
			OutBoneCount = max(OutBoneCount, (int)Bone + 1);
		}
		if (WeightSum != 255)
			return false;
	}
	OutDataPos = FoundDataPos;
	if (getenv("SC4_DEBUG_MESH"))
		appPrintf("SCDA native vertex stream: count=%d stride=36 data=%08X-%08X uvOff=12 influences=%d bones=%d first=(%g,%g,%g) uv=(%g,%g)\n",
			VertexCount, FoundDataPos, FoundDataPos + VertexCount * 36,
			OutInfluences.Num(), OutBoneCount,
			OutPoints[0].X, OutPoints[0].Y, OutPoints[0].Z,
			OutWedges[0].TexUV.U, OutWedges[0].TexUV.V);
	return true;
	unguard;
}

struct FSCDANativeBonePalette
{
	int		MaterialIndex;
	int		FirstFace;
	int		LastFace;
	int		FirstPoint;
	int		LastPoint;
	byte	Map[256];

	void Clear()
	{
		MaterialIndex = -1;
		FirstFace = LastFace = -1;
		FirstPoint = LastPoint = -1;
		memset(Map, 0xFF, sizeof(Map));
	}
};

struct FSCDANativeFaceRun
{
	int		MaterialIndex;
	int		FirstFace;
	int		FaceCount;
};

static bool ReadSCDANativeBonePalette(FArchive &Ar, int Pos, int Stop, int ExpectedFaceCount,
	int LocalBoneCount, int GlobalBoneCount, FSCDANativeBonePalette& Palette, int& OutEnd)
{
	guard(ReadSCDANativeBonePalette);
	if (Pos < 0 || Pos + 3 > Stop)
		return false;
	if ((int)ReadSCDAUInt16At(Ar, Pos, false) != ExpectedFaceCount)
		return false;
	byte Count = 0;
	Ar.Seek(Pos + 2);
	Ar << Count;
	if (Count < 1 || Count > 64 || Pos + 3 + Count * 2 > Stop)
		return false;

	Palette.Clear();
	int UsedLocals = 0;
	for (int i = 0; i < Count; i++)
	{
		byte GlobalBone = 0, LocalBone = 0;
		Ar << GlobalBone << LocalBone;
		if (GlobalBone >= GlobalBoneCount || LocalBone >= LocalBoneCount)
			return false;
		if (Palette.Map[LocalBone] == 0xFF)
			UsedLocals++;
		Palette.Map[LocalBone] = GlobalBone;
	}
	if (UsedLocals < min(LocalBoneCount, 2))
		return false;
	OutEnd = Pos + 3 + Count * 2;
	return true;
	unguard;
}

static bool FindSCDAPcV1HardwareBoneMap(FArchive &Ar, int Start, int Stop,
	int LocalBoneCount, int GlobalBoneCount, byte OutMap[256], int& OutPos)
{
	guard(FindSCDAPcV1HardwareBoneMap);
	memset(OutMap, 0xFF, 256);
	OutPos = 0;
	if (LocalBoneCount < 2 || LocalBoneCount > 64 || GlobalBoneCount <= 0 || GlobalBoneCount > 256)
		return false;
	if (Start < 0 || Stop <= Start)
		return false;

	for (int Pos = Start; Pos + 1 + LocalBoneCount * 2 <= Stop; Pos++)
	{
		byte Count = 0;
		Ar.Seek(Pos);
		Ar << Count;
		if (Count != LocalBoneCount)
			continue;

		int PairCount = 0;
		bool SeenLocal[256];
		memset(SeenLocal, 0, sizeof(SeenLocal));
		byte TestMap[256];
		memset(TestMap, 0xFF, sizeof(TestMap));
		for (int Pair = 0; Pair < LocalBoneCount; Pair++)
		{
			byte Global = 0, Local = 0;
			Ar.Seek(Pos + 1 + Pair * 2);
			Ar << Global << Local;
			if (Global >= GlobalBoneCount || Local >= LocalBoneCount || SeenLocal[Local])
				break;
			SeenLocal[Local] = true;
			TestMap[Local] = Global;
			PairCount++;
		}
		if (PairCount != LocalBoneCount)
			continue;

		memcpy(OutMap, TestMap, 256);
		OutPos = Pos;
		if (getenv("SC4_DEBUG_MESH"))
		{
			appPrintf("SCDA PC v1 hardware bone map: pos=%08X localBones=%d globalBones=%d", Pos, LocalBoneCount, GlobalBoneCount);
			for (int Local = 0; Local < LocalBoneCount && Local < 32; Local++)
				if (OutMap[Local] != 0xFF)
					appPrintf(" %d->%d", Local, OutMap[Local]);
			appPrintf("\n");
		}
		return true;
	}
	return false;
	unguard;
}

static bool HasSCDAPcV1Packed36Preamble(FArchive &Ar, int VertexStart, int MapPos, int Stop)
{
	guard(HasSCDAPcV1Packed36Preamble);
	const int Stride = 36;
	const int WeightOffset = 9;
	if (VertexStart < 0 || MapPos <= VertexStart || Stop <= VertexStart)
		return false;
	if (MapPos - VertexStart > Stride * 8)
		return false;

	int CheckedRecords = 0;
	int InvalidWeightRecords = 0;
	for (int Pos = VertexStart; Pos + WeightOffset + 4 <= Stop && Pos < MapPos; Pos += Stride)
	{
		int WeightSum = 0;
		for (int j = 0; j < 4; j++)
			WeightSum += ReadSCDAUInt16At(Ar, Pos + WeightOffset + j, false) & 0xFF;
		CheckedRecords++;
		if (WeightSum != 255)
			InvalidWeightRecords++;
	}
	return CheckedRecords >= 2 && InvalidWeightRecords >= 2;
	unguard;
}

static bool FindSCDAPcV1EmbeddedHardwareBoneMap(FArchive &Ar, int VertexStart, int Stop,
	int LocalBoneCount, int GlobalBoneCount, byte OutMap[256], int& OutPos)
{
	guard(FindSCDAPcV1EmbeddedHardwareBoneMap);
	memset(OutMap, 0xFF, 256);
	OutPos = 0;
	if (LocalBoneCount < 2 || LocalBoneCount > 64 || GlobalBoneCount <= 0 || GlobalBoneCount > 256)
		return false;
	if (VertexStart < 0 || Stop <= VertexStart)
		return false;

	const int SearchStop = min(Stop, VertexStart + 36 * 8);
	for (int Pos = VertexStart + 1; Pos + 1 + LocalBoneCount * 2 <= SearchStop; Pos++)
	{
		if (!HasSCDAPcV1Packed36Preamble(Ar, VertexStart, Pos, Stop))
			continue;

		byte Count = 0;
		Ar.Seek(Pos);
		Ar << Count;
		if (Count != LocalBoneCount)
			continue;

		int PairCount = 0;
		bool SeenLocal[256];
		memset(SeenLocal, 0, sizeof(SeenLocal));
		byte TestMap[256];
		memset(TestMap, 0xFF, sizeof(TestMap));
		for (int Pair = 0; Pair < LocalBoneCount; Pair++)
		{
			byte Global = 0, Local = 0;
			Ar.Seek(Pos + 1 + Pair * 2);
			Ar << Global << Local;
			if (Global >= GlobalBoneCount || Local >= LocalBoneCount || SeenLocal[Local])
				break;
			SeenLocal[Local] = true;
			TestMap[Local] = Global;
			PairCount++;
		}
		if (PairCount != LocalBoneCount)
			continue;

		memcpy(OutMap, TestMap, 256);
		OutPos = Pos;
		if (getenv("SC4_DEBUG_MESH"))
		{
			appPrintf("SCDA PC v1 embedded hardware bone map: pos=%08X localBones=%d globalBones=%d", Pos, LocalBoneCount, GlobalBoneCount);
			for (int Local = 0; Local < LocalBoneCount && Local < 32; Local++)
				if (OutMap[Local] != 0xFF)
					appPrintf(" %d->%d", Local, OutMap[Local]);
			appPrintf("\n");
		}
		return true;
	}
	return false;
	unguard;
}

static bool ApplySCDAPcV1HardwareBoneMap(const byte Map[256], int LocalBoneCount, TArray<FVertInfluence>& Influences)
{
	guard(ApplySCDAPcV1HardwareBoneMap);
	if (!Influences.Num() || LocalBoneCount <= 0)
		return false;
	int Remapped = 0;
	for (int i = 0; i < Influences.Num(); i++)
	{
		FVertInfluence& I = Influences[i];
		const int LocalBone = I.BoneIndex;
		if (LocalBone >= 0 && LocalBone < LocalBoneCount && Map[LocalBone] != 0xFF)
		{
			I.BoneIndex = Map[LocalBone];
			Remapped++;
		}
	}
	if (getenv("SC4_DEBUG_MESH"))
		appPrintf("SCDA PC v1 hardware bone remap: influences=%d remapped=%d\n",
			Influences.Num(), Remapped);
	return Remapped > 0;
	unguard;
}

// Xbox skin sections carry their own vertex and face ranges. Material runs
// are not a substitute for these records: even rigid sections have a palette.
// default.xbe: section serializer 0x002167E6, bone TMap serializer 0x002125AF.
static bool FindSCDAXboxBonePalettes(FArchive &Ar, int Start, int Stop,
	const TArray<VTriangle>& Triangles, int PointCount, int GlobalBoneCount,
	const TArray<FVertInfluence>& Influences, TArray<FSCDANativeBonePalette>& Palettes)
{
	guard(FindSCDAXboxBonePalettes);
	Palettes.Empty();
	const int SavePos = Ar.Tell();
	TArray<int> PointPalette;
	PointPalette.AddZeroed(PointCount);
	for (int Pos = Start; Pos + 25 < Stop; Pos++)
	{
		Ar.Seek(Pos);
		int SectionCount;
		if (!ReadSCDACompactIndex(Ar, Stop, SectionCount) || SectionCount < 1 || SectionCount > 256)
			continue;
		TArray<FSCDANativeBonePalette> Candidate;
		int NextFace = 0, NextPoint = 0;
		for (int Section = 0; Section < SectionCount; Section++)
		{
			const int Header = Ar.Tell();
			if (Header + 19 > Stop) break;
			FSCDANativeBonePalette P;
			P.Clear();
			P.MaterialIndex = ReadSCDAUInt16At(Ar, Header, false);
			P.FirstPoint = ReadSCDAUInt16At(Ar, Header + 4, false);
			P.LastPoint = ReadSCDAUInt16At(Ar, Header + 6, false);
			const int VertexCount = ReadSCDAUInt16At(Ar, Header + 8, false);
			P.FirstFace = ReadSCDAUInt16At(Ar, Header + 14, false);
			const int FaceCount = ReadSCDAUInt16At(Ar, Header + 16, false);
			P.LastFace = P.FirstFace + FaceCount - 1;
			if (P.FirstPoint != NextPoint || P.LastPoint < P.FirstPoint || P.LastPoint >= PointCount ||
				VertexCount != P.LastPoint - P.FirstPoint + 1 || P.FirstFace != NextFace ||
				FaceCount < 1 || P.LastFace >= Triangles.Num()) break;
			Ar.Seek(Header + 18);
			int BoneCount;
			if (!ReadSCDACompactIndex(Ar, Stop, BoneCount) || BoneCount < 1 || BoneCount > 256 ||
				Ar.Tell() + BoneCount * 2 + 6 > Stop) break;
			bool Valid = true;
			bool SeenGlobal[256];
			memset(SeenGlobal, 0, sizeof(SeenGlobal));
			for (int Bone = 0; Bone < BoneCount; Bone++)
			{
				byte Global, Local;
				Ar << Global << Local;
				if (Global >= GlobalBoneCount || SeenGlobal[Global]) { Valid = false; break; }
				SeenGlobal[Global] = true;
				// Overflow/unused bones can alias slot zero. Preserve the actual
				// hardware slot owner, which precedes these fallback entries.
				if (P.Map[Local] == 0xFF) P.Map[Local] = Global;
				else if (Local != 0) { Valid = false; break; }
			}
			if (!Valid) break;
			Ar.Seek(Ar.Tell() + 6); // index count (u16), section flags (u32)
			const int End = Ar.Tell();
			for (int Face = P.FirstFace; Face <= P.LastFace && Valid; Face++)
			{
				if (Triangles[Face].MatIndex != P.MaterialIndex) { Valid = false; break; }
				for (int Corner = 0; Corner < 3; Corner++)
					if (Triangles[Face].WedgeIndex[Corner] < P.FirstPoint ||
						Triangles[Face].WedgeIndex[Corner] > P.LastPoint) Valid = false;
			}
			if (!Valid) break;
			for (int Point = P.FirstPoint; Point <= P.LastPoint; Point++) PointPalette[Point] = Section;
			new (Candidate) FSCDANativeBonePalette(P);
			NextFace = P.LastFace + 1;
			NextPoint = P.LastPoint + 1;
			Ar.Seek(End);
		}
		if (Candidate.Num() != SectionCount || NextFace != Triangles.Num() || NextPoint != PointCount) continue;
		bool Valid = true;
		for (int i = 0; i < Influences.Num(); i++)
		{
			const FVertInfluence& I = Influences[i];
			if (I.PointIndex < 0 || I.PointIndex >= PointCount || I.BoneIndex < 0 || I.BoneIndex >= 256 ||
				Candidate[PointPalette[I.PointIndex]].Map[I.BoneIndex] == 0xFF) { Valid = false; break; }
		}
		if (!Valid) continue;
		CopyArray(Palettes, Candidate);
		Ar.Seek(SavePos);
		if (getenv("SC4_DEBUG_MESH"))
			appPrintf("SCDA Xbox skin sections: pos=%08X sections=%d vertices=%d faces=%d\n",
				Pos, SectionCount, PointCount, Triangles.Num());
		return true;
	}
	Ar.Seek(SavePos);
	return false;
	unguard;
}

static bool FindSCDANativeBonePalettes(FArchive &Ar, int Start, int Stop, const TArray<VTriangle>& Triangles,
	int LocalBoneCount, int GlobalBoneCount, TArray<FSCDANativeBonePalette>& Palettes)
{
	guard(FindSCDANativeBonePalettes);
	const bool DebugPalette = getenv("SCDA_DEBUG_PALETTE") != NULL;
	if (DebugPalette)
		appPrintf("SCDA palette finder enter: start=%08X stop=%08X tris=%d local=%d global=%d\n",
			Start, Stop, Triangles.Num(), LocalBoneCount, GlobalBoneCount);
	Palettes.Empty();
	if (!Triangles.Num() || LocalBoneCount <= 0 || GlobalBoneCount <= 0)
		return false;

	static FSCDANativeFaceRun Runs[128];
	int RunCount = 0;
	for (int Face = 0; Face < Triangles.Num(); Face++)
	{
		int Mat = Triangles[Face].MatIndex;
		if (Mat < 0 || Mat >= 256)
			return false;
		if (!RunCount || Runs[RunCount - 1].MaterialIndex != Mat)
		{
			if (RunCount >= ARRAY_COUNT(Runs))
				return false;
			Runs[RunCount].MaterialIndex = Mat;
			Runs[RunCount].FirstFace = Face;
			Runs[RunCount].FaceCount = 1;
			RunCount++;
		}
		else
		{
			Runs[RunCount - 1].FaceCount++;
		}
	}
	if (!RunCount)
		return false;
	if (RunCount > 64)
		return false;
	if (DebugPalette)
		appPrintf("SCDA palette finder faceRuns=%d firstFaces=%d firstMat=%d\n",
			RunCount, Runs[0].FaceCount, Runs[0].MaterialIndex);

	int SavePos = Ar.Tell();
	int BestStart = 0;
	static byte BestMaps[64][64];
	static byte ChainMaps[64][64];
	static int BestMaterials[64];
	static int BestFirstFaces[64];
	static int BestLastFaces[64];
	memset(BestMaps, 0xFF, sizeof(BestMaps));
	memset(ChainMaps, 0xFF, sizeof(ChainMaps));
	if (DebugPalette)
		appPrintf("SCDA palette finder flat buffers ready\n");
	int BestPaletteCount = 0;
	int ValidChainCandidates = 0;
	int LastLoggedChainStart = -1;
	for (int Pos = Start; Pos < Stop - 8; Pos++)
	{
		int ChainCount = 0;
		memset(ChainMaps, 0xFF, sizeof(ChainMaps));
		int ScanPos = Pos;
		int ChainFirstStart = -1;
		bool Valid = true;
		for (int i = 0; i < RunCount; i++)
		{
			const int FaceCount = Runs[i].FaceCount;
			bool Found = false;
			for (int Candidate = ScanPos; Candidate < Stop - 8 && Candidate < ScanPos + 0x80; Candidate++)
			{
				FSCDANativeBonePalette P;
				int End = 0;
				if (!ReadSCDANativeBonePalette(Ar, Candidate, Stop, FaceCount, LocalBoneCount, GlobalBoneCount, P, End))
					continue;
				if (ChainFirstStart < 0)
					ChainFirstStart = Candidate;
				for (int Local = 0; Local < LocalBoneCount && Local < 64; Local++)
					ChainMaps[ChainCount][Local] = P.Map[Local];
				ChainCount++;
				ScanPos = End;
				Found = true;
				break;
			}
			if (!Found)
			{
				Valid = false;
				break;
			}
		}
		if (Valid && ChainCount == RunCount)
		{
			ValidChainCandidates++;
			if (DebugPalette && ChainFirstStart != LastLoggedChainStart)
			{
				LastLoggedChainStart = ChainFirstStart;
				appPrintf("SCDA palette chain candidate %d at %08X\n", ValidChainCandidates, ChainFirstStart);
				if (RunCount > 0)
				{
					appPrintf("  first:");
					for (int Local = 0; Local < LocalBoneCount && Local < 16; Local++)
						if (ChainMaps[0][Local] != 0xFF)
							appPrintf(" %d->%d", Local, ChainMaps[0][Local]);
					appPrintf("\n");
				}
				if (RunCount > 8)
				{
					appPrintf("  run8:");
					for (int Local = 0; Local < LocalBoneCount && Local < 16; Local++)
						if (ChainMaps[8][Local] != 0xFF)
							appPrintf(" %d->%d", Local, ChainMaps[8][Local]);
					appPrintf("\n");
				}
			}
			if (!BestPaletteCount)
			{
				BestStart = ChainFirstStart;
				BestPaletteCount = ChainCount;
				for (int i = 0; i < ChainCount; i++)
				{
					BestMaterials[i] = Runs[i].MaterialIndex;
					BestFirstFaces[i] = Runs[i].FirstFace;
					BestLastFaces[i] = Runs[i].FirstFace + Runs[i].FaceCount - 1;
					for (int Local = 0; Local < LocalBoneCount && Local < 64; Local++)
						BestMaps[i][Local] = ChainMaps[i][Local];
				}
			}
		}
	}
	Ar.Seek(SavePos);
	if (!BestPaletteCount)
		return false;

	Palettes.Empty(BestPaletteCount);
	for (int i = 0; i < BestPaletteCount; i++)
	{
		FSCDANativeBonePalette P;
		P.Clear();
		P.MaterialIndex = BestMaterials[i];
		P.FirstFace = BestFirstFaces[i];
		P.LastFace = BestLastFaces[i];
		for (int Local = 0; Local < LocalBoneCount && Local < 64; Local++)
			P.Map[Local] = BestMaps[i][Local];
		new (Palettes) FSCDANativeBonePalette(P);
	}
	if (getenv("SC4_DEBUG_MESH"))
	{
		appPrintf("SCDA bone palettes: pos=%08X sections=%d localBones=%d globalBones=%d\n",
			BestStart, Palettes.Num(), LocalBoneCount, GlobalBoneCount);
		for (int i = 0; i < Palettes.Num(); i++)
		{
			appPrintf("  faces=%d-%d mat=%d", Palettes[i].FirstFace, Palettes[i].LastFace, Palettes[i].MaterialIndex);
			for (int Local = 0; Local < LocalBoneCount && Local < 32; Local++)
				if (Palettes[i].Map[Local] != 0xFF)
					appPrintf(" %d->%d", Local, Palettes[i].Map[Local]);
			appPrintf("\n");
		}
	}
	return true;
	unguard;
}

static bool ApplySCDANativeBonePalettes(const TArray<VTriangle>& Triangles, int PointCount,
	const TArray<FSCDANativeBonePalette>& Palettes, TArray<FVertInfluence>& Influences)
{
	guard(ApplySCDANativeBonePalettes);
	if (!Triangles.Num() || !Palettes.Num() || !Influences.Num() || PointCount <= 0)
		return false;

	TArray<const FSCDANativeBonePalette*> PaletteByPoint;
	PaletteByPoint.Empty(PointCount);
	PaletteByPoint.AddZeroed(PointCount);
	TArray<int> PaletteIndexByPoint;
	PaletteIndexByPoint.Empty(PointCount);
	PaletteIndexByPoint.AddZeroed(PointCount);
	for (int i = 0; i < PointCount; i++)
		PaletteIndexByPoint[i] = -1;
	int PointPaletteConflicts = 0;
	for (int PaletteIndex = 0; PaletteIndex < Palettes.Num(); PaletteIndex++)
	{
		const FSCDANativeBonePalette* P = &Palettes[PaletteIndex];
		if (P->FirstPoint >= 0 && P->LastPoint < PointCount)
		{
			for (int Point = P->FirstPoint; Point <= P->LastPoint; Point++)
			{
				PaletteByPoint[Point] = P;
				PaletteIndexByPoint[Point] = PaletteIndex;
			}
			continue;
		}
		const int FirstFace = max(P->FirstFace, 0);
		const int LastFace = min(P->LastFace, Triangles.Num() - 1);
		for (int Face = FirstFace; Face <= LastFace; Face++)
		{
			for (int j = 0; j < 3; j++)
			{
				int PointIndex = Triangles[Face].WedgeIndex[j];
				if (PointIndex >= 0 && PointIndex < PointCount)
				{
					if (!PaletteByPoint[PointIndex])
					{
						PaletteByPoint[PointIndex] = P;
						PaletteIndexByPoint[PointIndex] = PaletteIndex;
					}
					else if (PaletteByPoint[PointIndex] != P)
						PointPaletteConflicts++;
				}
			}
		}
	}

	static int LocalUsage[64][64];
	static float LocalWeightUsage[64][64];
	memset(LocalUsage, 0, sizeof(LocalUsage));
	memset(LocalWeightUsage, 0, sizeof(LocalWeightUsage));
	int Remapped = 0;
	for (int i = 0; i < Influences.Num(); i++)
	{
		FVertInfluence& I = Influences[i];
		if (I.PointIndex < 0 || I.PointIndex >= PointCount)
			continue;
		const FSCDANativeBonePalette* P = PaletteByPoint[I.PointIndex];
		if (!P)
			continue;
		int LocalBone = I.BoneIndex;
		int PaletteIndex = PaletteIndexByPoint[I.PointIndex];
		if (PaletteIndex >= 0 && PaletteIndex < 64 && LocalBone >= 0 && LocalBone < 64)
		{
			LocalUsage[PaletteIndex][LocalBone]++;
			LocalWeightUsage[PaletteIndex][LocalBone] += I.Weight;
		}
		if (LocalBone >= 0 && LocalBone < 256 && P->Map[LocalBone] != 0xFF)
		{
			I.BoneIndex = P->Map[LocalBone];
			Remapped++;
		}
	}
	if (getenv("SC4_DEBUG_MESH"))
	{
		appPrintf("SCDA bone palette remap: influences=%d remapped=%d pointConflicts=%d\n",
			Influences.Num(), Remapped, PointPaletteConflicts);
		if (getenv("SCDA_DEBUG_PALETTE"))
		{
			for (int PaletteIndex = 0; PaletteIndex < Palettes.Num() && PaletteIndex < 64; PaletteIndex++)
			{
				appPrintf("  usage faces=%d-%d", Palettes[PaletteIndex].FirstFace, Palettes[PaletteIndex].LastFace);
				for (int Local = 0; Local < 64; Local++)
					if (LocalUsage[PaletteIndex][Local])
						appPrintf(" %d:%d/%.1f", Local, LocalUsage[PaletteIndex][Local], LocalWeightUsage[PaletteIndex][Local]);
				appPrintf("\n");
			}
		}
	}
	return Remapped > 0;
	unguard;
}

static int CountSCDARawFaceRecordRun(FArchive &Ar, int Pos, int Stop, const TArray<FVector>& Points, bool BigEndian,
	int& ValidCount, float& AreaScore, float& AvgMaxEdge, int& LongEdges)
{
	guard(CountSCDARawFaceRecordRun);
	const int PointCount = Points.Num();
	ValidCount = 0;
	AreaScore = 0;
	AvgMaxEdge = 3.4e38f;
	LongEdges = 0;
	if (PointCount <= 0 || Pos < 0 || Pos + 8 > Stop)
		return 0;

	int RawCount = 0;
	float SumMaxEdge = 0;
	for (int P = Pos; P <= Stop - 8; P += 8)
	{
		uint16 A = ReadSCDAUInt16At(Ar, P + 0, BigEndian);
		uint16 B = ReadSCDAUInt16At(Ar, P + 2, BigEndian);
		uint16 C = ReadSCDAUInt16At(Ar, P + 4, BigEndian);
		uint16 Aux = ReadSCDAUInt16At(Ar, P + 6, BigEndian);
		if (A >= PointCount || B >= PointCount || C >= PointCount || Aux > 255)
			break;
		RawCount++;
		if (A == B || A == C || B == C)
			continue;
		const float Area = GetSCDATriangleAreaSq(Points, A, B, C);
		if (Area > 1.0e14f)
			continue;
		const FVector& PA = Points[A];
		const FVector& PB = Points[B];
		const FVector& PC = Points[C];
		const float ABx = PB.X - PA.X, ABy = PB.Y - PA.Y, ABz = PB.Z - PA.Z;
		const float BCx = PC.X - PB.X, BCy = PC.Y - PB.Y, BCz = PC.Z - PB.Z;
		const float CAx = PA.X - PC.X, CAy = PA.Y - PC.Y, CAz = PA.Z - PC.Z;
		const float E0 = sqrt(ABx * ABx + ABy * ABy + ABz * ABz);
		const float E1 = sqrt(BCx * BCx + BCy * BCy + BCz * BCz);
		const float E2 = sqrt(CAx * CAx + CAy * CAy + CAz * CAz);
		const float MaxEdge = max(E0, max(E1, E2));
		SumMaxEdge += MaxEdge;
		if (MaxEdge > 35.0f)
			LongEdges++;
		AreaScore += min(Area, 1000000.0f);
		ValidCount++;
		if (RawCount > 100000)
			break;
	}
	if (ValidCount > 0)
		AvgMaxEdge = SumMaxEdge / ValidCount;
	return RawCount;
	unguard;
}

static bool ReadSCDARawFaceRecordsChecked(FArchive &Ar, int Pos, int Stop, int RawCount, const TArray<FVector>& Points, bool BigEndian, TArray<VTriangle>& OutTriangles)
{
	guard(ReadSCDARawFaceRecordsChecked);
	const int PointCount = Points.Num();
	if (RawCount < 1 || PointCount <= 0 || Pos < 0 || Pos + RawCount * 8 > Stop)
		return false;
	OutTriangles.Empty(RawCount);
	for (int i = 0; i < RawCount; i++)
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
		if (A == B || A == C || B == C)
			continue;
		const float Area = GetSCDATriangleAreaSq(Points, A, B, C);
		if (Area > 1.0e14f)
			continue;
		int Index = OutTriangles.AddZeroed(1);
		VTriangle &T = OutTriangles[Index];
		T.WedgeIndex[0] = A;
		T.WedgeIndex[1] = B;
		T.WedgeIndex[2] = C;
		T.MatIndex = Aux & 0xFF;
		T.AuxMatIndex = 0;
		T.SmoothingGroups = 0;
	}
	return OutTriangles.Num() > 0;
	unguard;
}

struct FSCDAStripCandidate
{
	int Pos;
	int IndexCount;
	int TriangleCount;
	int LongEdges;
	float AvgMaxEdge;
};

static bool AppendSCDAStripTriangles(FArchive &Ar, int Pos, int IndexCount, const TArray<FVector>& Points, TArray<VTriangle>& OutTriangles)
{
	guard(AppendSCDAStripTriangles);
	int IndexBase = OutTriangles.Num();
	for (int i = 2; i < IndexCount; i++)
	{
		uint16 I0 = ReadSCDAUInt16At(Ar, Pos + (i - 2) * 2, false);
		uint16 I1 = ReadSCDAUInt16At(Ar, Pos + (i - 1) * 2, false);
		uint16 I2 = ReadSCDAUInt16At(Ar, Pos + i * 2, false);
		uint16 A, B, C;
		if (i & 1)
		{
			A = I0; B = I1; C = I2;
		}
		else
		{
			A = I1; B = I0; C = I2;
		}
		if (A >= Points.Num() || B >= Points.Num() || C >= Points.Num() || A == B || A == C || B == C)
			continue;
		int OutIndex = OutTriangles.AddZeroed(1);
		VTriangle &T = OutTriangles[OutIndex];
		T.WedgeIndex[0] = A;
		T.WedgeIndex[1] = B;
		T.WedgeIndex[2] = C;
		T.MatIndex = 0;
		T.AuxMatIndex = 0;
		T.SmoothingGroups = 0;
	}
	return OutTriangles.Num() > IndexBase;
	unguard;
}

static bool FindSCDACoherentStripBlocks(FArchive &Ar, int Start, int Stop, const TArray<FVector>& Points, int PointsPos, int VertexStride, TArray<VTriangle>& OutTriangles, int& OutPos)
{
	guard(FindSCDACoherentStripBlocks);
	const int PointCount = Points.Num();
	if (PointCount <= 0 || PointCount > 65535 || VertexStride <= 0)
		return false;
	int ScanStart = PointsPos + PointCount * VertexStride;
	ScanStart = max(ScanStart, Start);
	if (ScanStart >= Stop - 6)
		return false;

	TArray<FSCDAStripCandidate> Candidates;
	for (int Pos = ScanStart; Pos <= Stop - 6; Pos += 2)
	{
		int IndexCount = 0;
		int Triangles = 0;
		int LongEdges = 0;
		float SumMaxEdge = 0;
		uint16 Prev2 = 0, Prev1 = 0;
		for (int P = Pos; P <= Stop - 2; P += 2)
		{
			uint16 I = ReadSCDAUInt16At(Ar, P, false);
			if (I >= PointCount)
				break;
			IndexCount++;
			if (IndexCount < 3)
			{
				Prev2 = Prev1;
				Prev1 = I;
				continue;
			}
			uint16 A, B, C;
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
			if (A == B || A == C || B == C)
				continue;
			float DX = Points[A].X - Points[B].X;
			float DY = Points[A].Y - Points[B].Y;
			float DZ = Points[A].Z - Points[B].Z;
			float E0 = sqrt(DX * DX + DY * DY + DZ * DZ);
			DX = Points[B].X - Points[C].X;
			DY = Points[B].Y - Points[C].Y;
			DZ = Points[B].Z - Points[C].Z;
			float E1 = sqrt(DX * DX + DY * DY + DZ * DZ);
			DX = Points[C].X - Points[A].X;
			DY = Points[C].Y - Points[A].Y;
			DZ = Points[C].Z - Points[A].Z;
			float E2 = sqrt(DX * DX + DY * DY + DZ * DZ);
			float MaxEdge = max(E0, max(E1, E2));
			SumMaxEdge += MaxEdge;
			if (MaxEdge > 15.0f)
				LongEdges++;
			Triangles++;
			if (IndexCount > 4096)
				break;
		}
		if (Triangles >= 12)
		{
			float AvgMaxEdge = SumMaxEdge / Triangles;
			if (AvgMaxEdge < 18.0f && LongEdges * 10 < Triangles)
			{
				FSCDAStripCandidate &C = Candidates[Candidates.AddDefaulted()];
				C.Pos = Pos;
				C.IndexCount = IndexCount;
				C.TriangleCount = Triangles;
				C.LongEdges = LongEdges;
				C.AvgMaxEdge = AvgMaxEdge;
				Pos += max(0, IndexCount * 2 - 2);
			}
		}
	}
	if (!Candidates.Num())
		return false;

	OutTriangles.Empty();
	OutPos = Candidates[0].Pos;
	for (int i = 0; i < Candidates.Num(); i++)
		AppendSCDAStripTriangles(Ar, Candidates[i].Pos, Candidates[i].IndexCount, Points, OutTriangles);
	if (OutTriangles.Num() < 60)
		return false;
	if (getenv("SC4_DEBUG_MESH"))
		appPrintf("SCDA coherent strip blocks: blocks=%d triangles=%d first=%08X\n",
			Candidates.Num(), OutTriangles.Num(), OutPos);
	return true;
	unguard;
}

static bool FindSCDARawIndexBlock(FArchive &Ar, int Start, int Stop, const TArray<FVector>& Points, int PointsPos, TArray<VTriangle>& OutTriangles, int& OutPos)
{
	guard(FindSCDARawIndexBlock);
	int PointCount = Points.Num();
	if (PointCount <= 0 || PointCount > 65535)
		return false;

	if (PointCount <= 255)
	{
		int BestPos = 0;
		int BestTriangleCount = 0;
		bool BestStrip = false;
		for (int Mode = 0; Mode < 2; Mode++)
		{
			const bool Strip = Mode != 0;
			for (int Pos = Start; Pos <= Stop - (Strip ? 3 : 4); Pos++)
			{
				int TriangleCount = 0;
				int IndexCount = 0;
				byte Prev2 = 0, Prev1 = 0;
				for (int P = Pos; P < Stop; P++)
				{
					byte I;
					Ar.Seek(P);
					Ar << I;
					if (I >= PointCount)
						break;
					IndexCount++;
					byte A, B, C;
					if (!Strip)
					{
						if ((IndexCount % 3) != 0)
							continue;
						byte B0, B1;
						Ar.Seek(P - 2);
						Ar << B0 << B1;
						A = B0; B = B1; C = I;
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
					if (!IsSaneSCDATriangleArea(Area))
					{
						if (!Strip)
							break;
						continue;
					}
					TriangleCount++;
					if (IndexCount > 20000)
						break;
				}
				if (TriangleCount > BestTriangleCount)
				{
					BestPos = Pos;
					BestTriangleCount = TriangleCount;
					BestStrip = Strip;
				}
				if (!Strip && TriangleCount >= 30)
					Pos += max(0, TriangleCount * 3 - 1);
				else if (Strip && TriangleCount >= 30)
					Pos += max(0, TriangleCount + 2 - 1);
			}
		}
		if (BestTriangleCount >= 30)
		{
			OutTriangles.Empty(BestTriangleCount);
			OutTriangles.AddZeroed(BestTriangleCount);
			int OutIndex = 0;
			byte Prev2 = 0, Prev1 = 0;
			int IndexCount = 0;
			for (int P = BestPos; P < Stop && OutIndex < BestTriangleCount; P++)
			{
				byte I;
				Ar.Seek(P);
				Ar << I;
				if (I >= PointCount)
					break;
				IndexCount++;
				byte A, B, C;
				if (!BestStrip)
				{
					if ((IndexCount % 3) != 0)
						continue;
					byte B0, B1;
					Ar.Seek(P - 2);
					Ar << B0 << B1;
					A = B0; B = B1; C = I;
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
				appPrintf("SCDA raw byte index block: pos=%08X triangles=%d mode=%s\n",
					BestPos, OutTriangles.Num(), BestStrip ? "strip" : "list");
			return true;
		}
	}

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
	int BestRecordRawCount = 0;
	int BestRecordValidCount = 0;
	float BestRecordArea = 0;
	float BestRecordAvgMaxEdge = 3.4e38f;
	int BestRecordLongEdges = 0;
	bool BestRecordBigEndian = false;
	for (int Endian = 0; Endian < 1; Endian++)
	{
		const bool BigEndian = Endian != 0;
		for (int Pos = Start; Pos <= Stop - 8; Pos++)
		{
			int ValidCount = 0;
			float AreaScore = 0;
			float AvgMaxEdge = 3.4e38f;
			int LongEdges = 0;
			const int RawCount = CountSCDARawFaceRecordRun(Ar, Pos, Stop, Points, BigEndian,
				ValidCount, AreaScore, AvgMaxEdge, LongEdges);
			if (AvgMaxEdge > 25.0f || LongEdges * 5 >= ValidCount)
			{
				if (RawCount >= 300)
					Pos += RawCount * 8 - 1;
				continue;
			}
			if (ValidCount > BestRecordValidCount || (ValidCount == BestRecordValidCount && AreaScore > BestRecordArea))
			{
				BestRecordPos = Pos;
				BestRecordRawCount = RawCount;
				BestRecordValidCount = ValidCount;
				BestRecordArea = AreaScore;
				BestRecordAvgMaxEdge = AvgMaxEdge;
				BestRecordLongEdges = LongEdges;
				BestRecordBigEndian = BigEndian;
			}
			if (RawCount >= 300)
				Pos += RawCount * 8 - 1;
		}
	}
	if (BestRecordValidCount >= 300)
	{
		if (!ReadSCDARawFaceRecordsChecked(Ar, BestRecordPos, Stop, BestRecordRawCount, Points, BestRecordBigEndian, OutTriangles))
			return false;
		OutPos = BestRecordPos;
		if (getenv("SC4_DEBUG_MESH"))
			appPrintf("SCDA raw face records: pos=%08X triangles=%d raw=%d endian=%s stride=8 avgMaxEdge=%g long=%d\n",
				BestRecordPos, OutTriangles.Num(), BestRecordRawCount, BestRecordBigEndian ? "BE" : "LE",
				BestRecordAvgMaxEdge, BestRecordLongEdges);
		return true;
	}

	int BestPos = 0;
	int BestTriangleCount = 0;
	float BestArea = 0;
	float BestAvgMaxEdge = 3.4e38f;
	int BestLongEdges = 0;
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
				float SumMaxEdge = 0;
				int LongEdges = 0;
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
					const float E0 = sqrt(ABx * ABx + ABy * ABy + ABz * ABz);
					float BCx = Points[C].X - Points[B].X;
					float BCy = Points[C].Y - Points[B].Y;
					float BCz = Points[C].Z - Points[B].Z;
					const float E1 = sqrt(BCx * BCx + BCy * BCy + BCz * BCz);
					float CAx = Points[A].X - Points[C].X;
					float CAy = Points[A].Y - Points[C].Y;
					float CAz = Points[A].Z - Points[C].Z;
					const float E2 = sqrt(CAx * CAx + CAy * CAy + CAz * CAz);
					const float MaxEdge = max(E0, max(E1, E2));
					float CX = ABy * ACz - ABz * ACy;
					float CY = ABz * ACx - ABx * ACz;
					float CZ = ABx * ACy - ABy * ACx;
					float Area = CX * CX + CY * CY + CZ * CZ;
					if (!IsSaneSCDATriangleArea(Area))
					{
						if (!Strip)
							break;
						continue;
					}
					AreaScore += min(Area, 1000000.0f);
					SumMaxEdge += MaxEdge;
					if (MaxEdge > 35.0f)
						LongEdges++;
					TriangleCount++;
					if (IndexCount > 200000)
						break;
				}
				if (TriangleCount < 100)
					continue;
				const float AvgMaxEdge = SumMaxEdge / TriangleCount;
				if (AvgMaxEdge > 25.0f || LongEdges * 5 >= TriangleCount)
					continue;
				if (TriangleCount > BestTriangleCount || (TriangleCount == BestTriangleCount && AvgMaxEdge < BestAvgMaxEdge) ||
					(TriangleCount == BestTriangleCount && AvgMaxEdge == BestAvgMaxEdge && AreaScore > BestArea))
				{
					BestPos = Pos;
					BestTriangleCount = TriangleCount;
					BestArea = AreaScore;
					BestAvgMaxEdge = AvgMaxEdge;
					BestLongEdges = LongEdges;
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

static bool FindSCDAPackedMeshBlock(FArchive &Ar, int Start, int Stop, TArray<FVector>& OutPoints, TArray<VTriangle>& OutTriangles, int& OutPointsPos, int& OutIndicesPos)
{
	guard(FindSCDAPackedMeshBlock);
	int BestPointsPos = 0;
	int BestIndicesPos = 0;
	int BestStride = 0;
	int BestTriangleCount = 0;
	bool bSawLargeVertexCandidate = false;
	TArray<FVector> BestPoints;
	TArray<VTriangle> BestTriangles;

	for (int Stride = 20; Stride <= 48; Stride += 4)
	{
		for (int Base = Start; Base < Start + Stride && Base <= Stop - 6; Base++)
		{
			int Count = 0;
			int MinX =  0x7FFFFFFF, MinY =  0x7FFFFFFF, MinZ =  0x7FFFFFFF;
			int MaxX = -0x7FFFFFFF, MaxY = -0x7FFFFFFF, MaxZ = -0x7FFFFFFF;
			int LastX = 0, LastY = 0, LastZ = 0;
			for (int Pos = Base; Pos <= Stop - Stride; Pos += Stride)
			{
				int16 X, Y, Z;
				Ar.Seek(Pos);
				Ar << X << Y << Z;
				if (X == -32768 || Y == -32768 || Z == -32768)
					break;
				if (abs((int)X) > 30000 || abs((int)Y) > 30000 || abs((int)Z) > 30000)
					break;
				if (Count > 0 &&
					(abs((int)X - LastX) > 12000 || abs((int)Y - LastY) > 12000 || abs((int)Z - LastZ) > 12000))
					break;
				MinX = min(MinX, (int)X); MaxX = max(MaxX, (int)X);
				MinY = min(MinY, (int)Y); MaxY = max(MaxY, (int)Y);
				MinZ = min(MinZ, (int)Z); MaxZ = max(MaxZ, (int)Z);
				LastX = X; LastY = Y; LastZ = Z;
				Count++;
				if (Count > 100000)
					break;
			}
			int Extent = (MaxX - MinX) + (MaxY - MinY) + (MaxZ - MinZ);
			if (Count < 64 || Extent < 256)
				continue;
			if (Count >= 512)
				bSawLargeVertexCandidate = true;

			TArray<FVector> TestPoints;
			TestPoints.Empty(Count);
			TestPoints.AddUninitialized(Count);
			for (int i = 0; i < Count; i++)
			{
				int16 X, Y, Z;
				Ar.Seek(Base + i * Stride);
				Ar << X << Y << Z;
				TestPoints[i].Set(X / 32.0f, Y / 32.0f, Z / 32.0f);
			}

			TArray<VTriangle> TestTriangles;
			int TestIndicesPos = 0;
			if (!FindSCDARawIndexBlock(Ar, Start, Stop, TestPoints, Base, TestTriangles, TestIndicesPos))
				continue;
			if (TestTriangles.Num() > BestTriangleCount)
			{
				CopyArray(BestPoints, TestPoints);
				CopyArray(BestTriangles, TestTriangles);
				BestPointsPos = Base;
				BestIndicesPos = TestIndicesPos;
				BestStride = Stride;
				BestTriangleCount = TestTriangles.Num();
			}
		}
	}

	if (!BestTriangleCount)
		return false;
	if (bSawLargeVertexCandidate && BestPoints.Num() < 512)
	{
		if (getenv("SC4_DEBUG_MESH"))
			appPrintf("SCDA packed mesh rejected tiny indexed candidate: points=%d triangles=%d; larger vertex candidate present\n",
				BestPoints.Num(), BestTriangles.Num());
		return false;
	}
	CopyArray(OutPoints, BestPoints);
	CopyArray(OutTriangles, BestTriangles);
	OutPointsPos = BestPointsPos;
	OutIndicesPos = BestIndicesPos;
	if (getenv("SC4_DEBUG_MESH"))
		appPrintf("SCDA packed mesh block: points=%d triangles=%d stride=%d pointsPos=%08X indicesPos=%08X first=(%g,%g,%g)\n",
			OutPoints.Num(), OutTriangles.Num(), BestStride, OutPointsPos, OutIndicesPos,
			OutPoints[0].X, OutPoints[0].Y, OutPoints[0].Z);
	return true;
	unguard;
}

static bool FindSCDARawWedgeIndexBlock(FArchive &Ar, int Start, int Stop, const TArray<FVector>& Points, const TArray<FMeshWedge>& Wedges, TArray<VTriangle>& OutTriangles, int& OutPos)
{
	guard(FindSCDARawWedgeIndexBlock);
	const int WedgeCount = Wedges.Num();
	if (WedgeCount <= 0 || WedgeCount > 65535 || Points.Num() <= 0)
		return false;
	Start = max(Start, 0);
	if (Start >= Stop - 6)
		return false;

	int BestPos = 0;
	int BestTriangleCount = 0;
	float BestArea = 0;
	float BestAvgMaxEdge = 3.4e38f;
	int BestLongEdges = 0;
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
				float SumMaxEdge = 0;
				int LongEdges = 0;
				uint16 Prev2 = 0, Prev1 = 0;
				for (int P = Pos; P <= Stop - 2; P += 2)
				{
					const uint16 I = ReadSCDAUInt16At(Ar, P, BigEndian);
					if (I >= WedgeCount)
						break;
					IndexCount++;
					if (!Strip && (IndexCount % 3) != 0)
						continue;
					if (Strip && IndexCount < 3)
					{
						Prev2 = Prev1;
						Prev1 = I;
						continue;
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
					float Area, MaxEdge;
					if (!GetSCDAWedgeTriangleMetrics(Points, Wedges, A, B, C, Area, MaxEdge) || !IsSaneSCDATriangleArea(Area))
					{
						if (!Strip)
							break;
						continue;
					}
					AreaScore += min(Area, 1000000.0f);
					SumMaxEdge += MaxEdge;
					if (MaxEdge > 35.0f)
						LongEdges++;
					TriangleCount++;
					if (IndexCount > 200000)
						break;
				}
				if (TriangleCount < 100)
					continue;
				const float AvgMaxEdge = SumMaxEdge / TriangleCount;
				if (AvgMaxEdge > 25.0f || LongEdges * 5 >= TriangleCount)
					continue;
				if (TriangleCount > BestTriangleCount || (TriangleCount == BestTriangleCount && AvgMaxEdge < BestAvgMaxEdge) ||
					(TriangleCount == BestTriangleCount && AvgMaxEdge == BestAvgMaxEdge && AreaScore > BestArea))
				{
					BestPos = Pos;
					BestTriangleCount = TriangleCount;
					BestArea = AreaScore;
					BestAvgMaxEdge = AvgMaxEdge;
					BestLongEdges = LongEdges;
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
		if (I >= WedgeCount)
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
		float Area;
		if (!GetSCDAWedgeTriangleAreaSq(Points, Wedges, A, B, C, Area) || !IsSaneSCDATriangleArea(Area))
			continue;
		OutTriangles[OutIndex].WedgeIndex[0] = A;
		OutTriangles[OutIndex].WedgeIndex[1] = B;
		OutTriangles[OutIndex].WedgeIndex[2] = C;
		OutIndex++;
	}
	OutTriangles.RemoveAt(OutIndex, OutTriangles.Num() - OutIndex);
	OutPos = BestPos;
	if (getenv("SC4_DEBUG_MESH"))
		appPrintf("SCDA raw wedge index block: pos=%08X triangles=%d wedges=%d endian=%s mode=%s avgMaxEdge=%g long=%d\n",
			BestPos, OutTriangles.Num(), WedgeCount, BestBigEndian ? "BE" : "LE", BestStrip ? "strip" : "list", BestAvgMaxEdge, BestLongEdges);
	return OutTriangles.Num() >= 100;
	unguard;
}

static bool ReadSCDAPointIndexBlock(FArchive &Ar, int Start, int Stop, const TArray<FVector>& Points,
	const TArray<FMeshWedge>& BaseWedges, TArray<FMeshWedge>& OutWedges, TArray<VTriangle>& OutTriangles, int& OutPos)
{
	guard(ReadSCDAPointIndexBlock);
	const int PointCount = Points.Num();
	if (PointCount <= 0 || PointCount > 65535 || Start >= Stop - 6)
		return false;

	int BestPos = 0;
	int BestTriangleCount = 0;
	float BestAvgMaxEdge = 3.4e38f;
	int BestLongEdges = 0;

	for (int Pos = Start; Pos <= Stop - 6; Pos += 2)
	{
		int TriangleCount = 0;
		int IndexCount = 0;
		float SumMaxEdge = 0;
		int LongEdges = 0;
		for (int P = Pos; P <= Stop - 6; P += 6)
		{
			const uint16 A = ReadSCDAUInt16At(Ar, P + 0, false);
			const uint16 B = ReadSCDAUInt16At(Ar, P + 2, false);
			const uint16 C = ReadSCDAUInt16At(Ar, P + 4, false);
			if (A >= PointCount || B >= PointCount || C >= PointCount)
				break;
			IndexCount += 3;
			if (A == B || A == C || B == C)
				continue;
			const float Area = GetSCDATriangleAreaSq(Points, A, B, C);
			if (!IsSaneSCDATriangleArea(Area))
				break;
			const FVector& PA = Points[A];
			const FVector& PB = Points[B];
			const FVector& PC = Points[C];
			float DX = PA.X - PB.X;
			float DY = PA.Y - PB.Y;
			float DZ = PA.Z - PB.Z;
			float MaxEdge = DX * DX + DY * DY + DZ * DZ;
			DX = PB.X - PC.X;
			DY = PB.Y - PC.Y;
			DZ = PB.Z - PC.Z;
			MaxEdge = max(MaxEdge, DX * DX + DY * DY + DZ * DZ);
			DX = PC.X - PA.X;
			DY = PC.Y - PA.Y;
			DZ = PC.Z - PA.Z;
			MaxEdge = max(MaxEdge, DX * DX + DY * DY + DZ * DZ);
			SumMaxEdge += sqrt(MaxEdge);
			if (MaxEdge > 35.0f * 35.0f)
				LongEdges++;
			TriangleCount++;
			if (IndexCount > 200000)
				break;
		}
		if (TriangleCount < 512)
			continue;
		const float AvgMaxEdge = SumMaxEdge / TriangleCount;
		if (LongEdges * 4 >= TriangleCount)
			continue;
		if (TriangleCount > BestTriangleCount || (TriangleCount == BestTriangleCount && AvgMaxEdge < BestAvgMaxEdge))
		{
			BestPos = Pos;
			BestTriangleCount = TriangleCount;
			BestAvgMaxEdge = AvgMaxEdge;
			BestLongEdges = LongEdges;
		}
		Pos += max(0, TriangleCount * 6 - 2);
	}

	if (!BestTriangleCount)
		return false;

	OutWedges.Empty(PointCount);
	OutWedges.AddZeroed(PointCount);
	for (int i = 0; i < PointCount; i++)
	{
		OutWedges[i].iVertex = i;
		if (i < BaseWedges.Num())
			OutWedges[i].TexUV = BaseWedges[i].TexUV;
	}

	OutTriangles.Empty(BestTriangleCount);
	OutTriangles.AddZeroed(BestTriangleCount);
	int OutIndex = 0;
	for (int P = BestPos; P <= Stop - 6 && OutIndex < BestTriangleCount; P += 6)
	{
		const uint16 A = ReadSCDAUInt16At(Ar, P + 0, false);
		const uint16 B = ReadSCDAUInt16At(Ar, P + 2, false);
		const uint16 C = ReadSCDAUInt16At(Ar, P + 4, false);
		if (A >= PointCount || B >= PointCount || C >= PointCount)
			break;
		if (A == B || A == C || B == C)
			continue;
		const float Area = GetSCDATriangleAreaSq(Points, A, B, C);
		if (!IsSaneSCDATriangleArea(Area))
			break;
		OutTriangles[OutIndex].WedgeIndex[0] = A;
		OutTriangles[OutIndex].WedgeIndex[1] = B;
		OutTriangles[OutIndex].WedgeIndex[2] = C;
		OutIndex++;
	}
	OutTriangles.RemoveAt(OutIndex, OutTriangles.Num() - OutIndex);
	OutPos = BestPos;
	if (getenv("SC4_DEBUG_MESH"))
		appPrintf("SCDA point index block: pos=%08X triangles=%d points=%d avgMaxEdge=%g long=%d\n",
			BestPos, OutTriangles.Num(), PointCount, BestAvgMaxEdge, BestLongEdges);
	return OutTriangles.Num() >= 512;
	unguard;
}

static bool ReadSCDAFaceRecord8Block(FArchive &Ar, int Pos, int Stop, const TArray<FVector>& Points, TArray<VTriangle>& OutTriangles, int& OutPos, int MaterialCount = 0)
{
	guard(ReadSCDAFaceRecord8Block);
	const int PointCount = Points.Num();
	if (PointCount <= 0 || Pos < 0 || Stop <= Pos || ((Stop - Pos) & 7))
		return false;
	const int RawCount = (Stop - Pos) / 8;
	if (RawCount < 512 || RawCount > 65535)
		return false;
	int Count = RawCount;
	for (int i = 0; i < RawCount; i++)
	{
		const int P = Pos + i * 8;
		const int Control = ReadSCDAUInt16At(Ar, P + 0, false);
		const int A = ReadSCDAUInt16At(Ar, P + 2, false);
		const int B = ReadSCDAUInt16At(Ar, P + 4, false);
		const int C = ReadSCDAUInt16At(Ar, P + 6, false);
		const bool bKnownMaterial = MaterialCount > 0 && Control >= 0 && Control < MaterialCount;
		if (i >= 32 && ((Control != 1 && !bKnownMaterial) || A >= PointCount || B >= PointCount || C >= PointCount))
		{
			Count = i;
			break;
		}
	}
	if (Count < 512)
		return false;

	int Valid = 0;
	int Degenerate = 0;
	int BadArea = 0;
	int OverPointCount = 0;
	int LongEdges = 0;
	float SumMaxEdge = 0;
	for (int i = 0; i < Count; i++)
	{
		const int P = Pos + i * 8;
		const int Control = ReadSCDAUInt16At(Ar, P + 0, false);
		const int A = ReadSCDAUInt16At(Ar, P + 2, false);
		const int B = ReadSCDAUInt16At(Ar, P + 4, false);
		const int C = ReadSCDAUInt16At(Ar, P + 6, false);
		if (A >= PointCount || B >= PointCount || C >= PointCount)
		{
			OverPointCount++;
			continue;
		}
		if (A == B || A == C || B == C)
		{
			Degenerate++;
			continue;
		}
		// Avoid wedge remapping here; these records index the decoded point stream directly.
		const FVector& PA = Points[A];
		const FVector& PB = Points[B];
		const FVector& PC = Points[C];
		const float Area = GetSCDATriangleAreaSq(Points, A, B, C);
		if (!IsSaneSCDATriangleArea(Area))
		{
			BadArea++;
			continue;
		}
		float DX = PA.X - PB.X;
		float DY = PA.Y - PB.Y;
		float DZ = PA.Z - PB.Z;
		float MaxEdge = DX * DX + DY * DY + DZ * DZ;
		DX = PB.X - PC.X;
		DY = PB.Y - PC.Y;
		DZ = PB.Z - PC.Z;
		MaxEdge = max(MaxEdge, DX * DX + DY * DY + DZ * DZ);
		DX = PC.X - PA.X;
		DY = PC.Y - PA.Y;
		DZ = PC.Z - PA.Z;
		MaxEdge = max(MaxEdge, DX * DX + DY * DY + DZ * DZ);
		SumMaxEdge += sqrt(MaxEdge);
		if (MaxEdge > 35.0f * 35.0f)
			LongEdges++;
		Valid++;
	}
	if (Valid < Count * 3 / 4 || OverPointCount > Count / 32)
		return false;

	OutTriangles.Empty(Valid);
	OutTriangles.AddZeroed(Valid);
	int OutIndex = 0;
	for (int i = 0; i < Count; i++)
	{
		const int P = Pos + i * 8;
		const int Control = ReadSCDAUInt16At(Ar, P + 0, false);
		const int A = ReadSCDAUInt16At(Ar, P + 2, false);
		const int B = ReadSCDAUInt16At(Ar, P + 4, false);
		const int C = ReadSCDAUInt16At(Ar, P + 6, false);
		if (A >= PointCount || B >= PointCount || C >= PointCount || A == B || A == C || B == C)
			continue;
		const float Area = GetSCDATriangleAreaSq(Points, A, B, C);
		if (!IsSaneSCDATriangleArea(Area))
			continue;
		VTriangle& T = OutTriangles[OutIndex++];
		T.WedgeIndex[0] = A;
		T.WedgeIndex[1] = B;
		T.WedgeIndex[2] = C;
		T.MatIndex = (MaterialCount > 0 && Control >= 0 && Control < MaterialCount) ? Control : 0;
		T.AuxMatIndex = 0;
		T.SmoothingGroups = 0;
	}
	OutTriangles.RemoveAt(OutIndex, OutTriangles.Num() - OutIndex);
	OutPos = Pos;
	if (getenv("SC4_DEBUG_MESH"))
		appPrintf("SCDA face8 block: pos=%08X-%08X records=%d/%d triangles=%d points=%d avgMaxEdge=%g long=%d deg=%d badArea=%d over=%d\n",
			Pos, Pos + Count * 8, Count, RawCount, OutTriangles.Num(), PointCount, SumMaxEdge / max(1, Valid), LongEdges, Degenerate, BadArea, OverPointCount);
	return OutTriangles.Num() >= Count * 3 / 4;
	unguard;
}

static float GetSCDATriangleMaxEdge(const TArray<FVector>& Points, int A, int B, int C)
{
	const FVector& PA = Points[A];
	const FVector& PB = Points[B];
	const FVector& PC = Points[C];
	float DX = PA.X - PB.X;
	float DY = PA.Y - PB.Y;
	float DZ = PA.Z - PB.Z;
	float MaxEdge = DX * DX + DY * DY + DZ * DZ;
	DX = PB.X - PC.X;
	DY = PB.Y - PC.Y;
	DZ = PB.Z - PC.Z;
	MaxEdge = max(MaxEdge, DX * DX + DY * DY + DZ * DZ);
	DX = PC.X - PA.X;
	DY = PC.Y - PA.Y;
	DZ = PC.Z - PA.Z;
	MaxEdge = max(MaxEdge, DX * DX + DY * DY + DZ * DZ);
	return sqrt(MaxEdge);
}

static bool ScoreSCDAABCSectionOffset(FArchive &Ar, int Pos, int Count, const TArray<FVector>& Points,
	int Material, int Offset, int SampleLimit, float& OutAvgMaxEdge, int& OutLongEdges, int& OutValid)
{
	guard(ScoreSCDAABCSectionOffset);
	const int PointCount = Points.Num();
	float SumMaxEdge = 0;
	OutLongEdges = 0;
	OutValid = 0;
	int Seen = 0;
	for (int i = 0; i < Count && Seen < SampleLimit; i++)
	{
		const int P = Pos + i * 8;
		const int Mat = ReadSCDAUInt16At(Ar, P + 6, false);
		if (Mat != Material)
			continue;
		Seen++;
		const int A = ReadSCDAUInt16At(Ar, P + 0, false) + Offset;
		const int B = ReadSCDAUInt16At(Ar, P + 2, false) + Offset;
		const int C = ReadSCDAUInt16At(Ar, P + 4, false) + Offset;
		if (A < 0 || B < 0 || C < 0 || A >= PointCount || B >= PointCount || C >= PointCount ||
			A == B || A == C || B == C)
			continue;
		const float Area = GetSCDATriangleAreaSq(Points, A, B, C);
		if (!IsSaneSCDATriangleArea(Area))
			continue;
		const float MaxEdge = GetSCDATriangleMaxEdge(Points, A, B, C);
		SumMaxEdge += MaxEdge;
		if (MaxEdge > 35.0f)
			OutLongEdges++;
		OutValid++;
	}
	if (!OutValid)
	{
		OutAvgMaxEdge = 3.4e38f;
		return false;
	}
	OutAvgMaxEdge = SumMaxEdge / OutValid;
	return true;
	unguard;
}

static void FindSCDAABCSectionBaseOffsets(FArchive &Ar, int Pos, int Count, const TArray<FVector>& Points,
	int MaterialCount, int Offsets[64])
{
	guard(FindSCDAABCSectionBaseOffsets);
	for (int i = 0; i < 64; i++)
		Offsets[i] = 0;
	const int MatLimit = min((MaterialCount > 0) ? MaterialCount : 16, 64);
	const int PointCount = Points.Num();
	for (int Mat = 0; Mat < MatLimit; Mat++)
	{
		int FaceCount = 0;
		int MaxIndex = 0;
		for (int i = 0; i < Count; i++)
		{
			const int P = Pos + i * 8;
			if ((int)ReadSCDAUInt16At(Ar, P + 6, false) != Mat)
				continue;
			FaceCount++;
			MaxIndex = max(MaxIndex, (int)ReadSCDAUInt16At(Ar, P + 0, false));
			MaxIndex = max(MaxIndex, (int)ReadSCDAUInt16At(Ar, P + 2, false));
			MaxIndex = max(MaxIndex, (int)ReadSCDAUInt16At(Ar, P + 4, false));
		}
		if (FaceCount < 16)
			continue;

		float DirectAvg = 0;
		int DirectLong = 0, DirectValid = 0;
		if (!ScoreSCDAABCSectionOffset(Ar, Pos, Count, Points, Mat, 0, 2000, DirectAvg, DirectLong, DirectValid))
			continue;

		int BestOffset = 0;
		float BestAvg = DirectAvg;
		int BestLong = DirectLong, BestValid = DirectValid;
		float BestScore = DirectAvg + DirectLong * 0.1f;
		const int MaxOffset = min(256, PointCount - 1 - MaxIndex);
		for (int Offset = 1; Offset <= MaxOffset; Offset++)
		{
			float Avg = 0;
			int LongEdges = 0, Valid = 0;
			if (!ScoreSCDAABCSectionOffset(Ar, Pos, Count, Points, Mat, Offset, 2000, Avg, LongEdges, Valid))
				continue;
			if (Valid < DirectValid * 3 / 4)
				continue;
			const float Score = Avg + LongEdges * 0.1f;
			if (Score < BestScore)
			{
				BestScore = Score;
				BestOffset = Offset;
				BestAvg = Avg;
				BestLong = LongEdges;
				BestValid = Valid;
			}
		}
		const bool bLargeBadSection = (FaceCount >= 128) &&
			(DirectLong > DirectValid / 8 || DirectAvg > 20.0f) &&
			BestLong < DirectLong / 4 && BestAvg * 2.0f < DirectAvg;
		const bool bClearSectionBase = (FaceCount >= 16) &&
			BestLong <= DirectLong && BestAvg < 5.0f && BestAvg * 4.0f < DirectAvg;
		const bool bSmallClearSectionBase = (FaceCount < 128) &&
			BestLong <= DirectLong && BestAvg < 2.0f && BestAvg * 3.0f < DirectAvg;
		if (BestOffset > 0 && (bLargeBadSection || bClearSectionBase || bSmallClearSectionBase))
		{
			Offsets[Mat] = BestOffset;
			if (getenv("SC4_DEBUG_MESH"))
				appPrintf("SCDA ABC section base: mat=%d add=%d directAvg=%g directLong=%d bestAvg=%g bestLong=%d faces=%d\n",
					Mat, BestOffset, DirectAvg, DirectLong, BestAvg, BestLong, FaceCount);
		}
	}
	int CommonOffset = 0;
	int CommonOffsetUses = 0;
	for (int Mat = 0; Mat < MatLimit; Mat++)
	{
		if (Offsets[Mat] <= 0)
			continue;
		int Uses = 0;
		for (int OtherMat = 0; OtherMat < MatLimit; OtherMat++)
			if (Offsets[OtherMat] == Offsets[Mat])
				Uses++;
		if (Uses > CommonOffsetUses)
		{
			CommonOffset = Offsets[Mat];
			CommonOffsetUses = Uses;
		}
	}
	if (CommonOffset > 0)
	{
		for (int Mat = 0; Mat < MatLimit; Mat++)
		{
			if (Offsets[Mat] != 0)
				continue;
			int FaceCount = 0;
			int MinIndex = PointCount;
			int MaxIndex = 0;
			for (int i = 0; i < Count; i++)
			{
				const int P = Pos + i * 8;
				if ((int)ReadSCDAUInt16At(Ar, P + 6, false) != Mat)
					continue;
				FaceCount++;
				const int A = ReadSCDAUInt16At(Ar, P + 0, false);
				const int B = ReadSCDAUInt16At(Ar, P + 2, false);
				const int C = ReadSCDAUInt16At(Ar, P + 4, false);
				MinIndex = min(MinIndex, min(A, min(B, C)));
				MaxIndex = max(MaxIndex, max(A, max(B, C)));
			}
			if (FaceCount <= 0 || FaceCount >= 16 || MinIndex >= CommonOffset || MaxIndex + CommonOffset >= PointCount)
				continue;

			float DirectAvg = 0, CommonAvg = 0;
			int DirectLong = 0, CommonLong = 0, DirectValid = 0, CommonValid = 0;
			if (!ScoreSCDAABCSectionOffset(Ar, Pos, Count, Points, Mat, 0, FaceCount, DirectAvg, DirectLong, DirectValid) ||
				!ScoreSCDAABCSectionOffset(Ar, Pos, Count, Points, Mat, CommonOffset, FaceCount, CommonAvg, CommonLong, CommonValid))
				continue;
			if (CommonValid < FaceCount || CommonAvg > 10.0f)
				continue;
			if (DirectAvg > 20.0f || CommonAvg * 3.0f < DirectAvg || CommonLong < DirectLong)
			{
				Offsets[Mat] = CommonOffset;
				if (getenv("SC4_DEBUG_MESH"))
					appPrintf("SCDA ABC tiny section inherited base: mat=%d add=%d directAvg=%g directLong=%d commonAvg=%g commonLong=%d faces=%d\n",
						Mat, CommonOffset, DirectAvg, DirectLong, CommonAvg, CommonLong, FaceCount);
			}
		}
	}
	unguard;
}

static bool ScoreSCDAABCBlockCandidate(FArchive &Ar, int Pos, int Count, const TArray<FVector>& Points,
	int MaterialCount, float& OutScore, float& OutAvgMaxEdge, int& OutLongEdges)
{
	guard(ScoreSCDAABCBlockCandidate);
	int Offsets[64];
	FindSCDAABCSectionBaseOffsets(Ar, Pos, min(Count, 3000), Points, MaterialCount, Offsets);
	const int PointCount = Points.Num();
	float SumMaxEdge = 0;
	int Valid = 0;
	OutLongEdges = 0;
	const int ProbeCount = min(Count, 64);
	for (int i = 0; i < ProbeCount; i++)
	{
		const int P = Pos + i * 8;
		const int Mat = ReadSCDAUInt16At(Ar, P + 6, false);
		const int Offset = (Mat >= 0 && Mat < 64) ? Offsets[Mat] : 0;
		const int A = ReadSCDAUInt16At(Ar, P + 0, false) + Offset;
		const int B = ReadSCDAUInt16At(Ar, P + 2, false) + Offset;
		const int C = ReadSCDAUInt16At(Ar, P + 4, false) + Offset;
		if (A < 0 || B < 0 || C < 0 || A >= PointCount || B >= PointCount || C >= PointCount ||
			A == B || A == C || B == C)
			continue;
		const float Area = GetSCDATriangleAreaSq(Points, A, B, C);
		if (!IsSaneSCDATriangleArea(Area))
			continue;
		const float MaxEdge = GetSCDATriangleMaxEdge(Points, A, B, C);
		SumMaxEdge += MaxEdge;
		if (MaxEdge > 35.0f)
			OutLongEdges++;
		Valid++;
	}
	if (Valid < ProbeCount * 3 / 4)
		return false;
	OutAvgMaxEdge = SumMaxEdge / Valid;
	OutScore = OutAvgMaxEdge + OutLongEdges * 0.1f;
	return true;
	unguard;
}

static bool ScoreSCDAABCBlockDirectCandidate(FArchive &Ar, int Pos, int Count, const TArray<FVector>& Points,
	float& OutScore, float& OutAvgMaxEdge, int& OutLongEdges)
{
	guard(ScoreSCDAABCBlockDirectCandidate);
	const int PointCount = Points.Num();
	float SumMaxEdge = 0;
	int Valid = 0;
	OutLongEdges = 0;
	const int ProbeCount = min(Count, 64);
	for (int i = 0; i < ProbeCount; i++)
	{
		const int P = Pos + i * 8;
		const int A = ReadSCDAUInt16At(Ar, P + 0, false);
		const int B = ReadSCDAUInt16At(Ar, P + 2, false);
		const int C = ReadSCDAUInt16At(Ar, P + 4, false);
		if (A >= PointCount || B >= PointCount || C >= PointCount || A == B || A == C || B == C)
			continue;
		const float Area = GetSCDATriangleAreaSq(Points, A, B, C);
		if (!IsSaneSCDATriangleArea(Area))
			continue;
		const float MaxEdge = GetSCDATriangleMaxEdge(Points, A, B, C);
		SumMaxEdge += MaxEdge;
		if (MaxEdge > 35.0f)
			OutLongEdges++;
		Valid++;
	}
	if (Valid < ProbeCount * 3 / 4)
		return false;
	OutAvgMaxEdge = SumMaxEdge / Valid;
	OutScore = OutAvgMaxEdge + OutLongEdges * 0.1f;
	return true;
	unguard;
}

static bool IsSCDAABCDirectWindowCoherent(FArchive &Ar, int Pos, int Stop, const TArray<FVector>& Points,
	int MaterialCount, int ProbeCount = 64)
{
	guard(IsSCDAABCDirectWindowCoherent);
	if (Pos < 0 || Pos + ProbeCount * 8 > Stop || Points.Num() <= 0)
		return false;
	const int PointCount = Points.Num();
	const int MatLimit = (MaterialCount > 0) ? MaterialCount : 16;
	for (int i = 0; i < ProbeCount; i++)
	{
		const int P = Pos + i * 8;
		const int A = ReadSCDAUInt16At(Ar, P + 0, false);
		const int B = ReadSCDAUInt16At(Ar, P + 2, false);
		const int C = ReadSCDAUInt16At(Ar, P + 4, false);
		const int Mat = ReadSCDAUInt16At(Ar, P + 6, false);
		if (A >= PointCount || B >= PointCount || C >= PointCount || Mat >= MatLimit || A == B || A == C || B == C)
			return false;
	}
	float Score = 0;
	float AvgMaxEdge = 0;
	int LongEdges = 0;
	if (!ScoreSCDAABCBlockDirectCandidate(Ar, Pos, ProbeCount, Points, Score, AvgMaxEdge, LongEdges))
		return false;
	return Score < 20.0f;
	unguard;
}

static int BacktrackSCDAABCBlockStart(FArchive &Ar, int Start, int Stop, int Pos, const TArray<FVector>& Points,
	int MaterialCount)
{
	guard(BacktrackSCDAABCBlockStart);
	int Best = Pos;
	for (int Test = Pos - 8; Test >= Start; Test -= 8)
	{
		if (!IsSCDAABCDirectWindowCoherent(Ar, Test, Stop, Points, MaterialCount))
			break;
		Best = Test;
	}
	return Best;
	unguard;
}

static bool ReadSCDAFaceRecord8ABCBlock(FArchive &Ar, int Pos, int Stop, int MaxRecords,
	const TArray<FVector>& Points, TArray<VTriangle>& OutTriangles, int& OutPos, int MaterialCount = 0)
{
	guard(ReadSCDAFaceRecord8ABCBlock);
	const int PointCount = Points.Num();
	if (PointCount <= 0 || Pos < 0 || Pos + 8 > Stop || MaxRecords < 512)
		return false;

	const int MatLimit = (MaterialCount > 0) ? MaterialCount : 16;
	int Count = 0;
	const int MaxPossibleRecords = min(65535, (Stop - Pos) / 8);
	for (int i = 0; i < MaxPossibleRecords; i++)
	{
		const int P = Pos + i * 8;
		const int A = ReadSCDAUInt16At(Ar, P + 0, false);
		const int B = ReadSCDAUInt16At(Ar, P + 2, false);
		const int C = ReadSCDAUInt16At(Ar, P + 4, false);
		const int Mat = ReadSCDAUInt16At(Ar, P + 6, false);
		if (A >= PointCount || B >= PointCount || C >= PointCount || Mat >= MatLimit)
			break;
		Count++;
	}
	if (Count < 512 || Count < MaxRecords / 4)
		return false;
	int Valid = 0;
	int Degenerate = 0;
	int BadArea = 0;
	int OverPointCount = 0;
	int LongEdges = 0;
	float SumMaxEdge = 0;
	int SectionOffsets[64];
	FindSCDAABCSectionBaseOffsets(Ar, Pos, Count, Points, MaterialCount, SectionOffsets);
	for (int i = 0; i < Count; i++)
	{
		const int P = Pos + i * 8;
		const int Mat = ReadSCDAUInt16At(Ar, P + 6, false);
		const int Offset = (Mat >= 0 && Mat < 64) ? SectionOffsets[Mat] : 0;
		const int A = ReadSCDAUInt16At(Ar, P + 0, false) + Offset;
		const int B = ReadSCDAUInt16At(Ar, P + 2, false) + Offset;
		const int C = ReadSCDAUInt16At(Ar, P + 4, false) + Offset;
		if (A >= PointCount || B >= PointCount || C >= PointCount)
		{
			OverPointCount++;
			continue;
		}
		if (A == B || A == C || B == C)
		{
			Degenerate++;
			continue;
		}
		const FVector& PA = Points[A];
		const FVector& PB = Points[B];
		const FVector& PC = Points[C];
		const float Area = GetSCDATriangleAreaSq(Points, A, B, C);
		if (!IsSaneSCDATriangleArea(Area))
		{
			BadArea++;
			continue;
		}
		const float MaxEdge = GetSCDATriangleMaxEdge(Points, A, B, C);
		SumMaxEdge += MaxEdge;
		if (MaxEdge > 35.0f)
			LongEdges++;
		Valid++;
	}
	if (Valid < Count / 2 || OverPointCount > Count / 64)
		return false;

	OutTriangles.Empty(Valid);
	OutTriangles.AddZeroed(Valid);
	int OutIndex = 0;
	for (int i = 0; i < Count; i++)
	{
		const int P = Pos + i * 8;
		const int Mat = ReadSCDAUInt16At(Ar, P + 6, false);
		const int Offset = (Mat >= 0 && Mat < 64) ? SectionOffsets[Mat] : 0;
		const int A = ReadSCDAUInt16At(Ar, P + 0, false) + Offset;
		const int B = ReadSCDAUInt16At(Ar, P + 2, false) + Offset;
		const int C = ReadSCDAUInt16At(Ar, P + 4, false) + Offset;
		if (A >= PointCount || B >= PointCount || C >= PointCount || A == B || A == C || B == C)
			continue;
		const float Area = GetSCDATriangleAreaSq(Points, A, B, C);
		if (!IsSaneSCDATriangleArea(Area))
			continue;
		VTriangle& T = OutTriangles[OutIndex++];
		T.WedgeIndex[0] = A;
		T.WedgeIndex[1] = B;
		T.WedgeIndex[2] = C;
		T.MatIndex = (MaterialCount > 0 && Mat >= 0 && Mat < MaterialCount) ? Mat : 0;
		T.AuxMatIndex = 0;
		T.SmoothingGroups = 0;
	}
	OutTriangles.RemoveAt(OutIndex, OutTriangles.Num() - OutIndex);
	OutPos = Pos;
	if (getenv("SC4_DEBUG_MESH"))
		appPrintf("SCDA face8 ABC block: pos=%08X-%08X records=%d triangles=%d points=%d materials=%d avgMaxEdge=%g long=%d deg=%d badArea=%d over=%d\n",
			Pos, Pos + Count * 8, Count, OutTriangles.Num(), PointCount, MaterialCount,
			SumMaxEdge / max(1, Valid), LongEdges, Degenerate, BadArea, OverPointCount);
	return OutTriangles.Num() >= Count / 2;
	unguard;
}

static bool FindSCDAFaceRecord8ABCBlock(FArchive &Ar, int Start, int Stop, int MaxRecords,
	const TArray<FVector>& Points, TArray<VTriangle>& OutTriangles, int& OutPos, int MaterialCount = 0)
{
	guard(FindSCDAFaceRecord8ABCBlock);
	if (MaxRecords < 512 || Points.Num() <= 0 || Start < 0 || Stop <= Start)
		return false;
	const int Count = MaxRecords;
	const int PointCount = Points.Num();
	const int MatLimit = (MaterialCount > 0) ? MaterialCount : 16;
	int BestPos = 0;
	int BestValid = 0;
	int BestMatBad = 0x7FFFFFFF;
	int BestOver = 0x7FFFFFFF;
	float BestScore = 3.4e38f;
	float BestAvgMaxEdge = 3.4e38f;
	int BestLongEdges = 0x7FFFFFFF;
	for (int Phase = 0; Phase < 8; Phase++)
	{
		bool bTriedSectionBaseForRun = false;
		for (int Pos = Start + Phase; Pos + Count * 8 <= Stop; Pos += 8)
		{
			int Valid = 0;
			int Over = 0;
			int MatBad = 0;
			const int ProbeCount = min(Count, 8);
			for (int i = 0; i < ProbeCount; i++)
			{
				const int P = Pos + i * 8;
				const int A = ReadSCDAUInt16At(Ar, P + 0, false);
				const int B = ReadSCDAUInt16At(Ar, P + 2, false);
				const int C = ReadSCDAUInt16At(Ar, P + 4, false);
				const int Mat = ReadSCDAUInt16At(Ar, P + 6, false);
				if (A >= PointCount || B >= PointCount || C >= PointCount)
				{
					Over++;
					continue;
				}
				if (Mat >= MatLimit)
					MatBad++;
				if (A != B && A != C && B != C)
					Valid++;
			}
			if (Valid < ProbeCount * 3 / 4 || Over > 0 || MatBad > 0)
				continue;

			float Score = 3.4e38f;
			float AvgMaxEdge = 3.4e38f;
			int LongEdges = 0;
			if (!ScoreSCDAABCBlockDirectCandidate(Ar, Pos, Count, Points, Score, AvgMaxEdge, LongEdges))
				continue;
			if (Score >= 20.0f && !bTriedSectionBaseForRun)
			{
				float SectionScore = 3.4e38f;
				float SectionAvgMaxEdge = 3.4e38f;
				int SectionLongEdges = 0;
				bTriedSectionBaseForRun = true;
				if (ScoreSCDAABCBlockCandidate(Ar, Pos, Count, Points, MaterialCount, SectionScore, SectionAvgMaxEdge, SectionLongEdges) &&
					SectionScore < Score)
				{
					Score = SectionScore;
					AvgMaxEdge = SectionAvgMaxEdge;
					LongEdges = SectionLongEdges;
				}
			}
			if (Score < 20.0f)
			{
				if (!BestPos || Pos < BestPos)
				{
					BestPos = Pos;
					BestValid = Valid;
					BestMatBad = MatBad;
					BestOver = Over;
					BestScore = Score;
					BestAvgMaxEdge = AvgMaxEdge;
					BestLongEdges = LongEdges;
				}
				break;
			}
			if (!BestPos || (BestScore >= 20.0f && Score < BestScore))
			{
				BestPos = Pos;
				BestValid = Valid;
				BestMatBad = MatBad;
				BestOver = Over;
				BestScore = Score;
				BestAvgMaxEdge = AvgMaxEdge;
				BestLongEdges = LongEdges;
			}
			Pos += 0x78;
		}
	}
	if (!BestPos)
		return false;
	if (getenv("SC4_DEBUG_MESH"))
		appPrintf("SCDA face8 ABC candidate: pos=%08X records=%d validProbe=%d matBad=%d over=%d avgMaxEdge=%g long=%d score=%g\n",
			BestPos, Count, BestValid, BestMatBad, BestOver, BestAvgMaxEdge, BestLongEdges, BestScore);
	BestPos = BacktrackSCDAABCBlockStart(Ar, Start, Stop, BestPos, Points, MaterialCount);
	return ReadSCDAFaceRecord8ABCBlock(Ar, BestPos, Stop, Count, Points, OutTriangles, OutPos, MaterialCount);
	unguard;
}

static bool FindSCDALongFaceRecord8ABCBlock(FArchive &Ar, int Start, int Stop, int MinRecords,
	const TArray<FVector>& Points, TArray<VTriangle>& OutTriangles, int& OutPos, int MaterialCount = 0)
{
	guard(FindSCDALongFaceRecord8ABCBlock);
	if (MinRecords < 512 || Points.Num() <= 0 || Start < 0 || Stop <= Start)
		return false;
	const int PointCount = Points.Num();
	const int MatLimit = (MaterialCount > 0) ? MaterialCount : 16;
	int BestPos = 0;
	int BestCount = 0;
	for (int Phase = 0; Phase < 8; Phase++)
	{
		for (int Pos = Start + Phase; Pos <= Stop - 8; Pos += 8)
		{
			int Count = 0;
			while (Pos + Count * 8 + 8 <= Stop)
			{
				const int P = Pos + Count * 8;
				const int A = ReadSCDAUInt16At(Ar, P + 0, false);
				const int B = ReadSCDAUInt16At(Ar, P + 2, false);
				const int C = ReadSCDAUInt16At(Ar, P + 4, false);
				const int Mat = ReadSCDAUInt16At(Ar, P + 6, false);
				if (A >= PointCount || B >= PointCount || C >= PointCount || Mat >= MatLimit ||
					A == B || A == C || B == C)
					break;
				Count++;
			}
			if (Count > BestCount)
			{
				BestPos = Pos;
				BestCount = Count;
			}
			if (Count >= 512)
				Pos += Count * 8 - 8;
		}
	}
	if (BestCount < MinRecords)
		return false;
	if (getenv("SC4_DEBUG_MESH"))
		appPrintf("SCDA long face8 ABC candidate: pos=%08X records=%d min=%d\n",
			BestPos, BestCount, MinRecords);
	return ReadSCDAFaceRecord8ABCBlock(Ar, BestPos, Stop, MinRecords, Points, OutTriangles, OutPos, MaterialCount);
	unguard;
}

static bool ReadSCDAPointStrip16Block(FArchive &Ar, int Pos, int Stop, const TArray<FVector>& Points, TArray<VTriangle>& OutTriangles, int& OutPos)
{
	guard(ReadSCDAPointStrip16Block);
	const int PointCount = Points.Num();
	if (PointCount <= 0 || Pos < 0 || Stop <= Pos)
		return false;

	int WordCount = 0;
	for (int P = Pos; P + 2 <= Stop; P += 2)
	{
		const int Value = ReadSCDAUInt16At(Ar, P, false);
		if (Value >= PointCount && Value != 0xFFFF)
			break;
		WordCount++;
		if (WordCount > 40000)
			break;
	}
	if (WordCount < 512)
		return false;

	TArray<VTriangle> TestTriangles;
	TestTriangles.Empty(max(0, WordCount - 2));
	int LongEdges = 0;
	int Degenerate = 0;
	int Restart = 0;
	float SumMaxEdge = 0;
	for (int i = 2; i < WordCount; i++)
	{
		const int I0 = ReadSCDAUInt16At(Ar, Pos + (i - 2) * 2, false);
		const int I1 = ReadSCDAUInt16At(Ar, Pos + (i - 1) * 2, false);
		const int I2 = ReadSCDAUInt16At(Ar, Pos + i * 2, false);
		if (I0 == 0xFFFF || I1 == 0xFFFF || I2 == 0xFFFF)
		{
			Restart++;
			continue;
		}
		int A, B, C;
		if (i & 1)
		{
			A = I0; B = I1; C = I2;
		}
		else
		{
			A = I1; B = I0; C = I2;
		}
		if (A >= PointCount || B >= PointCount || C >= PointCount)
			continue;
		if (A == B || A == C || B == C)
		{
			Degenerate++;
			continue;
		}
		const float Area = GetSCDATriangleAreaSq(Points, A, B, C);
		if (!IsSaneSCDATriangleArea(Area))
			continue;
		const FVector& PA = Points[A];
		const FVector& PB = Points[B];
		const FVector& PC = Points[C];
		float DX = PA.X - PB.X;
		float DY = PA.Y - PB.Y;
		float DZ = PA.Z - PB.Z;
		float MaxEdge = DX * DX + DY * DY + DZ * DZ;
		DX = PB.X - PC.X;
		DY = PB.Y - PC.Y;
		DZ = PB.Z - PC.Z;
		MaxEdge = max(MaxEdge, DX * DX + DY * DY + DZ * DZ);
		DX = PC.X - PA.X;
		DY = PC.Y - PA.Y;
		DZ = PC.Z - PA.Z;
		MaxEdge = max(MaxEdge, DX * DX + DY * DY + DZ * DZ);
		SumMaxEdge += sqrt(MaxEdge);
		if (MaxEdge > 35.0f * 35.0f)
			LongEdges++;
		VTriangle& T = TestTriangles[TestTriangles.AddZeroed(1)];
		T.WedgeIndex[0] = A;
		T.WedgeIndex[1] = B;
		T.WedgeIndex[2] = C;
	}
	if (TestTriangles.Num() < 1000)
		return false;
	CopyArray(OutTriangles, TestTriangles);
	OutPos = Pos;
	if (getenv("SC4_DEBUG_MESH"))
		appPrintf("SCDA point strip16 block: pos=%08X words=%d triangles=%d points=%d avgMaxEdge=%g long=%d deg=%d restart=%d\n",
			Pos, WordCount, OutTriangles.Num(), PointCount, SumMaxEdge / max(1, OutTriangles.Num()), LongEdges, Degenerate, Restart);
	return true;
	unguard;
}

static void DumpSCDADenseIndexStreams(FArchive &Ar, int Start, int Stop, int PointCount)
{
	guard(DumpSCDADenseIndexStreams);
	if (!getenv("SC4_DEBUG_MESH") || PointCount <= 0)
		return;

	struct FDenseIndexRun
	{
		int Pos;
		int Count;
		int MaxIndex;
		int OverPointCount;
		int Over4096;
	};
	TArray<FDenseIndexRun> Runs;
	const int MaxAllowed = 5000;
	for (int Pos = Start; Pos <= Stop - 16; Pos += 2)
	{
		int Count = 0;
		int MaxIndex = 0;
		int OverPointCount = 0;
		int Over4096 = 0;
		for (int P = Pos; P <= Stop - 2; P += 2)
		{
			const int Value = ReadSCDAUInt16At(Ar, P, false);
			if (Value > MaxAllowed && Value != 0xFFFF)
				break;
			if (Value != 0xFFFF)
			{
				MaxIndex = max(MaxIndex, Value);
				if (Value >= PointCount)
					OverPointCount++;
				if (Value >= 4096)
					Over4096++;
			}
			Count++;
			if (Count > 20000)
				break;
		}
		if (Count >= 512 && MaxIndex >= 512)
		{
			FDenseIndexRun& Run = Runs[Runs.AddDefaulted()];
			Run.Pos = Pos;
			Run.Count = Count;
			Run.MaxIndex = MaxIndex;
			Run.OverPointCount = OverPointCount;
			Run.Over4096 = Over4096;
			Pos += max(0, Count * 2 - 2);
		}
	}
	if (!Runs.Num())
		return;
	appPrintf("SCDA dense index-like streams for pointCount=%d:\n", PointCount);
	for (int i = 0; i < Runs.Num() && i < 12; i++)
	{
		const FDenseIndexRun& Run = Runs[i];
		appPrintf("  stream pos=%08X count=%d end=%08X max=%d overPoints=%d over4096=%d\n",
			Run.Pos, Run.Count, Run.Pos + Run.Count * 2, Run.MaxIndex, Run.OverPointCount, Run.Over4096);
	}
	unguard;
}

static bool HasSCDAPlainRemap16(FArchive &Ar, int Pos, int Count, int PointCount)
{
	guard(HasSCDAPlainRemap16);
	int Bad = 0;
	int MaxValue = 0;
	for (int i = 0; i < Count; i++)
	{
		const int Value = ReadSCDAUInt16At(Ar, Pos + i * 2, false);
		MaxValue = max(MaxValue, Value);
		if (Value >= PointCount)
		{
			if (++Bad > 32)
				return false;
		}
	}
	return MaxValue >= min(PointCount - 1, 512);
	unguard;
}

static bool HasSCDAPlainRemap32(FArchive &Ar, int Pos, int Count, int PointCount)
{
	guard(HasSCDAPlainRemap32);
	int Bad = 0;
	int MaxValue = 0;
	for (int i = 0; i < Count; i++)
	{
		const unsigned Value = ReadSCDAUInt32At(Ar, Pos + i * 4, false);
		if (Value < (unsigned)PointCount)
			MaxValue = max(MaxValue, (int)Value);
		else if (++Bad > 32)
			return false;
	}
	return MaxValue >= min(PointCount - 1, 512);
	unguard;
}

static void DumpSCDALodLikeBuffers(FArchive &Ar, int Start, int Stop, int PointCount, int PointsPos)
{
	guard(DumpSCDALodLikeBuffers);
	if (!getenv("SC4_DEBUG_MESH") || PointCount <= 0 || PointsPos <= Start)
	{
		if (getenv("SCDA_DEBUG_LOD_SCAN"))
			appPrintf("SCDA lod-like scan skipped: start=%08X stop=%08X pointsPos=%08X pointCount=%d\n", Start, Stop, PointsPos, PointCount);
		return;
	}
	if (getenv("SCDA_DEBUG_LOD_SCAN"))
		appPrintf("SCDA lod-like scan: start=%08X stop=%08X pointsPos=%08X pointCount=%d\n", Start, Stop, PointsPos, PointCount);

	int Printed = 0;
	for (int Header = Start; Header + 0x38 <= min(Stop, PointsPos); Header++)
	{
		const unsigned SectionCount = ReadSCDAUInt32At(Ar, Header + 0x00, false);
		const unsigned Count = ReadSCDAUInt32At(Ar, Header + 0x04, false);
		const unsigned ByteSize = ReadSCDAUInt32At(Ar, Header + 0x34, false);
		if (getenv("SCDA_DEBUG_LOD_SCAN") && Count == 8072)
			appPrintf("SCDA lod-like count probe: header=%08X sections=%u count=%u byteSize=%u dataEnd=%08X pointsPos=%08X\n",
				Header, SectionCount, Count, ByteSize, Header + 0x38 + ByteSize, PointsPos);
		if (SectionCount < 1 || SectionCount > 16 || Count < 512 || Count > 20000)
			continue;
		const unsigned Extra2 = (ByteSize >= Count * 2) ? ByteSize - Count * 2 : 0xFFFFFFFF;
		const unsigned Extra4 = (ByteSize >= Count * 4) ? ByteSize - Count * 4 : 0xFFFFFFFF;
		const unsigned Extra8 = (ByteSize >= Count * 8) ? ByteSize - Count * 8 : 0xFFFFFFFF;
		if (Extra2 > 0x80 && Extra4 > 0x80 && Extra8 > 0x80)
			continue;
		const int DataPos = Header + 0x38;
		const int DataEnd = DataPos + ByteSize;
		if (DataEnd > Stop || DataEnd > PointsPos)
			continue;

		int MaxRaw = 0;
		int Max13 = 0;
		int OverPointsRaw = 0;
		int OverPoints13 = 0;
		int HighFlagWords = 0;
		const int WordCount = ByteSize / 2;
		for (int i = 0; i < WordCount; i++)
		{
			const int Value = ReadSCDAUInt16At(Ar, DataPos + i * 2, false);
			const int Value13 = Value & 0x1FFF;
			MaxRaw = max(MaxRaw, Value);
			Max13 = max(Max13, Value13);
			if (Value >= PointCount)
				OverPointsRaw++;
			if (Value13 >= PointCount)
				OverPoints13++;
			if (Value & 0xE000)
				HighFlagWords++;
		}

		int Remap16Pos = 0;
		int Remap32Pos = 0;
		for (int Pos = DataEnd; Pos + (int)Count * 2 <= PointsPos && !Remap16Pos; Pos += 2)
		{
			if (HasSCDAPlainRemap16(Ar, Pos, Count, PointCount))
				Remap16Pos = Pos;
		}
		for (int Pos = DataEnd; Pos + (int)Count * 4 <= PointsPos && !Remap32Pos; Pos += 4)
		{
			if (HasSCDAPlainRemap32(Ar, Pos, Count, PointCount))
				Remap32Pos = Pos;
		}

		const unsigned BestExtra = min(Extra2, min(Extra4, Extra8));
		appPrintf("SCDA lod-like buffer: header=%08X sections=%u count=%u byteSize=%u extra=%u data=%08X-%08X words=%d maxRaw=%d max13=%d overPointsRaw=%d overPoints13=%d highFlags=%d remap16=%08X remap32=%08X\n",
			Header, SectionCount, Count, ByteSize, BestExtra, DataPos, DataEnd, WordCount, MaxRaw, Max13, OverPointsRaw, OverPoints13, HighFlagWords, Remap16Pos, Remap32Pos);
		if (++Printed >= 8)
			break;
	}
	unguard;
}

static bool ReadSCDALodLikeTopology(FArchive &Ar, int Start, int Stop, const TArray<FVector>& Points, TArray<VTriangle>& OutTriangles, int& OutPos, int PaletteStop = 0)
{
	guard(ReadSCDALodLikeTopology);
	const int PointCount = Points.Num();
	if (PointCount <= 0)
		return false;

	for (int Header = Start; Header + 0x3A <= Stop; Header++)
	{
		const unsigned SectionCount = ReadSCDAUInt32At(Ar, Header + 0x00, false);
		const unsigned Count = ReadSCDAUInt32At(Ar, Header + 0x04, false);
		if (SectionCount < 1 || SectionCount > 16 || Count < 512 || Count > 20000)
			continue;
		unsigned ByteSize = ReadSCDAUInt32At(Ar, Header + 0x34, false);
		int DataPos = Header + 0x38;
		if (ByteSize < Count * 3 || ByteSize > Count * 5 || ByteSize > 0x40000 || DataPos + (int)ByteSize > Stop)
		{
			ByteSize = ReadSCDAUInt32At(Ar, Header + 0x36, false);
			DataPos = Header + 0x3A;
			if (ByteSize < Count * 3 || ByteSize > Count * 5 || ByteSize > 0x40000 || DataPos + (int)ByteSize > Stop)
				continue;
		}
		const int DataEnd = DataPos + ByteSize;

		TArray<uint16> Palette;
		if (PaletteStop > DataEnd && PaletteStop <= Stop)
		{
			const int PaletteCount = (PaletteStop - DataEnd) / 2;
			if (PaletteCount >= 512 && PaletteCount <= 8192)
			{
				int BadPalette = 0;
				int MaxPalette = 0;
				Palette.Empty(PaletteCount);
				Palette.AddZeroed(PaletteCount);
				for (int i = 0; i < PaletteCount; i++)
				{
					const int Value = ReadSCDAUInt16At(Ar, DataEnd + i * 2, true);
					Palette[i] = Value;
					MaxPalette = max(MaxPalette, Value);
					if (Value >= PointCount)
						BadPalette++;
				}
				if (BadPalette || MaxPalette < min(PointCount - 1, 512))
					Palette.Empty();
			}
		}

		int Max13 = 0;
		for (int Pos = DataPos; Pos < DataEnd; Pos += 2)
			Max13 = max(Max13, (int)(ReadSCDAUInt16At(Ar, Pos, false) & 0x1FFF));
		const int Divisor = (Max13 >= PointCount * 2 && Max13 < PointCount * 4) ? 3 : 1;

		TArray<VTriangle> TestTriangles;
		TestTriangles.Empty(ByteSize / 6);
		TArray<FMeshWedge> TempWedges;
		TempWedges.Empty(PointCount);
		TempWedges.AddZeroed(PointCount);
		for (int i = 0; i < PointCount; i++)
			TempWedges[i].iVertex = i;
		int LongEdges = 0;
		float SumMaxEdge = 0;
		for (int Pos = DataPos; Pos + 5 < DataEnd; Pos += 6)
		{
			int A = (ReadSCDAUInt16At(Ar, Pos + 0, false) & 0x1FFF) / Divisor;
			int B = (ReadSCDAUInt16At(Ar, Pos + 2, false) & 0x1FFF) / Divisor;
			int C = (ReadSCDAUInt16At(Ar, Pos + 4, false) & 0x1FFF) / Divisor;
			if (Palette.Num())
			{
				A = ReadSCDAUInt16At(Ar, Pos + 0, false) & 0x0FFF;
				B = ReadSCDAUInt16At(Ar, Pos + 2, false) & 0x0FFF;
				C = ReadSCDAUInt16At(Ar, Pos + 4, false) & 0x0FFF;
				if (A >= Palette.Num() || B >= Palette.Num() || C >= Palette.Num())
					continue;
				A = Palette[A];
				B = Palette[B];
				C = Palette[C];
			}
			if (A >= PointCount || B >= PointCount || C >= PointCount || A == B || A == C || B == C)
				continue;
			float Area, MaxEdge;
			if (!GetSCDAWedgeTriangleMetrics(Points, TempWedges, A, B, C, Area, MaxEdge) || !IsSaneSCDATriangleArea(Area))
				continue;
			int OutIndex = TestTriangles.AddZeroed(1);
			VTriangle& T = TestTriangles[OutIndex];
			T.WedgeIndex[0] = A;
			T.WedgeIndex[1] = B;
			T.WedgeIndex[2] = C;
			T.MatIndex = 0;
			T.AuxMatIndex = 0;
			T.SmoothingGroups = 0;
			SumMaxEdge += MaxEdge;
			if (MaxEdge > 35.0f)
				LongEdges++;
		}
		if (TestTriangles.Num() < 300)
			continue;
		CopyArray(OutTriangles, TestTriangles);
		OutPos = DataPos;
		if (getenv("SC4_DEBUG_MESH"))
			appPrintf("SCDA lod-like topology: header=%08X indices=%08X-%08X triangles=%d sections=%u count=%u mask=%s divisor=%d palette=%d avgMaxEdge=%g long=%d\n",
				Header, DataPos, DataEnd, OutTriangles.Num(), SectionCount, Count,
				Palette.Num() ? "0FFF" : "1FFF", Divisor, Palette.Num(), SumMaxEdge / max(1, OutTriangles.Num()), LongEdges);
		return true;
	}
	return false;
	unguard;
}

static bool FindSCDABestWedge8Stream(FArchive &Ar, int Start, int Stop, int PointCount, TArray<FMeshWedge>& OutWedges, int& OutPos, int& OutEnd);
static bool FindSCDATopWedge8Stream(FArchive &Ar, int Start, int Stop, int& OutWedgeCount, int& OutMaxVertex);
static bool ReadSCDATopWedge8Stream(FArchive &Ar, int Start, int Stop, int WedgeCount, int PointCount, TArray<FMeshWedge>& OutWedges, int& OutEnd);

static bool FindSCDAPackedFloatMeshBlock(FArchive &Ar, int Start, int Stop,
	TArray<FVector>& OutPoints, TArray<FMeshWedge>& OutWedges, TArray<VTriangle>& OutTriangles, int& OutPointsPos, int& OutIndicesPos,
	const TArray<FMeshWedge> *KnownWedges = NULL, int KnownWedgesEnd = 0, int ExpectedPointCount = 0, bool ScanTopology = true)
{
	guard(FindSCDAPackedFloatMeshBlock);
	int BestPointsPos = 0;
	int BestIndicesPos = 0;
	int BestPointCount = 0;
	int BestStride = 0;
	int BestPosOffset = 0;
	int BestUvOffset = -1;
	int BestLayoutIndex = -1;
	int BestTailBytes = 0x7FFFFFFF;
	float BestScore = -3.4e38f;
	TArray<FVector> BestPoints;
	TArray<FMeshWedge> BestWedges;
	TArray<VTriangle> BestTriangles;

	struct FScdaPcVertexLayout
	{
		int Stride;
		int PosOffset;
		int UvOffset;
		int Align;
		int SearchStart;
		int SearchStop;
		int MinCount;
		float MinExtent;
		float MaxExtent;
	};
	FScdaPcVertexLayout Layouts[2];
	Layouts[0].Stride = 36;
	Layouts[0].PosOffset = 13;
	Layouts[0].UvOffset = 25;
	Layouts[0].Align = 0;
	Layouts[0].SearchStart = Start;
	Layouts[0].SearchStop = Stop;
	Layouts[0].MinCount = 1024;
	Layouts[0].MinExtent = 10.0f;
	Layouts[0].MaxExtent = 5000.0f;
	Layouts[1].Stride = 16;
	Layouts[1].PosOffset = 2;
	Layouts[1].UvOffset = -1;
	Layouts[1].Align = 16;
	Layouts[1].SearchStart = max(Start, Stop - 0x20000);
	Layouts[1].SearchStop = Stop;
	Layouts[1].MinCount = 512;
	Layouts[1].MinExtent = 10.0f;
	Layouts[1].MaxExtent = 10000.0f;

	const int LayoutCount = ARRAY_COUNT(Layouts);
	for (int LayoutIndex = 0; LayoutIndex < LayoutCount; LayoutIndex++)
	{
		const FScdaPcVertexLayout& Layout = Layouts[LayoutIndex];
		for (int Base = Layout.SearchStart; Base < Layout.SearchStop && Base + Layout.PosOffset + 12 <= Stop; Base++)
		{
			if (Layout.UvOffset >= 0 && Base + Layout.UvOffset + 4 > Stop)
				continue;
			if (Layout.Align && ((Base - Start) % Layout.Align) != 0)
				continue;
			int Count = 0;
			FVector Min, Max;
			Min.Set(3.4e38f, 3.4e38f, 3.4e38f);
			Max.Set(-3.4e38f, -3.4e38f, -3.4e38f);
			for (int Pos = Base; Pos + Layout.PosOffset + 12 <= Stop; Pos += Layout.Stride)
			{
				if (Layout.UvOffset >= 0 && Pos + Layout.UvOffset + 4 > Stop)
					break;
				const float X = ReadSCDAFloatAt(Ar, Pos + Layout.PosOffset + 0, false);
				const float Y = ReadSCDAFloatAt(Ar, Pos + Layout.PosOffset + 4, false);
				const float Z = ReadSCDAFloatAt(Ar, Pos + Layout.PosOffset + 8, false);
				if (!IsSaneSCDAFloat(X) || !IsSaneSCDAFloat(Y) || !IsSaneSCDAFloat(Z))
					break;
				if (Layout.Stride == 16 && Count == 0 &&
					fabs(X) < 0.000001f && fabs(Y) < 0.000001f && fabs(Z) < 0.000001f)
					break;
				Min.X = min(Min.X, X); Min.Y = min(Min.Y, Y); Min.Z = min(Min.Z, Z);
				Max.X = max(Max.X, X); Max.Y = max(Max.Y, Y); Max.Z = max(Max.Z, Z);
				Count++;
				if (ExpectedPointCount > 0 && Count >= ExpectedPointCount)
					break;
				if (Count > 65535)
					break;
			}
			if (Count < Layout.MinCount)
				continue;
			const float Extent = (Max.X - Min.X) + (Max.Y - Min.Y) + (Max.Z - Min.Z);
			if (Extent < Layout.MinExtent || Extent > Layout.MaxExtent)
				continue;
			const float DimX = Max.X - Min.X;
			const float DimY = Max.Y - Min.Y;
			const float DimZ = Max.Z - Min.Z;
			const float MinDim = min(DimX, min(DimY, DimZ));
			const float MaxDim = max(DimX, max(DimY, DimZ));
			const float MidDim = DimX + DimY + DimZ - MinDim - MaxDim;
			if (Layout.Stride == 36 && (MinDim < 5.0f || MaxDim / MinDim > 12.0f))
				continue;
			if (Layout.Stride == 36 && Count > 5000 && MidDim / MaxDim < 0.5f)
				continue;
			bool bLargeBodyOriented = false;
			if (Layout.Stride == 36 && ExpectedPointCount == 0 && Count > 1500 && Base != Start)
			{
				if (DimX < 50.0f || DimZ < 50.0f || DimY > max(DimX, DimZ) * 0.5f)
					continue;
				bLargeBodyOriented = true;
			}
			else if (Layout.Stride == 36 && ExpectedPointCount == 0 && Base != Start && Count > 1024)
			{
				if (DimX < 50.0f || DimZ < 50.0f)
					continue;
			}
			if (Layout.Stride == 36 && ExpectedPointCount > 0 && Count != ExpectedPointCount)
				continue;
			const int TailBytes = Stop - (Base + Count * Layout.Stride);
			if (Layout.Stride == 16 && TailBytes > 0x40)
				continue;
			if (Layout.Stride == 16 && TailBytes < 0)
				continue;

			TArray<FVector> TestPoints;
			TArray<FMeshWedge> TestWedges;
			TestPoints.Empty(Count);
			TestPoints.AddUninitialized(Count);
			TestWedges.Empty(Count);
			TestWedges.AddZeroed(Count);
			for (int i = 0; i < Count; i++)
			{
				const int Pos = Base + i * Layout.Stride;
				TestPoints[i].Set(
					ReadSCDAFloatAt(Ar, Pos + Layout.PosOffset + 0, false),
					ReadSCDAFloatAt(Ar, Pos + Layout.PosOffset + 4, false),
					ReadSCDAFloatAt(Ar, Pos + Layout.PosOffset + 8, false)
				);
				TestWedges[i].iVertex = i;
				if (Layout.UvOffset >= 0)
				{
					TestWedges[i].TexUV.U = ReadSCDAUInt16At(Ar, Pos + Layout.UvOffset + 0, false) / 2048.0f;
					TestWedges[i].TexUV.V = ReadSCDAUInt16At(Ar, Pos + Layout.UvOffset + 2, false) / 2048.0f;
				}
			}

			bool bBetter = false;
			float Score = 0;
			if (Layout.Stride == 36)
			{
				const float Balance = MinDim / max(1.0f, MaxDim) + MidDim / max(1.0f, MaxDim);
				Score = Count * 1.0f + Balance * 250.0f;
				if (ExpectedPointCount > 0)
					Score += 100000.0f - abs(Count - ExpectedPointCount) * 1000.0f;
			}
			if (!BestPointCount)
			{
				bBetter = true;
			}
			else if (LayoutIndex == 0)
			{
				if (ExpectedPointCount == 0 && bLargeBodyOriented && BestPointsPos != Start)
					bBetter = (Base > BestPointsPos + BestPointCount * BestStride);
				else if (ExpectedPointCount < 0)
					bBetter = (Count > BestPointCount);
				else
					bBetter = (Score > BestScore);
			}
			else
			{
				// The 16-byte Double Agent stream is a tail block; prefer the candidate
				// which lands closest to the export end, then the longest stream.
				bBetter = (TailBytes < BestTailBytes) || (TailBytes == BestTailBytes && Count > BestPointCount);
			}

			if (bBetter)
			{
				CopyArray(BestPoints, TestPoints);
				CopyArray(BestWedges, TestWedges);
				BestTriangles.Empty();
				BestPointsPos = Base;
				BestIndicesPos = 0;
				BestPointCount = Count;
				BestStride = Layout.Stride;
				BestPosOffset = Layout.PosOffset;
				BestUvOffset = Layout.UvOffset;
				BestLayoutIndex = LayoutIndex;
				BestTailBytes = TailBytes;
				BestScore = Score;
			}
			if (Layout.Stride == 36)
				Base += max(0, Count * Layout.Stride - 1);
		}
		if (BestPointCount && LayoutIndex == 0)
			break;
	}

	if (!BestPointCount)
		return false;
	const bool bScanFaceBuffer = ScanTopology && getenv("SCDA_DISABLE_FACE_BUFFER") == NULL;
	const bool bApplyFaceBuffer = !getenv("SCDA_PROBE_FACE_BUFFER_ONLY");
	bool bHaveExternalWedges = false;
	TArray<FMeshWedge> ProbeWedges;
	if (KnownWedges && KnownWedges->Num())
	{
		CopyArray(ProbeWedges, *KnownWedges);
		if (bApplyFaceBuffer)
			CopyArray(BestWedges, ProbeWedges);
		bHaveExternalWedges = true;
	}
	else if (bScanFaceBuffer)
	{
		TArray<FMeshWedge> FoundWedges;
		int FoundWedgesPos = 0;
		int FoundWedgesEnd = 0;
		if (FindSCDABestWedge8Stream(Ar, Start, BestPointsPos, BestPoints.Num(), FoundWedges, FoundWedgesPos, FoundWedgesEnd) &&
			FoundWedges.Num() >= BestPoints.Num())
		{
			CopyArray(ProbeWedges, FoundWedges);
			if (bApplyFaceBuffer)
				CopyArray(BestWedges, ProbeWedges);
			KnownWedgesEnd = FoundWedgesEnd;
			bHaveExternalWedges = true;
		}
	}
	if (bScanFaceBuffer)
	{
		DumpSCDADenseIndexStreams(Ar, Start, Stop, BestPoints.Num());
		DumpSCDALodLikeBuffers(Ar, Start, Stop, BestPoints.Num(), BestPointsPos);
		if (bApplyFaceBuffer && ReadSCDALodLikeTopology(Ar, Start, BestPointsPos ? BestPointsPos : Stop, BestPoints, BestTriangles, BestIndicesPos))
		{
			BestWedges.Empty(BestPoints.Num());
			BestWedges.AddZeroed(BestPoints.Num());
			for (int i = 0; i < BestWedges.Num(); i++)
				BestWedges[i].iVertex = i;
			bHaveExternalWedges = false;
		}
		if (!BestTriangles.Num() && bHaveExternalWedges)
			FindSCDARawWedgeIndexBlock(Ar, KnownWedgesEnd, Stop, BestPoints, bApplyFaceBuffer ? BestWedges : ProbeWedges,
				BestTriangles, BestIndicesPos);
		if (!BestTriangles.Num() && bHaveExternalWedges)
		{
			BestWedges.Empty(BestPoints.Num());
			BestWedges.AddZeroed(BestPoints.Num());
			for (int i = 0; i < BestWedges.Num(); i++)
				BestWedges[i].iVertex = i;
			bHaveExternalWedges = false;
		}
		if (!BestTriangles.Num())
			FindSCDACoherentStripBlocks(Ar, Start, Stop, BestPoints, BestPointsPos, BestStride, BestTriangles, BestIndicesPos) ||
				FindSCDARawIndexBlock(Ar, Start, Stop, BestPoints, BestPointsPos, BestTriangles, BestIndicesPos);
		if (!bApplyFaceBuffer)
		{
			BestTriangles.Empty();
			BestIndicesPos = 0;
			BestWedges.Empty(BestPoints.Num());
			BestWedges.AddZeroed(BestPoints.Num());
			for (int i = 0; i < BestWedges.Num(); i++)
				BestWedges[i].iVertex = i;
			if (getenv("SC4_DEBUG_MESH"))
				appPrintf("SCDA face buffer scan is probe-only; set SCDA_APPLY_FACE_BUFFER=1 to use candidate topology\n");
		}
	}
	if (!BestTriangles.Num() && BestPoints.Num() >= 3)
	{
		const int TriangleCount = BestPoints.Num() / 3;
		BestTriangles.Empty(TriangleCount);
		BestTriangles.AddZeroed(TriangleCount);
		for (int i = 0; i < TriangleCount; i++)
		{
			BestTriangles[i].WedgeIndex[0] = i * 3 + 0;
			BestTriangles[i].WedgeIndex[1] = i * 3 + 1;
			BestTriangles[i].WedgeIndex[2] = i * 3 + 2;
		}
		if (getenv("SC4_DEBUG_MESH"))
			appPrintf("SCDA packed float mesh has no verified face buffer; using diagnostic sequential triangles\n");
	}
	CopyArray(OutPoints, BestPoints);
	CopyArray(OutWedges, BestWedges);
	CopyArray(OutTriangles, BestTriangles);
	OutPointsPos = BestPointsPos;
	OutIndicesPos = BestIndicesPos;
	if (getenv("SC4_DEBUG_MESH"))
		appPrintf("SCDA packed float mesh block: layout=%d points=%d triangles=%d stride=%d posOff=%d uvOff=%d recordPos=%08X dataPos=%08X indicesPos=%08X tail=%d first=(%g,%g,%g) uv=(%g,%g)\n",
			BestLayoutIndex, OutPoints.Num(), OutTriangles.Num(), BestStride, BestPosOffset, BestUvOffset, OutPointsPos, OutPointsPos + BestPosOffset, OutIndicesPos, BestTailBytes,
			OutPoints[0].X, OutPoints[0].Y, OutPoints[0].Z, OutWedges[0].TexUV.U, OutWedges[0].TexUV.V);
	return true;
	unguard;
}

static bool ReadSCDAPcV1Packed36VertexStream(FArchive &Ar, int Start, int Stop,
	TArray<FVector>& OutPoints, TArray<FMeshWedge>& OutWedges, TArray<FVertInfluence>& OutInfluences,
	int& OutPointsPos, int& OutBoneCount)
{
	guard(ReadSCDAPcV1Packed36VertexStream);
	const int Stride = 36;
	const int PosOffset = 13;
	const int UvOffset = 25;
	const int BoneOffset = PosOffset + 28;
	const int WeightOffset = PosOffset + 32;
	int BestBase = 0;
	int BestCount = 0;
	float BestScore = -3.4e38f;

	auto ConsiderRun = [&](int RunBase, int Count, const FVector& Min, const FVector& Max)
	{
		if (Count < 1024)
			return;
		const float DimX = Max.X - Min.X;
		const float DimY = Max.Y - Min.Y;
		const float DimZ = Max.Z - Min.Z;
		const float MinDim = min(DimX, min(DimY, DimZ));
		const float MaxDim = max(DimX, max(DimY, DimZ));
		const float MidDim = DimX + DimY + DimZ - MinDim - MaxDim;
		const float Extent = DimX + DimY + DimZ;
		if (Extent < 10.0f || Extent > 5000.0f || MinDim < 5.0f || MaxDim / MinDim > 12.0f)
			return;
		if (Count > 5000 && MidDim / MaxDim < 0.5f)
			return;
		if (Count > 1500 && RunBase != Start)
		{
			if (DimX < 50.0f || DimZ < 50.0f || DimY > max(DimX, DimZ) * 0.5f)
				return;
		}

		const float Balance = MinDim / max(1.0f, MaxDim) + MidDim / max(1.0f, MaxDim);
		const float Score = Count + Balance * 250.0f;
		if (Count > BestCount || (Count == BestCount && Score > BestScore))
		{
			BestBase = RunBase;
			BestCount = Count;
			BestScore = Score;
		}
	};

	for (int Phase = 0; Phase < Stride; Phase++)
	{
		int RunBase = 0;
		int RunCount = 0;
		FVector Min, Max;
		Min.Set(3.4e38f, 3.4e38f, 3.4e38f);
		Max.Set(-3.4e38f, -3.4e38f, -3.4e38f);
		for (int Pos = Start + Phase; Pos + PosOffset + 12 <= Stop; Pos += Stride)
		{
			if (Pos + UvOffset + 4 > Stop)
				break;
			const float X = ReadSCDAFloatAt(Ar, Pos + PosOffset + 0, false);
			const float Y = ReadSCDAFloatAt(Ar, Pos + PosOffset + 4, false);
			const float Z = ReadSCDAFloatAt(Ar, Pos + PosOffset + 8, false);
			const bool bSane = IsSaneSCDAFloat(X) && IsSaneSCDAFloat(Y) && IsSaneSCDAFloat(Z);
			if (!bSane)
			{
				ConsiderRun(RunBase, RunCount, Min, Max);
				RunBase = 0;
				RunCount = 0;
				Min.Set(3.4e38f, 3.4e38f, 3.4e38f);
				Max.Set(-3.4e38f, -3.4e38f, -3.4e38f);
				continue;
			}
			if (!RunCount)
				RunBase = Pos;
			Min.X = min(Min.X, X); Min.Y = min(Min.Y, Y); Min.Z = min(Min.Z, Z);
			Max.X = max(Max.X, X); Max.Y = max(Max.Y, Y); Max.Z = max(Max.Z, Z);
			RunCount++;
			if (RunCount > 65535)
			{
				ConsiderRun(RunBase, RunCount, Min, Max);
				RunBase = 0;
				RunCount = 0;
				Min.Set(3.4e38f, 3.4e38f, 3.4e38f);
				Max.Set(-3.4e38f, -3.4e38f, -3.4e38f);
			}
		}
		ConsiderRun(RunBase, RunCount, Min, Max);
	}

	if (!BestCount)
		return false;

	OutPoints.Empty(BestCount);
	OutPoints.AddUninitialized(BestCount);
	OutWedges.Empty(BestCount);
	OutWedges.AddZeroed(BestCount);
	OutInfluences.Empty(BestCount * 2);
	OutBoneCount = 0;
	int InvalidInfluenceRecords = 0;
	for (int i = 0; i < BestCount; i++)
	{
		const int Pos = BestBase + i * Stride;
		OutPoints[i].Set(
			ReadSCDAFloatAt(Ar, Pos + PosOffset + 0, false),
			ReadSCDAFloatAt(Ar, Pos + PosOffset + 4, false),
			ReadSCDAFloatAt(Ar, Pos + PosOffset + 8, false)
		);
		OutWedges[i].iVertex = i;
		OutWedges[i].TexUV.U = ReadSCDAUInt16At(Ar, Pos + UvOffset + 0, false) / 2048.0f;
		OutWedges[i].TexUV.V = ReadSCDAUInt16At(Ar, Pos + UvOffset + 2, false) / 2048.0f;

		int WeightSum = 0;
		int MaxBone = 0;
		byte Bones[4];
		byte Weights[4];
		if (Pos + WeightOffset + 4 > Stop)
		{
			InvalidInfluenceRecords++;
			FVertInfluence& Influence = OutInfluences[OutInfluences.AddDefaulted()];
			Influence.Weight = 1.0f;
			Influence.PointIndex = i;
			Influence.BoneIndex = 0;
			OutBoneCount = max(OutBoneCount, 1);
			continue;
		}
		for (int j = 0; j < 4; j++)
		{
			Bones[j] = ReadSCDAUInt16At(Ar, Pos + BoneOffset + j, false) & 0xFF;
			Weights[j] = ReadSCDAUInt16At(Ar, Pos + WeightOffset + j, false) & 0xFF;
			WeightSum += Weights[j];
			MaxBone = max(MaxBone, (int)Bones[j]);
		}
		if (WeightSum != 255 || MaxBone >= 256)
		{
			InvalidInfluenceRecords++;
			FVertInfluence& Influence = OutInfluences[OutInfluences.AddDefaulted()];
			Influence.Weight = 1.0f;
			Influence.PointIndex = i;
			Influence.BoneIndex = 0;
			OutBoneCount = max(OutBoneCount, 1);
			continue;
		}
		for (int j = 0; j < 4; j++)
		{
			if (!Weights[j])
				continue;
			FVertInfluence& Influence = OutInfluences[OutInfluences.AddDefaulted()];
			Influence.Weight = Weights[j] / 255.0f;
			Influence.PointIndex = i;
			Influence.BoneIndex = Bones[j];
			OutBoneCount = max(OutBoneCount, (int)Bones[j] + 1);
		}
	}
	OutPointsPos = BestBase;
	if (getenv("SC4_DEBUG_MESH"))
		appPrintf("SCDA PC v1 packed36 vertices: points=%d recordPos=%08X dataPos=%08X influences=%d bones=%d invalidInfluences=%d first=(%g,%g,%g) uv=(%g,%g)\n",
			OutPoints.Num(), BestBase, BestBase + PosOffset,
			OutInfluences.Num(), OutBoneCount, InvalidInfluenceRecords,
			OutPoints[0].X, OutPoints[0].Y, OutPoints[0].Z,
			OutWedges[0].TexUV.U, OutWedges[0].TexUV.V);
	return true;
	unguard;
}

static bool ReadSCDAPcV1LinearizedMesh(FArchive &Ar, UnPackage *Package, int Start, int Stop,
	TArray<FVector>& OutPoints, TArray<FMeshWedge>& OutWedges, TArray<VTriangle>& OutTriangles,
	TArray<FVertInfluence>& OutInfluences, TArray<int>& OutMaterialRefs, int& OutPointsPos, int& OutIndicesPos, int& OutBoneCount,
	int& OutBonePaletteSearchStart)
{
	guard(ReadSCDAPcV1LinearizedMesh);
	OutBonePaletteSearchStart = 0;
	int TopologyEnd = 0;
	int TopologyHeader = 0;
	int TopologyCount = 0;
	bool bShiftedTopologyHeader = false;
	OutMaterialRefs.Empty();
	for (int Header = Start; Header + 0x3A <= min(Stop, Start + 0x100); Header++)
	{
		const unsigned SectionCount = ReadSCDAUInt32At(Ar, Header + 0x00, false);
		const unsigned Count = ReadSCDAUInt32At(Ar, Header + 0x04, false);
		if (SectionCount < 1 || SectionCount > 16 || Count < 512 || Count > 20000)
			continue;
		unsigned ByteSize = ReadSCDAUInt32At(Ar, Header + 0x34, false);
		int DataPos = Header + 0x38;
		if (ByteSize < Count * 3 || ByteSize > Count * 5 || ByteSize > 0x40000 || DataPos + (int)ByteSize > Stop)
		{
			ByteSize = ReadSCDAUInt32At(Ar, Header + 0x36, false);
			DataPos = Header + 0x3A;
			bShiftedTopologyHeader = true;
			if (ByteSize < Count * 3 || ByteSize > Count * 5 || ByteSize > 0x40000 || DataPos + (int)ByteSize > Stop)
			{
				// Some SCDA PC v1 demo meshes keep the section/count header,
				// but the face/index payload is serialized later as a bounded
				// ABC+material block rather than an inline byte-sized topology.
				TopologyHeader = Header;
				TopologyCount = Count;
				TopologyEnd = Header + 0x34;
				bShiftedTopologyHeader = true;
				break;
			}
		}
		else
		{
			bShiftedTopologyHeader = false;
		}
		const int DataEnd = DataPos + ByteSize;
		if (DataEnd <= Stop)
		{
			TopologyHeader = Header;
			TopologyCount = Count;
			TopologyEnd = DataEnd;
			break;
		}
	}
	if (!TopologyEnd)
	{
		if (getenv("SC4_DEBUG_MESH"))
			appPrintf("SCDA PC v1 fast mesh: no topology header in first 0x100 bytes\n");
		return false;
	}

	if (bShiftedTopologyHeader)
	{
		// SCDA PC linearized meshes with the shifted topology header use the
		// packed vertex stream.  The data immediately after the topology block
		// can look like 8-byte wedges, but it is not a UV/vertex wedge stream
		// for SamDiving-style meshes; accepting it sends the reader down the
		// native-wedge path and corrupts the face domain.
		const int PackedSearchStart = TopologyEnd;
		if (ReadSCDAPcV1Packed36VertexStream(Ar, PackedSearchStart, Stop, OutPoints, OutWedges, OutInfluences, OutPointsPos, OutBoneCount))
		{
			const int MaterialScanStop = min(OutPointsPos, TopologyEnd + 0x30000);
			const int FaceSearchStop = min(OutPointsPos, TopologyEnd + 0x50000);
			const int FaceSearchStart = TopologyEnd;
			if (!ReadSCDAPcV1HeaderMaterialRefs(Ar, Package, Start, Stop, OutMaterialRefs))
				ScanSCDAPcV1MaterialRefs(Ar, Package, TopologyEnd, MaterialScanStop, OutMaterialRefs);
			if (FindSCDALongFaceRecord8ABCBlock(Ar, FaceSearchStart, FaceSearchStop, TopologyCount,
					OutPoints, OutTriangles, OutIndicesPos, OutMaterialRefs.Num()) ||
				FindSCDAFaceRecord8ABCBlock(Ar, FaceSearchStart, FaceSearchStop, TopologyCount,
					OutPoints, OutTriangles, OutIndicesPos, OutMaterialRefs.Num()) ||
				ReadSCDALodLikeTopology(Ar, TopologyHeader, FaceSearchStop, OutPoints, OutTriangles, OutIndicesPos) ||
				FindSCDARawIndexBlock(Ar, FaceSearchStart, FaceSearchStop, OutPoints, OutPointsPos, OutTriangles, OutIndicesPos))
			{
				if (OutIndicesPos > 0)
					OutBonePaletteSearchStart = OutIndicesPos + TopologyCount * 8;
				if (getenv("SC4_DEBUG_MESH"))
					appPrintf("SCDA PC v1 shifted packed mesh: points=%d pointPos=%08X tris=%d indices=%08X materials=%d\n",
						OutPoints.Num(), OutPointsPos, OutTriangles.Num(), OutIndicesPos, OutMaterialRefs.Num());
				return true;
			}
		}
		if (getenv("SC4_DEBUG_MESH"))
			appPrintf("SCDA PC v1 shifted mesh: unable to pair packed vertices with topology\n");
		return false;
	}

	TArray<FMeshWedge> TopWedges;
	int TopWedgesPos = 0;
	int TopWedgesEnd = 0;
	int TopWedgeCount = 0;
	int TopMaxVertex = 0;
	const int WedgeScanStop = min(Stop, TopologyEnd + 0x4000);
	for (int Pos = TopologyEnd; Pos <= WedgeScanStop - 8; Pos += 2)
	{
		const uint16 U0 = ReadSCDAUInt16At(Ar, Pos + 0, false);
		const uint16 V0 = ReadSCDAUInt16At(Ar, Pos + 2, false);
		const uint16 Vertex0 = ReadSCDAUInt16At(Ar, Pos + 4, false);
		const uint16 Extra0 = ReadSCDAUInt16At(Ar, Pos + 6, false);
		if (U0 > 0x4000 || V0 > 0x4000 || Vertex0 > 256 || Extra0 > 8)
			continue;
		int Count = 0;
		int MaxVertex = 0;
		for (int P = Pos; P + 8 <= Stop; P += 8)
		{
			const uint16 Vertex = ReadSCDAUInt16At(Ar, P + 4, false);
			if (Vertex > 20000)
				break;
			MaxVertex = max(MaxVertex, (int)Vertex);
			Count++;
			if (Count > 20000)
				break;
		}
		if (Count >= 300 && Count <= 20000 && MaxVertex >= 512 && MaxVertex < Count &&
			ReadSCDATopWedge8Stream(Ar, Pos, Stop, Count, MaxVertex + 1, TopWedges, TopWedgesEnd))
		{
			TopWedgesPos = Pos;
			TopWedgeCount = Count;
			TopMaxVertex = MaxVertex;
			break;
		}
	}
	if (!TopWedges.Num())
	{
		const int PackedSearchStart = bShiftedTopologyHeader ? max(TopologyEnd, Stop - 0x70000) : TopologyEnd;
		if (ReadSCDAPcV1Packed36VertexStream(Ar, PackedSearchStart, Stop, OutPoints, OutWedges, OutInfluences, OutPointsPos, OutBoneCount) &&
			ReadSCDALodLikeTopology(Ar, TopologyHeader, min(OutPointsPos, Stop), OutPoints, OutTriangles, OutIndicesPos))
		{
			const int MaterialScanStop = bShiftedTopologyHeader ? min(OutPointsPos, TopologyEnd + 0x30000) : OutPointsPos;
			ScanSCDAPcV1MaterialRefs(Ar, Package, TopologyEnd, MaterialScanStop, OutMaterialRefs);
			if (getenv("SC4_DEBUG_MESH"))
				appPrintf("SCDA PC v1 linearized packed mesh: points=%d pointPos=%08X tris=%d indices=%08X materials=%d\n",
					OutPoints.Num(), OutPointsPos, OutTriangles.Num(), OutIndicesPos, OutMaterialRefs.Num());
			return true;
		}
		if (getenv("SC4_DEBUG_MESH"))
			appPrintf("SCDA PC v1 fast mesh: no wedge stream after topologyEnd=%08X\n", TopologyEnd);
		return false;
	}
	ScanSCDAPcV1MaterialRefs(Ar, Package, TopologyEnd, TopWedgesPos, OutMaterialRefs);

	const int ExpectedPointCount = TopMaxVertex + 1;
	if (ExpectedPointCount < 3 || ExpectedPointCount > 65535)
		return false;

	if (!ReadSCDANativeVertexStream(Ar, TopWedgesEnd, Stop, ExpectedPointCount,
		OutPoints, OutWedges, OutInfluences, OutPointsPos, OutBoneCount))
	{
		if (getenv("SC4_DEBUG_MESH"))
			appPrintf("SCDA PC v1 fast mesh: no vertex stream after wedges=%08X count=%d\n", TopWedgesEnd, ExpectedPointCount);
		if (ReadSCDAPcV1Packed36VertexStream(Ar, TopologyEnd, Stop, OutPoints, OutWedges, OutInfluences, OutPointsPos, OutBoneCount) &&
			ReadSCDALodLikeTopology(Ar, TopologyHeader, min(OutPointsPos, Stop), OutPoints, OutTriangles, OutIndicesPos))
		{
			ScanSCDAPcV1MaterialRefs(Ar, Package, TopologyEnd, min(OutPointsPos, TopologyEnd + 0x30000), OutMaterialRefs);
			if (getenv("SC4_DEBUG_MESH"))
				appPrintf("SCDA PC v1 linearized packed mesh after false wedge: points=%d pointPos=%08X tris=%d indices=%08X materials=%d\n",
					OutPoints.Num(), OutPointsPos, OutTriangles.Num(), OutIndicesPos, OutMaterialRefs.Num());
			return true;
		}
		return false;
	}

	if (ReadSCDAFaceRecord8Block(Ar, TopWedgesPos, TopWedgesEnd, OutPoints, OutTriangles, OutIndicesPos, OutMaterialRefs.Num()))
	{
		OutBonePaletteSearchStart = TopWedgesEnd;
	}
	else if (ReadSCDAPointStrip16Block(Ar, TopWedgesPos, OutPointsPos, OutPoints, OutTriangles, OutIndicesPos))
	{
		OutBonePaletteSearchStart = OutIndicesPos + TopWedgeCount * 2;
	}
	else if (ReadSCDALodLikeTopology(Ar, Start, min(OutPointsPos, Start + 0x20000), OutPoints, OutTriangles, OutIndicesPos, TopWedgesPos))
	{
		OutBonePaletteSearchStart = TopWedgesEnd;
	}
	else if (ReadSCDAPointIndexBlock(Ar, TopWedgesPos, TopWedgesEnd, OutPoints, OutWedges, OutWedges, OutTriangles, OutIndicesPos))
	{
		OutBonePaletteSearchStart = TopWedgesEnd;
	}
	else if (FindSCDARawWedgeIndexBlock(Ar, TopWedgesEnd, OutPointsPos, OutPoints, TopWedges, OutTriangles, OutIndicesPos))
	{
		CopyArray(OutWedges, TopWedges);
		OutBonePaletteSearchStart = OutIndicesPos + OutTriangles.Num() * 3 * 2;
	}
	else
	{
		if (getenv("SC4_DEBUG_MESH"))
			appPrintf("SCDA PC v1 fast mesh: no topology triangles before points=%08X\n", OutPointsPos);
		return false;
	}

	if (getenv("SC4_DEBUG_MESH"))
		appPrintf("SCDA PC v1 linearized mesh: wedges=%d wedgePos=%08X-%08X points=%d pointPos=%08X tris=%d indices=%08X materials=%d\n",
			TopWedges.Num(), TopWedgesPos, TopWedgesEnd, OutPoints.Num(), OutPointsPos, OutTriangles.Num(), OutIndicesPos, OutMaterialRefs.Num());
	return true;
	unguard;
}

static bool FindSCDATopWedge8Stream(FArchive &Ar, int Start, int Stop, int& OutWedgeCount, int& OutMaxVertex)
{
	guard(FindSCDATopWedge8Stream);
	OutWedgeCount = 0;
	OutMaxVertex = 0;
	if (Start + 8 > Stop)
		return false;

	int Count = 0;
	int MaxVertex = 0;
	int MinU = 0xFFFF, MinV = 0xFFFF, MaxU = 0, MaxV = 0;
	int NonZeroExtra = 0;
	int ZeroRun = 0;
	for (int Pos = Start; Pos + 8 <= Stop; Pos += 8)
	{
		uint16 U = ReadSCDAUInt16At(Ar, Pos + 0, false);
		uint16 V = ReadSCDAUInt16At(Ar, Pos + 2, false);
		uint16 Vertex = ReadSCDAUInt16At(Ar, Pos + 4, false);
		uint16 Extra = ReadSCDAUInt16At(Ar, Pos + 6, false);
		if (!U && !V && !Vertex && !Extra)
		{
			ZeroRun++;
			if (ZeroRun >= 16)
			{
				Count -= ZeroRun - 1;
				break;
			}
			Count++;
			continue;
		}
		ZeroRun = 0;
		MinU = min(MinU, (int)U); MaxU = max(MaxU, (int)U);
		MinV = min(MinV, (int)V); MaxV = max(MaxV, (int)V);
		MaxVertex = max(MaxVertex, (int)Vertex);
		if (Extra)
			NonZeroExtra++;
		Count++;
	}
	if (Count >= 128 && Count <= 20000 && MaxVertex < Count && MaxVertex >= 512 && MaxVertex <= 20000)
	{
		OutWedgeCount = Count;
		OutMaxVertex = MaxVertex;
	}
	if (Count < 128 || Count > 20000 || MaxVertex >= Count || MaxVertex < 512 || MaxVertex > 20000)
		return false;
	if ((MaxU > 65000 && MaxV > 65000) || (MinU == 0 && MinV == 0 && MaxU > 60000 && MaxV > 60000))
		return false;
	OutWedgeCount = Count;
	OutMaxVertex = MaxVertex;
	if (getenv("SC4_DEBUG_MESH"))
		appPrintf("SCDA top wedge8 stream: pos=%08X wedges=%d maxVertex=%d uvRange=(%d,%d)-(%d,%d) extraNonZero=%d next=%08X\n",
			Start, Count, MaxVertex, MinU, MinV, MaxU, MaxV, NonZeroExtra, Start + Count * 8);
	return true;
	unguard;
}

static bool ReadSCDATopWedge8Stream(FArchive &Ar, int Start, int Stop, int WedgeCount, int PointCount, TArray<FMeshWedge>& OutWedges, int& OutEnd)
{
	guard(ReadSCDATopWedge8Stream);
	if (WedgeCount < 3 || PointCount <= 0 || Start < 0 || Start + WedgeCount * 8 > Stop)
		return false;
	OutWedges.Empty(WedgeCount);
	OutWedges.AddZeroed(WedgeCount);
	int ValidWedges = 0;
	for (int i = 0; i < WedgeCount; i++)
	{
		const int Pos = Start + i * 8;
		const uint16 U = ReadSCDAUInt16At(Ar, Pos + 0, false);
		const uint16 V = ReadSCDAUInt16At(Ar, Pos + 2, false);
		const uint16 Vertex = ReadSCDAUInt16At(Ar, Pos + 4, false);
		if (Vertex >= PointCount)
		{
			OutWedges.Empty();
			return false;
		}
		OutWedges[i].iVertex = Vertex;
		OutWedges[i].TexUV.U = U / 2048.0f;
		OutWedges[i].TexUV.V = V / 2048.0f;
		if (U || V || Vertex)
			ValidWedges++;
	}
	if (ValidWedges < 3)
	{
		OutWedges.Empty();
		return false;
	}
	OutEnd = Start + WedgeCount * 8;
	if (getenv("SC4_DEBUG_MESH"))
		appPrintf("SCDA top wedge8 parsed: pos=%08X wedges=%d end=%08X firstVertex=%d firstUV=(%g,%g)\n",
			Start, OutWedges.Num(), OutEnd, OutWedges[0].iVertex, OutWedges[0].TexUV.U, OutWedges[0].TexUV.V);
	return true;
	unguard;
}

static bool FindSCDABestWedge8Stream(FArchive &Ar, int Start, int Stop, int PointCount, TArray<FMeshWedge>& OutWedges, int& OutPos, int& OutEnd)
{
	guard(FindSCDABestWedge8Stream);
	if (PointCount <= 0 || Start < 0 || Start + 8 > Stop)
		return false;

	int BestPos = 0;
	int BestCount = 0;
	int BestMaxVertex = 0;
	int BestUvSpan = 0;
	for (int Pos = Start; Pos <= Stop - 8; Pos += 2)
	{
		int Count = 0;
		int MaxVertex = 0;
		int MinU = 0xFFFF, MinV = 0xFFFF, MaxU = 0, MaxV = 0;
		int ZeroRun = 0;
		for (int P = Pos; P + 8 <= Stop; P += 8)
		{
			const uint16 U = ReadSCDAUInt16At(Ar, P + 0, false);
			const uint16 V = ReadSCDAUInt16At(Ar, P + 2, false);
			const uint16 Vertex = ReadSCDAUInt16At(Ar, P + 4, false);
			const uint16 Extra = ReadSCDAUInt16At(Ar, P + 6, false);
			if (!U && !V && !Vertex && !Extra)
			{
				ZeroRun++;
				if (ZeroRun >= 16)
				{
					Count -= ZeroRun - 1;
					break;
				}
				Count++;
				continue;
			}
			ZeroRun = 0;
			if (Vertex >= PointCount)
				break;
			MinU = min(MinU, (int)U); MaxU = max(MaxU, (int)U);
			MinV = min(MinV, (int)V); MaxV = max(MaxV, (int)V);
			MaxVertex = max(MaxVertex, (int)Vertex);
			Count++;
			if (Count > 20000)
				break;
		}
		if (Count < 300 || Count > 20000 || MaxVertex >= Count || MaxVertex < min(PointCount - 1, 256))
			continue;
		if (PointCount >= 1024 && MaxVertex < PointCount * 3 / 4)
			continue;
		const int UvSpan = (MaxU - MinU) + (MaxV - MinV);
		if (UvSpan < 64)
			continue;
		if (MaxVertex > BestMaxVertex || (MaxVertex == BestMaxVertex && Count > BestCount) || (MaxVertex == BestMaxVertex && Count == BestCount && UvSpan > BestUvSpan))
		{
			BestPos = Pos;
			BestCount = Count;
			BestMaxVertex = MaxVertex;
			BestUvSpan = UvSpan;
		}
	}
	if (!BestCount)
		return false;
	if (!ReadSCDATopWedge8Stream(Ar, BestPos, Stop, BestCount, PointCount, OutWedges, OutEnd))
		return false;
	OutPos = BestPos;
	if (getenv("SC4_DEBUG_MESH"))
		appPrintf("SCDA wedge8 stream: pos=%08X wedges=%d maxVertex=%d uvSpan=%d end=%08X\n",
			OutPos, BestCount, BestMaxVertex, BestUvSpan, OutEnd);
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

static int ReadSCDARefSkeleton(FArchive &Ar, UnPackage *Package, int Pos, int Stop, TArray<FMeshBone> &OutBones)
{
	guard(ReadSCDARefSkeleton);
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
	int MatchingPrefixes = 0;
	for (int i = 0; i < BoneCount; i++)
	{
		int NameIndex;
		unsigned Flags;
		FQuat Orientation;
		FVector Position;
		float Length;
		FVector Size;
		int NumChildren, ParentIndex;

		Ar << AR_INDEX(NameIndex);
		if (Ar.Tell() + 56 > Stop)
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
			fabs(Position.X) > 10000 || fabs(Position.Y) > 10000 || fabs(Position.Z) > 10000 ||
			Length != Length || Size.X != Size.X || Size.Y != Size.Y || Size.Z != Size.Z)
		{
			Ar.Seek(SavePos);
			return 0;
		}
		if (Flags == 0 && NameIndex >= 0 && (!Package || unsigned(NameIndex) < Package->Summary.NameCount))
			MatchingPrefixes++;
		if (QuatLen > 0.8f && QuatLen < 1.2f)
			ValidQuats++;
		if (i > 0)
			ParentCounts[ParentIndex]++;

		FMeshBone &B = Bones[i];
		B.Name = (Package && unsigned(NameIndex) < Package->Summary.NameCount)
			? Package->GetName(NameIndex) : "None";
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
	return BoneCount * 100 + MatchingChildren * 25 + ValidQuats * 5 + MatchingPrefixes;
	unguard;
}

static bool FindSCDARefSkeleton(FArchive &Ar, UnPackage *Package, int Start, int Stop, TArray<FMeshBone> &OutBones)
{
	guard(FindSCDARefSkeleton);
	int BestPos = 0;
	int BestScore = 0;
	TArray<FMeshBone> BestBones;
	for (int Pos = Start; Pos < Stop - 128; Pos++)
	{
		TArray<FMeshBone> Bones;
		int Score = ReadSCDARefSkeleton(Ar, Package, Pos, Stop, Bones);
		if (Score <= BestScore)
			continue;
		BestScore = Score;
		BestPos = Pos;
		CopyArray(BestBones, Bones);
		int PerfectScore = Bones.Num() * (100 + 25 + 5 + 1);
		if (Bones.Num() && BestScore >= PerfectScore)
			break;
	}
	if (!BestScore)
	{
		OutBones.Empty();
		return false;
	}

	CopyArray(OutBones, BestBones);
	if (getenv("SC4_DEBUG_MESH"))
		appPrintf("SCDA mesh RefSkeleton block: %08X bones=%d score=%d firstPos=(%g,%g,%g)\n",
			BestPos, OutBones.Num(), BestScore,
			OutBones.Num() ? OutBones[0].BonePos.Position.X : 0,
			OutBones.Num() ? OutBones[0].BonePos.Position.Y : 0,
			OutBones.Num() ? OutBones[0].BonePos.Position.Z : 0);
	return true;
	unguard;
}

static bool FindSCDARefSkeletonAligned(FArchive &Ar, UnPackage *Package, int Start, int Stop, TArray<FMeshBone> &OutBones)
{
	guard(FindSCDARefSkeletonAligned);
	int BestPos = 0;
	int BestScore = 0;
	TArray<FMeshBone> BestBones;
	for (int Pos = Start; Pos < Stop - 128; Pos += 4)
	{
		Ar.Seek(Pos);
		int BoneCount = 0;
		if (!ReadSCDACompactIndex(Ar, Stop, BoneCount) || BoneCount < 2 || BoneCount > 256)
			continue;
		int NameIndex = 0;
		if (!ReadSCDACompactIndex(Ar, Stop, NameIndex) ||
			!Package || unsigned(NameIndex) >= Package->Summary.NameCount ||
			Ar.Tell() + 4 > Stop)
			continue;
		if (ReadSCDAUInt32At(Ar, Ar.Tell(), false) != 0)
			continue;
		TArray<FMeshBone> Bones;
		int Score = ReadSCDARefSkeleton(Ar, Package, Pos, Stop, Bones);
		if (Score <= BestScore)
			continue;
		BestScore = Score;
		BestPos = Pos;
		CopyArray(BestBones, Bones);
		const int PerfectScore = Bones.Num() * (100 + 25 + 5 + 1);
		if (Bones.Num() && BestScore >= PerfectScore)
			break;
	}
	if (!BestScore)
	{
		OutBones.Empty();
		return false;
	}

	CopyArray(OutBones, BestBones);
	if (getenv("SC4_DEBUG_MESH"))
		appPrintf("SCDA mesh aligned RefSkeleton block: %08X bones=%d score=%d firstPos=(%g,%g,%g)\n",
			BestPos, OutBones.Num(), BestScore,
			OutBones.Num() ? OutBones[0].BonePos.Position.X : 0,
			OutBones.Num() ? OutBones[0].BonePos.Position.Y : 0,
			OutBones.Num() ? OutBones[0].BonePos.Position.Z : 0);
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
			const bool bAlreadySuppliedBindPose = AnimSet->BonePositions.Num() != 0;
			const bool bSCDADemoAnim =
				Animation->GetGame() == GAME_SplinterCell &&
				Animation->GetArVer() == 100 &&
				Animation->GetLicenseeVer() >= 158 &&
				Animation->GetLicenseeVer() <= 167;

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
					if (bSCDADemoAnim && !bAlreadySuppliedBindPose && !Seq->bAdditive && Track->KeyQuat.Num())
					{
						for (int KeyIndex = 0; KeyIndex < Track->KeyQuat.Num(); KeyIndex++)
						{
							CQuat Q = Mesh->RefSkeleton[BoneIndex].Orientation;
							Q.Mul(Track->KeyQuat[KeyIndex]);
							Q.Normalize();
							Track->KeyQuat[KeyIndex] = Q;
						}
					}
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

static bool IsSCCTBoneNameLike(const TArray<FMeshBone> &Bones, int BoneIndex, const char *Part)
{
	if (!Bones.IsValidIndex(BoneIndex))
		return false;
	return appStristr(*Bones[BoneIndex].Name, Part) != NULL;
}

static int FindSCCTBoneByName(const TArray<FMeshBone> &Bones, const char *Name)
{
	for (int BoneIndex = 0; BoneIndex < Bones.Num(); BoneIndex++)
		if (!stricmp(*Bones[BoneIndex].Name, Name))
			return BoneIndex;
	return -1;
}

static bool SCCTPaletteContainsBone(const FSCCTBonePalette &Palette, int BoneIndex)
{
	if (BoneIndex < 0)
		return false;
	for (int Local = 0; Local < 256; Local++)
		if (Palette.Map[Local] == BoneIndex)
			return true;
	return false;
}

static void FixSCCTLeadingBonePaletteEntry(TArray<FSCCTBonePalette> &Palettes, const TArray<FMeshBone> &Bones, const char *MeshName)
{
	guard(FixSCCTLeadingBonePaletteEntry);
	int Spine2Bone = FindSCCTBoneByName(Bones, "B Spine2");
	if (Spine2Bone < 0)
		return;
	int LeftClavicleBone = FindSCCTBoneByName(Bones, "B L Clavicle");
	int RightClavicleBone = FindSCCTBoneByName(Bones, "B R Clavicle");
	for (int PaletteIndex = 0; PaletteIndex < Palettes.Num(); PaletteIndex++)
	{
		FSCCTBonePalette &P = Palettes[PaletteIndex];
		if (P.Map[0] == 0xFF || P.Map[1] == 0xFF)
			continue;
		if (!IsSCCTBoneNameLike(Bones, P.Map[0], "Finger"))
			continue;
		const bool bTorsoSecond = IsSCCTBoneNameLike(Bones, P.Map[1], "Spine") ||
			IsSCCTBoneNameLike(Bones, P.Map[1], "Clavicle");
		const bool bHasBothClavicles = SCCTPaletteContainsBone(P, LeftClavicleBone) &&
			SCCTPaletteContainsBone(P, RightClavicleBone);
		if (!bTorsoSecond && !bHasBothClavicles)
		{
			continue;
		}

		P.Map[0] = Spine2Bone;
		if (getenv("SCCT_DEBUG_PALETTE"))
			appPrintf("SCCT fixed leading bone palette %s wedges=%d-%d\n",
				MeshName, P.FirstWedge, P.LastWedge);
	}
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

// Xbox uses ordinary compact-count arrays here, not lazy arrays or a packed
// topology word stream. See default.xbe UMesh 0x23F296 / USkeletalMesh 0x2218C2.
static int ReadSCDAXboxMeshCount(FArchive &Ar, int MinStride, int MaxCount = 1000000)
{
	int Count;
	if (!ReadSCDACompactIndex(Ar, Ar.GetStopper(), Count) || Count < 0 || Count > MaxCount ||
		Count > (Ar.GetStopper() - Ar.Tell()) / MinStride)
		appError("Invalid SCDA Xbox skeletal array extent");
	return Count;
}

template<class T> static void ReadSCDAXboxMeshArray(FArchive &Ar, TArray<T>& Array, int Stride, int MaxCount = 1000000)
{
	int Count = ReadSCDAXboxMeshCount(Ar, Stride, MaxCount);
	Array.Empty(Count);
	Array.AddDefaulted(Count);
	for (int i = 0; i < Count; i++) Ar << Array[i];
}

void USkeletalMesh::SerializeSCell(FArchive &Ar)
{
	if (Ar.ArVer == 100 && Ar.ArLicenseeVer == 127)
	{
		guard(SerializeSCDAXboxSkeletalMesh);
		// Manifests may map the native root alone or retain the None property
		// terminator. Recognize the primitive header at either exact location.
		int Start = Ar.Tell();
		int Stop = Ar.GetStopper();
		int NativeVersion = 0;
		if (Stop - Start >= 49)
		{
			Ar.Seek(Start + 41);
			Ar << NativeVersion;
		}
		Ar.Seek(Start);
		if (NativeVersion < 1 || NativeVersion > 4)
		{
			int NameIndex;
			if (!ReadSCDACompactIndex(Ar, Stop, NameIndex) || !Package ||
				unsigned(NameIndex) >= Package->Summary.NameCount ||
				stricmp(Package->GetName(NameIndex), "None"))
				appError("Invalid SCDA Xbox skeletal property terminator");
		}
		Ar << BoundingBox << BoundingSphere << Version << VertexCount;
		if (Version < 1 || Version > 4 || VertexCount < 1 || VertexCount > 65536)
			appError("Invalid SCDA Xbox skeletal native header");
		int Count = ReadSCDAXboxMeshCount(Ar, 4);
		Ar.Seek(Ar.Tell() + Count * 4); // FrameConnects
		Count = ReadSCDAXboxMeshCount(Ar, 38);
		Ar.Seek(Ar.Tell() + Count * 38); // FrameVerts, no memory alignment padding
		ReadSCDAXboxMeshArray(Ar, Textures, 1, 256);
		TArray<UMaterial*> SecondaryTextures;
		if (Version > 2) ReadSCDAXboxMeshArray(Ar, SecondaryTextures, 1, 256);
		Ar << MeshScale << MeshOrigin << RotOrigin;
		TArray<uint16> ObsoletePointMap;
		ReadSCDAXboxMeshArray(Ar, ObsoletePointMap, 2);
		ReadSCDAXboxMeshArray(Ar, FaceLevel, 2);
		ReadSCDAXboxMeshArray(Ar, Faces, 8);
		ReadSCDAXboxMeshArray(Ar, CollapseWedgeThus, 2);
		ReadSCDAXboxMeshArray(Ar, Super::Wedges, 10, 65536);
		ReadSCDAXboxMeshArray(Ar, Materials, 8, 256);
		Ar << MeshScaleMax << LODHysteresis << LODStrength << LODMinVerts << LODMorph << LODZDisplace;
		if (Version > 1) Ar << Version;
		if (Version != 1 && Version != 4) appError("Unsupported SCDA Xbox skeletal version");
		ReadSCDAXboxMeshArray(Ar, Points2, 12, 65536);
		TArray<FVector> ObsoletePoints;
		ReadSCDAXboxMeshArray(Ar, ObsoletePoints, 12);
		Count = ReadSCDAXboxMeshCount(Ar, 57, 1024);
		RefSkeleton.Empty(Count);
		RefSkeleton.AddZeroed(Count);
		for (int i = 0; i < Count; i++)
		{
			FMeshBone& Bone = RefSkeleton[i];
			int NameIndex;
			if (!ReadSCDACompactIndex(Ar, Stop, NameIndex) || !Package ||
				unsigned(NameIndex) >= Package->Summary.NameCount || Ar.Tell() + 56 > Stop)
				appError("Invalid SCDA Xbox skeletal bone name");
			Bone.Name = Package->GetName(NameIndex);
			Ar << Bone.Flags << Bone.BonePos << Bone.NumChildren << Bone.ParentIndex;
			if (Bone.ParentIndex < 0 || Bone.ParentIndex > i)
				appError("Invalid SCDA Xbox skeletal bone parent");
		}
		Ar << Animation << SkeletalDepth;
		Count = ReadSCDAXboxMeshCount(Ar, 5, 16);
		WeightIndices.Empty(Count);
		WeightIndices.AddDefaulted(Count);
		for (int i = 0; i < Count; i++)
		{
			ReadSCDAXboxMeshArray(Ar, WeightIndices[i].BoneInfIndices, 2, 65536);
			Ar << WeightIndices[i].StartBoneInf;
		}
		ReadSCDAXboxMeshArray(Ar, BoneInfluences, 4);
		if (Points2.Num() != VertexCount || !Faces.Num() || !Super::Wedges.Num() || !RefSkeleton.Num())
			appError("Incomplete SCDA Xbox skeletal geometry");
		TArray<byte> PointHasWeights;
		PointHasWeights.AddZeroed(VertexCount);
		for (int i = 0; i < WeightIndices.Num(); i++)
		{
			const VWeightIndex& WI = WeightIndices[i];
			if (WI.StartBoneInf < 0 || WI.StartBoneInf > BoneInfluences.Num() ||
				WI.BoneInfIndices.Num() > (BoneInfluences.Num() - WI.StartBoneInf) / (i + 1))
				appError("Invalid SCDA Xbox skeletal influence range");
			for (int j = 0; j < WI.BoneInfIndices.Num(); j++)
			{
				int Point = WI.BoneInfIndices[j];
				if (Point >= VertexCount || PointHasWeights[Point])
					appError("Invalid SCDA Xbox skeletal influence point");
				PointHasWeights[Point] = 1;
				unsigned Sum = 0;
				for (int k = 0; k <= i; k++)
				{
					const VBoneInfluence& BI = BoneInfluences[WI.StartBoneInf + j * (i + 1) + k];
					if (BI.BoneIndex >= RefSkeleton.Num()) appError("Invalid SCDA Xbox skeletal influence bone");
					Sum += BI.BoneWeight;
				}
				if (!Sum) appError("SCDA Xbox skeletal point has zero weight");
			}
		}
		for (int i = 0; i < VertexCount; i++)
			if (!PointHasWeights[i]) appError("SCDA Xbox skeletal point has no weights");
		for (int i = 0; i < Super::Wedges.Num(); i++)
			if (Super::Wedges[i].iVertex >= VertexCount) appError("Invalid SCDA Xbox skeletal wedge point");
		for (int i = 0; i < Faces.Num(); i++)
		{
			const FMeshFace& Face = Faces[i];
			if (Face.MaterialIndex >= Materials.Num() || Face.iWedge[0] >= Super::Wedges.Num() ||
				Face.iWedge[1] >= Super::Wedges.Num() || Face.iWedge[2] >= Super::Wedges.Num())
				appError("Invalid SCDA Xbox skeletal triangle");
		}
		for (int i = 0; i < Materials.Num(); i++)
			if (Materials[i].TextureIndex < 0 || Materials[i].TextureIndex >= Textures.Num())
				appError("Invalid SCDA Xbox skeletal material slot");
		// The base arrays contain the complete highest-detail source mesh.
		// GPU buffers in the remaining tail duplicate it for Xbox rendering.
		LODModels.Empty();
		UpgradeMesh();
		appPrintf("SCDA Xbox skeletal mesh %s: points=%d wedges=%d triangles=%d bones=%d influences=%d\n",
			Name, Points.Num(), Wedges.Num(), Triangles.Num(), RefSkeleton.Num(), VertInfluences.Num());
		DROP_REMAINING_DATA(Ar);
		ConvertMesh();
		return;
		unguard;
	}
	const bool isDoubleAgentMeshLayout =
		(Ar.ArVer >= 173 && Ar.ArVer <= 275 && Ar.ArLicenseeVer == 0) ||
		(Ar.ArVer == 100 && Ar.ArLicenseeVer >= 127);
	const bool debugDoubleAgent = (isDoubleAgentMeshLayout && getenv("SC4_DEBUG_MESH"));
	if (debugDoubleAgent)
	{
		appPrintf("SC4 SerializeSCell start %s pos=%08X version=%d\n", Name, Ar.Tell(), Version);
		appPrintf("SC4 pre-skel fields vertexCount=%d meshScale=(%g,%g,%g) meshOrigin=(%g,%g,%g) rot=(%d,%d,%d) faceLevel=%d faces=%d wedges=%d materials=%d textures=%d\n",
			VertexCount, VECTOR_ARG(MeshScale), VECTOR_ARG(MeshOrigin), FROTATOR_ARG(RotOrigin),
			FaceLevel.Num(), Faces.Num(), Wedges.Num(), Materials.Num(), Textures.Num());
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
		// Small resident foliage retains UObject's None terminator before its
		// native body. Strip it only when the following complete native header
		// validates, preserving the raw-root path used by character meshes.
		if (Ar.ArVer == 100 && Ar.ArLicenseeVer == 127 && Package && ScanStart < Stop)
		{
			int NameIndex;
			if (ReadSCDACompactIndex(Ar, Stop, NameIndex) && NameIndex >= 0 &&
				unsigned(NameIndex) < Package->Summary.NameCount &&
				!stricmp(Package->GetName(NameIndex), "None"))
			{
				int NativeStart = Ar.Tell();
				FSCDANativeMeshHeader Header;
				bool NativeHeader = ReadSCDANativeMeshHeader(Ar, NativeStart, Stop, Header);
				// Foliage has markerless topology, so the character face-header
				// reader stops after recognizing its primitive/transform fields.
				if (!NativeHeader && Header.TransformPos > NativeStart &&
					Header.TopologyWordCount >= 16 && Header.TopologyWordCount <= 100000 &&
					Header.TopologyDataPos + Header.TopologyWordCount * 2 <= Stop)
					NativeHeader = true;
				if (NativeHeader)
					ScanStart = NativeStart;
			}
			Ar.Seek(ScanStart);
		}
		if (debugDoubleAgent)
		{
			DumpSCDAMeshCompactNames(Ar, Package, ScanStart, Stop);
			DumpSCDAPackedVertexCandidates(Ar, ScanStart, Stop);
		}

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
			TArray<FMeshWedge> RawWedges;
			TArray<VTriangle> RawTriangles;
			TArray<FVertInfluence> NativeInfluences;
			TArray<FMeshBone> NativeBones;
			TArray<int> PcV1MaterialRefs;
			int NativeBoneCount = 0;
			int PcV1BonePaletteSearchStart = 0;
			bool bHaveRawMesh = false;
			TArray<byte> RawExportData;
			FArchive *RawScanAr = &Ar;
			int RawScanStart = ScanStart;
			int RawScanStop = Stop;
			FMemReader *RawMemReader = NULL;
			int SavePos = Ar.Tell();
			Ar.Seek(ScanStart);
			RawExportData.AddUninitialized(Stop - ScanStart);
			Ar.Serialize(RawExportData.GetData(), RawExportData.Num());
			Ar.Seek(SavePos);
			RawMemReader = new FMemReader(RawExportData.GetData(), RawExportData.Num());
			RawScanAr = RawMemReader;
			RawScanStart = 0;
			RawScanStop = RawExportData.Num();
			FSCDANativeMeshHeader NativeHeader;
			TArray<int> NativeMaterialRefs;
			const bool bScdaPcV1Linearized = (Ar.ArVer == 100 && Ar.ArLicenseeVer >= 158 && Ar.ArLicenseeVer <= 167);
			const bool bHaveNativeHeader = bScdaPcV1Linearized
				? false
				: ReadSCDANativeMeshHeader(*RawScanAr, RawScanStart, RawScanStop, NativeHeader, &NativeMaterialRefs);
			const bool bHaveNativeMaterials = !bScdaPcV1Linearized &&
				NativeHeader.TransformPos > RawScanStart && NativeHeader.TopologyWordCount >= 16 &&
				NativeHeader.TopologyWordCount <= 100000 &&
				NativeHeader.TopologyDataPos + NativeHeader.TopologyWordCount * 2 <= RawScanStop &&
				NativeMaterialRefs.Num() == NativeHeader.MaterialCount;
			int TopWedgeCount = 0;
			int TopMaxVertex = 0;
			const bool bHaveTopWedges = !bScdaPcV1Linearized && !bHaveNativeHeader && FindSCDATopWedge8Stream(*RawScanAr, RawScanStart, RawScanStop, TopWedgeCount, TopMaxVertex);
			const int ExpectedPointCount = bHaveNativeHeader ? NativeHeader.MaxFaceIndex + 1 :
				(bHaveTopWedges ? TopMaxVertex + 1 : (TopWedgeCount > 0 ? -1 : 0));
			TArray<FMeshWedge> TopWedges;
			int TopWedgesEnd = 0;
			if (TopWedgeCount > 0 && TopMaxVertex >= 0)
				ReadSCDATopWedge8Stream(*RawScanAr, RawScanStart, RawScanStop, TopWedgeCount, TopMaxVertex + 1, TopWedges, TopWedgesEnd);
			if (bHaveNativeHeader)
			{
				const int VertexSearchStart = NativeHeader.FacePos + NativeHeader.FaceCount * 8;
				bHaveRawMesh = ReadSCDANativeVertexStream(*RawScanAr, VertexSearchStart, RawScanStop,
					ExpectedPointCount, RawPoints, RawWedges, NativeInfluences, RawPointsPos, NativeBoneCount);
			}
			else if (bScdaPcV1Linearized)
			{
				unsigned PcV1StartTime = debugDoubleAgent ? appMilliseconds() : 0;
				bHaveRawMesh = ReadSCDAPcV1LinearizedMesh(*RawScanAr, Package, RawScanStart, RawScanStop,
					RawPoints, RawWedges, RawTriangles, NativeInfluences, PcV1MaterialRefs, RawPointsPos, RawIndicesPos, NativeBoneCount,
					PcV1BonePaletteSearchStart);
				if (debugDoubleAgent)
					appPrintf("SCDA PC v1 mesh reader time: %.3f sec\n", (appMilliseconds() - PcV1StartTime) / 1000.0f);
			}
			else
			{
				bHaveRawMesh = FindSCDAPackedFloatMeshBlock(*RawScanAr, RawScanStart, RawScanStop, RawPoints, RawWedges, RawTriangles, RawPointsPos, RawIndicesPos,
					TopWedges.Num() ? &TopWedges : NULL, TopWedgesEnd, ExpectedPointCount, true);
			}
			if (bHaveRawMesh && bHaveNativeHeader)
			{
				if (NativeHeader.MaxFaceIndex >= RawPoints.Num() ||
					!ReadSCDARawFaceRecords(*RawScanAr, NativeHeader.FacePos, RawScanStop, NativeHeader.FaceCount, RawPoints.Num(), false, RawTriangles))
				{
					bHaveRawMesh = false;
					RawTriangles.Empty();
				}
				else
				{
					RawIndicesPos = NativeHeader.FacePos;
					if (debugDoubleAgent)
						appPrintf("SCDA native face stream accepted: points=%d faces=%d materials=%d\n",
							RawPoints.Num(), RawTriangles.Num(), NativeHeader.MaterialCount);
				}
			}
			if (bHaveRawMesh && bHaveNativeHeader)
				FindSCDARefSkeleton(*RawScanAr, Package, RawScanStart, RawScanStop, NativeBones);
			else if (bHaveRawMesh && bScdaPcV1Linearized)
			{
				unsigned SkelStartTime = debugDoubleAgent ? appMilliseconds() : 0;
				FindSCDARefSkeletonAligned(*RawScanAr, Package, RawScanStart,
					RawPointsPos > RawScanStart ? RawPointsPos : RawScanStop, NativeBones);
				if (debugDoubleAgent)
					appPrintf("SCDA PC v1 skeleton search time: %.3f sec\n", (appMilliseconds() - SkelStartTime) / 1000.0f);
			}
			if (!bHaveRawMesh && !bScdaPcV1Linearized && FindSCDARawVectorBlock(*RawScanAr, RawScanStart, RawScanStop, RawPoints, RawPointsPos))
			{
				bHaveRawMesh = FindSCDARawIndexBlock(*RawScanAr, RawScanStart, RawScanStop, RawPoints, RawPointsPos, RawTriangles, RawIndicesPos);
				if (!bHaveRawMesh && getenv("SCDA_ALLOW_TRIANGLE_SOUP") && RawPoints.Num() >= 96)
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
			if (!bHaveRawMesh && !bScdaPcV1Linearized && getenv("SCDA_ALLOW_PACKED_INT_MESH"))
				bHaveRawMesh = FindSCDAPackedMeshBlock(*RawScanAr, RawScanStart, RawScanStop, RawPoints, RawTriangles, RawPointsPos, RawIndicesPos);
			if (!bHaveRawMesh && !bScdaPcV1Linearized && getenv("SCDA_ALLOW_TRIANGLE_SOUP"))
				bHaveRawMesh = FindSCDAPacked17TriangleSoup(*RawScanAr, RawScanStart, RawScanStop, RawPoints, RawTriangles, RawPointsPos);
			if (!bHaveRawMesh && !bScdaPcV1Linearized)
			{
				RawWedges.Empty();
				bHaveRawMesh = FindSCDAInlineTriangleStream(*RawScanAr, RawScanStart, RawScanStop,
					RawPoints, RawWedges, RawTriangles, RawPointsPos);
				if (bHaveRawMesh)
					RawIndicesPos = RawPointsPos;
			}
			if (bHaveRawMesh)
			{
				if (bHaveNativeMaterials || (bScdaPcV1Linearized && PcV1MaterialRefs.Num()))
				{
					const TArray<int>& MaterialRefs = bHaveNativeMaterials ? NativeMaterialRefs : PcV1MaterialRefs;
					Textures.Empty(MaterialRefs.Num());
					Textures.AddZeroed(MaterialRefs.Num());
					Materials.Empty(MaterialRefs.Num());
					Materials.AddZeroed(MaterialRefs.Num());
					for (int i = 0; i < MaterialRefs.Num(); i++)
					{
						const int Ref = MaterialRefs[i];
						UObject *Material = NULL;
						if (Package)
						{
							if (Ref < 0 && unsigned(-Ref - 1) < Package->Summary.ImportCount)
								Material = Package->CreateImport(-Ref - 1);
							else if (Ref > 0 && unsigned(Ref - 1) < Package->Summary.ExportCount)
								Material = Package->CreateExport(Ref - 1);
						}
						Textures[i] = static_cast<UMaterial*>(Material);
						Materials[i].TextureIndex = i;
					}
				}
				CopyArray(Points, RawPoints);
				CopyArray(Triangles, RawTriangles);
				if (RawWedges.Num())
				{
					CopyArray(Wedges, RawWedges);
				}
				else
				{
					Wedges.Empty(Points.Num());
					Wedges.AddZeroed(Points.Num());
					for (int i = 0; i < Wedges.Num(); i++)
						Wedges[i].iVertex = i;
				}
				TArray<FString> ManifestBoneNames;
				TArray<int> ManifestBoneParents;
				const bool bHaveManifestSkeleton = Package &&
					GetScdaV2ManifestSkeleton(*Package->GetFilename(),
						ManifestBoneNames, ManifestBoneParents) &&
					ManifestBoneNames.Num() >= NativeBoneCount;
				if (NativeBones.Num())
				{
					if (debugDoubleAgent && getenv("SCDA_DEBUG_PALETTE"))
					{
						appPrintf("SCDA native skeleton names:");
						for (int i = 0; i < NativeBones.Num() && i < 16; i++)
							appPrintf(" %d=%s", i, *NativeBones[i].Name);
						appPrintf("\n");
					}
					CopyArray(RefSkeleton, NativeBones);
					if (bHaveManifestSkeleton && ManifestBoneNames.Num() == RefSkeleton.Num())
					{
						for (int i = 0; i < RefSkeleton.Num(); i++)
						{
							RefSkeleton[i].Name = *ManifestBoneNames[i];
							RefSkeleton[i].ParentIndex = ManifestBoneParents[i];
							RefSkeleton[i].NumChildren = 0;
						}
						for (int i = 1; i < RefSkeleton.Num(); i++)
						{
							int Parent = RefSkeleton[i].ParentIndex;
							if (Parent >= 0 && Parent < RefSkeleton.Num())
								RefSkeleton[Parent].NumChildren++;
						}
					}
					if (debugDoubleAgent)
					{
						appPrintf("SCDA native skeleton: bones=%d skinPalette=%d manifest=%d root=%s pos=(%g,%g,%g)\n",
							RefSkeleton.Num(), NativeBoneCount, bHaveManifestSkeleton ? 1 : 0,
							RefSkeleton.Num() ? *RefSkeleton[0].Name : "",
							RefSkeleton.Num() ? RefSkeleton[0].BonePos.Position.X : 0,
							RefSkeleton.Num() ? RefSkeleton[0].BonePos.Position.Y : 0,
							RefSkeleton.Num() ? RefSkeleton[0].BonePos.Position.Z : 0);
						for (int i = 0; i < RefSkeleton.Num() && i < 8; i++)
							appPrintf("  SCDA bone[%d] name=%s parent=%d pos=(%g,%g,%g)\n",
								i, *RefSkeleton[i].Name, RefSkeleton[i].ParentIndex,
								RefSkeleton[i].BonePos.Position.X,
								RefSkeleton[i].BonePos.Position.Y,
								RefSkeleton[i].BonePos.Position.Z);
					}
				}
				else
				{
					const int BoneCount = bHaveManifestSkeleton
						? ManifestBoneNames.Num() : max(1, NativeBoneCount);
					RefSkeleton.Empty(BoneCount);
					RefSkeleton.AddZeroed(BoneCount);
					for (int i = 0; i < BoneCount; i++)
					{
						if (bHaveManifestSkeleton)
							RefSkeleton[i].Name = *ManifestBoneNames[i];
						else
						{
							char BoneName[32];
							appSprintf(ARRAY_ARG(BoneName), "SCDA_Bone_%02d", i);
							RefSkeleton[i].Name = BoneName;
						}
						RefSkeleton[i].ParentIndex = bHaveManifestSkeleton
							? ManifestBoneParents[i] : (i ? 0 : 0);
						RefSkeleton[i].BonePos.Orientation.W = 1.0f;
						RefSkeleton[i].BonePos.Size.Set(1.0f, 1.0f, 1.0f);
					}
					for (int i = 1; i < BoneCount; i++)
					{
						int Parent = RefSkeleton[i].ParentIndex;
						if (Parent >= 0 && Parent < BoneCount)
							RefSkeleton[Parent].NumChildren++;
					}
					if (debugDoubleAgent && bHaveManifestSkeleton)
						appPrintf("SCDA manifest skeleton: bones=%d skinPalette=%d root=%s\n",
							BoneCount, NativeBoneCount, *ManifestBoneNames[0]);
				}
				if (NativeInfluences.Num() && RefSkeleton.Num() && NativeBoneCount > 0 && NativeBoneCount <= RefSkeleton.Num())
				{
					const int PaletteSearchStart = bHaveNativeHeader
						? NativeHeader.FacePos + NativeHeader.FaceCount * 8
						: (PcV1BonePaletteSearchStart > 0 ? PcV1BonePaletteSearchStart :
							(RawIndicesPos > 0 ? RawIndicesPos + max(RawTriangles.Num(), Wedges.Num()) * 8 : RawScanStart));
					byte PcV1HardwareMap[256];
					int PcV1HardwareMapPos = 0;
					bool bRemappedNativeBones = false;
					if (bScdaPcV1Linearized &&
						FindSCDAPcV1HardwareBoneMap(*RawScanAr,
							PaletteSearchStart,
							RawPointsPos > 0 ? RawPointsPos : RawScanStop,
							NativeBoneCount, RefSkeleton.Num(), PcV1HardwareMap, PcV1HardwareMapPos))
					{
						bRemappedNativeBones = ApplySCDAPcV1HardwareBoneMap(PcV1HardwareMap, NativeBoneCount, NativeInfluences);
					}
					if (!bRemappedNativeBones && bScdaPcV1Linearized && RawPointsPos > 0 &&
						FindSCDAPcV1EmbeddedHardwareBoneMap(*RawScanAr,
							RawPointsPos, RawScanStop,
							NativeBoneCount, RefSkeleton.Num(), PcV1HardwareMap, PcV1HardwareMapPos))
					{
						bRemappedNativeBones = ApplySCDAPcV1HardwareBoneMap(PcV1HardwareMap, NativeBoneCount, NativeInfluences);
					}
					if (!bRemappedNativeBones)
					{
						TArray<FSCDANativeBonePalette> NativeBonePalettes;
						const int PaletteSearchStop = RawPointsPos > 0 ? RawPointsPos : RawScanStop;
						const bool bXboxSections = bHaveNativeHeader && Ar.ArVer == 100 && Ar.ArLicenseeVer == 127;
						const bool bHavePalettes = bXboxSections
							? FindSCDAXboxBonePalettes(*RawScanAr, PaletteSearchStart, PaletteSearchStop,
								RawTriangles, RawPoints.Num(), RefSkeleton.Num(), NativeInfluences, NativeBonePalettes)
							: FindSCDANativeBonePalettes(*RawScanAr, PaletteSearchStart, PaletteSearchStop,
								RawTriangles, NativeBoneCount, RefSkeleton.Num(), NativeBonePalettes);
						if (bHavePalettes)
						{
							bRemappedNativeBones = ApplySCDANativeBonePalettes(RawTriangles, RawPoints.Num(),
								NativeBonePalettes, NativeInfluences);
						}
					}
					if (!bRemappedNativeBones && debugDoubleAgent)
					{
						appPrintf("WARNING: Unable to locate SCDA native bone palettes for %s, using local bone indices\n", Name);
					}
				}
				if (NativeInfluences.Num())
					CopyArray(VertInfluences, NativeInfluences);
				else
				{
					VertInfluences.Empty(Points.Num());
					VertInfluences.AddZeroed(Points.Num());
					for (int i = 0; i < VertInfluences.Num(); i++)
					{
						VertInfluences[i].Weight = 1.0f;
						VertInfluences[i].PointIndex = i;
						VertInfluences[i].BoneIndex = 0;
					}
				}
				if (debugDoubleAgent)
					appPrintf("SCDA raw mesh fallback: points=%d triangles=%d pointsPos=%08X indicesPos=%08X\n",
						Points.Num(), Triangles.Num(), RawPointsPos, RawIndicesPos);
				delete RawMemReader;
				PrepareSCDASkeletalMeshForView(*this);
				DROP_REMAINING_DATA(Ar);
				ConvertMesh();
				return;
			}
			delete RawMemReader;

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

			appPrintf("WARNING: unable to locate Double Agent skeletal mesh face buffer for %s; loaded placeholder triangle\n", Name);
			Points.Empty(3);
			Points.AddZeroed(3);
			Points[1].Set(1.0f, 0.0f, 0.0f);
			Points[2].Set(0.0f, 1.0f, 0.0f);
			Wedges.Empty(3);
			Wedges.AddZeroed(3);
			for (int i = 0; i < Wedges.Num(); i++)
				Wedges[i].iVertex = i;
			Triangles.Empty(1);
			Triangles.AddZeroed(1);
			Triangles[0].WedgeIndex[0] = 0;
			Triangles[0].WedgeIndex[1] = 1;
			Triangles[0].WedgeIndex[2] = 2;
			RefSkeleton.Empty(1);
			RefSkeleton.AddZeroed(1);
			RefSkeleton[0].Name = "B";
			RefSkeleton[0].ParentIndex = -1;
			RefSkeleton[0].BonePos.Orientation.W = 1.0f;
			RefSkeleton[0].BonePos.Size.Set(1.0f, 1.0f, 1.0f);
			VertInfluences.Empty(3);
			VertInfluences.AddZeroed(3);
			for (int i = 0; i < VertInfluences.Num(); i++)
			{
				VertInfluences[i].Weight = 1.0f;
				VertInfluences[i].PointIndex = i;
				VertInfluences[i].BoneIndex = 0;
			}
			PrepareSCDASkeletalMeshForView(*this);
			DROP_REMAINING_DATA(Ar);
			ConvertMesh();
			return;
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
			FixSCCTLeadingBonePaletteEntry(BonePalettes, MeshBones, Name);
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

static uint32 ReadSCDAStaticLE32(const byte* Data, int Pos)
{
	return (uint32)(Data[Pos] | (Data[Pos + 1] << 8) | (Data[Pos + 2] << 16) | (Data[Pos + 3] << 24));
}

static bool ReadSCCTStaticSections(const byte* Data, int DataSize, int NumVerts, int NumFaces, int MaterialCount, TArray<FStaticMeshSection>& OutSections, const char* MeshName)
{
	guard(ReadSCCTStaticSections);

	if (MaterialCount <= 1 || MaterialCount > 64 || NumFaces <= 0)
		return false;

	for (int Pos = 0; Pos + 10 <= DataSize; Pos++)
	{
		bool bValid = true;
		int ExpectedFirstIndex = 0;
		int SearchPos = Pos;
		TArray<FStaticMeshSection> Candidate;
		Candidate.Empty(MaterialCount);

		for (int i = 0; i < MaterialCount; i++)
		{
			int Rec = -1;
			const int SearchEnd = min(DataSize - 10, SearchPos + 32);
			for (int Probe = SearchPos; Probe <= SearchEnd; Probe++)
			{
				const int FirstIndex = ReadSCDAStaticLE16(Data, Probe + 0);
				const int CountA     = ReadSCDAStaticLE16(Data, Probe + 6);
				const int CountB     = ReadSCDAStaticLE16(Data, Probe + 8);
				if (FirstIndex == ExpectedFirstIndex && CountA > 0 && (CountB == 0 || CountA == CountB))
				{
					Rec = Probe;
					break;
				}
			}
			if (Rec < 0)
			{
				bValid = false;
				break;
			}

			const int FirstIndex  = ReadSCDAStaticLE16(Data, Rec + 0);
			const int FirstVertex = ReadSCDAStaticLE16(Data, Rec + 2);
			const int LastVertex  = ReadSCDAStaticLE16(Data, Rec + 4);
			const int CountA      = ReadSCDAStaticLE16(Data, Rec + 6);
			const int CountB      = ReadSCDAStaticLE16(Data, Rec + 8);

			if (FirstIndex != ExpectedFirstIndex || CountA <= 0 || (CountB != 0 && CountA != CountB))
			{
				bValid = false;
				break;
			}
			if (FirstVertex < 0 || LastVertex < FirstVertex || LastVertex >= NumVerts)
			{
				bValid = false;
				break;
			}
			if (ExpectedFirstIndex + CountA * 3 > NumFaces * 3)
			{
				bValid = false;
				break;
			}

			FStaticMeshSection* Section = new (Candidate) FStaticMeshSection;
			memset(Section, 0, sizeof(FStaticMeshSection));
			Section->FirstIndex  = FirstIndex;
			Section->FirstVertex = FirstVertex;
			Section->LastVertex  = LastVertex;
			Section->fE          = CountA;
			Section->NumFaces    = CountA;
			ExpectedFirstIndex += CountA * 3;
			SearchPos = Rec + 10;
		}

		if (bValid && ExpectedFirstIndex == NumFaces * 3)
		{
			OutSections.Empty(Candidate.Num());
			OutSections.AddZeroed(Candidate.Num());
			for (int i = 0; i < Candidate.Num(); i++)
				OutSections[i] = Candidate[i];
			if (getenv("SCCT_SM_DEBUG"))
				appPrintf("SCCT StaticMesh %s: section table at tail +%X sections=%d faces=%d\n",
					MeshName, Pos, OutSections.Num(), NumFaces);
			return true;
		}
	}

	if (DataSize > 0x30 && (int)ReadSCDAStaticLE32(Data, 8) == MaterialCount)
	{
		struct FLocalSection
		{
			int FirstVertex;
			int LastVertex;
			int NumFaces;
		};

		TArray<FLocalSection> LaterSections;
		LaterSections.Empty(MaterialCount - 1);
		int SearchPos = 0x20;
		for (int i = 1; i < MaterialCount; i++)
		{
			int Rec = -1;
			const int SearchEnd = min(DataSize - 10, SearchPos + 0x28);
			for (int Probe = SearchPos; Probe <= SearchEnd; Probe++)
			{
				const int FirstVertex = ReadSCDAStaticLE16(Data, Probe + 2);
				const int LastVertex  = ReadSCDAStaticLE16(Data, Probe + 4);
				const int CountA      = ReadSCDAStaticLE16(Data, Probe + 6);
				const int CountB      = ReadSCDAStaticLE16(Data, Probe + 8);
				if (CountA > 0 && CountA == CountB &&
					FirstVertex >= 0 && FirstVertex < NumVerts &&
					LastVertex >= FirstVertex && LastVertex < NumVerts)
				{
					Rec = Probe;
					break;
				}
			}
			if (Rec < 0)
			{
				LaterSections.Empty();
				break;
			}

			FLocalSection* Section = new (LaterSections) FLocalSection;
			Section->FirstVertex = ReadSCDAStaticLE16(Data, Rec + 2);
			Section->LastVertex  = ReadSCDAStaticLE16(Data, Rec + 4);
			Section->NumFaces    = ReadSCDAStaticLE16(Data, Rec + 6);
			SearchPos = Rec + 10;
		}

		if (LaterSections.Num() == MaterialCount - 1)
		{
			int LaterFaces = 0;
			for (int i = 0; i < LaterSections.Num(); i++)
				LaterFaces += LaterSections[i].NumFaces;

			int FirstFaces = 0;
			const int ExplicitFirstFaces = ReadSCDAStaticLE16(Data, 0x17);
			if (ExplicitFirstFaces > 0 && ExplicitFirstFaces + LaterFaces == NumFaces)
			{
				FirstFaces = ExplicitFirstFaces;
			}
			else if (NumFaces > LaterFaces)
			{
				FirstFaces = NumFaces - LaterFaces;
			}

			const int FirstVertex = ReadSCDAStaticLE16(Data, 0x13);
			const int LastVertex  = ReadSCDAStaticLE16(Data, 0x15);
			if (FirstFaces > 0 && FirstVertex >= 0 && FirstVertex < NumVerts && LastVertex >= FirstVertex && LastVertex < NumVerts)
			{
				OutSections.Empty(MaterialCount);
				int RunningFirstIndex = 0;
				FStaticMeshSection* First = new (OutSections) FStaticMeshSection;
				memset(First, 0, sizeof(FStaticMeshSection));
				First->FirstIndex  = RunningFirstIndex;
				First->FirstVertex = FirstVertex;
				First->LastVertex  = LastVertex;
				First->fE          = FirstFaces;
				First->NumFaces    = FirstFaces;
				RunningFirstIndex += FirstFaces * 3;

				for (int i = 0; i < LaterSections.Num(); i++)
				{
					FStaticMeshSection* Section = new (OutSections) FStaticMeshSection;
					memset(Section, 0, sizeof(FStaticMeshSection));
					Section->FirstIndex  = RunningFirstIndex;
					Section->FirstVertex = LaterSections[i].FirstVertex;
					Section->LastVertex  = LaterSections[i].LastVertex;
					Section->fE          = LaterSections[i].NumFaces;
					Section->NumFaces    = LaterSections[i].NumFaces;
					RunningFirstIndex += LaterSections[i].NumFaces * 3;
				}

				if (RunningFirstIndex == NumFaces * 3)
				{
					if (getenv("SCCT_SM_DEBUG"))
						appPrintf("SCCT StaticMesh %s: section header table sections=%d faces=%d\n",
							MeshName, OutSections.Num(), NumFaces);
					return true;
				}
			}
		}
	}

	return false;

	unguard;
}

static int GetSCCTStaticExplicitSectionFaceCount(const byte* Data, int DataSize, int NumVerts, int MaterialCount)
{
	guard(GetSCCTStaticExplicitSectionFaceCount);

	if (MaterialCount <= 1 || MaterialCount > 64 || DataSize <= 0x30 || (int)ReadSCDAStaticLE32(Data, 8) != MaterialCount)
		return 0;

	const int FirstFaces = ReadSCDAStaticLE16(Data, 0x17);
	const int FirstVertex = ReadSCDAStaticLE16(Data, 0x13);
	const int LastVertex  = ReadSCDAStaticLE16(Data, 0x15);
	if (FirstFaces <= 0 || FirstVertex < 0 || FirstVertex >= NumVerts || LastVertex < FirstVertex || LastVertex >= NumVerts)
		return 0;

	int TotalFaces = FirstFaces;
	int SearchPos = 0x20;
	for (int i = 1; i < MaterialCount; i++)
	{
		int Rec = -1;
		const int SearchEnd = min(DataSize - 10, SearchPos + 0x28);
		for (int Probe = SearchPos; Probe <= SearchEnd; Probe++)
		{
			const int FirstVertex2 = ReadSCDAStaticLE16(Data, Probe + 2);
			const int LastVertex2  = ReadSCDAStaticLE16(Data, Probe + 4);
			const int CountA       = ReadSCDAStaticLE16(Data, Probe + 6);
			const int CountB       = ReadSCDAStaticLE16(Data, Probe + 8);
			if (CountA > 0 && CountA == CountB &&
				FirstVertex2 >= 0 && FirstVertex2 < NumVerts &&
				LastVertex2 >= FirstVertex2 && LastVertex2 < NumVerts)
			{
				Rec = Probe;
				TotalFaces += CountA;
				break;
			}
		}
		if (Rec < 0)
			return 0;
		SearchPos = Rec + 10;
	}

	return TotalFaces;

	unguard;
}

static bool FindSCCTStaticByteFaceBlock(const byte* Data, int DataSize, int NumVerts, int TargetFaces, TArray<uint16>& OutFaces, int& OutOffset)
{
	guard(FindSCCTStaticByteFaceBlock);

	OutOffset = -1;
	if (TargetFaces < 8 || DataSize < TargetFaces * 4)
		return false;

	for (int Offset = 0; Offset + TargetFaces * 4 <= DataSize; Offset++)
	{
		bool bValid = true;
		for (int i = 0; i < TargetFaces; i++)
		{
			const int Pos = Offset + i * 4;
			const int I0  = Data[Pos + 0];
			const int I1  = Data[Pos + 1];
			const int I2  = Data[Pos + 2];
			if (I0 >= NumVerts || I1 >= NumVerts || I2 >= NumVerts)
			{
				bValid = false;
				break;
			}
			if (I0 == I1 || I1 == I2 || I2 == I0)
			{
				bValid = false;
				break;
			}
		}
		if (!bValid)
			continue;

		OutFaces.Empty(TargetFaces * 3);
		OutFaces.AddZeroed(TargetFaces * 3);
		for (int i = 0; i < TargetFaces; i++)
		{
			const int Pos = Offset + i * 4;
			OutFaces[i * 3 + 0] = Data[Pos + 0];
			OutFaces[i * 3 + 1] = Data[Pos + 1];
			OutFaces[i * 3 + 2] = Data[Pos + 2];
		}
		OutOffset = Offset;
		return true;
	}

	return false;

	unguard;
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

static void DumpSCDAInlineStaticPacketSummary(const char* MeshName, const byte* Data, int DataSize)
{
	guard(DumpSCDAInlineStaticPacketSummary);
	const char* DebugPackets = getenv("SCDA_STATIC_PACKET_DEBUG");
	if (!DebugPackets || !strcmp(DebugPackets, "0") || !Data || DataSize <= 0)
		return;

	const char* DumpPath = getenv("SCDA_STATIC_PACKET_DUMP");
	if (!DumpPath || !DumpPath[0])
		DumpPath = "scda_inline_static_packet.bin";
	if (DumpPath && DumpPath[0])
	{
		FILE* F = fopen(DumpPath, "wb");
		if (F)
		{
			fwrite(Data, 1, DataSize, F);
			fclose(F);
			appPrintf("SCDA inline static packet dump %s -> %s size=%X\n", MeshName, DumpPath, DataSize);
		}
		else
		{
			appPrintf("SCDA inline static packet dump failed %s -> %s\n", MeshName, DumpPath);
		}
	}

	int Printable = 0;
	int Zeroes = 0;
	for (int i = 0; i < DataSize; i++)
	{
		if ((Data[i] >= 32 && Data[i] < 127) || Data[i] == '\n' || Data[i] == '\r' || Data[i] == '\t')
			Printable++;
		if (!Data[i])
			Zeroes++;
	}

	int PositionOpcodeCount = 0;
	int SanePositionOpcodeCount = 0;
	int VertexAssemblyCount = 0;
	for (int Pos = 0; Pos + 16 <= DataSize; Pos++)
	{
		if (Data[Pos] == 0x11 && Data[Pos + 1] == 0x3A && Data[Pos + 2] == 0x01)
		{
			PositionOpcodeCount++;
			float X = ReadSCDAStaticFloat(Data, Pos + 3);
			float Y = ReadSCDAStaticFloat(Data, Pos + 7);
			float Z = ReadSCDAStaticFloat(Data, Pos + 11);
			if (X == X && Y == Y && Z == Z &&
				fabs(X) < 100000.0f && fabs(Y) < 100000.0f && fabs(Z) < 100000.0f &&
				fabs(X) + fabs(Y) + fabs(Z) > 1.0f)
			{
				SanePositionOpcodeCount++;
			}
		}
		if (Data[Pos] == 0x30 && Data[Pos + 1] == 0x22 &&
			Data[Pos + 6] == 0x10 && Data[Pos + 7] == 0x22 &&
			Data[Pos + 12] == 0x2F && Data[Pos + 13] == 0x59)
		{
			const int ExtraCount = Data[Pos + 15];
			if (ExtraCount >= 1 && ExtraCount <= 8 && Data[Pos + 14] == ExtraCount * 4 + 1 &&
				Pos + 16 + ExtraCount * 4 <= DataSize)
			{
				VertexAssemblyCount++;
			}
		}
	}

	const char* Shape = "unknown";
	if (Printable * 100 / DataSize >= 65 && SanePositionOpcodeCount < 4 && VertexAssemblyCount < 4)
		Shape = "metadata/ascii";
	else if (SanePositionOpcodeCount >= 16 && VertexAssemblyCount >= 4)
		Shape = "command-packet";
	else if (Printable * 100 / DataSize < 45)
		Shape = "dense-packed";

	appPrintf("SCDA inline static packet %s size=%X shape=%s printable=%d%% zero=%d%% posOps=%d sanePos=%d asmOps=%d first32=",
		MeshName, DataSize, Shape, Printable * 100 / DataSize, Zeroes * 100 / DataSize,
		PositionOpcodeCount, SanePositionOpcodeCount, VertexAssemblyCount);
	for (int i = 0; i < min(DataSize, 32); i++)
		appPrintf("%s%02X", i ? " " : "", Data[i]);
	appPrintf("\n");

	if (SanePositionOpcodeCount >= 4)
	{
		int Printed = 0;
		for (int Pos = 0; Pos + 16 <= DataSize && Printed < 12; Pos++)
		{
			if (Data[Pos] != 0x11 || Data[Pos + 1] != 0x3A || Data[Pos + 2] != 0x01)
				continue;
			float X = ReadSCDAStaticFloat(Data, Pos + 3);
			float Y = ReadSCDAStaticFloat(Data, Pos + 7);
			float Z = ReadSCDAStaticFloat(Data, Pos + 11);
			if (!(X == X && Y == Y && Z == Z) ||
				fabs(X) >= 100000.0f || fabs(Y) >= 100000.0f || fabs(Z) >= 100000.0f ||
				fabs(X) + fabs(Y) + fabs(Z) <= 1.0f)
				continue;

			int AttrA = -1;
			int AttrB = -1;
			int AttrC = -1;
			if (Pos >= 10 && Data[Pos - 10] == 0x15 && Data[Pos - 9] == 0x22)
				AttrB = (int)ReadSCDAStaticLE32(Data, Pos - 8);
			if (Pos >= 13 && Data[Pos - 13] == 0x10 && Data[Pos - 12] == 0x05)
				AttrA = Data[Pos - 11];
			if (Pos >= 4 && Data[Pos - 4] == 0x13 && Data[Pos - 3] == 0x01)
				AttrC = ReadSCDAStaticLE16(Data, Pos - 2);
			appPrintf("  pos-op @%X attrA=%d attrB=%d attrC=%d xyz=(%g,%g,%g) after=%02X %02X %02X %02X\n",
				Pos, AttrA, AttrB, AttrC, X, Y, Z,
				(Pos + 15 < DataSize) ? Data[Pos + 15] : 0,
				(Pos + 16 < DataSize) ? Data[Pos + 16] : 0,
				(Pos + 17 < DataSize) ? Data[Pos + 17] : 0,
				(Pos + 18 < DataSize) ? Data[Pos + 18] : 0);
			Printed++;
		}
	}

	{
		TArray<FVector> OpcodePoints[256];
		int FirstOpcodePos[256];
		for (int i = 0; i < 256; i++)
			FirstOpcodePos[i] = -1;

		for (int Pos = 0; Pos + 16 <= DataSize; Pos++)
		{
			if (Data[Pos + 1] != 0x3A || Data[Pos + 2] != 0x01)
				continue;

			FVector V;
			V.X = ReadSCDAStaticFloat(Data, Pos + 3);
			V.Y = ReadSCDAStaticFloat(Data, Pos + 7);
			V.Z = ReadSCDAStaticFloat(Data, Pos + 11);
			if (V.X != V.X || V.Y != V.Y || V.Z != V.Z ||
				fabs(V.X) >= 100000.0f || fabs(V.Y) >= 100000.0f || fabs(V.Z) >= 100000.0f ||
				fabs(V.X) + fabs(V.Y) + fabs(V.Z) <= 10.0f)
			{
				continue;
			}

			const int Opcode = Data[Pos];
			if (FirstOpcodePos[Opcode] < 0)
				FirstOpcodePos[Opcode] = Pos;
			new (OpcodePoints[Opcode]) FVector(V);
		}

		for (int Opcode = 0; Opcode < 256; Opcode++)
		{
			TArray<FVector>& Points = OpcodePoints[Opcode];
			if (Points.Num() < 4)
				continue;

			FVector MinV, MaxV;
			MinV.Set( 1.0e30f,  1.0e30f,  1.0e30f);
			MaxV.Set(-1.0e30f, -1.0e30f, -1.0e30f);
			for (int i = 0; i < Points.Num(); i++)
			{
				MinV.X = min(MinV.X, Points[i].X); MinV.Y = min(MinV.Y, Points[i].Y); MinV.Z = min(MinV.Z, Points[i].Z);
				MaxV.X = max(MaxV.X, Points[i].X); MaxV.Y = max(MaxV.Y, Points[i].Y); MaxV.Z = max(MaxV.Z, Points[i].Z);
			}

			const float MirrorCenterX = (MinV.X + MaxV.X) * 0.5f;
			int MirrorMatches = 0;
			for (int i = 0; i < Points.Num(); i++)
			{
				const FVector& P = Points[i];
				const float MirrorX = MirrorCenterX * 2.0f - P.X;
				bool bMatched = false;
				for (int j = 0; j < Points.Num(); j++)
				{
					if (i == j)
						continue;
					const FVector& Q = Points[j];
					if (fabs(Q.X - MirrorX) <= 48.0f &&
						fabs(Q.Y - P.Y) <= 24.0f &&
						fabs(Q.Z - P.Z) <= 24.0f)
					{
						bMatched = true;
						break;
					}
				}
				if (bMatched)
					MirrorMatches++;
			}

			appPrintf("  xyz-op opcode=%02X count=%d first=%X mirrorX=%d/%d %.1f%% centerX=%g bounds=(%g,%g,%g)-(%g,%g,%g)\n",
				Opcode, Points.Num(), FirstOpcodePos[Opcode],
				MirrorMatches, Points.Num(), Points.Num() ? MirrorMatches * 100.0f / Points.Num() : 0.0f,
				MirrorCenterX, MinV.X, MinV.Y, MinV.Z, MaxV.X, MaxV.Y, MaxV.Z);
		}
	}

	if (VertexAssemblyCount >= 4)
	{
		int Printed = 0;
		for (int Pos = 0; Pos + 16 <= DataSize && Printed < 16; Pos++)
		{
			if (Data[Pos] != 0x30 || Data[Pos + 1] != 0x22 ||
				Data[Pos + 6] != 0x10 || Data[Pos + 7] != 0x22 ||
				Data[Pos + 12] != 0x2F || Data[Pos + 13] != 0x59)
				continue;
			const int ExtraCount = Data[Pos + 15];
			if (ExtraCount < 1 || ExtraCount > 8 || Data[Pos + 14] != ExtraCount * 4 + 1 ||
				Pos + 16 + ExtraCount * 4 > DataSize)
				continue;
			appPrintf("  asm-op @%X refs=%u,%u",
				Pos, ReadSCDAStaticLE32(Data, Pos + 2), ReadSCDAStaticLE32(Data, Pos + 8));
			for (int i = 0; i < ExtraCount; i++)
				appPrintf(",%u", ReadSCDAStaticLE32(Data, Pos + 16 + i * 4));
			appPrintf("\n");
			Printed++;
		}
	}

	struct FMarker
	{
		int Pos;
		byte Kind;
	};
	TArray<FMarker> Markers;
	for (int Pos = 0; Pos + 4 <= DataSize; Pos++)
	{
		if (Data[Pos + 1] == 0x41 && Data[Pos + 2] == 0x50 && Data[Pos + 3] == 0x00 &&
			(Data[Pos] == 0x70 || Data[Pos] == 0xF0))
		{
			FMarker& M = Markers[Markers.AddDefaulted()];
			M.Pos = Pos;
			M.Kind = Data[Pos];
		}
	}

	for (int i = 0; i < Markers.Num(); )
	{
		const int RunStart = Markers[i].Pos;
		const byte Kind = Markers[i].Kind;
		int RunEnd = RunStart;
		int Count = 0;
		while (i < Markers.Num() && Markers[i].Kind == Kind &&
			(Count == 0 || Markers[i].Pos - RunEnd >= 0x10 && Markers[i].Pos - RunEnd <= 0x40))
		{
			RunEnd = Markers[i].Pos;
			Count++;
			i++;
		}
		appPrintf("  AP-run kind=%02X first=%X last=%X count=%d\n", Kind, RunStart, RunEnd, Count);
	}

	for (int Pos = 0; Pos + 3 <= DataSize; )
	{
		int Count = 0;
		int MaxValue = 0;
		int Zeroes = 0;
		for (int P = Pos; P + 3 <= DataSize; P += 3)
		{
			const int Value = (Data[P] << 16) | (Data[P + 1] << 8) | Data[P + 2];
			if (Value >= 5000)
				break;
			MaxValue = max(MaxValue, Value);
			if (!Value)
				Zeroes++;
			Count++;
			if (Count >= 4096)
				break;
		}
		if (Count >= 32)
		{
			appPrintf("  be24-island pos=%X count=%d max=%d zeroes=%d first=",
				Pos, Count, MaxValue, Zeroes);
			for (int i = 0; i < min(Count, 12); i++)
			{
				const int P = Pos + i * 3;
				const int Value = (Data[P] << 16) | (Data[P + 1] << 8) | Data[P + 2];
				appPrintf("%s%d", i ? "," : "", Value);
			}
			appPrintf("\n");
			Pos += Count * 3;
			continue;
		}
		Pos++;
	}
	unguard;
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

static int SCDAStaticSignExtend(int Value, int Bits)
{
	const int Sign = 1 << (Bits - 1);
	const int Mask = (1 << Bits) - 1;
	Value &= Mask;
	return (Value & Sign) ? Value - (1 << Bits) : Value;
}

static FVector ReadSCDAStaticPacked10Vector(const byte* Data, int Pos)
{
	unsigned W = Data[Pos + 0] | (Data[Pos + 1] << 8) | (Data[Pos + 2] << 16) | (Data[Pos + 3] << 24);
	FVector V;
	V.X = (float)SCDAStaticSignExtend((int)(W >> 0), 10);
	V.Y = (float)SCDAStaticSignExtend((int)(W >> 10), 10);
	V.Z = (float)SCDAStaticSignExtend((int)(W >> 20), 10);
	return V;
}

static bool FindSCDAInlineStaticDescriptorSpan(const byte* Data, int DataSize, int& OutStart, int& OutEnd)
{
	guard(FindSCDAInlineStaticDescriptorSpan);
	OutStart = DataSize;
	OutEnd = 0;

	int BestRunStart = -1;
	int BestRunCount = 0;
	for (int Pos = 0; Pos + 25 <= DataSize; Pos++)
	{
		int Count = 0;
		while (Pos + Count * 25 + 20 <= DataSize)
		{
			const int Rec = Pos + Count * 25;
			const unsigned Ptr = Data[Rec + 0] | (Data[Rec + 1] << 8) | (Data[Rec + 2] << 16) | (Data[Rec + 3] << 24);
			const unsigned Type = (Data[Rec + 4] << 24) | (Data[Rec + 5] << 16) | (Data[Rec + 6] << 8) | Data[Rec + 7];
			const unsigned Off = (Data[Rec + 8] << 24) | (Data[Rec + 9] << 16) | (Data[Rec + 10] << 8) | Data[Rec + 11];
			const unsigned Size = (Data[Rec + 16] << 24) | (Data[Rec + 17] << 16) | (Data[Rec + 18] << 8) | Data[Rec + 19];
			if (Ptr != 0x00504170 || Type != 2 || Off >= (unsigned)DataSize || Size == 0 || Off + Size > (unsigned)DataSize)
				break;
			Count++;
		}
		if (Count > BestRunCount)
		{
			BestRunStart = Pos;
			BestRunCount = Count;
		}
	}

	if (BestRunCount < 4)
		return false;

	for (int i = 0; i < BestRunCount; i++)
	{
		const int Rec = BestRunStart + i * 25;
		const int Off = (Data[Rec + 8] << 24) | (Data[Rec + 9] << 16) | (Data[Rec + 10] << 8) | Data[Rec + 11];
		const int Size = (Data[Rec + 16] << 24) | (Data[Rec + 17] << 16) | (Data[Rec + 18] << 8) | Data[Rec + 19];
		OutStart = min(OutStart, Off);
		OutEnd = max(OutEnd, Off + Size);
	}

	if (OutStart < 0 || OutEnd <= OutStart || OutEnd > DataSize)
		return false;

	if (SCDAStaticDebugEnabled())
		appPrintf("SCDA inline descriptor span: table=%X count=%d data=%X-%X\n",
			BestRunStart, BestRunCount, OutStart, OutEnd);
	return true;
	unguard;
}

static bool SerializeDoubleAgentInlineStaticCommandPacket(UStaticMesh* Mesh, const byte* Data, int DataSize)
{
	guard(SerializeDoubleAgentInlineStaticCommandPacket);

	if (getenv("SCDA_INLINE_STATIC_LOCAL_BLOCKS"))
	{
		TArray<FVector> LocalPoints;
		TArray<uint16> LocalIndices;

		for (int Start = 0; Start + 36 <= DataSize; Start++)
		{
			TArray<FVector> RunPoints;
			for (int Pos = Start; Pos + 12 <= DataSize; Pos += 12)
			{
				FVector V;
				V.X = ReadSCDAStaticFloat(Data, Pos + 0);
				V.Y = ReadSCDAStaticFloat(Data, Pos + 4);
				V.Z = ReadSCDAStaticFloat(Data, Pos + 8);
				const float MaxAbs = max(max(fabs(V.X), fabs(V.Y)), fabs(V.Z));
				if (V.X != V.X || V.Y != V.Y || V.Z != V.Z ||
					MaxAbs < 0.001f || MaxAbs > 100.0f)
				{
					break;
				}
				new (RunPoints) FVector(V);
				if (RunPoints.Num() > 256)
					break;
			}
			if (RunPoints.Num() < 3)
				continue;

			const int RunEnd = Start + RunPoints.Num() * 12;
			int BestFaceStart = -1;
			int BestFaceCount = 0;
			for (int Probe = RunEnd; Probe < min(DataSize - 4, RunEnd + 32); Probe++)
			{
				int FaceCount = 0;
				for (int Pos = Probe; Pos + 4 <= DataSize; Pos += 4)
				{
					const int A = Data[Pos + 0];
					const int B = Data[Pos + 1];
					const int C = Data[Pos + 2];
					if (Data[Pos + 3] != 0 ||
						A >= RunPoints.Num() || B >= RunPoints.Num() || C >= RunPoints.Num() ||
						A == B || B == C || A == C)
					{
						break;
					}
					FaceCount++;
					if (FaceCount > 2048)
						break;
				}
				if (FaceCount > BestFaceCount)
				{
					BestFaceCount = FaceCount;
					BestFaceStart = Probe;
				}
			}
			if (BestFaceCount < 2)
				continue;

			const int BaseVertex = LocalPoints.Num();
			for (int i = 0; i < RunPoints.Num(); i++)
				new (LocalPoints) FVector(RunPoints[i]);
			for (int i = 0; i < BestFaceCount; i++)
			{
				const int Pos = BestFaceStart + i * 4;
				new (LocalIndices) uint16(BaseVertex + Data[Pos + 0]);
				new (LocalIndices) uint16(BaseVertex + Data[Pos + 1]);
				new (LocalIndices) uint16(BaseVertex + Data[Pos + 2]);
			}

			if (SCDAStaticDebugEnabled())
				appPrintf("SCDA inline local block %s: verts=%d faces=%d vertexStart=%X faceStart=%X\n",
					Mesh->Name, RunPoints.Num(), BestFaceCount, Start, BestFaceStart);

			Start = RunEnd - 1;
		}

		if (LocalPoints.Num() >= 3 && LocalIndices.Num() >= 3)
		{
			Mesh->VertexStream.Vert.Empty(LocalPoints.Num());
			Mesh->VertexStream.Vert.AddZeroed(LocalPoints.Num());
			FStaticMeshUVStream* UV = new (Mesh->UVStream) FStaticMeshUVStream;
			UV->Data.Empty(LocalPoints.Num());
			UV->Data.AddZeroed(LocalPoints.Num());
			for (int i = 0; i < LocalPoints.Num(); i++)
			{
				Mesh->VertexStream.Vert[i].Pos = LocalPoints[i];
				Mesh->VertexStream.Vert[i].Normal.Set(0, 0, 1);
				UV->Data[i].U = 0.0f;
				UV->Data[i].V = 0.0f;
			}

			Mesh->IndexStream1.Indices.Empty(LocalIndices.Num());
			Mesh->IndexStream1.Indices.AddZeroed(LocalIndices.Num());
			for (int i = 0; i < LocalIndices.Num(); i++)
				Mesh->IndexStream1.Indices[i] = LocalIndices[i];

			for (int i = 0; i + 2 < LocalIndices.Num(); i += 3)
			{
				const int A = LocalIndices[i + 0];
				const int B = LocalIndices[i + 1];
				const int C = LocalIndices[i + 2];
				FVector AB, AC;
				AB.Set(LocalPoints[B].X - LocalPoints[A].X, LocalPoints[B].Y - LocalPoints[A].Y, LocalPoints[B].Z - LocalPoints[A].Z);
				AC.Set(LocalPoints[C].X - LocalPoints[A].X, LocalPoints[C].Y - LocalPoints[A].Y, LocalPoints[C].Z - LocalPoints[A].Z);
				const FVector N = SCDAStaticCross(AB, AC);
				Mesh->VertexStream.Vert[A].Normal.Add(N);
				Mesh->VertexStream.Vert[B].Normal.Add(N);
				Mesh->VertexStream.Vert[C].Normal.Add(N);
			}
			for (int i = 0; i < LocalPoints.Num(); i++)
				SCDAStaticNormalize(Mesh->VertexStream.Vert[i].Normal);

			FStaticMeshSection* Section = new (Mesh->Sections) FStaticMeshSection;
			memset(Section, 0, sizeof(FStaticMeshSection));
			Section->FirstIndex  = 0;
			Section->FirstVertex = 0;
			Section->LastVertex  = LocalPoints.Num() - 1;
			Section->fE          = LocalIndices.Num() / 3;
			Section->NumFaces    = LocalIndices.Num() / 3;

			if (SCDAStaticDebugEnabled())
				appPrintf("SCDA inline local block mesh %s: verts=%d tris=%d\n",
					Mesh->Name, LocalPoints.Num(), LocalIndices.Num() / 3);

			Mesh->ConvertMesh();
			return true;
		}
	}

	int VertexOpcode = 0x11;
	const char* VertexOpcodeEnv = getenv("SCDA_INLINE_STATIC_VERTEX_OPCODE");
	if (VertexOpcodeEnv && VertexOpcodeEnv[0])
	{
		VertexOpcode = (int)strtol(VertexOpcodeEnv, NULL, 0);
		VertexOpcode &= 0xFF;
	}
	float MinAbsPosition = 1.0f;
	const char* MinAbsEnv = getenv("SCDA_INLINE_STATIC_VERTEX_MIN_ABS");
	if (MinAbsEnv && MinAbsEnv[0])
		MinAbsPosition = (float)atof(MinAbsEnv);

	TArray<FVector> Points;
	int FirstPositionOpcode = -1;
	for (int Pos = 0; Pos + 16 <= DataSize; Pos++)
	{
		if (Data[Pos] != VertexOpcode || Data[Pos + 1] != 0x3A || Data[Pos + 2] != 0x01)
			continue;
		FVector V;
		V.X = ReadSCDAStaticFloat(Data, Pos + 3);
		V.Y = ReadSCDAStaticFloat(Data, Pos + 7);
		V.Z = ReadSCDAStaticFloat(Data, Pos + 11);
		if (V.X != V.X || V.Y != V.Y || V.Z != V.Z ||
			fabs(V.X) >= 100000.0f || fabs(V.Y) >= 100000.0f || fabs(V.Z) >= 100000.0f ||
			fabs(V.X) + fabs(V.Y) + fabs(V.Z) <= MinAbsPosition)
		{
			continue;
		}
		if (FirstPositionOpcode < 0)
			FirstPositionOpcode = Pos;
		new (Points) FVector(V);
	}
	if (Points.Num() < 3)
		return false;

	{
	const float MarkerSize = 8.0f;
	Mesh->VertexStream.Vert.Empty(Points.Num() * 3);
	Mesh->VertexStream.Vert.AddZeroed(Points.Num() * 3);
	FStaticMeshUVStream* CloudUV = new (Mesh->UVStream) FStaticMeshUVStream;
	CloudUV->Data.Empty(Points.Num() * 3);
	CloudUV->Data.AddZeroed(Points.Num() * 3);
	Mesh->IndexStream1.Indices.Empty(Points.Num() * 3);
	Mesh->IndexStream1.Indices.AddZeroed(Points.Num() * 3);

	FVector MinV, MaxV;
	MinV.Set( 1.0e30f,  1.0e30f,  1.0e30f);
	MaxV.Set(-1.0e30f, -1.0e30f, -1.0e30f);
	for (int i = 0; i < Points.Num(); i++)
	{
		MinV.X = min(MinV.X, Points[i].X); MinV.Y = min(MinV.Y, Points[i].Y); MinV.Z = min(MinV.Z, Points[i].Z);
		MaxV.X = max(MaxV.X, Points[i].X); MaxV.Y = max(MaxV.Y, Points[i].Y); MaxV.Z = max(MaxV.Z, Points[i].Z);
		const int Base = i * 3;
		Mesh->VertexStream.Vert[Base + 0].Pos.Set(Points[i].X - MarkerSize, Points[i].Y, Points[i].Z);
		Mesh->VertexStream.Vert[Base + 1].Pos.Set(Points[i].X + MarkerSize, Points[i].Y, Points[i].Z);
		Mesh->VertexStream.Vert[Base + 2].Pos.Set(Points[i].X, Points[i].Y + MarkerSize, Points[i].Z);
		for (int j = 0; j < 3; j++)
		{
			Mesh->VertexStream.Vert[Base + j].Normal.Set(0, 0, 1);
			Mesh->IndexStream1.Indices[Base + j] = Base + j;
		}
		CloudUV->Data[Base + 0].U = 0.0f; CloudUV->Data[Base + 0].V = 0.0f;
		CloudUV->Data[Base + 1].U = 1.0f; CloudUV->Data[Base + 1].V = 0.0f;
		CloudUV->Data[Base + 2].U = 0.5f; CloudUV->Data[Base + 2].V = 1.0f;
	}

	FStaticMeshSection* CloudSection = new (Mesh->Sections) FStaticMeshSection;
	memset(CloudSection, 0, sizeof(FStaticMeshSection));
	CloudSection->FirstIndex  = 0;
	CloudSection->FirstVertex = 0;
	CloudSection->LastVertex  = Mesh->VertexStream.Vert.Num() - 1;
	CloudSection->fE          = Points.Num();
	CloudSection->NumFaces    = Points.Num();

	if (SCDAStaticDebugEnabled())
		appPrintf("SCDA inline command vertex cloud %s: opcode=%02X minAbs=%g records=%d markerVerts=%d markerTris=%d firstRecord=%X firstPos=%X bounds=(%g,%g,%g)-(%g,%g,%g)\n",
			Mesh->Name, VertexOpcode, MinAbsPosition, Points.Num(), Mesh->VertexStream.Vert.Num(), Mesh->IndexStream1.Indices.Num() / 3,
			FirstPositionOpcode >= 0 ? FirstPositionOpcode - 0x40 : -1, FirstPositionOpcode,
			MinV.X, MinV.Y, MinV.Z, MaxV.X, MaxV.Y, MaxV.Z);

	Mesh->ConvertMesh();
	return true;
	}

	TArray<uint16> Indices;
	for (int Pos = 0; Pos + 16 <= DataSize; Pos++)
	{
		if (Data[Pos] != 0x30 || Data[Pos + 1] != 0x22 ||
			Data[Pos + 6] != 0x10 || Data[Pos + 7] != 0x22 ||
			Data[Pos + 12] != 0x2F || Data[Pos + 13] != 0x59)
		{
			continue;
		}
		const int ExtraCount = Data[Pos + 15];
		if (ExtraCount < 1 || ExtraCount > 8 || Data[Pos + 14] != ExtraCount * 4 + 1 ||
			Pos + 16 + ExtraCount * 4 > DataSize)
		{
			continue;
		}

		int Refs[10];
		int RefCount = 0;
		Refs[RefCount++] = ReadSCDAStaticLE32(Data, Pos + 2);
		Refs[RefCount++] = ReadSCDAStaticLE32(Data, Pos + 8);
		for (int i = 0; i < ExtraCount && RefCount < ARRAY_COUNT(Refs); i++)
			Refs[RefCount++] = ReadSCDAStaticLE32(Data, Pos + 16 + i * 4);

		bool bValid = true;
		for (int i = 0; i < RefCount; i++)
		{
			if (Refs[i] < 0 || Refs[i] >= Points.Num())
			{
				bValid = false;
				break;
			}
		}
		if (!bValid || RefCount < 3)
			continue;

		for (int i = 2; i < RefCount; i++)
		{
			const int A = Refs[0];
			const int B = Refs[i - 1];
			const int C = Refs[i];
			if (A == B || B == C || A == C)
				continue;
			new (Indices) uint16(A);
			new (Indices) uint16(B);
			new (Indices) uint16(C);
		}
	}

	if (Indices.Num() < 3)
	{
		for (int i = 0; i + 2 < Points.Num(); i += 3)
		{
			new (Indices) uint16(i + 0);
			new (Indices) uint16(i + 1);
			new (Indices) uint16(i + 2);
		}
	}
	if (Indices.Num() < 3)
		return false;

	Mesh->VertexStream.Vert.Empty(Points.Num());
	Mesh->VertexStream.Vert.AddZeroed(Points.Num());
	FStaticMeshUVStream* UV = new (Mesh->UVStream) FStaticMeshUVStream;
	UV->Data.Empty(Points.Num());
	UV->Data.AddZeroed(Points.Num());

	FVector MinV, MaxV;
	MinV.Set( 1.0e30f,  1.0e30f,  1.0e30f);
	MaxV.Set(-1.0e30f, -1.0e30f, -1.0e30f);
	for (int i = 0; i < Points.Num(); i++)
	{
		MinV.X = min(MinV.X, Points[i].X); MinV.Y = min(MinV.Y, Points[i].Y); MinV.Z = min(MinV.Z, Points[i].Z);
		MaxV.X = max(MaxV.X, Points[i].X); MaxV.Y = max(MaxV.Y, Points[i].Y); MaxV.Z = max(MaxV.Z, Points[i].Z);
	}
	const float SizeX = max(MaxV.X - MinV.X, 0.001f);
	const float SizeY = max(MaxV.Y - MinV.Y, 0.001f);

	for (int i = 0; i < Points.Num(); i++)
	{
		FStaticMeshVertex& Vtx = Mesh->VertexStream.Vert[i];
		Vtx.Pos = Points[i];
		Vtx.Normal.Set(0, 0, 0);
		UV->Data[i].U = (Points[i].X - MinV.X) / SizeX;
		UV->Data[i].V = (Points[i].Y - MinV.Y) / SizeY;
	}

	Mesh->IndexStream1.Indices.Empty(Indices.Num());
	Mesh->IndexStream1.Indices.AddZeroed(Indices.Num());
	for (int i = 0; i < Indices.Num(); i++)
		Mesh->IndexStream1.Indices[i] = Indices[i];

	for (int i = 0; i + 2 < Mesh->IndexStream1.Indices.Num(); i += 3)
	{
		const int A = Mesh->IndexStream1.Indices[i + 0];
		const int B = Mesh->IndexStream1.Indices[i + 1];
		const int C = Mesh->IndexStream1.Indices[i + 2];
		if (A < 0 || A >= Points.Num() || B < 0 || B >= Points.Num() || C < 0 || C >= Points.Num())
			continue;
		FVector AB, AC;
		AB.Set(Points[B].X - Points[A].X, Points[B].Y - Points[A].Y, Points[B].Z - Points[A].Z);
		AC.Set(Points[C].X - Points[A].X, Points[C].Y - Points[A].Y, Points[C].Z - Points[A].Z);
		const FVector N = SCDAStaticCross(AB, AC);
		Mesh->VertexStream.Vert[A].Normal.Add(N);
		Mesh->VertexStream.Vert[B].Normal.Add(N);
		Mesh->VertexStream.Vert[C].Normal.Add(N);
	}
	for (int i = 0; i < Points.Num(); i++)
		SCDAStaticNormalize(Mesh->VertexStream.Vert[i].Normal);

	FStaticMeshSection* Section = new (Mesh->Sections) FStaticMeshSection;
	memset(Section, 0, sizeof(FStaticMeshSection));
	Section->FirstIndex  = 0;
	Section->FirstVertex = 0;
	Section->LastVertex  = Points.Num() - 1;
	Section->fE          = Mesh->IndexStream1.Indices.Num() / 3;
	Section->NumFaces    = Mesh->IndexStream1.Indices.Num() / 3;

	if (SCDAStaticDebugEnabled())
		appPrintf("SCDA inline command mesh %s: verts=%d tris=%d bounds=(%g,%g,%g)-(%g,%g,%g)\n",
			Mesh->Name, Points.Num(), Mesh->IndexStream1.Indices.Num() / 3,
			MinV.X, MinV.Y, MinV.Z, MaxV.X, MaxV.Y, MaxV.Z);

	Mesh->ConvertMesh();
	return true;
	unguard;
}

static bool SerializeDoubleAgentInlineStaticMeshPacket(UStaticMesh* Mesh, const byte* Data, int DataSize)
{
	guard(SerializeDoubleAgentInlineStaticMeshPacket);

	struct FSCDAInlineStaticSectionCandidate
	{
		int HeaderStart;
		int IndexStart;
		int PositionStart;
		int Count;
		int PackedBytes;
		int PositionBytes;
		FVector MinV;
		FVector MaxV;
	};
	TArray<FSCDAInlineStaticSectionCandidate> InlineSections;
	for (int Pos = 0; Pos + 32 < DataSize; Pos++)
	{
		if (Data[Pos] != 1)
			continue;
		const int Count = ReadSCDAStaticLE16(Data, Pos + 1);
		const int PackedBytes = ReadSCDAStaticLE16(Data, Pos + 3);
		const int PositionBytes = ReadSCDAStaticLE16(Data, Pos + 5);
		if (Count < 3 || Count > 96 || PositionBytes != Count * 12 || PackedBytes < 0 || PackedBytes > 0x1000)
			continue;

		const int IndexStart = Pos + 7;
		if (IndexStart + Count + PackedBytes + PositionBytes > DataSize)
			continue;

		int ValidLocalIndices = 0;
		for (int i = 0; i < Count; i++)
		{
			if (Data[IndexStart + i] < Count)
				ValidLocalIndices++;
		}
		if (ValidLocalIndices < (Count * 3) / 4)
			continue;

		const int BasePositionStart = IndexStart + Count + PackedBytes;
		int BestPositionStart = -1;
		float BestScore = -1.0e30f;
		FVector BestMin, BestMax;
		for (int Align = 0; Align < 12; Align++)
		{
			const int PositionStart = BasePositionStart + Align;
			if (PositionStart + PositionBytes > DataSize)
				continue;

			int SanePoints = 0;
			FVector MinV, MaxV;
			MinV.Set( 1.0e30f,  1.0e30f,  1.0e30f);
			MaxV.Set(-1.0e30f, -1.0e30f, -1.0e30f);
			for (int i = 0; i < Count; i++)
			{
				const int P = PositionStart + i * 12;
				FVector V;
				V.X = ReadSCDAStaticFloat(Data, P + 0);
				V.Y = ReadSCDAStaticFloat(Data, P + 4);
				V.Z = ReadSCDAStaticFloat(Data, P + 8);
				const float MaxAbs = max(max(fabs(V.X), fabs(V.Y)), fabs(V.Z));
				if (V.X == V.X && V.Y == V.Y && V.Z == V.Z && MaxAbs >= 0.001f && MaxAbs <= 1000.0f)
					SanePoints++;
				MinV.X = min(MinV.X, V.X); MinV.Y = min(MinV.Y, V.Y); MinV.Z = min(MinV.Z, V.Z);
				MaxV.X = max(MaxV.X, V.X); MaxV.Y = max(MaxV.Y, V.Y); MaxV.Z = max(MaxV.Z, V.Z);
			}

			float Span[3] = { MaxV.X - MinV.X, MaxV.Y - MinV.Y, MaxV.Z - MinV.Z };
			if (Span[0] > Span[1]) Exchange(Span[0], Span[1]);
			if (Span[1] > Span[2]) Exchange(Span[1], Span[2]);
			if (Span[0] > Span[1]) Exchange(Span[0], Span[1]);
			if (SanePoints < (Count * 9) / 10 || Span[2] < 1.0f || Span[2] > 500.0f)
				continue;

			const float Score = SanePoints * 1000.0f + Span[2] - Span[0] * 0.1f - (float)Align;
			if (Score > BestScore)
			{
				BestScore = Score;
				BestPositionStart = PositionStart;
				BestMin = MinV;
				BestMax = MaxV;
			}
		}

		if (BestPositionStart < 0)
			continue;

		bool bOverlapsExisting = false;
		for (int i = 0; i < InlineSections.Num(); i++)
		{
			const FSCDAInlineStaticSectionCandidate& Other = InlineSections[i];
			const int ThisEnd = BestPositionStart + PositionBytes;
			const int OtherEnd = Other.PositionStart + Other.PositionBytes;
			if (BestPositionStart < OtherEnd && Other.PositionStart < ThisEnd)
			{
				bOverlapsExisting = true;
				break;
			}
		}
		if (bOverlapsExisting)
			continue;

		FSCDAInlineStaticSectionCandidate& Section = InlineSections[InlineSections.AddDefaulted()];
		Section.HeaderStart = Pos;
		Section.IndexStart = IndexStart;
		Section.PositionStart = BestPositionStart;
		Section.Count = Count;
		Section.PackedBytes = PackedBytes;
		Section.PositionBytes = PositionBytes;
		Section.MinV = BestMin;
		Section.MaxV = BestMax;
	}

	int InlineSectionVertexCount = 0;
	for (int i = 0; i < InlineSections.Num(); i++)
		InlineSectionVertexCount += InlineSections[i].Count;
	const bool bSectionChains = getenv("SCDA_INLINE_STATIC_SECTION_CHAINS") != NULL;
	const bool bSectionFans = getenv("SCDA_INLINE_STATIC_SECTION_FANS") != NULL;
	if (InlineSections.Num() >= 3 && InlineSectionVertexCount >= 32 && (bSectionChains || bSectionFans))
	{
		const bool bMirrorX = getenv("SCDA_INLINE_STATIC_MIRROR_X") != NULL;
		const bool bYForwardFromZ = getenv("SCDA_INLINE_STATIC_Y_FORWARD_FROM_Z") != NULL;
		Mesh->VertexStream.Vert.Empty(InlineSectionVertexCount);
		Mesh->VertexStream.Vert.AddZeroed(InlineSectionVertexCount);
		FStaticMeshUVStream* InlineUV = new (Mesh->UVStream) FStaticMeshUVStream;
		InlineUV->Data.Empty(InlineSectionVertexCount);
		InlineUV->Data.AddZeroed(InlineSectionVertexCount);
		Mesh->IndexStream1.Indices.Empty(InlineSectionVertexCount * 3);

		FVector MinV, MaxV;
		MinV.Set( 1.0e30f,  1.0e30f,  1.0e30f);
		MaxV.Set(-1.0e30f, -1.0e30f, -1.0e30f);
		int BaseVertex = 0;
		for (int SectionIndex = 0; SectionIndex < InlineSections.Num(); SectionIndex++)
		{
			const FSCDAInlineStaticSectionCandidate& Source = InlineSections[SectionIndex];
			const int FirstIndex = Mesh->IndexStream1.Indices.Num();
			for (int i = 0; i < Source.Count; i++)
			{
				const int P = Source.PositionStart + i * 12;
				FVector V;
				V.X = ReadSCDAStaticFloat(Data, P + 0);
				V.Y = ReadSCDAStaticFloat(Data, P + 4);
				V.Z = ReadSCDAStaticFloat(Data, P + 8);
				if (bYForwardFromZ)
				{
					FVector T;
					T.Set(V.X, V.Z, V.Y);
					V = T;
				}
				if (bMirrorX)
					V.X = -V.X;
				Mesh->VertexStream.Vert[BaseVertex + i].Pos = V;
				MinV.X = min(MinV.X, V.X); MinV.Y = min(MinV.Y, V.Y); MinV.Z = min(MinV.Z, V.Z);
				MaxV.X = max(MaxV.X, V.X); MaxV.Y = max(MaxV.Y, V.Y); MaxV.Z = max(MaxV.Z, V.Z);
				InlineUV->Data[BaseVertex + i].U = 0.0f;
				InlineUV->Data[BaseVertex + i].V = 0.0f;
			}

			if (bSectionFans)
			{
				int FanCount = Source.Count;
				const FVector& First = Mesh->VertexStream.Vert[BaseVertex].Pos;
				const FVector& Last = Mesh->VertexStream.Vert[BaseVertex + Source.Count - 1].Pos;
				const float DX = Last.X - First.X;
				const float DY = Last.Y - First.Y;
				const float DZ = Last.Z - First.Z;
				if (DX * DX + DY * DY + DZ * DZ < 0.0001f)
					FanCount--;
				for (int i = 1; i + 1 < FanCount; i++)
				{
					int A = 0;
					int B = i;
					int C = i + 1;
					if (bMirrorX)
						Exchange(B, C);
					new (Mesh->IndexStream1.Indices) uint16(BaseVertex + A);
					new (Mesh->IndexStream1.Indices) uint16(BaseVertex + B);
					new (Mesh->IndexStream1.Indices) uint16(BaseVertex + C);
				}
			}
			else
			{
				for (int i = 0; i + 2 < Source.Count; i++)
				{
					int A = Data[Source.IndexStart + i + 0];
					int B = Data[Source.IndexStart + i + 1];
					int C = Data[Source.IndexStart + i + 2];
					if (A >= Source.Count || B >= Source.Count || C >= Source.Count || A == B || A == C || B == C)
						continue;
					if (i & 1)
						Exchange(A, B);
					new (Mesh->IndexStream1.Indices) uint16(BaseVertex + A);
					new (Mesh->IndexStream1.Indices) uint16(BaseVertex + B);
					new (Mesh->IndexStream1.Indices) uint16(BaseVertex + C);
				}
			}

			FStaticMeshSection* OutSection = new (Mesh->Sections) FStaticMeshSection;
			memset(OutSection, 0, sizeof(FStaticMeshSection));
			OutSection->FirstIndex  = FirstIndex;
			OutSection->FirstVertex = BaseVertex;
			OutSection->LastVertex  = BaseVertex + Source.Count - 1;
			OutSection->fE          = (Mesh->IndexStream1.Indices.Num() - FirstIndex) / 3;
			OutSection->NumFaces    = (Mesh->IndexStream1.Indices.Num() - FirstIndex) / 3;

			if (SCDAStaticDebugEnabled())
				appPrintf("SCDA inline section %s: header=%X count=%d packed=%d pos=%X tris=%d bounds=(%g,%g,%g)-(%g,%g,%g)\n",
					Mesh->Name, Source.HeaderStart, Source.Count, Source.PackedBytes, Source.PositionStart,
					OutSection->NumFaces, Source.MinV.X, Source.MinV.Y, Source.MinV.Z, Source.MaxV.X, Source.MaxV.Y, Source.MaxV.Z);
			BaseVertex += Source.Count;
		}

		for (int i = 0; i + 2 < Mesh->IndexStream1.Indices.Num(); i += 3)
		{
			const int A = Mesh->IndexStream1.Indices[i + 0];
			const int B = Mesh->IndexStream1.Indices[i + 1];
			const int C = Mesh->IndexStream1.Indices[i + 2];
			FVector AB, AC;
			AB.Set(
				Mesh->VertexStream.Vert[B].Pos.X - Mesh->VertexStream.Vert[A].Pos.X,
				Mesh->VertexStream.Vert[B].Pos.Y - Mesh->VertexStream.Vert[A].Pos.Y,
				Mesh->VertexStream.Vert[B].Pos.Z - Mesh->VertexStream.Vert[A].Pos.Z);
			AC.Set(
				Mesh->VertexStream.Vert[C].Pos.X - Mesh->VertexStream.Vert[A].Pos.X,
				Mesh->VertexStream.Vert[C].Pos.Y - Mesh->VertexStream.Vert[A].Pos.Y,
				Mesh->VertexStream.Vert[C].Pos.Z - Mesh->VertexStream.Vert[A].Pos.Z);
			const FVector N = SCDAStaticCross(AB, AC);
			Mesh->VertexStream.Vert[A].Normal.Add(N);
			Mesh->VertexStream.Vert[B].Normal.Add(N);
			Mesh->VertexStream.Vert[C].Normal.Add(N);
		}
		for (int i = 0; i < Mesh->VertexStream.Vert.Num(); i++)
			SCDAStaticNormalize(Mesh->VertexStream.Vert[i].Normal);

		if (SCDAStaticDebugEnabled())
			appPrintf("SCDA inline section topology %s: mode=%s sections=%d verts=%d tris=%d mirrorX=%d yForwardFromZ=%d bounds=(%g,%g,%g)-(%g,%g,%g)\n",
				Mesh->Name, bSectionFans ? "fans" : "chains", InlineSections.Num(), InlineSectionVertexCount, Mesh->IndexStream1.Indices.Num() / 3, bMirrorX ? 1 : 0, bYForwardFromZ ? 1 : 0,
				MinV.X, MinV.Y, MinV.Z, MaxV.X, MaxV.Y, MaxV.Z);

		Mesh->ConvertMesh();
		return true;
	}

	if (getenv("SCDA_INLINE_STATIC_F32_BLOCK_CLOUD"))
	{
		struct FFloatRunCandidate
		{
			int Start;
			int Count;
			float Score;
			FVector MinV;
			FVector MaxV;
		};
		TArray<FFloatRunCandidate> Runs;

		for (int Phase = 0; Phase < 12; Phase++)
		{
			int RunStart = -1;
			int RunCount = 0;
			for (int Pos = Phase; Pos + 12 <= DataSize; Pos += 12)
			{
				FVector V;
				V.X = ReadSCDAStaticFloat(Data, Pos + 0);
				V.Y = ReadSCDAStaticFloat(Data, Pos + 4);
				V.Z = ReadSCDAStaticFloat(Data, Pos + 8);
				const float MaxAbs = max(max(fabs(V.X), fabs(V.Y)), fabs(V.Z));
				const bool bSane = V.X == V.X && V.Y == V.Y && V.Z == V.Z &&
					MaxAbs >= 0.001f && MaxAbs <= 1000.0f;
				if (bSane)
				{
					if (RunStart < 0)
						RunStart = Pos;
					RunCount++;
					continue;
				}

				if (RunStart >= 0 && RunCount >= 8)
				{
					FVector MinV, MaxV;
					MinV.Set( 1.0e30f,  1.0e30f,  1.0e30f);
					MaxV.Set(-1.0e30f, -1.0e30f, -1.0e30f);
					for (int i = 0; i < RunCount; i++)
					{
						const int P = RunStart + i * 12;
						FVector V;
						V.X = ReadSCDAStaticFloat(Data, P + 0);
						V.Y = ReadSCDAStaticFloat(Data, P + 4);
						V.Z = ReadSCDAStaticFloat(Data, P + 8);
						MinV.X = min(MinV.X, V.X); MinV.Y = min(MinV.Y, V.Y); MinV.Z = min(MinV.Z, V.Z);
						MaxV.X = max(MaxV.X, V.X); MaxV.Y = max(MaxV.Y, V.Y); MaxV.Z = max(MaxV.Z, V.Z);
					}
					float Span[3] = { MaxV.X - MinV.X, MaxV.Y - MinV.Y, MaxV.Z - MinV.Z };
					if (Span[0] > Span[1]) Exchange(Span[0], Span[1]);
					if (Span[1] > Span[2]) Exchange(Span[1], Span[2]);
					if (Span[0] > Span[1]) Exchange(Span[0], Span[1]);
					if (Span[0] > 0.01f && Span[1] > 1.0f && Span[2] > 10.0f)
					{
						const float LongRatio = Span[2] / max(Span[1], 0.001f);
						const float CrossRatio = Span[1] / max(Span[0], 0.001f);
						if (LongRatio >= 1.15f && LongRatio <= 6.0f && CrossRatio <= 3.25f)
						{
							FFloatRunCandidate& C = Runs[Runs.AddDefaulted()];
							C.Start = RunStart;
							C.Count = RunCount;
							C.Score = LongRatio * 10.0f + RunCount * 0.05f - CrossRatio;
							C.MinV = MinV;
							C.MaxV = MaxV;
						}
					}
				}
				RunStart = -1;
				RunCount = 0;
			}
		}

		TArray<FFloatRunCandidate> Selected;
		for (int i = 0; i < Runs.Num(); i++)
		{
			FFloatRunCandidate& Candidate = Runs[i];
			int Existing = -1;
			for (int j = 0; j < Selected.Num(); j++)
			{
				if (abs(Selected[j].Start - Candidate.Start) <= 8 ||
					abs((Selected[j].Start + Selected[j].Count * 12) - (Candidate.Start + Candidate.Count * 12)) <= 8)
				{
					Existing = j;
					break;
				}
			}
			if (Existing < 0)
			{
				new (Selected) FFloatRunCandidate(Candidate);
			}
			else if (Candidate.Score > Selected[Existing].Score)
			{
				Selected[Existing] = Candidate;
			}
		}

		int TotalPoints = 0;
		for (int i = 0; i < Selected.Num(); i++)
			TotalPoints += Selected[i].Count;
		if (TotalPoints < 8)
			return false;

		const float MarkerSize = 1.0f;
		Mesh->VertexStream.Vert.Empty(TotalPoints * 3);
		Mesh->VertexStream.Vert.AddZeroed(TotalPoints * 3);
		FStaticMeshUVStream* CloudUV = new (Mesh->UVStream) FStaticMeshUVStream;
		CloudUV->Data.Empty(TotalPoints * 3);
		CloudUV->Data.AddZeroed(TotalPoints * 3);
		Mesh->IndexStream1.Indices.Empty(TotalPoints * 3);
		Mesh->IndexStream1.Indices.AddZeroed(TotalPoints * 3);

		FVector MinV, MaxV;
		MinV.Set( 1.0e30f,  1.0e30f,  1.0e30f);
		MaxV.Set(-1.0e30f, -1.0e30f, -1.0e30f);
		int PointIndex = 0;
		for (int RunIndex = 0; RunIndex < Selected.Num(); RunIndex++)
		{
			const FFloatRunCandidate& Run = Selected[RunIndex];
			if (SCDAStaticDebugEnabled())
				appPrintf("SCDA inline f32 block %s: start=%X count=%d score=%g bounds=(%g,%g,%g)-(%g,%g,%g)\n",
					Mesh->Name, Run.Start, Run.Count, Run.Score,
					Run.MinV.X, Run.MinV.Y, Run.MinV.Z, Run.MaxV.X, Run.MaxV.Y, Run.MaxV.Z);
			for (int i = 0; i < Run.Count; i++, PointIndex++)
			{
				const int Pos = Run.Start + i * 12;
				FVector P;
				P.X = ReadSCDAStaticFloat(Data, Pos + 0);
				P.Y = ReadSCDAStaticFloat(Data, Pos + 4);
				P.Z = ReadSCDAStaticFloat(Data, Pos + 8);
				MinV.X = min(MinV.X, P.X); MinV.Y = min(MinV.Y, P.Y); MinV.Z = min(MinV.Z, P.Z);
				MaxV.X = max(MaxV.X, P.X); MaxV.Y = max(MaxV.Y, P.Y); MaxV.Z = max(MaxV.Z, P.Z);

				const int Base = PointIndex * 3;
				Mesh->VertexStream.Vert[Base + 0].Pos.Set(P.X - MarkerSize, P.Y, P.Z);
				Mesh->VertexStream.Vert[Base + 1].Pos.Set(P.X + MarkerSize, P.Y, P.Z);
				Mesh->VertexStream.Vert[Base + 2].Pos.Set(P.X, P.Y + MarkerSize, P.Z);
				for (int j = 0; j < 3; j++)
				{
					Mesh->VertexStream.Vert[Base + j].Normal.Set(0, 0, 1);
					Mesh->IndexStream1.Indices[Base + j] = Base + j;
				}
				CloudUV->Data[Base + 0].U = 0.0f; CloudUV->Data[Base + 0].V = 0.0f;
				CloudUV->Data[Base + 1].U = 1.0f; CloudUV->Data[Base + 1].V = 0.0f;
				CloudUV->Data[Base + 2].U = 0.5f; CloudUV->Data[Base + 2].V = 1.0f;
			}
		}

		FStaticMeshSection* CloudSection = new (Mesh->Sections) FStaticMeshSection;
		memset(CloudSection, 0, sizeof(FStaticMeshSection));
		CloudSection->FirstIndex  = 0;
		CloudSection->FirstVertex = 0;
		CloudSection->LastVertex  = Mesh->VertexStream.Vert.Num() - 1;
		CloudSection->fE          = TotalPoints;
		CloudSection->NumFaces    = TotalPoints;

		if (SCDAStaticDebugEnabled())
			appPrintf("SCDA inline f32 block cloud %s: blocks=%d points=%d markerVerts=%d bounds=(%g,%g,%g)-(%g,%g,%g)\n",
				Mesh->Name, Selected.Num(), TotalPoints, Mesh->VertexStream.Vert.Num(),
				MinV.X, MinV.Y, MinV.Z, MaxV.X, MaxV.Y, MaxV.Z);

		Mesh->ConvertMesh();
		return true;
	}

	const char* GenericCloudFormat = getenv("SCDA_INLINE_STATIC_CLOUD_FORMAT");
	if (GenericCloudFormat && GenericCloudFormat[0] && strcmp(GenericCloudFormat, "0") != 0)
	{
		const char* StartEnv = getenv("SCDA_INLINE_STATIC_CLOUD_START");
		const char* CountEnv = getenv("SCDA_INLINE_STATIC_CLOUD_COUNT");
		const char* StrideEnv = getenv("SCDA_INLINE_STATIC_CLOUD_STRIDE");
		const char* ScaleEnv = getenv("SCDA_INLINE_STATIC_CLOUD_SCALE");
		const int CloudStart = StartEnv && StartEnv[0] ? (int)strtol(StartEnv, NULL, 0) : 0;
		const int CloudCount = CountEnv && CountEnv[0] ? (int)strtol(CountEnv, NULL, 0) : 0;
		const int CloudStride = StrideEnv && StrideEnv[0] ? (int)strtol(StrideEnv, NULL, 0) : 12;
		const float CloudScale = ScaleEnv && ScaleEnv[0] ? (float)atof(ScaleEnv) : 1.0f;
		const int ElemSize = !stricmp(GenericCloudFormat, "f32") ? 12 :
			(!stricmp(GenericCloudFormat, "i16") ? 6 :
			(!stricmp(GenericCloudFormat, "p10") ? 4 : 0));
		if (ElemSize <= 0 || CloudStart < 0 || CloudCount < 3 || CloudStride < ElemSize ||
			CloudStart > DataSize - ElemSize || CloudStart + (CloudCount - 1) * CloudStride + ElemSize > DataSize)
		{
			return false;
		}

		const float MarkerSize = 1.0f;
		Mesh->VertexStream.Vert.Empty(CloudCount * 3);
		Mesh->VertexStream.Vert.AddZeroed(CloudCount * 3);
		FStaticMeshUVStream* CloudUV = new (Mesh->UVStream) FStaticMeshUVStream;
		CloudUV->Data.Empty(CloudCount * 3);
		CloudUV->Data.AddZeroed(CloudCount * 3);
		Mesh->IndexStream1.Indices.Empty(CloudCount * 3);
		Mesh->IndexStream1.Indices.AddZeroed(CloudCount * 3);

		FVector MinV, MaxV;
		MinV.Set( 1.0e30f,  1.0e30f,  1.0e30f);
		MaxV.Set(-1.0e30f, -1.0e30f, -1.0e30f);
		for (int i = 0; i < CloudCount; i++)
		{
			const int Pos = CloudStart + i * CloudStride;
			FVector P;
			if (!stricmp(GenericCloudFormat, "f32"))
			{
				P.X = ReadSCDAStaticFloat(Data, Pos + 0);
				P.Y = ReadSCDAStaticFloat(Data, Pos + 4);
				P.Z = ReadSCDAStaticFloat(Data, Pos + 8);
			}
			else if (!stricmp(GenericCloudFormat, "i16"))
			{
				P.X = (float)(int16)ReadSCDAStaticLE16(Data, Pos + 0);
				P.Y = (float)(int16)ReadSCDAStaticLE16(Data, Pos + 2);
				P.Z = (float)(int16)ReadSCDAStaticLE16(Data, Pos + 4);
			}
			else
			{
				P = ReadSCDAStaticPacked10Vector(Data, Pos);
			}
			P.Scale(CloudScale);
			MinV.X = min(MinV.X, P.X); MinV.Y = min(MinV.Y, P.Y); MinV.Z = min(MinV.Z, P.Z);
			MaxV.X = max(MaxV.X, P.X); MaxV.Y = max(MaxV.Y, P.Y); MaxV.Z = max(MaxV.Z, P.Z);

			const int Base = i * 3;
			Mesh->VertexStream.Vert[Base + 0].Pos.Set(P.X - MarkerSize, P.Y, P.Z);
			Mesh->VertexStream.Vert[Base + 1].Pos.Set(P.X + MarkerSize, P.Y, P.Z);
			Mesh->VertexStream.Vert[Base + 2].Pos.Set(P.X, P.Y + MarkerSize, P.Z);
			for (int j = 0; j < 3; j++)
			{
				Mesh->VertexStream.Vert[Base + j].Normal.Set(0, 0, 1);
				Mesh->IndexStream1.Indices[Base + j] = Base + j;
			}
			CloudUV->Data[Base + 0].U = 0.0f; CloudUV->Data[Base + 0].V = 0.0f;
			CloudUV->Data[Base + 1].U = 1.0f; CloudUV->Data[Base + 1].V = 0.0f;
			CloudUV->Data[Base + 2].U = 0.5f; CloudUV->Data[Base + 2].V = 1.0f;
		}

		FStaticMeshSection* CloudSection = new (Mesh->Sections) FStaticMeshSection;
		memset(CloudSection, 0, sizeof(FStaticMeshSection));
		CloudSection->FirstIndex  = 0;
		CloudSection->FirstVertex = 0;
		CloudSection->LastVertex  = Mesh->VertexStream.Vert.Num() - 1;
		CloudSection->fE          = CloudCount;
		CloudSection->NumFaces    = CloudCount;

		if (SCDAStaticDebugEnabled())
			appPrintf("SCDA inline generic vertex cloud %s: format=%s start=%X count=%d stride=%d scale=%g bounds=(%g,%g,%g)-(%g,%g,%g)\n",
				Mesh->Name, GenericCloudFormat, CloudStart, CloudCount, CloudStride, CloudScale,
				MinV.X, MinV.Y, MinV.Z, MaxV.X, MaxV.Y, MaxV.Z);

		Mesh->ConvertMesh();
		return true;
	}

	const char* Packed10CloudEnv = getenv("SCDA_INLINE_STATIC_PACKED10_CLOUD");
	if (Packed10CloudEnv && strcmp(Packed10CloudEnv, "0") != 0)
	{
		int PackedVertexCount = 0x148;
		const char* CountEnv = getenv("SCDA_INLINE_STATIC_PACKED10_COUNT");
		if (CountEnv && CountEnv[0])
			PackedVertexCount = (int)strtol(CountEnv, NULL, 0);
		if (PackedVertexCount < 3 || PackedVertexCount > DataSize / 4)
			return false;

		const float Scale = 1.0f / 16.0f;
		const float MarkerSize = 0.5f;
		Mesh->VertexStream.Vert.Empty(PackedVertexCount * 3);
		Mesh->VertexStream.Vert.AddZeroed(PackedVertexCount * 3);
		FStaticMeshUVStream* CloudUV = new (Mesh->UVStream) FStaticMeshUVStream;
		CloudUV->Data.Empty(PackedVertexCount * 3);
		CloudUV->Data.AddZeroed(PackedVertexCount * 3);
		Mesh->IndexStream1.Indices.Empty(PackedVertexCount * 3);
		Mesh->IndexStream1.Indices.AddZeroed(PackedVertexCount * 3);

		FVector MinV, MaxV;
		MinV.Set( 1.0e30f,  1.0e30f,  1.0e30f);
		MaxV.Set(-1.0e30f, -1.0e30f, -1.0e30f);
		for (int i = 0; i < PackedVertexCount; i++)
		{
			FVector P = ReadSCDAStaticPacked10Vector(Data, i * 4);
			P.Scale(Scale);
			MinV.X = min(MinV.X, P.X); MinV.Y = min(MinV.Y, P.Y); MinV.Z = min(MinV.Z, P.Z);
			MaxV.X = max(MaxV.X, P.X); MaxV.Y = max(MaxV.Y, P.Y); MaxV.Z = max(MaxV.Z, P.Z);

			const int Base = i * 3;
			Mesh->VertexStream.Vert[Base + 0].Pos.Set(P.X - MarkerSize, P.Y, P.Z);
			Mesh->VertexStream.Vert[Base + 1].Pos.Set(P.X + MarkerSize, P.Y, P.Z);
			Mesh->VertexStream.Vert[Base + 2].Pos.Set(P.X, P.Y + MarkerSize, P.Z);
			for (int j = 0; j < 3; j++)
			{
				Mesh->VertexStream.Vert[Base + j].Normal.Set(0, 0, 1);
				Mesh->IndexStream1.Indices[Base + j] = Base + j;
			}
			CloudUV->Data[Base + 0].U = 0.0f; CloudUV->Data[Base + 0].V = 0.0f;
			CloudUV->Data[Base + 1].U = 1.0f; CloudUV->Data[Base + 1].V = 0.0f;
			CloudUV->Data[Base + 2].U = 0.5f; CloudUV->Data[Base + 2].V = 1.0f;
		}

		FStaticMeshSection* CloudSection = new (Mesh->Sections) FStaticMeshSection;
		memset(CloudSection, 0, sizeof(FStaticMeshSection));
		CloudSection->FirstIndex  = 0;
		CloudSection->FirstVertex = 0;
		CloudSection->LastVertex  = Mesh->VertexStream.Vert.Num() - 1;
		CloudSection->fE          = PackedVertexCount;
		CloudSection->NumFaces    = PackedVertexCount;

		if (SCDAStaticDebugEnabled())
			appPrintf("SCDA inline packed10 vertex cloud %s: packedVerts=%d markerVerts=%d markerTris=%d bounds=(%g,%g,%g)-(%g,%g,%g)\n",
				Mesh->Name, PackedVertexCount, Mesh->VertexStream.Vert.Num(), Mesh->IndexStream1.Indices.Num() / 3,
				MinV.X, MinV.Y, MinV.Z, MaxV.X, MaxV.Y, MaxV.Z);

		Mesh->ConvertMesh();
		return true;
	}

	int DenseStart = 0;
	int DenseEnd = 0;
	if (!FindSCDAInlineStaticDescriptorSpan(Data, DataSize, DenseStart, DenseEnd))
		return false;
	if (!getenv("SCDA_INLINE_DESCRIPTOR_ONLY"))
	{
		DenseStart = 0;
		DenseEnd = DataSize;
	}

	TArray<FVector> Points;
	TArray<uint16> Indices;
	TArray<FSCDAStaticSectionInfo> PacketSections;

	for (int Pos = DenseStart; Pos < DenseEnd; Pos++)
	{
		if (Data[Pos] != 0 && Data[Pos] != 1)
			continue;
		int Run = 1;
		while (Pos + Run < DenseEnd && Run < 255)
		{
			const int Prev = Data[Pos + Run - 1];
			const int Next = Data[Pos + Run];
			if (Next <= Prev || Next >= 128 || Next - Prev > 8)
				break;
			Run++;
		}
		if (Run < 8 || Pos + Run + Run * 4 > DenseEnd)
			continue;

		// Avoid consuming the same monotonically increasing index list from the
		// middle.  These byte-local palettes may start at either 0 or 1, but a
		// sub-run would have the previous byte equal to this run's predecessor.
		if (Pos > DenseStart && Data[Pos - 1] < Data[Pos] && Data[Pos] - Data[Pos - 1] <= 8)
			continue;

		const int FirstPoint = Points.Num();
		const int FirstIndex = Indices.Num();
		for (int i = 0; i < Run; i++)
		{
			FVector V = ReadSCDAStaticPacked10Vector(Data, Pos + Run + i * 4);
			// The packet positions are quantized.  Keep the scale conservative;
			// exact object-space scale can be corrected once the surrounding
			// transform packet is mapped.
			V.Scale(1.0f / 16.0f);
			new (Points) FVector(V);
		}

		for (int i = 2; i < Run; i++)
		{
			const uint16 A = (uint16)(FirstPoint + i - 2);
			const uint16 B = (uint16)(FirstPoint + i - 1);
			const uint16 C = (uint16)(FirstPoint + i);
			if ((i & 1) == 0)
			{
				new (Indices) uint16(A);
				new (Indices) uint16(B);
				new (Indices) uint16(C);
			}
			else
			{
				new (Indices) uint16(B);
				new (Indices) uint16(A);
				new (Indices) uint16(C);
			}
		}

		const int NumFaces = (Indices.Num() - FirstIndex) / 3;
		if (NumFaces > 0)
		{
			FSCDAStaticSectionInfo& S = PacketSections[PacketSections.AddDefaulted()];
			S.FirstIndex = FirstIndex;
			S.FirstVertex = FirstPoint;
			S.LastVertex = FirstPoint + Run - 1;
			S.NumFaces = NumFaces;
		}

		Pos += Run + Run * 4 - 1;
	}

	if (Points.Num() < 16 || Indices.Num() < 48)
		return false;

	Mesh->VertexStream.Vert.Empty(Points.Num());
	Mesh->VertexStream.Vert.AddZeroed(Points.Num());
	FStaticMeshUVStream* UV = new (Mesh->UVStream) FStaticMeshUVStream;
	UV->Data.Empty(Points.Num());
	UV->Data.AddZeroed(Points.Num());

	FVector MinV, MaxV;
	MinV.Set( 1.0e30f,  1.0e30f,  1.0e30f);
	MaxV.Set(-1.0e30f, -1.0e30f, -1.0e30f);
	for (int i = 0; i < Points.Num(); i++)
	{
		MinV.X = min(MinV.X, Points[i].X); MinV.Y = min(MinV.Y, Points[i].Y); MinV.Z = min(MinV.Z, Points[i].Z);
		MaxV.X = max(MaxV.X, Points[i].X); MaxV.Y = max(MaxV.Y, Points[i].Y); MaxV.Z = max(MaxV.Z, Points[i].Z);
	}

	const float SizeX = max(MaxV.X - MinV.X, 0.001f);
	const float SizeY = max(MaxV.Y - MinV.Y, 0.001f);
	for (int i = 0; i < Points.Num(); i++)
	{
		FStaticMeshVertex& Vtx = Mesh->VertexStream.Vert[i];
		Vtx.Pos = Points[i];
		Vtx.Normal.Set(0, 0, 0);
		UV->Data[i].U = (Points[i].X - MinV.X) / SizeX;
		UV->Data[i].V = (Points[i].Y - MinV.Y) / SizeY;
	}

	Mesh->IndexStream1.Indices.Empty(Indices.Num());
	Mesh->IndexStream1.Indices.AddZeroed(Indices.Num());
	for (int i = 0; i < Indices.Num(); i++)
		Mesh->IndexStream1.Indices[i] = Indices[i];

	for (int i = 0; i + 2 < Mesh->IndexStream1.Indices.Num(); i += 3)
	{
		const int A = Mesh->IndexStream1.Indices[i + 0];
		const int B = Mesh->IndexStream1.Indices[i + 1];
		const int C = Mesh->IndexStream1.Indices[i + 2];
		if (A < 0 || A >= Points.Num() || B < 0 || B >= Points.Num() || C < 0 || C >= Points.Num())
			continue;
		FVector AB, AC;
		AB.Set(Points[B].X - Points[A].X, Points[B].Y - Points[A].Y, Points[B].Z - Points[A].Z);
		AC.Set(Points[C].X - Points[A].X, Points[C].Y - Points[A].Y, Points[C].Z - Points[A].Z);
		const FVector N = SCDAStaticCross(AB, AC);
		Mesh->VertexStream.Vert[A].Normal.Add(N);
		Mesh->VertexStream.Vert[B].Normal.Add(N);
		Mesh->VertexStream.Vert[C].Normal.Add(N);
	}
	for (int i = 0; i < Points.Num(); i++)
		SCDAStaticNormalize(Mesh->VertexStream.Vert[i].Normal);

	for (int i = 0; i < PacketSections.Num(); i++)
	{
		const FSCDAStaticSectionInfo& Src = PacketSections[i];
		FStaticMeshSection* Section = new (Mesh->Sections) FStaticMeshSection;
		memset(Section, 0, sizeof(FStaticMeshSection));
		Section->FirstIndex  = Src.FirstIndex;
		Section->FirstVertex = Src.FirstVertex;
		Section->LastVertex  = Src.LastVertex;
		Section->fE          = Src.NumFaces;
		Section->NumFaces    = Src.NumFaces;
	}

	if (SCDAStaticDebugEnabled())
		appPrintf("SCDA inline packet mesh %s: verts=%d tris=%d sections=%d bounds=(%g,%g,%g)-(%g,%g,%g)\n",
			Mesh->Name, Points.Num(), Mesh->IndexStream1.Indices.Num() / 3, PacketSections.Num(),
			MinV.X, MinV.Y, MinV.Z, MaxV.X, MaxV.Y, MaxV.Z);

	Mesh->ConvertMesh();
	return true;
	unguard;
}

static bool SerializeDoubleAgentStaticMeshPackedTriangles(UStaticMesh* Mesh, const byte* Data, int DataSize)
{
	guard(SerializeDoubleAgentStaticMeshPackedTriangles);
	if (DataSize < 16 * 3)
		return false;

	int BestBase = -1;
	int BestVertexCount = 0;
	int BestGoodUVs = 0;
	int BestExtent[3] = {0, 0, 0};
	float BestScore = -1.0e30f;
	const int MaxBase = 0;
	for (int Base = 0; Base <= MaxBase; Base++)
	{
		const int TestVertexCount = (DataSize - Base) / 16;
		const int TestIndexCount = (TestVertexCount / 3) * 3;
		if (TestIndexCount < 3)
			continue;
		const byte* TestData = Data + Base;
	int MinX =  0x7FFFFFFF, MinY =  0x7FFFFFFF, MinZ =  0x7FFFFFFF;
	int MaxX = -0x7FFFFFFF, MaxY = -0x7FFFFFFF, MaxZ = -0x7FFFFFFF;
	int GoodUVs = 0;
		for (int i = 0; i < TestVertexCount; i++)
	{
		const int Pos = i * 16;
			const int X = (int16)ReadSCDAStaticLE16(TestData, Pos + 0);
			const int Y = (int16)ReadSCDAStaticLE16(TestData, Pos + 2);
			const int Z = (int16)ReadSCDAStaticLE16(TestData, Pos + 4);
		if (X == -32768 || Y == -32768 || Z == -32768)
				goto next_base;
		MinX = min(MinX, X); MaxX = max(MaxX, X);
		MinY = min(MinY, Y); MaxY = max(MaxY, Y);
		MinZ = min(MinZ, Z); MaxZ = max(MaxZ, Z);
			const int U = (int16)ReadSCDAStaticLE16(TestData, Pos + 8);
			const int V = (int16)ReadSCDAStaticLE16(TestData, Pos + 10);
		if (abs(U) < 32760 && abs(V) < 32760 && (abs(U) + abs(V)) > 0)
			GoodUVs++;
	}
		const int ExtentX = MaxX - MinX;
		const int ExtentY = MaxY - MinY;
		const int ExtentZ = MaxZ - MinZ;
		const int Extent = ExtentX + ExtentY + ExtentZ;
		if (Extent < 32)
			continue;
		if (ExtentX > 30000 || ExtentY > 30000 || ExtentZ > 30000)
			continue;
		int SmallAxes = 0;
		if (ExtentX < 32) SmallAxes++;
		if (ExtentY < 32) SmallAxes++;
		if (ExtentZ < 32) SmallAxes++;
		if (SmallAxes >= 2)
			continue;
		const float HugePenalty = (ExtentX > 60000 || ExtentY > 60000 || ExtentZ > 60000) ? 1000.0f : 0.0f;
		const float Score = GoodUVs * 10.0f + min(Extent, 120000) * 0.001f - Base * 0.02f - HugePenalty;
		if (Score > BestScore)
		{
			BestScore = Score;
			BestBase = Base;
			BestVertexCount = TestVertexCount;
			BestGoodUVs = GoodUVs;
			BestExtent[0] = ExtentX;
			BestExtent[1] = ExtentY;
			BestExtent[2] = ExtentZ;
		}
next_base:
		;
	}
	if (BestBase < 0)
		return false;
	Data += BestBase;
	DataSize -= BestBase;
	const int VertexCount = BestVertexCount;
	const int IndexCount = (VertexCount / 3) * 3;
	const int GoodUVs = BestGoodUVs;

	Mesh->VertexStream.Vert.Empty(VertexCount);
	Mesh->VertexStream.Vert.AddZeroed(VertexCount);
	FStaticMeshUVStream* UV = new (Mesh->UVStream) FStaticMeshUVStream;
	UV->Data.Empty(VertexCount);
	UV->Data.AddZeroed(VertexCount);

	for (int i = 0; i < VertexCount; i++)
	{
		const int Pos = i * 16;
		FStaticMeshVertex& Vtx = Mesh->VertexStream.Vert[i];
		Vtx.Pos.X = (int16)ReadSCDAStaticLE16(Data, Pos + 0) / 32.0f;
		Vtx.Pos.Y = (int16)ReadSCDAStaticLE16(Data, Pos + 2) / 32.0f;
		Vtx.Pos.Z = (int16)ReadSCDAStaticLE16(Data, Pos + 4) / 32.0f;
		Vtx.Normal.Set(0, 0, 0);
		UV->Data[i].U = (int16)ReadSCDAStaticLE16(Data, Pos + 8) / 2048.0f;
		UV->Data[i].V = (int16)ReadSCDAStaticLE16(Data, Pos + 10) / 2048.0f;
	}

	Mesh->IndexStream1.Indices.Empty(IndexCount);
	Mesh->IndexStream1.Indices.AddZeroed(IndexCount);
	for (int i = 0; i < IndexCount; i++)
		Mesh->IndexStream1.Indices[i] = i;

	for (int i = 0; i + 2 < IndexCount; i += 3)
	{
		FStaticMeshVertex& A = Mesh->VertexStream.Vert[i + 0];
		FStaticMeshVertex& B = Mesh->VertexStream.Vert[i + 1];
		FStaticMeshVertex& C = Mesh->VertexStream.Vert[i + 2];
		FVector AB, AC;
		AB.Set(B.Pos.X - A.Pos.X, B.Pos.Y - A.Pos.Y, B.Pos.Z - A.Pos.Z);
		AC.Set(C.Pos.X - A.Pos.X, C.Pos.Y - A.Pos.Y, C.Pos.Z - A.Pos.Z);
		FVector N = SCDAStaticCross(AB, AC);
		A.Normal.Add(N);
		B.Normal.Add(N);
		C.Normal.Add(N);
	}
	for (int i = 0; i < VertexCount; i++)
		SCDAStaticNormalize(Mesh->VertexStream.Vert[i].Normal);

	FStaticMeshSection* Section = new (Mesh->Sections) FStaticMeshSection;
	memset(Section, 0, sizeof(FStaticMeshSection));
	Section->FirstIndex  = 0;
	Section->FirstVertex = 0;
	Section->LastVertex  = VertexCount - 1;
	Section->fE          = IndexCount / 3;
	Section->NumFaces    = IndexCount / 3;

	const char* DebugStatic = getenv("SC4_DEBUG_STATIC");
	if (DebugStatic && strcmp(DebugStatic, "0") != 0)
		appPrintf("SC4 StaticMesh %s packed16 base=%X vertices=%d tris=%d uvGood=%d extent=(%d,%d,%d) materials=%d first=(%g,%g,%g) uv=(%g,%g)\n",
			Mesh->Name, BestBase, VertexCount, IndexCount / 3, GoodUVs,
			BestExtent[0], BestExtent[1], BestExtent[2], Mesh->Materials.Num(),
			Mesh->VertexStream.Vert[0].Pos.X, Mesh->VertexStream.Vert[0].Pos.Y, Mesh->VertexStream.Vert[0].Pos.Z,
			UV->Data[0].U, UV->Data[0].V);

	Mesh->ConvertMesh();
	return true;
	unguard;
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

	int PayloadBase = -1;
	int BestIndexCount = 0;
	float BestPayloadScore = -1.0e30f;
	FSCDAStaticVertexLayout Layout;
	memset(&Layout, 0, sizeof(Layout));
	const int MaxPayloadBase = min(Size - 64, 0x1000);
	for (int Base = 0; Base <= MaxPayloadBase; Base++)
	{
		FSCDAStaticVertexLayout TestLayout;
		if (!FindSCDAStaticVertexLayout(Raw.GetData() + Base, Size - Base, TestLayout))
			continue;

		int TestSectionCount = 0;
		memcpy(&TestSectionCount, Raw.GetData() + Base, 4);
		TArray<FSCDAStaticSectionInfo> TestSections;
		if (!ReadSCDAStaticSections(Raw.GetData() + Base, Size - Base, TestSectionCount, TestLayout.Count, TestSections))
			continue;

		int TestExpectedIndexCount = 0;
		for (int i = 0; i < TestSections.Num(); i++)
			TestExpectedIndexCount += TestSections[i].NumFaces * 3;

		TArray<uint16> TestIndices;
		int TestIndexPos = 0;
		if (!FindSCDAStaticIndexBlock(Raw.GetData() + Base, Size - Base,
			TestLayout.Start + TestLayout.Count * TestLayout.Stride, TestLayout,
			TestExpectedIndexCount, TestIndices, TestIndexPos))
			continue;

		const float Score = TestLayout.Score + TestIndices.Num() * 0.05f - Base * 0.002f;
		if (Score > BestPayloadScore)
		{
			PayloadBase = Base;
			Layout = TestLayout;
			BestIndexCount = TestIndices.Num();
			BestPayloadScore = Score;
		}
	}
	if (PayloadBase < 0)
	{
		if (SerializeDoubleAgentStaticMeshPackedTriangles(Mesh, Raw.GetData(), Size))
		{
			DROP_REMAINING_DATA(Ar);
			return true;
		}
		return false;
	}

	const byte* Data = Raw.GetData() + PayloadBase;
	const int DataSize = Size - PayloadBase;
	int SectionCount = 0;
	memcpy(&SectionCount, Data, 4);
	TArray<FSCDAStaticSectionInfo> SCDASections;
	ReadSCDAStaticSections(Data, DataSize, SectionCount, Layout.Count, SCDASections);
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
		V.Pos.X = ReadSCDAStaticFloat(Data, Pos + 0);
		V.Pos.Y = ReadSCDAStaticFloat(Data, Pos + 4);
		V.Pos.Z = ReadSCDAStaticFloat(Data, Pos + 8);
		if (Layout.Stride >= 24)
		{
			V.Normal.X = ReadSCDAStaticFloat(Data, Pos + 12);
			V.Normal.Y = ReadSCDAStaticFloat(Data, Pos + 16);
			V.Normal.Z = ReadSCDAStaticFloat(Data, Pos + 20);
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
	FindSCDAStaticIndexBlock(Data, DataSize, Layout.Start + Layout.Count * Layout.Stride, Layout, ExpectedIndexCount, Mesh->IndexStream1.Indices, IndexPos);
	const int VertexEnd = Layout.Start + Layout.Count * Layout.Stride;
	if (IndexPos > VertexEnd)
		FindSCDAStaticUVBlock(Data, DataSize, VertexEnd, IndexPos, Layout, Mesh->IndexStream1.Indices, *UV, GoodUVs);

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
		appPrintf("SC4 StaticMesh %s payloadBase=%X layout start=%X count=%d stride=%d indices=%d expected=%d probeIdx=%d score=%g normals=%d uvs=%d\n",
			Mesh->Name, PayloadBase, Layout.Start, Layout.Count, Layout.Stride, Mesh->IndexStream1.Indices.Num(),
			ExpectedIndexCount, BestIndexCount, BestPayloadScore, GoodNormals, GoodUVs);

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


// SCDA Xbox native StaticMesh layout, verified against default.xbe
// UStaticMesh::Serialize and the x01_Iceland_a LIN records. Data starts after
// UPrimitive's properties and bounds. Every 28-byte vertex is float3, UV16,
// and three packed basis vectors; index sections are lists or strips.
static bool SerializeDoubleAgentXboxStaticMesh(UStaticMesh* Mesh, const byte* Data, int DataSize)
{
	if (!Data || DataSize < 64)
		return false;
	int Pos = 8;
	int VertexCount;
	if (!ReadSCDAStaticCompactIndex(Data, DataSize, Pos, VertexCount) ||
		VertexCount < 3 || VertexCount > 65535 || VertexCount > (DataSize - Pos) / 28)
		return false;
	const int VertexPos = Pos;
	Pos += VertexCount * 28;
	if (Pos > DataSize - 5)
		return false;
	const int Revision = ReadSCDAStaticLE32(Data, Pos);
	Pos += 4;
	int IndexCount;
	if (!ReadSCDAStaticCompactIndex(Data, DataSize, Pos, IndexCount) ||
		IndexCount < 3 || IndexCount > 1000000 || IndexCount > (DataSize - Pos) / 2)
		return false;
	const int IndexPos = Pos;
	Pos += IndexCount * 2;
	if (Pos > DataSize - 8)
		return false;
	Pos += 4; // index buffer revision
	int HasColors = ReadSCDAStaticLE32(Data, Pos);
	Pos += 4;
	if (HasColors != 0 && HasColors != 1)
		return false;
	if (HasColors)
	{
		int ColorCount;
		if (!ReadSCDAStaticCompactIndex(Data, DataSize, Pos, ColorCount) ||
			ColorCount != VertexCount || ColorCount > (DataSize - Pos - 4) / 4)
			return false;
		Pos += ColorCount * 4 + 4;
	}
	int SectionCount;
	if (!ReadSCDAStaticCompactIndex(Data, DataSize, Pos, SectionCount) ||
		SectionCount <= 0 || SectionCount > 64 ||
		(Mesh->Materials.Num() && SectionCount != Mesh->Materials.Num()))
		return false;
	struct FXboxSection
	{
		int Strip, FirstIndex, FirstVertex, LastVertex, FaceCount, PrimitiveCount;
	};
	TArray<FXboxSection> SourceSections;
	int TotalFaces = 0;
	for (int s = 0; s < SectionCount; s++)
	{
		if (Pos > DataSize - 18)
			return false;
		FXboxSection& S = SourceSections[SourceSections.AddDefaulted()];
		S.Strip = ReadSCDAStaticLE32(Data, Pos);
		S.FirstIndex = ReadSCDAStaticLE16(Data, Pos + 4);
		S.FirstVertex = ReadSCDAStaticLE16(Data, Pos + 6);
		S.LastVertex = ReadSCDAStaticLE16(Data, Pos + 8);
		S.FaceCount = ReadSCDAStaticLE16(Data, Pos + 10);
		S.PrimitiveCount = ReadSCDAStaticLE16(Data, Pos + 12);
		Pos += 18;
		int MaterialRef;
		if (!ReadSCDAStaticCompactIndex(Data, DataSize, Pos, MaterialRef) || MaterialRef > 0 ||
			(S.Strip != 0 && S.Strip != 1) || S.LastVertex >= VertexCount ||
			S.FirstVertex > S.LastVertex || S.FaceCount <= 0)
			return false;
		int Length = S.Strip ? S.PrimitiveCount + 2 : S.PrimitiveCount * 3;
		if (Length < 3 || S.FirstIndex > IndexCount - Length)
			return false;
		int ValidFaces = 0;
		for (int i = 2; i < Length; i += S.Strip ? 1 : 3)
		{
			int A = ReadSCDAStaticLE16(Data, IndexPos + 2 * (S.FirstIndex + i - 2));
			int B = ReadSCDAStaticLE16(Data, IndexPos + 2 * (S.FirstIndex + i - 1));
			int C = ReadSCDAStaticLE16(Data, IndexPos + 2 * (S.FirstIndex + i));
			// Mixed list/strip meshes may reuse vertices outside the section's
			// cached draw range (e.g. x_okb_tankerboat). Bound actual references
			// against the complete vertex buffer instead.
			if (A >= VertexCount || B >= VertexCount || C >= VertexCount)
				return false;
			ValidFaces += A != B && B != C && A != C;
		}
		if (ValidFaces != S.FaceCount)
			return false;
		TotalFaces += ValidFaces;
	}
	if (TotalFaces < 1 || TotalFaces > 1000000)
		return false;

	Mesh->VertexStream.Vert.Empty(VertexCount);
	Mesh->VertexStream.Vert.AddZeroed(VertexCount);
	Mesh->VertexStream.Revision = Revision;
	FStaticMeshUVStream* UV = new (Mesh->UVStream) FStaticMeshUVStream;
	UV->Data.AddZeroed(VertexCount);
	for (int i = 0; i < VertexCount; i++)
	{
		const int P = VertexPos + i * 28;
		FStaticMeshVertex& V = Mesh->VertexStream.Vert[i];
		V.Pos.Set(ReadSCDAStaticFloat(Data, P), ReadSCDAStaticFloat(Data, P + 4),
			ReadSCDAStaticFloat(Data, P + 8));
		V.Normal.Set(0, 0, 0);
		UV->Data[i].U = (int16)ReadSCDAStaticLE16(Data, P + 12) / 2048.0f;
		UV->Data[i].V = (int16)ReadSCDAStaticLE16(Data, P + 14) / 2048.0f;
	}
	Mesh->IndexStream1.Indices.Empty(TotalFaces * 3);
	Mesh->IndexStream1.Indices.AddZeroed(TotalFaces * 3);
	int Out = 0;
	for (int s = 0; s < SourceSections.Num(); s++)
	{
		const FXboxSection& S = SourceSections[s];
		FStaticMeshSection* Section = new (Mesh->Sections) FStaticMeshSection;
		memset(Section, 0, sizeof(FStaticMeshSection));
		Section->FirstIndex = Out;
		Section->FirstVertex = S.FirstVertex;
		Section->LastVertex = S.LastVertex;
		Section->fE = S.FaceCount;
		Section->NumFaces = S.FaceCount;
		int Length = S.Strip ? S.PrimitiveCount + 2 : S.PrimitiveCount * 3;
		for (int i = 2; i < Length; i += S.Strip ? 1 : 3)
		{
			uint16 A = ReadSCDAStaticLE16(Data, IndexPos + 2 * (S.FirstIndex + i - 2));
			uint16 B = ReadSCDAStaticLE16(Data, IndexPos + 2 * (S.FirstIndex + i - 1));
			uint16 C = ReadSCDAStaticLE16(Data, IndexPos + 2 * (S.FirstIndex + i));
			if (A == B || B == C || A == C)
				continue;
			uint16* Dst = Mesh->IndexStream1.Indices.GetData() + Out;
			if (S.Strip && (i & 1)) { Dst[0] = B; Dst[1] = A; Dst[2] = C; }
			else { Dst[0] = A; Dst[1] = B; Dst[2] = C; }
			Out += 3;
		}
	}
	if (Out != TotalFaces * 3)
		return false;
	for (int i = 0; i < Out; i += 3)
	{
		int A = Mesh->IndexStream1.Indices[i];
		int B = Mesh->IndexStream1.Indices[i + 1];
		int C = Mesh->IndexStream1.Indices[i + 2];
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
	for (int i = 0; i < VertexCount; i++)
		SCDAStaticNormalize(Mesh->VertexStream.Vert[i].Normal);
	if (SCDAStaticDebugEnabled())
		appPrintf("SCDA Xbox StaticMesh %s: verts=%d faces=%d sections=%d\n",
			Mesh->Name, VertexCount, TotalFaces, SectionCount);
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
#if SPLINTER_CELL
	const bool isScdaV2SyntheticStatic =
		(Ar.Game == GAME_SplinterCell && Ar.ArVer == 100 && Ar.ArLicenseeVer >= 127 &&
		Package && strstr(*Package->GetFilename(), "_sm.usx") && !strstr(*Package->GetFilename(), "_lin_sm_"));
	const bool isScdaV2InlineStatic =
		(Ar.Game == GAME_SplinterCell && Ar.ArVer == 100 && Ar.ArLicenseeVer >= 127 &&
		Package && strstr(*Package->GetFilename(), "_lin_sm_"));
	if (isScdaV2InlineStatic)
	{
		Super::Serialize(Ar);
		const int SavePos = Ar.Tell();
		const int SaveStop = Ar.GetStopper();
		const int RawSize = SaveStop - SavePos;
		if (RawSize > 0 && RawSize < 0x1000000)
		{
			TArray<byte> RawStatic;
			RawStatic.AddUninitialized(RawSize);
			Ar.Serialize(RawStatic.GetData(), RawSize);
			Ar.Seek(SavePos);
			if (SerializeDoubleAgentXboxStaticMesh(this, RawStatic.GetData(), RawSize))
			{
				DROP_REMAINING_DATA(Ar);
				return;
			}
			if (getenv("SCDA_DUMP_STATIC_RAW"))
			{
				char Filename[256];
				appSprintf(ARRAY_ARG(Filename), "scda_inline_static_%s_raw.bin", Name);
				FILE *F = fopen(Filename, "wb");
				if (F)
				{
					fwrite(RawStatic.GetData(), 1, RawSize, F);
					fclose(F);
					appPrintf("SCDA dumped inline static mesh export: %s size=%d\n", Filename, RawSize);
				}
			}
		}
		appNotify("Unable to decode SCDA V2 inline static mesh %s, skipping raw payload", Name);
		DROP_REMAINING_DATA(Ar);
		ConvertMesh();
		return;
	}
	if (isScdaV2SyntheticStatic)
	{
		const char* DebugStaticEnv = getenv("SC4_DEBUG_STATIC");
		if (DebugStaticEnv && strcmp(DebugStaticEnv, "0") != 0)
		{
			int SavePos = Ar.Tell();
			int Stop = Ar.GetStopper();
			byte Bytes[96];
			int Count = min(ARRAY_COUNT(Bytes), Stop - SavePos);
			appPrintf("SCDA V2 StaticMesh raw %s pos=%08X stopper=%08X size=%X\n", Name, SavePos, Stop, Stop - SavePos);
			if (Count > 0)
			{
				Ar.Serialize(Bytes, Count);
				appPrintf("SCDA V2 StaticMesh raw bytes:");
				for (int i = 0; i < Count; i++)
					appPrintf(" %02X", Bytes[i]);
				appPrintf("\n");
				Ar.Seek(SavePos);
			}
		}
		if (SerializeDoubleAgentStaticMesh(this, Ar))
			return;
		appNotify("Unable to decode SCDA V2 static mesh %s, skipping raw payload", Name);
		DROP_REMAINING_DATA(Ar);
		ConvertMesh();
		return;
	}
#endif
	Super::Serialize(Ar);

#if SPLINTER_CELL
	const bool isDoubleAgentOnlineStatic =
		(Ar.Game == GAME_SplinterCell && Ar.ArVer >= 173 && Ar.ArVer <= 275 && Ar.ArLicenseeVer == 0) ||
		(Ar.Game == GAME_SplinterCell && Ar.ArVer == 100 && Ar.ArLicenseeVer >= 127);
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
		const bool bIsSCCTStatic = Ar.ArLicenseeVer == 124;
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
			UV->Data[i].U = (int16)(PackedNormals[0].Data & 0xFFFF) / 2048.0f;
			UV->Data[i].V = (int16)(PackedNormals[0].Data >> 16) / 2048.0f;
		}
		Ar << VertexStream.Revision;
		Ar << IndexStream1;
		Ar << IndexStream2;
		if (getenv("SCCT_SM_DEBUG"))
		{
			appPrintf("SCCT StaticMesh %s: verts=%d idx1=%d idx2=%d materials=%d\n",
				Name, NumVerts, IndexStream1.Indices.Num(), IndexStream2.Indices.Num(), Materials.Num());
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
		auto ConvertSCCTStripFaces = [NumVerts](const TArray<uint16>& StripIndices, TArray<uint16>& OutFaces) -> int
		{
			OutFaces.Empty(StripIndices.Num() * 3);
			for (int i = 2; i < StripIndices.Num(); i++)
			{
				const int A = StripIndices[i - 2] & 0x7FFF;
				const int B = StripIndices[i - 1] & 0x7FFF;
				const int C = StripIndices[i] & 0x7FFF;
				if (A >= NumVerts || B >= NumVerts || C >= NumVerts)
					continue;
				if (A == B || B == C || C == A)
					continue;

				const int Base = OutFaces.Num();
				OutFaces.AddZeroed(3);
				if (i & 1)
				{
					OutFaces[Base + 0] = B;
					OutFaces[Base + 1] = A;
					OutFaces[Base + 2] = C;
				}
				else
				{
					OutFaces[Base + 0] = A;
					OutFaces[Base + 1] = B;
					OutFaces[Base + 2] = C;
				}
			}
			return OutFaces.Num() / 3;
		};

		bool bLooksLikeStrip1 = false;
		if (bIsSCCTStatic && IndexStream1.Indices.Num() >= 3 && (IndexStream1.Indices.Num() % 3) == 0)
		{
			int DegenerateTriplets = 0;
			for (int i = 0; i < IndexStream1.Indices.Num(); i += 3)
			{
				const int A = IndexStream1.Indices[i + 0] & 0x7FFF;
				const int B = IndexStream1.Indices[i + 1] & 0x7FFF;
				const int C = IndexStream1.Indices[i + 2] & 0x7FFF;
				if (A == B || B == C || C == A)
					DegenerateTriplets++;
			}

			const int ListFaces = IndexStream1.Indices.Num() / 3;
			bLooksLikeStrip1 = DegenerateTriplets > ListFaces / 4;
			if (getenv("SCCT_SM_DEBUG") && bLooksLikeStrip1)
			{
				appPrintf("SCCT StaticMesh %s: idx1 has %d/%d degenerate triplets, testing as strip\n",
					Name, DegenerateTriplets, ListFaces);
			}
		}

		if (!getenv("SCCT_SM_BYTEFACES") && bLooksLikeStrip1 && !getenv("SCCT_SM_POINT_INDICES"))
		{
			TArray<uint16> StripFaces;
			const int StripFaceCount = ConvertSCCTStripFaces(IndexStream1.Indices, StripFaces);
			if (StripFaceCount >= 3)
			{
				IndexStream1.Indices.Empty(StripFaces.Num());
				IndexStream1.Indices.AddZeroed(StripFaces.Num());
				for (int i = 0; i < StripFaces.Num(); i++)
					IndexStream1.Indices[i] = StripFaces[i];
				bHaveByteFaces = true;
				if (getenv("SCCT_SM_DEBUG"))
					appPrintf("SCCT StaticMesh %s: idx1 strip faces=%d verts=%d stripIndices=%d\n",
						Name, StripFaceCount, NumVerts, StripFaces.Num());
			}
		}

		if (!bHaveByteFaces && !getenv("SCCT_SM_BYTEFACES") && IndexStream1.Indices.Num() >= 3 && (IndexStream1.Indices.Num() % 3) == 0)
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
			ConvertSCCTStripFaces(StripIndices, StripFaces);
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
		TArray<byte> Tail;
		if (TailSize > 0)
		{
			Tail.Empty(TailSize);
			Tail.AddZeroed(TailSize);
			Ar.Serialize(Tail.GetData(), TailSize);
		}
		if (getenv("SCCT_SM_DEBUG"))
		{
			appPrintf("SCCT StaticMesh %s: tail start=%08X size=%d\n", Name, TailStart, TailSize);
		}
		if (bIsSCCTStatic && Tail.Num())
		{
			const int ExplicitFaceCount = GetSCCTStaticExplicitSectionFaceCount(Tail.GetData(), Tail.Num(), NumVerts, Materials.Num());
			if (ExplicitFaceCount > 0 && ExplicitFaceCount != IndexStream1.Indices.Num() / 3)
			{
				TArray<uint16> TailFaces;
				int TailFaceOffset = -1;
				if (FindSCCTStaticByteFaceBlock(Tail.GetData(), Tail.Num(), NumVerts, ExplicitFaceCount, TailFaces, TailFaceOffset))
				{
					IndexStream1.Indices.Empty(TailFaces.Num());
					IndexStream1.Indices.AddZeroed(TailFaces.Num());
					for (int i = 0; i < TailFaces.Num(); i++)
						IndexStream1.Indices[i] = TailFaces[i];
					bHaveByteFaces = true;
					if (getenv("SCCT_SM_DEBUG"))
					{
						appPrintf("SCCT StaticMesh %s: metadata byte face block at %08X (+%X) faces=%d verts=%d tail=%d\n",
							Name, TailStart + TailFaceOffset, TailFaceOffset, ExplicitFaceCount, NumVerts, TailSize);
					}
				}
			}
		}
		if (!bHaveByteFaces && getenv("SCCT_SM_BYTEFACES") && TailSize > 0)
		{
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
			const int NumFaces = NumIndices / 3;
			bool bHaveSections = false;
			if (bIsSCCTStatic && Tail.Num())
				bHaveSections = ReadSCCTStaticSections(Tail.GetData(), Tail.Num(), NumVerts, NumFaces, Materials.Num(), Sections, Name);
			if (!bHaveSections)
			{
				FStaticMeshSection* Section = new (Sections) FStaticMeshSection;
				memset(Section, 0, sizeof(FStaticMeshSection));
				Section->FirstIndex  = 0;
				Section->FirstVertex = 0;
				Section->LastVertex  = NumVerts - 1;
				Section->fE          = NumIndices;
				Section->NumFaces    = NumFaces;
			}
		}

		Ar.Seek(Ar.GetStopper());
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
