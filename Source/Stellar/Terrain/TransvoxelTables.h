// Transvoxel Algorithm lookup tables.
// Converted from Eric Lengyel's Transvoxel tables (https://transvoxel.org/).
// Copyright 2009 by Eric Lengyel. The tables may be used freely with attribution.
// Generated from the Godot project's TransvoxelTables.cs (Scripts/convert_tables.py) - do not edit by hand.

#pragma once

#include "CoreMinimal.h"

/** Triangulation data for one equivalence class of a regular or transition cell. */
template <int32 MaxIndices>
struct TTransvoxelCellData
{
	/** High nibble = vertex count, low nibble = triangle count. */
	uint8 GeometryCounts;
	/** Groups of 3 indices giving the triangulation. */
	uint8 VertexIndex[MaxIndices];

	int32 VertexCount() const { return GeometryCounts >> 4; }
	int32 TriangleCount() const { return GeometryCounts & 0x0F; }
};

using FRegularCellData = TTransvoxelCellData<15>;
using FTransitionCellData = TTransvoxelCellData<36>;

namespace TransvoxelTables
{
	/** Maps an 8-bit regular Marching Cubes case index to an equivalence class index (0-15). */
	extern const uint8 RegularCellClass[256];
	/** Triangulation data for the 16 distinct regular cell classes. */
	extern const FRegularCellData RegularCellData[16];
	/** Per case, the vertex edge codes (high byte: reuse data, low byte: corner pair). Unused entries are 0. */
	extern const uint16 RegularVertexData[256][12];
	/** Maps a 9-bit transition cell case index to an equivalence class index; bit 7 flags inverted winding. */
	extern const uint8 TransitionCellClass[512];
	/** Triangulation data for the 56 distinct transition cell classes. */
	extern const FTransitionCellData TransitionCellData[56];
	/** Corner reuse data for the 13 transition cell samples. */
	extern const uint8 TransitionCornerData[13];
	/** Per case, the transition vertex edge codes. Unused entries are 0. */
	extern const uint16 TransitionVertexData[512][12];
}
