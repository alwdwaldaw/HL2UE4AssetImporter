#pragma once

#include "CoreMinimal.h"
#include "MeshDescription.h"

#include "PhysicsEngine/BodySetup.h"

class FMeshUtils
{
private:

	FMeshUtils();
	
public:

	/**
	 * Clips a mesh and removes all geometry behind the specified planes.
	 * Any polygons intersecting a plane will be cut.
	 * Normals, tangents and texture coordinates will be preserved.
	 */
	static void Clip(FMeshDescription& meshDesc, const TArray<FPlane>& clipPlanes);

	/**
	 * Cleans a mesh, removing degenerate edges and polys, and removing unused elements.
	 */
	static void Clean(FMeshDescription& meshDesc);

	/**
	 * Decomposes a UCX mesh into a body setup.
	 */
	static void DecomposeUCXMesh(const TArray<FVector>& CollisionVertices, const TArray<int32>& CollisionFaceIdx, UBodySetup* BodySetup);

private:

	static FVertexInstanceID ClipEdge(FMeshDescription& meshDesc, const FVertexInstanceID& a, const FVertexInstanceID& b, const FPlane& clipPlane);

	
};
