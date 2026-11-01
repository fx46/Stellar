#pragma once

#include "CoreMinimal.h"
#include "Terrain/PlanetField.h"

/**
 * An octree node: Level 0 is a leaf of FPlanetOctree::LeafSize, each level up doubles the size.
 * X/Y/Z count nodes of that level from the root cube's minimum corner.
 */
struct FNodeKey
{
	int32 Level = 0;
	int32 X = 0, Y = 0, Z = 0;

	FNodeKey() = default;
	FNodeKey(int32 InLevel, int32 InX, int32 InY, int32 InZ) : Level(InLevel), X(InX), Y(InY), Z(InZ) {}

	FNodeKey Parent() const { return FNodeKey(Level + 1, X >> 1, Y >> 1, Z >> 1); }
	FNodeKey Child(int32 I) const { return FNodeKey(Level - 1, X * 2 + (I & 1), Y * 2 + ((I >> 1) & 1), Z * 2 + ((I >> 2) & 1)); }

	bool operator==(const FNodeKey& O) const { return Level == O.Level && X == O.X && Y == O.Y && Z == O.Z; }
	bool operator!=(const FNodeKey& O) const { return !(*this == O); }

	friend uint32 GetTypeHash(const FNodeKey& K)
	{
		return HashCombineFast(HashCombineFast(::GetTypeHash(K.Level), ::GetTypeHash(K.X)), HashCombineFast(::GetTypeHash(K.Y), ::GetTypeHash(K.Z)));
	}
};

/**
 * Which parts of a huge planet are meshed, and how coarsely: a level-of-detail octree over a
 * cube around the planet (centre at the origin). Every node is meshed with the same number of
 * cells, so a node twice as far is twice as coarse and twice as large - the planet's whole
 * surface is a few thousand meshes at any radius.
 *
 * A node is split while the camera is within SplitFactor of its size (distance to its box).
 * Face neighbours in the result then differ by at most one level - what the Transvoxel
 * transition cells stitch - for any split factor above sqrt(3): if a meshed node A of size s
 * touched a node two levels finer, that node's parent Q (size s/2, touching A) was split, so
 * dist(camera, Q) < K s/2, and dist(camera, A) <= dist(camera, Q) + diag(Q) < K s/2 + 0.87 s;
 * but A was not split, so dist(camera, A) >= K s, which needs K < 1.73. Nodes that cannot hold
 * surface, or cannot be seen, are dropped, not split (dropping only removes nodes: it cannot
 * bring two levels together). Engine-free.
 */
class STELLAR_API FPlanetOctree
{
public:
	FPlanetOctree(double BoundRadius, double InLeafSize = 8.0, double InSplitFactor = 2.0);

	double LeafSize;
	int32 RootLevel;
	/** Half the root cube's side; the cube spans [-Half, Half] on each axis. */
	double Half;
	/** Split while the camera is closer than this many node sizes. */
	double SplitFactor;

	FNodeKey Root() const { return FNodeKey(RootLevel, 0, 0, 0); }
	double Size(int32 Level) const { return LeafSize * double(1ll << Level); }

	/** The node's minimum corner (planet frame). */
	FVector3d Min(const FNodeKey& K) const;

	/** Distance from a point to the node's box (0 inside). */
	double Distance(const FNodeKey& K, const FVector3d& P) const;

	/** Nearest and farthest distance from the planet's centre to the node's box. */
	FRange RadialRange(const FNodeKey& K) const;

	/** Nearest and farthest distance from the planet's centre to the cube at Lo of side S. */
	static FRange RadialRange(double X0, double Y0, double Z0, double S);

	/**
	 * The nodes to mesh for a camera at C (planet frame): split by distance, dropped (with
	 * everything inside them) where Keep says no. Each level's candidates are tested together
	 * (in parallel when bParallel), since the test samples the terrain.
	 */
	TArray<FNodeKey> Select(const FVector3d& C, TFunctionRef<bool(const FNodeKey&)> Keep, bool bParallel = true) const;

	/** Bit mask (bit = face index, axis * 2 + side) of the faces across which the node in Cut
	 * is one level coarser - the faces whose transition cells this node owns. */
	int32 CoarserFaces(const FNodeKey& K, const TSet<FNodeKey>& Cut) const;
};
