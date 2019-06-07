#pragma once

#include "BSPImporter.h"
#include "Paths.h"
#include "IHL2Editor.h"

#ifdef WITH_EDITOR
#include "EditorActorFolders.h"
#include "Engine/World.h"
#include "Engine/Brush.h"
#include "Builders/CubeBuilder.h"
#include "Engine/Polys.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Selection.h"
#include "Lightmass/LightmassImportanceVolume.h"
#include "Builders/EditorBrushBuilder.h"
#include "Components/BrushComponent.h"
#include "VBSPBrushBuilder.h"
#include "MeshAttributes.h"
#include "Internationalization/Regex.h"
#include "MeshAttributes.h"
#include "MeshSplitter.h"
#endif

DEFINE_LOG_CATEGORY(LogHL2BSPImporter);

FBSPImporter::FBSPImporter()
{

}

FBSPImporter::~FBSPImporter()
{

}

#ifdef WITH_EDITOR

bool FBSPImporter::ImportToCurrentLevel(const FString& fileName)
{
	FString bspDir = FPaths::GetPath(fileName) + "/";
	FString bspFileName = FPaths::GetCleanFilename(fileName);
	UE_LOG(LogHL2BSPImporter, Log, TEXT("Importing map '%s' to current level"), *bspFileName);
	const auto bspDirConvert = StringCast<ANSICHAR, TCHAR>(*bspDir);
	const auto bspFileNameConvert = StringCast<ANSICHAR, TCHAR>(*bspFileName);
	Valve::BSPFile bspFile;
	if (!bspFile.parse(std::string(bspDirConvert.Get()), std::string(bspFileNameConvert.Get())))
	{
		UE_LOG(LogHL2BSPImporter, Error, TEXT("Failed to parse BSP"));
		return false;
	}
	return ImportToWorld(bspFile, GEditor->GetEditorWorldContext().World());
}

bool FBSPImporter::ImportToWorld(const Valve::BSPFile& bspFile, UWorld* world)
{
	//if (!ImportBrushesToWorld(bspFile, world)) { return false; }
	if (!ImportGeometryToWorld(bspFile, world)) { return false; }
	return true;
}

bool FBSPImporter::ImportBrushesToWorld(const Valve::BSPFile& bspFile, UWorld* world)
{
	const FName brushesFolder = TEXT("HL2Brushes");
	UE_LOG(LogHL2BSPImporter, Log, TEXT("Importing brushes..."));
	FActorFolders& folders = FActorFolders::Get();
	folders.CreateFolder(*world, brushesFolder);
	for (uint32 i = 0, l = bspFile.m_Models.size(); i < l; ++i)
	{
		const Valve::BSP::dmodel_t& model = bspFile.m_Models[i];
		TArray<uint16> brushIndices;
		GatherBrushes(bspFile, model.m_Headnode, brushIndices);
		brushIndices.Sort();

		int num = 0;
		for (uint16 brushIndex : brushIndices)
		{
			if (i == 0)
			{
				AActor* brushActor = ImportBrush(world, bspFile, brushIndex);
				GEditor->SelectActor(brushActor, true, false, true, false);
				folders.SetSelectedFolderPath(brushesFolder);
				++num;
				//if (num >= 100) break;
			}
			else
			{
				// TODO: Deal with brush entities
			}
		}
	}

	//GEditor->csgRebuild(world);
	GEditor->RebuildAlteredBSP();

	return true;
}

