#pragma once

#include "MeshUtils.h"
#include "MeshAttributes.h"
#include "Model.h"
#include "Engine/Polys.h"
#include "BSPOps.h"

FMeshUtils::FMeshUtils() { }

/**
 * Clips a mesh and removes all geometry behind the specified planes.
 * Any polygons intersecting a plane will be cut.
 * Normals, tangents and texture coordinates will be preserved.
 */
void FMeshUtils::Clip(FMeshDescription& meshDesc, const TArray<FPlane>& clipPlanes)
{
	// Get attributes
	const TAttributesSet<FVertexID>& vertexAttr = meshDesc.VertexAttributes();
	TMeshAttributesConstRef<FVertexID, FVector> vertexAttrPosition = vertexAttr.GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);

	TArray<FVertexInstanceID> arr1, arr2;

	// Iterate all polys
	TArray<FPolygonID> allPolyIDs;
	for (const FPolygonID& polyID : meshDesc.Polygons().GetElementIDs())
	{
		allPolyIDs.Add(polyID);
	}
	for (const FPolygonID& polyID : allPolyIDs)
	{
		FMeshPolygon poly = meshDesc.GetPolygon(polyID);
		const FPolygonGroupID& polyGroupID = meshDesc.GetPolygonPolygonGroup(polyID);
		TArray<FVertexInstanceID>* oldPoly = &arr1;
		TArray<FVertexInstanceID>* newPoly = &arr2;

		newPoly->Empty(poly.PerimeterContour.VertexInstanceIDs.Num());
		newPoly->Append(poly.PerimeterContour.VertexInstanceIDs);

		// Iterate all planes
		bool changesMade = false;
		for (const FPlane& plane : clipPlanes)
		{
			Swap(oldPoly, newPoly);
			newPoly->Empty(oldPoly->Num());
			bool firstVert = true, lastWasClipped = false;
			FVertexInstanceID lastVertInstID;

			// Go through each vert and identify ones that move from one side of the plane to the other
			for (int i = 0, l = oldPoly->Num(); i <= l; ++i)
			{
				const bool last = i == l;
				const FVertexInstanceID& vertInstID = (*oldPoly)[i % l];

				const FVertexID& vertID = meshDesc.GetVertexInstanceVertex(vertInstID);
				const FVector vertPos = vertexAttrPosition[vertID];

				const bool isClipped = plane.PlaneDot(vertPos) < 0.0f;
				if (isClipped) { changesMade = true; }
				if (firstVert)
				{
					firstVert = false;
				}
				else
				{
					if (isClipped != lastWasClipped)
					{
						newPoly->Add(ClipEdge(meshDesc, lastVertInstID, vertInstID, plane));
					}
				}
				if (!isClipped && !last)
				{
					newPoly->Add(vertInstID);
				}
				lastWasClipped = isClipped;
				lastVertInstID = vertInstID;
			}

			// If there's no poly left, early out
			if (newPoly->Num() < 3) { break; }
		}

		// If there's no a poly left, delete it
		if (newPoly->Num() < 3)
		{
			meshDesc.DeletePolygon(polyID);
		}
		// If some form of clipping happened, replace it
		else if (changesMade)
		{
			meshDesc.DeletePolygon(polyID);
			meshDesc.CreatePolygon(polyGroupID, *newPoly);
		}
	}

	// Clean up after ourselves
	Clean(meshDesc);
}

