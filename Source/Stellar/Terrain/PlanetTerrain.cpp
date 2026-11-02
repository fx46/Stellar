#include "Terrain/PlanetTerrain.h"

namespace
{
	/** Cells per side of the boxes a node's corner grid is first tested in, before its 2-cell blocks. */
	constexpr int32 SuperBlock = 8;
}

FPlanetTerrain::FPlanetTerrain(double Radius, int32 Seed, double LeafSize, double SplitFactor)
	: Field(Radius, Seed)
	, Octree(Radius + FPlanetField::MaxHeight + 64, LeafSize, SplitFactor)
{
}

FBox3d FPlanetTerrain::ReadBox(const FNodeKey& K) const
{
	const FVector3d Lo = Octree.Min(K);
	const double S = Octree.Size(K.Level);
	const double Pad = TransvoxelMesher::GradientStep + 2 * S / Cells;
	return FBox3d(Lo - FVector3d(Pad), Lo + FVector3d(S + Pad));
}

int32 FPlanetTerrain::CachedNodes() const
{
	FReadScopeLock Lock(HoldsLock);
	return Holds.Num();
}

bool FPlanetTerrain::HoldsProceduralSurface(const FNodeKey& K) const
{
	{
		FReadScopeLock Lock(HoldsLock);
		if (const bool* Found = Holds.Find(K))
			return *Found;
	}
	const FVector3d M = Octree.Min(K);
	const bool bHolds = MayHold(M.X, M.Y, M.Z, Octree.Size(K.Level), 2);
	FWriteScopeLock Lock(HoldsLock);
	// A pure cache: bounded, so it does not grow with the area explored (a cut needs a few
	// thousand entries; a full cache is simply rebuilt as nodes are tested again).
	if (Holds.Num() >= 400000)
		Holds.Reset();
	Holds.Add(K, bHolds);
	return bHolds;
}

bool FPlanetTerrain::HoldsSurface(const FNodeKey& K, const FTerrainEdits* Edits) const
{
	if (HoldsProceduralSurface(K))
		return true;
	return Edits != nullptr && !Edits->IsEmpty() && Edits->Touches(ReadBox(K));
}

bool FPlanetTerrain::MayHold(double X, double Y, double Z, double Size, int32 Depth) const
{
	if (!MayHoldSurface(X, Y, Z, Size))
		return false;
	if (Depth == 0)
		return true;
	const double H = Size * 0.5;
	for (int32 I = 0; I < 8; I++)
		if (MayHold(X + (I & 1) * H, Y + ((I >> 1) & 1) * H, Z + ((I >> 2) & 1) * H, H, Depth - 1))
			return true;
	return false;
}

bool FPlanetTerrain::MayHoldSurface(double X, double Y, double Z, double Size) const
{
	// A surface point p in the cube is at radius R + h(dir p), and dir p lies within the angle
	// the cube's bounding ball subtends, so its surface point lies within R times that angle
	// (an arc, longer than the straight line the noise sees) of the centre's.
	const double R = Field.GetRadius();
	const FRange Radial = FPlanetOctree::RadialRange(X, Y, Z, Size);
	if (Radial.Lo > R + FPlanetField::MaxHeight + 1 || Radial.Hi < R + FPlanetField::MinHeight - 1)
		return false;
	const double Cx = X + Size * 0.5, Cy = Y + Size * 0.5, Cz = Z + Size * 0.5;
	const double Rc = FMath::Sqrt(Cx * Cx + Cy * Cy + Cz * Cz);
	const double Ball = Size * 0.8660254;
	if (Rc <= Ball)
		return true;
	const double Reach = R * FMath::Asin(Ball / Rc);
	const double S = R / Rc;
	const FRange H = Field.HeightRange(Cx * S, Cy * S, Cz * S, Reach);
	// The density's early-outs (a metre past the global bounds) keep its sign.
	return Radial.Hi >= R + H.Lo - 1e-3 && Radial.Lo <= R + H.Hi + 1e-3;
}

bool FPlanetTerrain::Visible(const FNodeKey& K, const FVector3d& C) const
{
	// No ground lies below R + MinHeight, so that sphere hides everything past the distance at
	// which a line from the camera grazing it reaches the node's highest possible point.
	const double Occluder = Field.GetRadius() + FPlanetField::MinHeight;
	const double Rc = C.Length();
	if (Rc <= Occluder)
		return true;
	const FRange Radial = Octree.RadialRange(K);
	const double Top = FMath::Min(Radial.Hi, Field.GetRadius() + FPlanetField::MaxHeight);
	if (Top <= Occluder)
		return true; // (holds no surface: HoldsSurface drops it)
	const double Reach = FMath::Sqrt(Rc * Rc - Occluder * Occluder) + FMath::Sqrt(Top * Top - Occluder * Occluder);
	return Octree.Distance(K, C) <= Reach;
}

TArray<FNodeKey> FPlanetTerrain::Select(const FVector3d& C, const FTerrainEdits* Edits) const
{
	return Octree.Select(C, [this, &C, Edits](const FNodeKey& K) { return Visible(K, C) && HoldsSurface(K, Edits); });
}

