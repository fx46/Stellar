#pragma once

#include "CoreMinimal.h"

/** Density of the terrain at a planet-frame point (negative inside rock). */
using FSolidSampler = TFunctionRef<double(const FVector3d& P)>;

/**
 * The frame of a fluid region: a box of cells in the planet frame, small enough (tens of metres)
 * that the planet's curvature inside it is negligible, so "down" is one axis.
 */
struct STELLAR_API FFluidFrame
{
	/** The region's minimum corner (planet frame, metres). */
	FVector3d Origin = FVector3d::ZeroVector;
	/** Orthonormal axes: X and Y level, Z up (Y = Z x X, Unreal's handedness). */
	FVector3d X = FVector3d(1, 0, 0), Y = FVector3d(0, 1, 0), Z = FVector3d(0, 0, 1);
	double CellSize = 0.5;
	int32 Nx = 0, Ny = 0, Nz = 0;

	/** A frame of Size metres (x, y, z) around Centre, level with Up, X along Forward. */
	static FFluidFrame Around(const FVector3d& Centre, const FVector3d& Up, const FVector3d& Forward, const FVector3d& Size, double Cell);

	int32 Num() const { return Nx * Ny * Nz; }
	int32 Index(int32 I, int32 J, int32 K) const { return (K * Ny + J) * Nx + I; }
	FVector3d CellCentre(int32 I, int32 J, int32 K) const
	{
		return Origin + X * ((I + 0.5) * CellSize) + Y * ((J + 0.5) * CellSize) + Z * ((K + 0.5) * CellSize);
	}
	/** Region-local coordinates (metres along X, Y, Z) of a planet-frame point. */
	FVector3d ToLocal(const FVector3d& P) const { const FVector3d Q = P - Origin; return FVector3d(Q.Dot(X), Q.Dot(Y), Q.Dot(Z)); }
	FVector3d FromLocal(const FVector3d& L) const { return Origin + X * L.X + Y * L.Y + Z * L.Z; }
	FBox3d PlanetBounds() const;
	void Serialize(FArchive& Ar);
};

/** One step's diagnostics. */
struct FFluidStats
{
	int32 ActiveCells = 0;
	int32 Steps = 0;
	double StepMs = 0;
	double Volume = 0;
	bool bSettled = true;
};

/**
 * Representation A of the milestone 0c spike: a cellular volume of fluid over the terrain's own
 * cells. Each cell holds a fill (0 empty, 1 full, slightly more under pressure); rock cells come
 * from the edited terrain density. A step lets each stacked pair of cells settle (the lower fills
 * from the upper; an overfull cell overflows up), then levels supported water sideways - a
 * mass-based water automaton after W. Gallant's, made incompressible: each pair is updated once
 * per step and sideways exchanges cannot overshoot, so a region comes to rest exactly, and every
 * transfer is exact, so volume is conserved. Without compression there is no pressure: water
 * does not rise up the far side of a U-bend (a known limit of this spike). Only cells that changed (and their neighbours) are visited:
 * a settled region costs nothing, and an excavation reactivates only what it touched.
 *
 * It holds caves, overhangs and sealed cavities - whatever the terrain is - because it lives in
 * the same 3D cells. Engine-free.
 */
class STELLAR_API FFluidVolume
{
public:
	static constexpr float MaxMass = 1.0f;
	static constexpr float MinMass = 0.0001f;

	FFluidVolume() = default;
	explicit FFluidVolume(const FFluidFrame& InFrame);

	const FFluidFrame& Frame() const { return F; }

	/** Mark rock cells from the terrain density at their centres. Water in a cell that becomes
	 * rock moves up to the first open cell above it (conserved). Wakes the box. */
	void RebuildSolid(FSolidSampler Density, const FBox3d* PlanetBox = nullptr);

	bool IsSolid(int32 I, int32 J, int32 K) const { return Solid[F.Index(I, J, K)] != 0; }
	float Mass(int32 I, int32 J, int32 K) const { return Water[F.Index(I, J, K)]; }

	/** Add water at a planet-frame point (a spring, a breach); returns what was added. */
	float AddAt(const FVector3d& P, float Amount);
	/** Fill every open cell inside the sphere (a sealed pocket, a lake). */
	void FillSphere(const FVector3d& Centre, double Radius);

	/** One step. Visits only the active cells. */
	FFluidStats Step();

	/** Total water (cell fills; one full cell holds CellSize^3 cubic metres). */
	double TotalMass() const;
	double CubicMetres() const { return TotalMass() * F.CellSize * F.CellSize * F.CellSize; }
	/** Water (cubic metres) in cells whose centres lie within Radius of Centre. */
	double CubicMetresIn(const FVector3d& Centre, double Radius) const;
	int32 NumActive() const { return Active.Num(); }
	bool IsSettled() const { return Active.Num() == 0; }
	int64 Bytes() const { return int64(Water.Num()) * (sizeof(float) * 2 + 2) + int64(Active.Num()) * 4; }

	/** Fill at a region-local point, trilinear over cell centres (what the surface is meshed from). */
	float SampleLocal(const FVector3d& L) const;

	/** Cells whose fill changed since the last call (as a box in cells), for remeshing. */
	bool TakeDirty(FIntVector& OutMin, FIntVector& OutMax);

	void Serialize(FArchive& Ar);

private:
	void Wake(int32 I, int32 J, int32 K);
	void WakeAround(int32 I, int32 J, int32 K);
	void MarkDirty(int32 I, int32 J, int32 K);

	FFluidFrame F;
	TArray<float> Water;
	TArray<float> Next;
	TArray<uint8> Solid;
	TArray<uint8> InActive;
	TArray<int32> Active;
	TArray<int32> Touched;
	TArray<uint8> InTouched;
	TArray<uint8> PairDone;
	FIntVector DirtyMin = FIntVector(MAX_int32), DirtyMax = FIntVector(MIN_int32);
};

/**
 * Representation B of the spike: a 2.5D water layer over the terrain's top surface (shallow
 * water by virtual pipes: O'Brien and Hodgins; Mei et al.). One column per cell, ground height
 * from the first rock under the region's top. Cheap and smooth for surface water, but it sees
 * only the uppermost ground: no caves, overhangs or sealed cavities. Engine-free.
 */
class STELLAR_API FFluidHeightfield
{
public:
	explicit FFluidHeightfield(const FFluidFrame& InFrame);

	/** Ground height per column (metres above the region's floor) from the terrain density. */
	void RebuildGround(FSolidSampler Density);
	void AddAt(const FVector3d& P, double CubicMetres);
	/** One step of Dt seconds; returns the largest depth change (settled when tiny). */
	double Step(double Dt);
	double CubicMetres() const;
	double Depth(int32 I, int32 J) const { return D[J * F.Nx + I]; }
	double Ground(int32 I, int32 J) const { return G[J * F.Nx + I]; }
	int64 Bytes() const { return int64(D.Num()) * sizeof(double) * 6; }

private:
	FFluidFrame F;
	TArray<double> G, D, FluxR, FluxL, FluxU, FluxDn;
};