FVertexInstanceID FMeshUtils::ClipEdge(FMeshDescription& meshDesc, const FVertexInstanceID& vertAInstID, const FVertexInstanceID& vertBInstID, const FPlane& clipPlane)
{
	// Lookup base vertices
	const FVertexID& vertAID = meshDesc.GetVertexInstanceVertex(vertAInstID);
	const FVertexID& vertBID = meshDesc.GetVertexInstanceVertex(vertBInstID);

	// Grab all mesh attributes
	TAttributesSet<FVertexID>& vertexAttr = meshDesc.VertexAttributes();
	TMeshAttributesRef<FVertexID, FVector> vertexAttrPosition = vertexAttr.GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);
	TAttributesSet<FVertexInstanceID>& vertexInstAttr = meshDesc.VertexInstanceAttributes();
	TMeshAttributesRef<FVertexInstanceID, FVector> vertexInstAttrNormal = vertexInstAttr.GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal);
	TMeshAttributesRef<FVertexInstanceID, FVector> vertexInstAttrTangent = vertexInstAttr.GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Tangent);
	TMeshAttributesRef<FVertexInstanceID, FVector2D> vertexInstAttrUV0 = vertexInstAttr.GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
	TMeshAttributesRef<FVertexInstanceID, FVector4> vertexInstAttrCol = vertexInstAttr.GetAttributesRef<FVector4>(MeshAttribute::VertexInstance::Color);

	// Lookup vertex positions
	const FVector& vertAPos = vertexAttrPosition[vertAID];
	const FVector& vertBPos = vertexAttrPosition[vertBID];

	// Intersect the line from vertA to vertB with the plane
	float mu;
	{
		const FVector pointOnPlane = FVector::PointPlaneProject(FVector::ZeroVector, clipPlane);
		mu = FMath::Clamp(FVector::DotProduct(pointOnPlane - vertAPos, clipPlane) / FVector::DotProduct(clipPlane, vertBPos - vertAPos), 0.0f, 1.0f);
	}

	// Create new vertex
	const FVertexID newVertID = meshDesc.CreateVertex();
	vertexAttrPosition[newVertID] = FMath::Lerp(vertAPos, vertBPos, mu);

	// Create new vertex instance
	const FVertexInstanceID newVertInstID = meshDesc.CreateVertexInstance(newVertID);
	vertexInstAttrNormal[newVertInstID] = FMath::Lerp(vertexInstAttrNormal[vertAInstID], vertexInstAttrNormal[vertBInstID], mu).GetUnsafeNormal();
	vertexInstAttrTangent[newVertInstID] = FMath::Lerp(vertexInstAttrTangent[vertAInstID], vertexInstAttrTangent[vertBInstID], mu).GetUnsafeNormal();
	vertexInstAttrUV0[newVertInstID] = FMath::Lerp(vertexInstAttrUV0[vertAInstID], vertexInstAttrUV0[vertBInstID], mu);
	vertexInstAttrCol[newVertInstID] = FMath::Lerp(vertexInstAttrCol[vertAInstID], vertexInstAttrCol[vertBInstID], mu);

	return newVertInstID;
}

void FMeshUtils::Clean(FMeshDescription& meshDesc)
{
	// Delete degenerate polygons
	TAttributesSet<FVertexID>& vertexAttr = meshDesc.VertexAttributes();
	TMeshAttributesRef<FVertexID, FVector> vertexAttrPosition = vertexAttr.GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);
	for (const FPolygonID& polyID : meshDesc.Polygons().GetElementIDs())
	{
		FMeshPolygon& poly = meshDesc.GetPolygon(polyID);
		const int numVerts = poly.PerimeterContour.VertexInstanceIDs.Num();
		TArray<FVertexInstanceID> toDelete;
		for (int i = 0; i < numVerts; ++i)
		{
			const FVertexInstanceID& vertAinstID = poly.PerimeterContour.VertexInstanceIDs[i];
			const FVertexID& vertAID = meshDesc.GetVertexInstanceVertex(vertAinstID);
			const FVector& vertAPos = vertexAttrPosition[vertAID];
			for (int j = i + 1; j < numVerts; ++j)
			{
				const FVertexInstanceID& vertBinstID = poly.PerimeterContour.VertexInstanceIDs[j];
				const FVertexID& vertBID = meshDesc.GetVertexInstanceVertex(vertBinstID);
				const FVector& vertBPos = vertexAttrPosition[vertBID];
				if (vertAPos.Equals(vertBPos, 0.00001f))
				{
					toDelete.AddUnique(vertBinstID);
				}
			}
		}
		if (numVerts - toDelete.Num() < 3)
		{
			meshDesc.DeletePolygon(polyID);
		}
		else if (toDelete.Num() > 0)
		{
			TArray<FVertexInstanceID> newPerimeterContour = poly.PerimeterContour.VertexInstanceIDs;
			for (const FVertexInstanceID& vertInstID : toDelete)
			{
				newPerimeterContour.Remove(vertInstID);
			}
			const FPolygonGroupID polyGroupID = meshDesc.GetPolygonPolygonGroup(polyID);
			meshDesc.DeletePolygon(polyID);
			meshDesc.CreatePolygonWithID(polyID, polyGroupID, newPerimeterContour);
		}
	}

	// Delete unused edges
	for (const FEdgeID& edgeID : meshDesc.Edges().GetElementIDs())
	{
		if (meshDesc.GetEdge(edgeID).ConnectedPolygons.Num() == 0)
		{
			meshDesc.DeleteEdge(edgeID);
		}
	}

	//// Delete unused vertex instances
	for (const FVertexInstanceID& vertInstID : meshDesc.VertexInstances().GetElementIDs())
	{
		if (meshDesc.GetVertexInstance(vertInstID).ConnectedPolygons.Num() == 0)
		{
			meshDesc.DeleteVertexInstance(vertInstID);
		}
	}

	//// Delete unused vertices
	for (const FVertexID& vertID : meshDesc.Vertices().GetElementIDs())
	{
		if (meshDesc.GetVertex(vertID).VertexInstanceIDs.Num() == 0)
		{
			meshDesc.DeleteVertex(vertID);
		}
	}

	//// Delete any empty polygon groups
	for (const FPolygonGroupID& polyGroupID : meshDesc.PolygonGroups().GetElementIDs())
	{
		if (meshDesc.GetPolygonGroup(polyGroupID).Polygons.Num() == 0)
		{
			meshDesc.DeletePolygonGroup(polyGroupID);
		}
	}

	// Remap element IDs
	FElementIDRemappings remappings;
	meshDesc.Compact(remappings);

	// Retriangulate mesh
	meshDesc.TriangulateMesh();
}