bool FBSPImporter::ImportGeometryToWorld(const Valve::BSPFile& bspFile, UWorld* world)
{
	const FName geometryFolder = TEXT("HL2Geometry");
	UE_LOG(LogHL2BSPImporter, Log, TEXT("Importing geometry..."));
	FActorFolders& folders = FActorFolders::Get();
	folders.CreateFolder(*world, geometryFolder);

	const Valve::BSP::dmodel_t& bspWorldModel = bspFile.m_Models[0];

	TArray<AStaticMeshActor*> staticMeshes;
	RenderTreeToActors(bspFile, world, staticMeshes, bspWorldModel.m_Headnode);
	for (AStaticMeshActor* actor : staticMeshes)
	{
		GEditor->SelectActor(actor, true, false, true, false);
		folders.SetSelectedFolderPath(geometryFolder);
	}

	FVector mins(bspWorldModel.m_Mins(0, 0), bspWorldModel.m_Mins(0, 1), bspWorldModel.m_Mins(0, 2));
	FVector maxs(bspWorldModel.m_Maxs(0, 0), bspWorldModel.m_Maxs(0, 1), bspWorldModel.m_Maxs(0, 2));
	ALightmassImportanceVolume* lightmassImportanceVolume = world->SpawnActor<ALightmassImportanceVolume>();
	lightmassImportanceVolume->Brush = NewObject<UModel>(lightmassImportanceVolume, NAME_None, RF_Transactional);
	lightmassImportanceVolume->Brush->Initialize(nullptr, true);
	lightmassImportanceVolume->Brush->Polys = NewObject<UPolys>(lightmassImportanceVolume->Brush, NAME_None, RF_Transactional);
	lightmassImportanceVolume->GetBrushComponent()->Brush = lightmassImportanceVolume->Brush;
	lightmassImportanceVolume->SetActorLocation(FMath::Lerp(mins, maxs, 0.5f) * FVector(1.0f, -1.0f, 1.0f));
	UCubeBuilder* brushBuilder = NewObject<UCubeBuilder>(lightmassImportanceVolume);
	brushBuilder->X = maxs.X - mins.X;
	brushBuilder->Y = maxs.Y - mins.Y;
	brushBuilder->Z = maxs.Z - mins.Z;
	brushBuilder->Build(world, lightmassImportanceVolume);

	return true;
}

void FBSPImporter::RenderTreeToActors(const Valve::BSPFile& bspFile, UWorld* world, TArray<AStaticMeshActor*>& out, uint32 nodeIndex)
{
	const static bool useCells = true;
	const static float cellSize = 1024.0f;

	// Gather all faces from tree
	TArray<uint16> faces;
	GatherFaces(bspFile, nodeIndex, faces);

	// Render whole tree to a single mesh
	FMeshDescription meshDesc;
	UStaticMesh::RegisterMeshAttributes(meshDesc);
	RenderFacesToMesh(bspFile, faces, meshDesc);

	// Early out if we're not using cells
	if (!useCells)
	{
		out.Add(RenderMeshToActor(world, meshDesc));
		return;
	}

	// Determine cell mins and maxs
	const Valve::BSP::snode_t& bspNode = bspFile.m_Nodes[nodeIndex];
	const int cellMinX = FMath::FloorToInt(bspNode.m_Mins[0] / cellSize);
	const int cellMaxX = FMath::CeilToInt(bspNode.m_Maxs[0] / cellSize);
	const int cellMinY = FMath::FloorToInt(bspNode.m_Mins[1] / cellSize);
	const int cellMaxY = FMath::CeilToInt(bspNode.m_Maxs[1] / cellSize);

	// Iterate each cell
	for (int cellX = cellMinX; cellX <= cellMaxX; ++cellX)
	{
		for (int cellY = cellMinY; cellY <= cellMaxY; ++cellY)
		{
			// Establish bounding planes for cell
			TArray<FPlane> boundingPlanes;
			boundingPlanes.Add(FPlane(FVector(cellX * cellSize), FVector::ForwardVector));
			boundingPlanes.Add(FPlane(FVector((cellX + 1) * cellSize), FVector::BackwardVector));
			boundingPlanes.Add(FPlane(FVector(cellY * cellSize), FVector::RightVector));
			boundingPlanes.Add(FPlane(FVector((cellY + 1) * cellSize), FVector::LeftVector));

			// Clip the mesh by the planes into a new one
			FMeshDescription cellMeshDesc = meshDesc;
			FMeshSplitter::Clip(cellMeshDesc, boundingPlanes);

			// Check if it has anything
			if (cellMeshDesc.Polygons().Num() > 0)
			{
				// Create a static mesh for it
				AStaticMeshActor* staticMeshActor = RenderMeshToActor(world, cellMeshDesc);
				staticMeshActor->SetActorLabel(FString::Printf(TEXT("Cell_%d_%d"), cellX, cellY));
				out.Add(staticMeshActor);
			}
		}
	}
}