void FPlanetTerrain::Mesh(const FNodeKey& K, int32 CoarserFaces, const FTerrainEdits* Edits, FTransvoxelMesh& Out, bool bDense) const
{
	const FVector3d O = Octree.Min(K);
	const float Stride = float(Octree.Size(K.Level) / Cells);

	TArray<FBox3d> EditBoxes;
	if (Edits != nullptr && !Edits->IsEmpty())
		Edits->Gather(ReadBox(K), EditBoxes);
	const bool bEdited = EditBoxes.Num() > 0;
	const FTerrainEdits::FSampler Sampler(bEdited ? Edits : nullptr);

	auto Density = [this, &O, &Sampler, bEdited](float X, float Y, float Z) -> float
	{
		float D = Field.Density(O.X, O.Y, O.Z, X, Y, Z);
		if (bEdited)
			D += Sampler.Delta(O.X + X, O.Y + Y, O.Z + Z);
		return D;
	};

	// The sparse corner grid: boxes of SuperBlock cells the surface cannot reach are skipped
	// whole, then each remaining block is tested, and only the corners of blocks that may hold
	// surface are evaluated - with the same float positions the dense grid uses, so the same
	// values. Blocks an edited brick reaches always count.
	auto FillCorners = [&](const FVector3f& Origin, float S, int32 N, float* Grid, int32 Block, bool* Active)
	{
		const int32 CellsN = N - 1, Nb = CellsN / Block;
		static thread_local TArray<bool> FilledStore;
		FilledStore.SetNumUninitialized(N * N * N, EAllowShrinking::No);
		bool* Filled = FilledStore.GetData();
		FMemory::Memzero(Filled, N * N * N);
		FMemory::Memzero(Active, Nb * Nb * Nb);
		const int32 Per = FMath::Max(1, SuperBlock / Block), Ns = (Nb + Per - 1) / Per;
		// Corner positions are floats (as the dense grid has them), a rounding away from the
		// exact lattice: grow each tested box by more than that.
		const double Pad = (FMath::Abs(Origin.X) + FMath::Abs(Origin.Y) + FMath::Abs(Origin.Z) + CellsN * double(S)) * 3e-7 + 1e-3;
		auto May = [&](int32 X, int32 Y, int32 Z, int32 Span) -> bool
		{
			const double Bx = O.X + Origin.X + X * double(S) - Pad, By = O.Y + Origin.Y + Y * double(S) - Pad, Bz = O.Z + Origin.Z + Z * double(S) - Pad;
			const double Side = Span * double(S) + 2 * Pad;
			if (bEdited)
			{
				const FBox3d Box(FVector3d(Bx, By, Bz), FVector3d(Bx + Side, By + Side, Bz + Side));
				for (const FBox3d& E : EditBoxes)
					if (E.Intersect(Box))
						return true;
			}
			return MayHoldSurface(Bx, By, Bz, Side);
		};
		for (int32 Sx = 0; Sx < Ns; Sx++)
			for (int32 Sy = 0; Sy < Ns; Sy++)
				for (int32 Sz = 0; Sz < Ns; Sz++)
				{
					if (!May(Sx * Per * Block, Sy * Per * Block, Sz * Per * Block, Per * Block))
						continue;
					for (int32 Bx = Sx * Per; Bx < FMath::Min(Nb, Sx * Per + Per); Bx++)
						for (int32 By = Sy * Per; By < FMath::Min(Nb, Sy * Per + Per); By++)
							for (int32 Bz = Sz * Per; Bz < FMath::Min(Nb, Sz * Per + Per); Bz++)
							{
								if (!May(Bx * Block, By * Block, Bz * Block, Block))
									continue;
								Active[(Bx * Nb + By) * Nb + Bz] = true;
								for (int32 X = Bx * Block; X <= Bx * Block + Block; X++)
									for (int32 Y = By * Block; Y <= By * Block + Block; Y++)
										for (int32 Z = Bz * Block; Z <= Bz * Block + Block; Z++)
										{
											const int32 I = (X * N + Y) * N + Z;
											if (Filled[I])
												continue;
											Filled[I] = true;
											Grid[I] = Density(Origin.X + X * S, Origin.Y + Y * S, Origin.Z + Z * S);
										}
							}
				}
	};

	auto Color = [&](const FVector3f& P, const FVector3f& N) { return VertexColor(O, P, N, bEdited ? Edits : nullptr, Stride); };
	const FVertexColorFunc ColorRef(Color);
	const FSparseCornerFunc CornersRef(FillCorners);
	TransvoxelMesher::MeshRegion(Out, Density, FVector3f::ZeroVector, Stride, Cells, CoarserFaces, &ColorRef, bDense ? nullptr : &CornersRef);
}

double FPlanetTerrain::Density(const FVector3d& P, const FTerrainEdits* Edits) const
{
	double D = Field.Density(P);
	if (Edits != nullptr && !Edits->IsEmpty())
		D += Edits->Delta(P.X, P.Y, P.Z);
	return D;
}