struct FMeshConnectivityVertex
{
	FVector				Position;
	TArray<int32>		Triangles;

	/** Constructor */
	FMeshConnectivityVertex(const FVector& v)
		: Position(v)
	{
	}

	/** Check if this vertex is in the same place as given point */
	FORCEINLINE bool IsSame(const FVector& v)
	{
		const float eps = 0.01f;
		return v.Equals(Position, eps);
	}

	/** Add link to triangle */
	FORCEINLINE void AddTriangleLink(int32 Triangle)
	{
		Triangles.Add(Triangle);
	}
};

struct FMeshConnectivityTriangle
{
	int32				Vertices[3];
	int32				Group;

	/** Constructor */
	FMeshConnectivityTriangle(int32 a, int32 b, int32 c)
		: Group(INDEX_NONE)
	{
		Vertices[0] = a;
		Vertices[1] = b;
		Vertices[2] = c;
	}
};

struct FMeshConnectivityGroup
{
	TArray<int32>		Triangles;
};

class FMeshConnectivityBuilder
{
public:
	TArray<FMeshConnectivityVertex>		Vertices;
	TArray<FMeshConnectivityTriangle>	Triangles;
	TArray<FMeshConnectivityGroup>		Groups;

public:
	/** Add vertex to connectivity information */
	int32 AddVertex(const FVector& v)
	{
		// Try to find existing vertex
		// TODO: should use hash map
		for (int32 i = 0; i < Vertices.Num(); ++i)
		{
			if (Vertices[i].IsSame(v))
			{
				return i;
			}
		}

		// Add new vertex
		new (Vertices) FMeshConnectivityVertex(v);
		return Vertices.Num() - 1;
	}

	/** Add triangle to connectivity information */
	int32 AddTriangle(const FVector& a, const FVector& b, const FVector& c)
	{
		// Map vertices
		int32 VertexA = AddVertex(a);
		int32 VertexB = AddVertex(b);
		int32 VertexC = AddVertex(c);

		// Make sure triangle is not degenerated
		if (VertexA != VertexB && VertexB != VertexC && VertexC != VertexA)
		{
			// Setup connectivity info
			int32 TriangleIndex = Triangles.Num();
			Vertices[VertexA].AddTriangleLink(TriangleIndex);
			Vertices[VertexB].AddTriangleLink(TriangleIndex);
			Vertices[VertexC].AddTriangleLink(TriangleIndex);

			// Create triangle
			new (Triangles) FMeshConnectivityTriangle(VertexA, VertexB, VertexC);
			return TriangleIndex;
		}
		else
		{
			// Degenerated triangle
			return INDEX_NONE;
		}
	}