AStaticMeshActor* FBSPImporter::RenderMeshToActor(UWorld* world, const FMeshDescription& meshDesc)
{
	UStaticMesh* staticMesh = NewObject<UStaticMesh>(world);
	FStaticMeshSourceModel& staticMeshSourceModel = staticMesh->AddSourceModel();
	FMeshBuildSettings& settings = staticMeshSourceModel.BuildSettings;
	settings.bRecomputeNormals = false;
	settings.bRecomputeTangents = false;
	settings.bGenerateLightmapUVs = true;
	settings.SrcLightmapIndex = 0;
	settings.DstLightmapIndex = 1;
	settings.bRemoveDegenerates = false;
	settings.bUseFullPrecisionUVs = true;
	settings.MinLightmapResolution = 128;
	staticMesh->LightMapResolution = 128;
	FMeshDescription* worldModelMesh = staticMesh->CreateMeshDescription(0);
	*worldModelMesh = meshDesc;
	const auto& importedMaterialSlotNameAttr = worldModelMesh->PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);
	for (const FPolygonGroupID& polyGroupID : worldModelMesh->PolygonGroups().GetElementIDs())
	{
		FName material = importedMaterialSlotNameAttr[polyGroupID];
		const int32 meshSlot = staticMesh->StaticMaterials.Emplace(nullptr, material, material);
		staticMesh->SectionInfoMap.Set(0, meshSlot, FMeshSectionInfo(meshSlot));
		staticMesh->SetMaterial(meshSlot, Cast<UMaterialInterface>(IHL2Editor::Get().TryResolveHL2Material(material.ToString())));
	}
	staticMesh->CommitMeshDescription(0);
	staticMesh->LightMapCoordinateIndex = 1;
	staticMesh->Build();

	FTransform transform = FTransform::Identity;
	transform.SetScale3D(FVector(1.0f, -1.0f, 1.0f));

	AStaticMeshActor* staticMeshActor = world->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), transform);
	UStaticMeshComponent* staticMeshComponent = staticMeshActor->GetStaticMeshComponent();
	staticMeshComponent->SetStaticMesh(staticMesh);
	FLightmassPrimitiveSettings& lightmassSettings = staticMeshComponent->LightmassSettings;
	lightmassSettings.bUseEmissiveForStaticLighting = true;
	staticMeshComponent->bCastShadowAsTwoSided = true;

	staticMeshActor->PostEditChange();
	return staticMeshActor;
}

AActor* FBSPImporter::ImportBrush(UWorld* world, const Valve::BSPFile& bspFile, uint16 brushIndex)
{
	const Valve::BSP::dbrush_t& brush = bspFile.m_Brushes[brushIndex];

	// Create and initialise brush actor
	ABrush* brushActor = world->SpawnActor<ABrush>(FVector::ZeroVector, FRotator::ZeroRotator);
	brushActor->SetActorLabel(FString::Printf(TEXT("Brush_%d"), brushIndex));
	brushActor->Brush = NewObject<UModel>(brushActor, NAME_None, RF_Transactional);
	brushActor->Brush->Initialize(nullptr, true);
	brushActor->Brush->Polys = NewObject<UPolys>(brushActor->Brush, NAME_None, RF_Transactional);
	brushActor->GetBrushComponent()->Brush = brushActor->Brush;

	// Create and initialise brush builder
	UVBSPBrushBuilder* brushBuilder = NewObject<UVBSPBrushBuilder>(brushActor);
	brushActor->BrushBuilder = brushBuilder;
	
	// Add all planes to the brush builder
	for (uint32 i = 0, l = brush.m_Numsides; i < l; ++i)
	{
		const Valve::BSP::dbrushside_t& brushSide = bspFile.m_Brushsides[brush.m_Firstside + i];
		const Valve::BSP::cplane_t& plane = bspFile.m_Planes[brushSide.m_Planenum];

		brushBuilder->Planes.Add(ValveToUnrealPlane(plane));
	}

	// Evaluate the geometric center of the brush and transform all planes to it
	FVector origin = brushBuilder->EvaluateGeometricCenter();
	FTransform transform = FTransform::Identity;
	transform.SetLocation(-origin);
	FMatrix transformMtx = transform.ToMatrixNoScale();
	for (uint32 i = 0, l = brush.m_Numsides; i < l; ++i)
	{
		brushBuilder->Planes[i] = brushBuilder->Planes[i].TransformBy(transformMtx);
	}

	// Relocate the brush
	brushActor->SetActorLocation(origin);

	// Build brush geometry
	brushBuilder->Build(world, brushActor);

	return brushActor;
}

