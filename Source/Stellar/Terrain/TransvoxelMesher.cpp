#include "Terrain/TransvoxelMesher.h"
#include "Terrain/TransvoxelTables.h"

namespace
{
	// Lengyel regular-cell corner numbering: corner i -> (i&1, (i>>1)&1, (i>>2)&1).
	constexpr int32 CornerX(int32 I) { return I & 1; }
	constexpr int32 CornerY(int32 I) { return (I >> 1) & 1; }
	constexpr int32 CornerZ(int32 I) { return (I >> 2) & 1; }
	constexpr int32 CornerAxis(int32 I, int32 Axis) { return (I >> Axis) & 1; }

	// Face index = axis * 2 + side (side 0 = negative, 1 = positive).
	// Tangent bases per face, U x V = W with W pointing INTO the chunk (right-handed algebra).
	constexpr int32 FaceU[6][3] = {
		{ 0, 1, 0 }, // -X: U=+Y
		{ 0, 0, 1 }, // +X: U=+Z
		{ 0, 0, 1 }, // -Y: U=+Z
		{ 1, 0, 0 }, // +Y: U=+X
		{ 1, 0, 0 }, // -Z: U=+X
		{ 0, 1, 0 }, // +Z: U=+Y
	};
	constexpr int32 FaceV[6][3] = {
		{ 0, 0, 1 }, // -X: V=+Z
		{ 0, 1, 0 }, // +X: V=+Y
		{ 1, 0, 0 }, // -Y: V=+X
		{ 0, 0, 1 }, // +Y: V=+Z
		{ 0, 1, 0 }, // -Z: V=+Y
		{ 1, 0, 0 }, // +Z: V=+X
	};

	// Transition case code bit order: samples 0,1,2,5,8,7,6,3 -> bits 0..7, sample 4 -> bit 8.
	constexpr int32 TransCaseOrder[9] = { 0, 1, 2, 5, 8, 7, 6, 3, 4 };
	// Half-resolution samples 9,A,B,C share density values with full-res samples 0,2,6,8.
	constexpr int32 HalfResMap[4] = { 0, 2, 6, 8 };

	/** Edge key -> vertex index for one region: a flat table with a generation stamp per slot
	 * (no clearing between regions, no hashing). */
	struct FEdgeMap
	{
		TArray<int32> Stamp, Vertex;
		int32 Generation = 0;

		void Reset(int32 Keys)
		{
			if (Stamp.Num() < Keys)
			{
				Stamp.SetNumZeroed(Keys);
				Vertex.SetNumZeroed(Keys);
				Generation = 0;
			}
			if (++Generation == MAX_int32)
			{
				FMemory::Memzero(Stamp.GetData(), Stamp.Num() * sizeof(int32));
				Generation = 1;
			}
		}

		bool TryGet(int32 Key, int32& Out) const
		{
			if (Stamp[Key] == Generation)
			{
				Out = Vertex[Key];
				return true;
			}
			return false;
		}

		void Set(int32 Key, int32 V)
		{
			Stamp[Key] = Generation;
			Vertex[Key] = V;
		}
	};

	/** Per-thread meshing scratch, reused from region to region. */
	struct FScratch
	{
		TArray<float> Grid;
		TArray<bool> Active;
		TArray<uint64> Valid;
		TArray<uint64> SignBits;
		TArray<int32> SignStamp;
		int32 SignGeneration = 0;
		FEdgeMap Edges;

		static FScratch& For(int32 N, int32 Nb)
		{
			static thread_local FScratch Mine;
			if (Mine.Grid.Num() < N * N * N)
				Mine.Grid.SetNumUninitialized(N * N * N);
			if (Mine.Active.Num() < Nb * Nb * Nb)
				Mine.Active.SetNumUninitialized(Nb * Nb * Nb);
			return Mine;
		}

		void ResetSigns(int32 Rows)
		{
			if (SignBits.Num() < Rows)
			{
				SignBits.SetNumZeroed(Rows);
				SignStamp.SetNumZeroed(Rows);
				SignGeneration = 0;
			}
			if (++SignGeneration == MAX_int32)
			{
				FMemory::Memzero(SignStamp.GetData(), SignStamp.Num() * sizeof(int32));
				SignGeneration = 1;
			}
		}
	};

