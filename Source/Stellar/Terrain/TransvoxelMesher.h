#pragma once

#include "CoreMinimal.h"

/** One meshed region: node-local positions, normals and colours, and triangle indices. */
struct FTransvoxelMesh
{
	TArray<FVector3f> Vertices;
	TArray<FVector3f> Normals;
	TArray<FColor> Colors;
	TArray<int32> Indices;

	/** Everything past these counts is transition-cell geometry (for debug views). */
	int32 RegularVertexCount = 0;
	int32 RegularIndexCount = 0;

	bool IsEmpty() const { return Indices.Num() == 0; }
};

/** Signed-distance-style density field. Negative = inside solid. */
using FDensityFunc = TFunctionRef<float(float X, float Y, float Z)>;

/**
 * Fills a region's corner grid only where surface can be: the n-1 cells per axis are blocks of
 * BlockCells^3, and Active[(bx*nb + by)*nb + bz] (nb = (n-1)/BlockCells) comes back true for
 * every block that may hold surface, with all of its corners filled exactly (the values the
 * density gives at Origin + i * Stride). Every corner of an inactive block has one and the same
 * sign (no surface in it); its values are left unset.
 */
using FSparseCornerFunc = TFunctionRef<void(const FVector3f& Origin, float Stride, int32 N, float* Grid, int32 BlockCells, bool* Active)>;

/** Optional per-vertex colour: receives the vertex position and its surface normal. */
using FVertexColorFunc = TFunctionRef<FColor(const FVector3f& Position, const FVector3f& Normal)>;

/**
 * Transvoxel mesher (Lengyel, https://transvoxel.org/), ported from the Godot project.
 * Regular Marching Cubes cells plus transition cells on chunk faces whose neighbour is one LOD
 * COARSER. Transition cells belong to the FINER chunk; coarse chunks never deform, so same-LOD
 * interfaces are always sealed by shared samples.
 *
 * Boundary deformation (Lengyel 4.4-4.5): vertices within one cell of a flagged face receive an
 * inward offset (up to s/2, ramped to zero one cell in) PROJECTED ONTO THE SURFACE TANGENT
 * PLANE, so vertices slide along the isosurface instead of off it; the band this opens next to
 * the boundary plane is filled by the transition cells (full-res side deformed, half-res side on
 * the undeformed boundary plane, matching the coarse neighbour's plain mesh).
 *
 * Triangles are emitted reversed from Lengyel's reference order (as the Godot version does):
 * their algebraic normal (b - a) x (c - a) points into the solid, which is what Unreal renders
 * as the front face of ground seen from above.
 */
namespace TransvoxelMesher
{
	/** Half the central-difference step of vertex normals (metres). Must not depend on the LOD
	 * stride: chunks of different LODs share seam vertices. */
	constexpr float GradientStep = 0.5f;

	/**
	 * Mesh a cube of Cells cells from Origin with cell size Stride. CoarserFaces is a bit mask
	 * (axis * 2 + side, side 0 = negative): faces whose neighbour is one LOD coarser. Corners
	 * (optional) fills the corner grid sparsely; without it every corner is sampled. Cells must
	 * be even when any face is flagged.
	 */
	STELLAR_API void MeshRegion(FTransvoxelMesh& Out, FDensityFunc Density, const FVector3f& Origin, float Stride, int32 Cells,
		int32 CoarserFaces, const FVertexColorFunc* VertexColor, const FSparseCornerFunc* Corners);
}