void FBSPImporter::GatherBrushes(const Valve::BSPFile& bspFile, uint32 nodeIndex, TArray<uint16>& out)
{
	const Valve::BSP::snode_t& node = bspFile.m_Nodes[nodeIndex];
	if (node.m_Children[0] < 0)
	{
		const Valve::BSP::dleaf_t& leaf = bspFile.m_Leaves[-1-node.m_Children[0]];
		for (uint32 i = 0; i < leaf.m_Numleafbrushes; ++i)
		{
			out.AddUnique(bspFile.m_Leafbrushes[leaf.m_Firstleafbrush + i]);
		}
	}
	else
	{
		GatherBrushes(bspFile, (uint32)node.m_Children[0], out);
	}
	if (node.m_Children[1] < 0)
	{
		const Valve::BSP::dleaf_t& leaf = bspFile.m_Leaves[-1 - node.m_Children[1]];
		for (uint32 i = 0; i < leaf.m_Numleafbrushes; ++i)
		{
			out.AddUnique(bspFile.m_Leafbrushes[leaf.m_Firstleafbrush + i]);
		}
	}
	else
	{
		GatherBrushes(bspFile, (uint32)node.m_Children[1], out);
	}
}

void FBSPImporter::GatherFaces(const Valve::BSPFile& bspFile, uint32 nodeIndex, TArray<uint16>& out, TSet<int16>* clusterFilter)
{
	const Valve::BSP::snode_t& node = bspFile.m_Nodes[nodeIndex];
	if (node.m_Children[0] < 0)
	{
		const Valve::BSP::dleaf_t& leaf = bspFile.m_Leaves[-1 - node.m_Children[0]];
		if (clusterFilter == nullptr || clusterFilter->Contains(leaf.m_Cluster))
		{
			for (uint32 i = 0; i < leaf.m_Numleaffaces; ++i)
			{
				out.AddUnique(bspFile.m_Leaffaces[leaf.m_Firstleafface + i]);
			}
		}
	}
	else
	{
		GatherFaces(bspFile, (uint32)node.m_Children[0], out, clusterFilter);
	}
	if (node.m_Children[1] < 0)
	{
		const Valve::BSP::dleaf_t& leaf = bspFile.m_Leaves[-1 - node.m_Children[1]];
		if (clusterFilter == nullptr || clusterFilter->Contains(leaf.m_Cluster))
		{
			for (uint32 i = 0; i < leaf.m_Numleaffaces; ++i)
			{
				out.AddUnique(bspFile.m_Leaffaces[leaf.m_Firstleafface + i]);
			}
		}
	}
	else
	{
		GatherFaces(bspFile, (uint32)node.m_Children[1], out, clusterFilter);
	}
}

void FBSPImporter::GatherClusters(const Valve::BSPFile& bspFile, uint32 nodeIndex, TArray<int16>& out)
{
	const Valve::BSP::snode_t& node = bspFile.m_Nodes[nodeIndex];
	if (node.m_Children[0] < 0)
	{
		const Valve::BSP::dleaf_t& leaf = bspFile.m_Leaves[-1 - node.m_Children[0]];
		if (leaf.m_Numleaffaces > 0)
		{
			out.AddUnique(leaf.m_Cluster);
		}
	}
	else
	{
		GatherClusters(bspFile, (uint32)node.m_Children[0], out);
	}
	if (node.m_Children[1] < 0)
	{
		const Valve::BSP::dleaf_t& leaf = bspFile.m_Leaves[-1 - node.m_Children[1]];
		if (leaf.m_Numleaffaces > 0)
		{
			out.AddUnique(leaf.m_Cluster);
		}
	}
	else
	{
		GatherClusters(bspFile, (uint32)node.m_Children[1], out);
	}
}

FPlane FBSPImporter::ValveToUnrealPlane(const Valve::BSP::cplane_t& plane)
{
	return FPlane(plane.m_Normal(0, 0), plane.m_Normal(0, 1), plane.m_Normal(0, 2), plane.m_Distance);
}