	/** Chunk-local position of transition sample index Si (0-0xC). All samples lie on the
	 * boundary plane; the fine side is separated from it afterwards by the deformation. */
	void TransitionSamplePos(int32 Si, const float Fo[3], const int32 U[3], const int32 V[3], float Stride, float Result[3])
	{
		float Du, Dv;
		if (Si <= 8)
		{
			Du = float(Si % 3) * Stride;
			Dv = float(Si / 3) * Stride;
		}
		else
		{
			const int32 C = Si - 9; // 9,A,B,C -> corners (0,0),(1,0),(0,1),(1,1)
			Du = float(C & 1) * 2.f * Stride;
			Dv = float((C >> 1) & 1) * 2.f * Stride;
		}
		for (int32 K = 0; K < 3; K++)
			Result[K] = Fo[K] + U[K] * Du + V[K] * Dv;
	}

	FVector3f NormalOf(float Gx, float Gy, float Gz)
	{
		const FVector3f G(Gx, Gy, Gz);
		return G.SizeSquared() > 1e-12f ? G.GetUnsafeNormal() : FVector3f(0, 0, 1);
	}
}

void TransvoxelMesher::MeshRegion(FTransvoxelMesh& Mesh, FDensityFunc Density, const FVector3f& Origin, float Stride, int32 Cells,
	int32 CoarserFaces, const FVertexColorFunc* VertexColor, const FSparseCornerFunc* Corners)
{
	Mesh.Vertices.Reset();
	Mesh.Normals.Reset();
	Mesh.Colors.Reset();
	Mesh.Indices.Reset();

	const int32 Size = Cells;
	const float Extent = float(Cells) * Stride;
	const float HalfStride = Stride * 0.5f;
	const int32 N = Size + 1, NN = N * N;
	// Blocks of 2 cells: transition cells pair fine cells up, so a pair never straddles two
	// blocks. (Odd sizes only come from tests without coarser faces.)
	const int32 Block = (Size & 1) == 0 ? 2 : 1;
	const int32 Nb = Size / Block;

	// ---- the corner grid ----
	FScratch& Scratch = FScratch::For(N, Nb);
	float* Grid = Scratch.Grid.GetData();
	bool* Active = Scratch.Active.GetData();
	if (Corners != nullptr)
	{
		(*Corners)(Origin, Stride, N, Grid, Block, Active);
	}
	else
	{
		for (int32 X = 0; X < N; X++)
			for (int32 Y = 0; Y < N; Y++)
				for (int32 Z = 0; Z < N; Z++)
					Grid[(X * N + Y) * N + Z] = Density(Origin.X + X * Stride, Origin.Y + Y * Stride, Origin.Z + Z * Stride);
		for (int32 I = 0; I < Nb * Nb * Nb; I++)
			Active[I] = true;
	}

	// Vertex of each lattice edge, once made: edge (grid index of its lower end) * 3 + axis.
	FEdgeMap& Edges = Scratch.Edges;
	Edges.Reset(N * NN * 3);

	auto FaceActive = [CoarserFaces](int32 Axis, int32 Side) { return (CoarserFaces & (1 << (Axis * 2 + Side))) != 0; };

	// Central-difference gradient of the density field, chunk-local coordinates.
	auto Gradient = [&Density](float Wx, float Wy, float Wz, float& Gx, float& Gy, float& Gz)
	{
		Gx = Density(Wx + GradientStep, Wy, Wz) - Density(Wx - GradientStep, Wy, Wz);
		Gy = Density(Wx, Wy + GradientStep, Wz) - Density(Wx, Wy - GradientStep, Wz);
		Gz = Density(Wx, Wy, Wz + GradientStep) - Density(Wx, Wy, Wz - GradientStep);
	};

	// Tangential boundary deformation. P is chunk-local (relative to Origin); modified in place.
	auto Deform = [&](float P[3])
	{
		float Off[3] = { 0.f, 0.f, 0.f };
		bool bAny = false;
		for (int32 Axis = 0; Axis < 3; Axis++)
			for (int32 Side = 0; Side < 2; Side++)
			{
				if (!FaceActive(Axis, Side))
					continue;
				const float W = Side == 0 ? P[Axis] : Extent - P[Axis];
				if (W >= Stride)
					continue;
				Off[Axis] += (1.f - W / Stride) * HalfStride * (Side == 0 ? 1.f : -1.f);
				bAny = true;
			}
		if (!bAny)
			return;
		// Project the offset onto the tangent plane of the isosurface so the vertex slides
		// along the surface instead of being dragged off it.
		float Gx, Gy, Gz;
		Gradient(Origin.X + P[0], Origin.Y + P[1], Origin.Z + P[2], Gx, Gy, Gz);
		const float G2 = Gx * Gx + Gy * Gy + Gz * Gz;
		if (G2 > 1e-12f)
		{
			const float Dot = (Off[0] * Gx + Off[1] * Gy + Off[2] * Gz) / G2;
			Off[0] -= Gx * Dot;
			Off[1] -= Gy * Dot;
			Off[2] -= Gz * Dot;
		}
		P[0] += Off[0];
		P[1] += Off[1];
		P[2] += Off[2];
	};

	// Lengyel's reference order has the algebraic normal (b - a) x (c - a) pointing out of the
	// solid; Unreal's front faces are the other way round, so emit (a, c, b) - as Godot does.
	auto AddTriangle = [&Mesh](int32 A, int32 B, int32 C)
	{
		Mesh.Indices.Add(A);
		Mesh.Indices.Add(C);
		Mesh.Indices.Add(B);
	};

	// ---- regular cells, in the blocks that can hold surface ----
	float D[8];
	float P3[3];
	int32 CellVerts[12];

	// One cell: its corners' signs pick the case; vertices are made once per lattice edge.
	auto Cell = [&](int32 Lx, int32 Ly, int32 Lz)
	{
		const int32 B0 = (Lx * N + Ly) * N + Lz;
		const float G0 = Grid[B0], G1 = Grid[B0 + NN], G2 = Grid[B0 + N], G3 = Grid[B0 + NN + N];
		const float G4 = Grid[B0 + 1], G5 = Grid[B0 + NN + 1], G6 = Grid[B0 + N + 1], G7 = Grid[B0 + NN + N + 1];
		const int32 CaseCode = (G0 < 0.f ? 1 : 0) | (G1 < 0.f ? 2 : 0) | (G2 < 0.f ? 4 : 0) | (G3 < 0.f ? 8 : 0)
			| (G4 < 0.f ? 16 : 0) | (G5 < 0.f ? 32 : 0) | (G6 < 0.f ? 64 : 0) | (G7 < 0.f ? 128 : 0);
		if (CaseCode == 0 || CaseCode == 255)
			return;
		D[0] = G0; D[1] = G1; D[2] = G2; D[3] = G3; D[4] = G4; D[5] = G5; D[6] = G6; D[7] = G7;

		const int32 Cls = TransvoxelTables::RegularCellClass[CaseCode];
		const FRegularCellData& Data = TransvoxelTables::RegularCellData[Cls];
		const uint16* VData = TransvoxelTables::RegularVertexData[CaseCode];

		for (int32 I = 0; I < Data.VertexCount(); I++)
		{
			const int32 Code = VData[I];
			int32 V0 = (Code >> 4) & 0x0F;
			int32 V1 = Code & 0x0F;
			// The edge, from its lower lattice end: the same vertex for every cell on it, and
			// placed the same way whichever cell makes it first.
			if (V0 > V1)
				Swap(V0, V1);
			const int32 Ax = CornerX(V0) != CornerX(V1) ? 0 : CornerY(V0) != CornerY(V1) ? 1 : 2;
			const int32 Key = (((Lx + CornerX(V0)) * N + (Ly + CornerY(V0))) * N + (Lz + CornerZ(V0))) * 3 + Ax;
			int32 Vi;
			if (!Edges.TryGet(Key, Vi))
			{
				const float T = D[V0] / (D[V0] - D[V1]);
				P3[0] = (float(Lx + CornerX(V0)) + float(CornerX(V1) - CornerX(V0)) * T) * Stride;
				P3[1] = (float(Ly + CornerY(V0)) + float(CornerY(V1) - CornerY(V0)) * T) * Stride;
				P3[2] = (float(Lz + CornerZ(V0)) + float(CornerZ(V1) - CornerZ(V0)) * T) * Stride;
				Deform(P3);
				Vi = Mesh.Vertices.Num();
				Mesh.Vertices.Add(FVector3f(Origin.X + P3[0], Origin.Y + P3[1], Origin.Z + P3[2]));
				Edges.Set(Key, Vi);
			}
			CellVerts[I] = Vi;
		}
		for (int32 I = 0; I < Data.TriangleCount(); I++)
			AddTriangle(CellVerts[Data.VertexIndex[I * 3]], CellVerts[Data.VertexIndex[I * 3 + 1]], CellVerts[Data.VertexIndex[I * 3 + 2]]);
	};

	if (N <= 64)
	{
		// Bit rows (the binary-meshing trick): per corner row a 64-bit mask of which corners are
		// rock; a row of cells is crossed by surface where its four corner rows, at z or z+1, are
		// not all alike. Only cells of active blocks count (the corners of the rest were not read).
		TArray<uint64>& Valid = Scratch.Valid;
		Valid.SetNumZeroed(Size * Size, EAllowShrinking::No);
		FMemory::Memzero(Valid.GetData(), Size * Size * sizeof(uint64));
		for (int32 Bx = 0; Bx < Nb; Bx++)
			for (int32 By = 0; By < Nb; By++)
				for (int32 Bz = 0; Bz < Nb; Bz++)
				{
					if (!Active[(Bx * Nb + By) * Nb + Bz])
						continue;
					const uint64 Bits = ((1ull << Block) - 1) << (Bz * Block);
					for (int32 Lx = Bx * Block; Lx < (Bx + 1) * Block; Lx++)
						for (int32 Ly = By * Block; Ly < (By + 1) * Block; Ly++)
							Valid[Lx * Size + Ly] |= Bits;
				}
		Scratch.ResetSigns(N * N);
		auto Signs = [&](int32 X, int32 Y) -> uint64
		{
			const int32 R = X * N + Y;
			if (Scratch.SignStamp[R] == Scratch.SignGeneration)
				return Scratch.SignBits[R];
			uint64 M = 0;
			const int32 At = R * N;
			for (int32 Z = 0; Z < N; Z++)
				if (Grid[At + Z] < 0.f)
					M |= 1ull << Z;
			Scratch.SignBits[R] = M;
			Scratch.SignStamp[R] = Scratch.SignGeneration;
			return M;
		};
		for (int32 Lx = 0; Lx < Size; Lx++)
			for (int32 Ly = 0; Ly < Size; Ly++)
			{
				const uint64 V = Valid[Lx * Size + Ly];
				if (V == 0)
					continue;
				const uint64 S00 = Signs(Lx, Ly), S10 = Signs(Lx + 1, Ly), S01 = Signs(Lx, Ly + 1), S11 = Signs(Lx + 1, Ly + 1);
				const uint64 Any = S00 | S10 | S01 | S11, All = S00 & S10 & S01 & S11;
				uint64 Crossed = ((Any | (Any >> 1)) & ~(All & (All >> 1))) & V;
				while (Crossed != 0)
				{
					const int32 Lz = int32(FMath::CountTrailingZeros64(Crossed));
					Crossed &= Crossed - 1;
					Cell(Lx, Ly, Lz);
				}
			}
	}
	else
	{
		for (int32 Bx = 0; Bx < Nb; Bx++)
			for (int32 By = 0; By < Nb; By++)
				for (int32 Bz = 0; Bz < Nb; Bz++)
				{
					if (!Active[(Bx * Nb + By) * Nb + Bz])
						continue;
					for (int32 Lx = Bx * Block; Lx < (Bx + 1) * Block; Lx++)
						for (int32 Ly = By * Block; Ly < (By + 1) * Block; Ly++)
							for (int32 Lz = Bz * Block; Lz < (Bz + 1) * Block; Lz++)
								Cell(Lx, Ly, Lz);
				}
	}

	Mesh.RegularVertexCount = Mesh.Vertices.Num();
	Mesh.RegularIndexCount = Mesh.Indices.Num();

	// ---- transition cells (one per 2x2 group of boundary cells) ----
	float DVals[9];
	float Fo[3], Sp0[3], Sp1[3];
	int32 Node[3];

	for (int32 Face = 0; Face < 6; Face++)
	{
		if ((CoarserFaces & (1 << Face)) == 0)
			continue;
		const int32 Axis = Face / 2;
		const int32 Side = Face % 2;
		const int32* U = FaceU[Face];
		const int32* V = FaceV[Face];
		const int32 UAxis = U[0] != 0 ? 0 : U[1] != 0 ? 1 : 2;
		const int32 VAxis = V[0] != 0 ? 0 : V[1] != 0 ? 1 : 2;
		const float Plane = Side == 0 ? 0.f : Extent;
		const int32 PlaneNode = Side == 0 ? 0 : Size;
		const int32 BoundaryCell = Side == 0 ? 0 : Size - 1;

		for (int32 Gu = 0; Gu < Size / 2; Gu++)
			for (int32 Gv = 0; Gv < Size / 2; Gv++)
			{
				// The 2x2 boundary cells of the group: surface can only be here if one of their
				// blocks is active.
				bool bAny = false;
				for (int32 Du = 0; Du < 2 && !bAny; Du++)
					for (int32 Dv = 0; Dv < 2 && !bAny; Dv++)
					{
						Node[Axis] = BoundaryCell / Block;
						Node[UAxis] = (2 * Gu + Du) / Block;
						Node[VAxis] = (2 * Gv + Dv) / Block;
						bAny = Active[(Node[0] * Nb + Node[1]) * Nb + Node[2]];
					}
				if (!bAny)
					continue;

				// Density values: 3x3 boundary-plane grid nodes at fine spacing.
				for (int32 Fi = 0; Fi < 9; Fi++)
				{
					Node[0] = 0; Node[1] = 0; Node[2] = 0;
					Node[Axis] = PlaneNode;
					for (int32 K = 0; K < 3; K++)
						Node[K] += U[K] * (2 * Gu + Fi % 3) + V[K] * (2 * Gv + Fi / 3);
					DVals[Fi] = Grid[(Node[0] * N + Node[1]) * N + Node[2]];
				}

				int32 CaseCode = 0;
				for (int32 Bit = 0; Bit < 9; Bit++)
					if (DVals[TransCaseOrder[Bit]] < 0.f)
						CaseCode |= 1 << Bit;
				if (CaseCode == 0 || CaseCode == 511)
					continue;

				const int32 Cls = TransvoxelTables::TransitionCellClass[CaseCode];
				// The fine-owns cell is mirrored along its depth axis relative to Lengyel's
				// canonical layout, so the per-case inversion flag is applied inverted.
				const bool bInvert = (Cls & 0x80) == 0;
				const FTransitionCellData& Data = TransvoxelTables::TransitionCellData[Cls & 0x7F];
				const uint16* VData = TransvoxelTables::TransitionVertexData[CaseCode];

				// Chunk-local origin of this group's face quad (2s x 2s).
				Fo[0] = 0.f; Fo[1] = 0.f; Fo[2] = 0.f;
				Fo[Axis] = Plane;
				for (int32 K = 0; K < 3; K++)
					Fo[K] += U[K] * float(2 * Gu) * Stride + V[K] * float(2 * Gv) * Stride;

				const int32 BaseIndex = Mesh.Vertices.Num();
				for (int32 I = 0; I < Data.VertexCount(); I++)
				{
					const int32 Code = VData[I];
					const int32 S0 = (Code >> 4) & 0x0F;
					const int32 S1 = Code & 0x0F;
					const float D0 = DVals[S0 <= 8 ? S0 : HalfResMap[S0 - 9]];
					const float D1 = DVals[S1 <= 8 ? S1 : HalfResMap[S1 - 9]];
					const float T = D0 / (D0 - D1);
					TransitionSamplePos(S0, Fo, U, V, Stride, Sp0);
					TransitionSamplePos(S1, Fo, U, V, Stride, Sp1);
					for (int32 K = 0; K < 3; K++)
						P3[K] = Sp0[K] + (Sp1[K] - Sp0[K]) * T;
					// Fine-side vertices (both samples on the full-res face) get the same
					// tangential deformation as the regular mesh; coarse-side vertices stay on the
					// undeformed boundary plane, matching the coarse neighbour.
					if (S0 <= 8 && S1 <= 8)
						Deform(P3);
					Mesh.Vertices.Add(FVector3f(Origin.X + P3[0], Origin.Y + P3[1], Origin.Z + P3[2]));
				}
				for (int32 I = 0; I < Data.TriangleCount(); I++)
				{
					int32 A = Data.VertexIndex[I * 3];
					const int32 B = Data.VertexIndex[I * 3 + 1];
					int32 C = Data.VertexIndex[I * 3 + 2];
					if (bInvert)
						Swap(A, C);
					AddTriangle(BaseIndex + A, BaseIndex + B, BaseIndex + C);
				}
			}
	}

	// ---- normals and colours ----
	const int32 Count = Mesh.Vertices.Num();
	Mesh.Normals.SetNumUninitialized(Count);
	for (int32 I = 0; I < Count; I++)
	{
		const FVector3f& W = Mesh.Vertices[I];
		float Gx, Gy, Gz;
		Gradient(W.X, W.Y, W.Z, Gx, Gy, Gz);
		Mesh.Normals[I] = NormalOf(Gx, Gy, Gz);
	}
	Mesh.Colors.SetNumUninitialized(Count);
	for (int32 I = 0; I < Count; I++)
		Mesh.Colors[I] = VertexColor != nullptr ? (*VertexColor)(Mesh.Vertices[I], Mesh.Normals[I]) : FColor::White;
}
