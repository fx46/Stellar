import re, sys
src = open(sys.argv[1], encoding='utf-8').read()
out_dir = sys.argv[2]

def block(name):
    m = re.search(r'public static readonly [\w\[\]]+ ' + name + r'\s*=\s*new [\w\[\]\d]+\s*\{(.*?)\n    \};', src, re.S)
    assert m, name
    return m.group(1)

def nums(s):
    return [int(x, 16) if x.lower().startswith('0x') else int(x) for x in re.findall(r'0x[0-9A-Fa-f]+|\b\d+\b', s)]

reg_class = nums(block('RegularCellClass')); assert len(reg_class) == 256
tr_class = nums(block('TransitionCellClass')); assert len(tr_class) == 512
tr_corner = nums(block('TransitionCornerData')); assert len(tr_corner) == 13

def cells(name, count):
    b = block(name)
    items = re.findall(r'new\(\s*(0x[0-9A-Fa-f]+)\s*,\s*new byte\[\]\s*\{([^}]*)\}\s*\)', b)
    assert len(items) == count, (name, len(items))
    return [(int(g, 16), nums(v)) for g, v in items]

reg_data = cells('RegularCellData', 16)
tr_data = cells('TransitionCellData', 56)

def jag(name, count):
    b = block(name)
    rows = re.findall(r'new ushort\[\]\s*\{([^}]*)\}', b)
    assert len(rows) == count, (name, len(rows))
    return [nums(r) for r in rows]

reg_vert = jag('RegularVertexData', 256)
tr_vert = jag('TransitionVertexData', 512)
for g, v in reg_data: assert len(v) == (g & 15) * 3 and len(v) <= 15
for g, v in tr_data: assert len(v) == (g & 15) * 3 and len(v) <= 36
assert max(len(r) for r in reg_vert) <= 12 and max(len(r) for r in tr_vert) <= 12

def row(vals, fmt='0x{:02X}', per=16, indent='\t'):
    lines = []
    for i in range(0, len(vals), per):
        lines.append(indent + ', '.join(fmt.format(v) for v in vals[i:i + per]) + ',')
    return '\n'.join(lines)

hdr = '''// Transvoxel Algorithm lookup tables.
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
'''

def cell_rows(data, maxn):
    lines = []
    for g, v in data:
        padded = v + [0] * (maxn - len(v))
        lines.append('\t{ 0x%02X, { %s } },' % (g, ', '.join(str(x) for x in padded)))
    return '\n'.join(lines)

def vert_rows(data):
    lines = []
    for r in data:
        padded = r + [0] * (12 - len(r))
        lines.append('\t{ %s },' % ', '.join('0x%04X' % x for x in padded))
    return '\n'.join(lines)

cpp = '''// Transvoxel Algorithm lookup tables (c) 2009 Eric Lengyel, https://transvoxel.org/.
// Generated from the Godot project's TransvoxelTables.cs (Scripts/convert_tables.py) - do not edit by hand.

#include "Terrain/TransvoxelTables.h"

namespace TransvoxelTables
{
const uint8 RegularCellClass[256] =
{
%s
};

const FRegularCellData RegularCellData[16] =
{
%s
};

const uint16 RegularVertexData[256][12] =
{
%s
};

const uint8 TransitionCellClass[512] =
{
%s
};

const FTransitionCellData TransitionCellData[56] =
{
%s
};

const uint8 TransitionCornerData[13] =
{
%s
};

const uint16 TransitionVertexData[512][12] =
{
%s
};
}
''' % (row(reg_class), cell_rows(reg_data, 15), vert_rows(reg_vert), row(tr_class), cell_rows(tr_data, 36),
       row(tr_corner), vert_rows(tr_vert))

open(out_dir + '/TransvoxelTables.h', 'w', newline='\n').write(hdr)
open(out_dir + '/TransvoxelTables.cpp', 'w', newline='\n').write(cpp)
print('ok', len(reg_vert), len(tr_vert))