	/** Create connectivity groups */
	void CreateConnectivityGroups()
	{
		// Delete group list
		Groups.Empty();

		// Reset group assignments
		for (int32 i = 0; i < Triangles.Num(); i++)
		{
			Triangles[i].Group = INDEX_NONE;
		}

		// Flood fill using connectivity info
		for (;; )
		{
			// Find first triangle without group assignment
			int32 InitialTriangle = INDEX_NONE;
			for (int32 i = 0; i < Triangles.Num(); i++)
			{
				if (Triangles[i].Group == INDEX_NONE)
				{
					InitialTriangle = i;
					break;
				}
			}

			// No more unassigned triangles, flood fill is done
			if (InitialTriangle == INDEX_NONE)
			{
				break;
			}

			// Create group
			int32 GroupIndex = Groups.AddZeroed(1);

			// Start flood fill using connectivity information
			FloodFillTriangleGroups(InitialTriangle, GroupIndex);
		}
	}

private:
	/** FloodFill core */
	void FloodFillTriangleGroups(int32 InitialTriangleIndex, int32 GroupIndex)
	{
		TArray<int32> TriangleStack;

		// Start with given triangle
		TriangleStack.Add(InitialTriangleIndex);

		// Set the group for our first triangle
		Triangles[InitialTriangleIndex].Group = GroupIndex;

		// Process until we have triangles in stack
		while (TriangleStack.Num())
		{
			// Pop triangle index from stack
			int32 TriangleIndex = TriangleStack.Pop();

			FMeshConnectivityTriangle& Triangle = Triangles[TriangleIndex];

			// All triangles should already have a group before we start processing neighbors
			checkSlow(Triangle.Group == GroupIndex);

			// Add to list of triangles in group
			Groups[GroupIndex].Triangles.Add(TriangleIndex);

			// Recurse to all other triangles connected with this one
			for (int32 i = 0; i < 3; i++)
			{
				int32 VertexIndex = Triangle.Vertices[i];
				const FMeshConnectivityVertex& Vertex = Vertices[VertexIndex];

				for (int32 j = 0; j < Vertex.Triangles.Num(); j++)
				{
					int32 OtherTriangleIndex = Vertex.Triangles[j];
					FMeshConnectivityTriangle& OtherTriangle = Triangles[OtherTriangleIndex];

					// Only recurse if triangle was not already assigned to a group
					if (OtherTriangle.Group == INDEX_NONE)
					{
						// OK, the other triangle now belongs to our group!
						OtherTriangle.Group = GroupIndex;

						// Add the other triangle to the stack to be processed
						TriangleStack.Add(OtherTriangleIndex);
					}
				}
			}
		}
	}
};

void FMeshUtils::DecomposeUCXMesh(const TArray<FVector>& CollisionVertices, const TArray<int32>& CollisionFaceIdx, UBodySetup* BodySetup)
{
	// We keep no ref to this Model, so it will be GC'd at some point after the import.
	auto TempModel = NewObject<UModel>();
	TempModel->Initialize(nullptr, 1);

	FMeshConnectivityBuilder ConnectivityBuilder;

	// Send triangles to connectivity builder
	for (int32 x = 0; x < CollisionFaceIdx.Num(); x += 3)
	{
		const FVector& VertexA = CollisionVertices[CollisionFaceIdx[x + 2]];
		const FVector& VertexB = CollisionVertices[CollisionFaceIdx[x + 1]];
		const FVector& VertexC = CollisionVertices[CollisionFaceIdx[x + 0]];
		ConnectivityBuilder.AddTriangle(VertexA, VertexB, VertexC);
	}

	ConnectivityBuilder.CreateConnectivityGroups();

	// For each valid group build BSP and extract convex hulls
	for (int32 i = 0; i < ConnectivityBuilder.Groups.Num(); i++)
	{
		const FMeshConnectivityGroup& Group = ConnectivityBuilder.Groups[i];

		// TODO: add some BSP friendly checks here
		// e.g. if group triangles form a closed mesh

		// Generate polygons from group triangles
		TempModel->Polys->Element.Empty();

		for (int32 j = 0; j < Group.Triangles.Num(); j++)
		{
			const FMeshConnectivityTriangle& Triangle = ConnectivityBuilder.Triangles[Group.Triangles[j]];

			FPoly* Poly = new(TempModel->Polys->Element) FPoly();
			Poly->Init();
			Poly->iLink = j / 3;

			// Add vertices
			new(Poly->Vertices) FVector(ConnectivityBuilder.Vertices[Triangle.Vertices[0]].Position);
			new(Poly->Vertices) FVector(ConnectivityBuilder.Vertices[Triangle.Vertices[1]].Position);
			new(Poly->Vertices) FVector(ConnectivityBuilder.Vertices[Triangle.Vertices[2]].Position);

			// Update polygon normal
			Poly->CalcNormal(1);
		}

		// Build bounding box.
		TempModel->BuildBound();

		// Build BSP for the brush.
		FBSPOps::bspBuild(TempModel, FBSPOps::BSP_Good, 15, 70, 1, 0);
		FBSPOps::bspRefresh(TempModel, 1);
		FBSPOps::bspBuildBounds(TempModel);

		// Convert collision model into a collection of convex hulls.
		// Generated convex hulls will be added to existing ones
		BodySetup->CreateFromModel(TempModel, false);
	}
}