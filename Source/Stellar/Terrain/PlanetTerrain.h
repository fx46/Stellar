#pragma once

#include "CoreMinimal.h"
#include "Misc/ScopeRWLock.h"
#include "Terrain/PlanetField.h"
#include "Terrain/PlanetOctree.h"
#include "Terrain/TerrainEdits.h"
#include "Terrain/TransvoxelMesher.h"

/**
 * A huge planet's terrain (port of the Godot ScaleTerrain, plus edits): its field, its
 * level-of-detail octree, which nodes can hold surface or be seen, and each node's mesh.
 * Meshes come from the Transvoxel mesher a node at a time, in node-local coordinates (planet
 * axes, origin at the node's minimum corner, metres): the density adds the node's origin in
 * doubles, so a mesh is as precise on the far side of a 1000 km planet as at its centre.
 *
 * Edits (FTerrainEdits) are an offset over the procedural field. The height bounds that let
 * meshing skip empty space only describe the procedural ground, so any node or block an edited
 * brick can reach is treated as possibly holding surface.
 */
class STELLAR_API FPlanetTerrain
{
public:
	/** Cells per node along an axis (even, for the transition cells). */
	static constexpr int32 Cells = 32;

	FPlanetTerrain(double Radius, int32 Seed = 1337, double LeafSize = 8.0, double SplitFactor = 2.0);

	const FPlanetField Field;
	const FPlanetOctree Octree;

	/** Lattice spacing of the finest nodes, and of the edit lattice (metres). */
	double LeafStride() const { return Octree.LeafSize / Cells; }

	/** The node's box (planet frame), grown by what its mesh reads beyond it (normals). */
	FBox3d ReadBox(const FNodeKey& K) const;

	/** Whether procedural surface can pass through the node (cached): false only when it
	 * provably cannot. Tested whole, then as 8 and 64 sub-boxes, whose bounds are tighter. */
	bool HoldsProceduralSurface(const FNodeKey& K) const;

	/** Whether surface can pass through the node, edits included. */
	bool HoldsSurface(const FNodeKey& K, const FTerrainEdits* Edits) const;

	/** Whether the procedural surface can pass through the cube at Lo of side Size. */
	bool MayHoldSurface(double X, double Y, double Z, double Size) const;

	/** Whether any of the node can be seen from C over the planet's curve. */
	bool Visible(const FNodeKey& K, const FVector3d& C) const;

	/** The nodes to mesh for a camera at C (planet frame). */
	TArray<FNodeKey> Select(const FVector3d& C, const FTerrainEdits* Edits) const;

	/** The node's mesh, node-local metres (empty when no surface crosses it). */
	void Mesh(const FNodeKey& K, int32 CoarserFaces, const FTerrainEdits* Edits, FTransvoxelMesh& Out, bool bDense = false) const;

	/** Density at a planet-frame point, edits included. */
	double Density(const FVector3d& P, const FTerrainEdits* Edits) const;

	/** Outward surface normal (normalised density gradient) at P. */
	FVector3d Normal(const FVector3d& P, const FTerrainEdits* Edits, double Step = 0.15) const;

	/** The original ground's height above the radius in the direction of P, and the point. */
	FVector3d SurfaceAt(const FVector3d& P, double* OutHeight = nullptr) const;

	/** First point along a ray where the density turns negative, by marching and bisection. */
	bool Trace(const FVector3d& From, const FVector3d& Dir, double MaxDistance, const FTerrainEdits* Edits, FVector3d& OutHit) const;

	/** The vertex colour at a node-local vertex: the surface's colour on original ground, the
	 * rock's in a cut (so cuts show the strata). */
	FColor VertexColor(const FVector3d& NodeMin, const FVector3f& Local, const FVector3f& Normal, bool bEdited, float Stride) const;

	int32 CachedNodes() const;

private:
	bool MayHold(double X, double Y, double Z, double Size, int32 Depth) const;

	mutable FRWLock HoldsLock;
	mutable TMap<FNodeKey, bool> Holds;
};