FVector3d FPlanetTerrain::Normal(const FVector3d& P, const FTerrainEdits* Edits, double Step) const
{
	const FVector3d G(
		Density(P + FVector3d(Step, 0, 0), Edits) - Density(P - FVector3d(Step, 0, 0), Edits),
		Density(P + FVector3d(0, Step, 0), Edits) - Density(P - FVector3d(0, Step, 0), Edits),
		Density(P + FVector3d(0, 0, Step), Edits) - Density(P - FVector3d(0, 0, Step), Edits));
	const double L = G.Length();
	return L > 1e-12 ? G / L : P.GetSafeNormal();
}

FVector3d FPlanetTerrain::SurfaceAt(const FVector3d& P, double* OutHeight) const
{
	const FVector3d U = P.GetSafeNormal();
	const double H = Field.Height(U);
	if (OutHeight != nullptr)
		*OutHeight = H;
	return U * (Field.GetRadius() + H);
}

bool FPlanetTerrain::Trace(const FVector3d& From, const FVector3d& Dir, double MaxDistance, const FTerrainEdits* Edits, FVector3d& OutHit) const
{
	double T = 0;
	double Prev = Density(From, Edits);
	if (Prev < 0)
	{
		OutHit = From;
		return true;
	}
	while (T < MaxDistance)
	{
		// The density is a radial height, not a distance: step under it on slopes.
		const double Step = FMath::Clamp(Prev * 0.5, 0.05, 4.0);
		const double Next = FMath::Min(T + Step, MaxDistance);
		const double D = Density(From + Dir * Next, Edits);
		if (D < 0)
		{
			double Lo = T, Hi = Next;
			for (int32 I = 0; I < 24; I++)
			{
				const double Mid = (Lo + Hi) * 0.5;
				if (Density(From + Dir * Mid, Edits) < 0) Hi = Mid; else Lo = Mid;
			}
			OutHit = From + Dir * Hi;
			return true;
		}
		T = Next;
		Prev = D;
	}
	return false;
}

FGeoSample FPlanetTerrain::RockAt(const FVector3d& P, const FTerrainEdits* Edits) const
{
	if (Edits != nullptr && Edits->MaterialAt(P) == EEditMaterial::Fill)
		return { EGeoMaterial::Fill, 0.8f };
	return Field.Geology(P, -Field.Density(P));
}

FColor FPlanetTerrain::VertexColor(const FVector3d& NodeMin, const FVector3f& Local, const FVector3f& Normal, const FTerrainEdits* Edits, float Stride) const
{
	const FVector3d P = NodeMin + FVector3d(Local);
	const double R = Field.GetRadius();
	const double Rp = P.Length();
	const FVector3d Up = P / Rp;
	const FHeightLayers L = Field.Layers(Up.X * R, Up.Y * R, Up.Z * R);
	const float Slope = float(FVector3d(Normal).Dot(Up));
	FLinearColor Surface = Field.SurfaceColor(L, Rp - R, Slope);
	// Cliffs show their layers.
	const float Steep = 1.f - FMath::SmoothStep(0.5f, 0.8f, Slope);
	if (Steep > 0.01f)
		Surface = FMath::Lerp(Surface, Field.CliffColor(P, L, Stride), Steep * 0.9f);
	// Alpha: how bare the rock is (0 dust, 1 rock), for the material's roughness.
	const uint8 Bare = uint8(FMath::Clamp((1.f - FMath::SmoothStep(0.62f, 0.86f, Slope)) * 255.f + float(L.Outcrop) * 20.f, 0.f, 255.f));
	if (Edits == nullptr)
	{
		FColor C = Surface.ToFColor(true);
		C.A = Bare;
		return C;
	}
	// Built ground (sampled half a cell inside, where the vertex's lattice points are).
	const FVector3d Inside = P - FVector3d(Normal) * (Stride * 0.5);
	if (Edits->MaterialAt(Inside) == EEditMaterial::Fill)
	{
		FColor C = Field.MaterialColor(EGeoMaterial::Fill, P).ToFColor(true);
		C.A = 200;
		return C;
	}
	// Depth under the original ground: zero on untouched surface (within the mesh's own
	// interpolation error), growing in a cut.
	const double Depth = (R + L.Total()) - Rp;
	const double Skin = 0.25 + 0.5 * Stride;
	if (Depth < Skin)
	{
		FColor C = Surface.ToFColor(true);
		C.A = Bare;
		return C;
	}
	const FGeoSample Geo = Field.Geology(P, Depth);
	const FLinearColor Rock = Geo.Material == EGeoMaterial::Regolith ? Surface * 0.62f : Field.MaterialColor(Geo.Material, P);
	const float T = float(FMath::SmoothStep(Skin, Skin + 0.35, Depth));
	FColor C = FMath::Lerp(Surface * 0.8f, Rock, T).ToFColor(true);
	C.A = uint8(FMath::Lerp(float(Bare), Geo.Material == EGeoMaterial::Regolith ? 60.f : 230.f, T));
	return C;
}