void FBSPImporter::RenderFacesToMesh(const Valve::BSPFile& bspFile, const TArray<uint16>& faceIndices, FMeshDescription& meshDesc)
{
	TMap<uint32, FVertexID> valveToUnrealVertexMap;
	TMap<FName, FPolygonGroupID> materialToPolyGroupMap;
	TMap<FPolygonID, uint16> polyToValveFaceMap;

	TAttributesSet<FVertexID>& vertexAttr = meshDesc.VertexAttributes();
	TMeshAttributesRef<FVertexID, FVector> vertexAttrPosition = vertexAttr.GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);

	TAttributesSet<FVertexInstanceID>& vertexInstanceAttr = meshDesc.VertexInstanceAttributes();
	TMeshAttributesRef<FVertexInstanceID, FVector2D> vertexInstanceAttrUV = vertexInstanceAttr.GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
	// vertexInstanceAttrUV.SetNumIndices(2);

	TAttributesSet<FEdgeID>& edgeAttr = meshDesc.EdgeAttributes();
	TMeshAttributesRef<FEdgeID, bool> edgeAttrIsHard = edgeAttr.GetAttributesRef<bool>(MeshAttribute::Edge::IsHard);
	TMeshAttributesRef<FEdgeID, float> edgeCreaseSharpness = edgeAttr.GetAttributesRef<float>(MeshAttribute::Edge::CreaseSharpness);

	TAttributesSet<FPolygonGroupID>& polyGroupAttr = meshDesc.PolygonGroupAttributes();
	TMeshAttributesRef<FPolygonGroupID, FName> polyGroupMaterial = polyGroupAttr.GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);

	int cnt = 0;

	// Pass 1: Create geometry
	for (const uint16 faceIndex : faceIndices)
	{
		const Valve::BSP::dface_t& bspFace = bspFile.m_Surfaces[faceIndex];
		const Valve::BSP::texinfo_t& bspTexInfo = bspFile.m_Texinfos[bspFace.m_Texinfo];
		const uint16 texDataIndex = (uint16)bspTexInfo.m_Texdata;
		const Valve::BSP::texdata_t& bspTexData = bspFile.m_Texdatas[bspTexInfo.m_Texdata];
		const char* bspMaterialName = &bspFile.m_TexdataStringData[0] + bspFile.m_TexdataStringTable[bspTexData.m_NameStringTableID];
		FString parsedMaterialName = ParseMaterialName(bspMaterialName);
		if (parsedMaterialName.Contains(TEXT("tools/"), ESearchCase::IgnoreCase)) { continue; }
		if (parsedMaterialName.Contains(TEXT("tools\\"), ESearchCase::IgnoreCase)) { continue; }
		FName material(*parsedMaterialName);

		// Create polygroup if needed (we make one per material/texdata)
		FPolygonGroupID polyGroup;
		if (!materialToPolyGroupMap.Contains(material))
		{
			polyGroup = meshDesc.CreatePolygonGroup();
			materialToPolyGroupMap.Add(material, polyGroup);
			polyGroupMaterial[polyGroup] = material;
		}
		else
		{
			polyGroup = materialToPolyGroupMap[material];
		}

		TArray<FVertexInstanceID> polyVerts;
		TSet<uint16> bspVerts; // track all vbsp verts we've visited as it likes to revisit them sometimes and cause degenerate polys

		// Iterate all edges
		for (uint16 i = 0; i < bspFace.m_Numedges; ++i)
		{
			const int32 surfEdge = bspFile.m_Surfedges[bspFace.m_Firstedge + i];
			const uint32 edgeIndex = (uint32)(surfEdge < 0 ? -surfEdge : surfEdge);

			const Valve::BSP::dedge_t& bspEdge = bspFile.m_Edges[edgeIndex];
			const uint16 vertIndex = surfEdge < 0 ? bspEdge.m_V[1] : bspEdge.m_V[0];

			if (bspVerts.Contains(vertIndex)) { break; }
			bspVerts.Add(vertIndex);

			// Create vertices if needed
			FVertexID vert;
			if (!valveToUnrealVertexMap.Contains(vertIndex))
			{
				vert = meshDesc.CreateVertex();
				valveToUnrealVertexMap.Add(vertIndex, vert);
				const Valve::BSP::mvertex_t& bspVertex = bspFile.m_Vertexes[vertIndex];
				vertexAttrPosition[vert] = FVector(bspVertex.m_Position(0, 0), bspVertex.m_Position(0, 1), bspVertex.m_Position(0, 2));
			}
			else
			{
				vert = valveToUnrealVertexMap[vertIndex];
			}

			// Create vertex instance
			FVertexInstanceID vertInst = meshDesc.CreateVertexInstance(vert);

			// Calculate texture coords
			FVector pos = vertexAttrPosition[vert];
			{
				const FVector texU_XYZ = FVector(bspTexInfo.m_TextureVecs[0][0], bspTexInfo.m_TextureVecs[0][1], bspTexInfo.m_TextureVecs[0][2]);
				const float texU_W = bspTexInfo.m_TextureVecs[0][3];
				const FVector texV_XYZ = FVector(bspTexInfo.m_TextureVecs[1][0], bspTexInfo.m_TextureVecs[1][1], bspTexInfo.m_TextureVecs[1][2]);
				const float texV_W = bspTexInfo.m_TextureVecs[1][3];
				vertexInstanceAttrUV.Set(vertInst, 0, FVector2D(
					(FVector::DotProduct(texU_XYZ, pos) + texU_W) / bspTexData.m_Width,
					(FVector::DotProduct(texV_XYZ, pos) + texV_W) / bspTexData.m_Height
				));
			}
			//{
			//	const FVector texU_XYZ = FVector(bspTexInfo.m_LightmapVecs[0][0], bspTexInfo.m_LightmapVecs[0][1], bspTexInfo.m_LightmapVecs[0][2]);
			//	const float texU_W = bspTexInfo.m_LightmapVecs[0][3];
			//	const FVector texV_XYZ = FVector(bspTexInfo.m_LightmapVecs[1][0], bspTexInfo.m_LightmapVecs[1][1], bspTexInfo.m_LightmapVecs[1][2]);
			//	const float texV_W = bspTexInfo.m_LightmapVecs[1][3];
			//	vertexInstanceAttrUV.Set(vertInst, 1, FVector2D(
			//		((FVector::DotProduct(texU_XYZ, pos) + texU_W)/* - bspFace.m_LightmapTextureMinsInLuxels[0]*/) / (bspFace.m_LightmapTextureSizeInLuxels[0] + 1),
			//		((FVector::DotProduct(texV_XYZ, pos) + texV_W)/* - bspFace.m_LightmapTextureMinsInLuxels[1]*/) / (bspFace.m_LightmapTextureSizeInLuxels[1] + 1)
			//	));
			//}

			// Push
			polyVerts.Add(vertInst);
		}

		// Create poly
		if (polyVerts.Num() > 2)
		{
			FPolygonID poly = meshDesc.CreatePolygon(polyGroup, polyVerts);
			FMeshPolygon& polygon = meshDesc.GetPolygon(poly);
			polyToValveFaceMap.Add(poly, faceIndex);
		}
	}

	// Pass 2: Set smoothing groups
	for (const auto& pair : polyToValveFaceMap)
	{
		const Valve::BSP::dface_t& bspFace = bspFile.m_Surfaces[pair.Value];

		// Find all edges
		TArray<FEdgeID> edges;
		meshDesc.GetPolygonEdges(pair.Key, edges);
		for (const FEdgeID& edge : edges)
		{
			// Find polys connected to the edge
			const TArray<FPolygonID>& otherPolys = meshDesc.GetEdgeConnectedPolygons(edge);
			for (const FPolygonID& otherPoly : otherPolys)
			{
				if (otherPoly != pair.Key)
				{
					const Valve::BSP::dface_t& bspOtherFace = bspFile.m_Surfaces[polyToValveFaceMap[otherPoly]];
					const bool isHard = !SharesSmoothingGroup(bspFace.m_SmoothingGroups, bspOtherFace.m_SmoothingGroups);
					edgeAttrIsHard[edge] = isHard;
					edgeCreaseSharpness[edge] = isHard ? 1.0f : 0.0f;
				}
			}
		}
	}

	// Compute tangents and normals
	meshDesc.ComputeTangentsAndNormals(EComputeNTBsOptions::Normals & EComputeNTBsOptions::Tangents);

	// Triangulate
	meshDesc.TriangulateMesh();
}

FString FBSPImporter::ParseMaterialName(const char* bspMaterialName)
{
	// It might be something like "brick/brick06c" which is fine
	// But it might also be like "maps/<mapname>/brick/brick06c_x_y_z" which is not fine
	// We need to identify the latter and convert it to the former
	FString bspMaterialNameAsStr(bspMaterialName);
	const static FRegexPattern patternCubemappedMaterial(TEXT("^maps[\\\\\\/]\\w+[\\\\\\/](.+)(?:_-?(?:\\d*\\.)?\\d+){3}$"));
	FRegexMatcher matchCubemappedMaterial(patternCubemappedMaterial, bspMaterialNameAsStr);
	if (matchCubemappedMaterial.FindNext())
	{
		bspMaterialNameAsStr = matchCubemappedMaterial.GetCaptureGroup(1);
	}
	return bspMaterialNameAsStr;
}

bool FBSPImporter::SharesSmoothingGroup(uint16 groupA, uint16 groupB)
{
	for (uint16 i = 0; i < 16; ++i)
	{
		const uint16 mask = 1 << i;
		if ((groupA & mask) && (groupB & mask)) { return true; }
	}
	return false;
}

#endif