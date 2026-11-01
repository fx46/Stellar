#pragma once

#include "CoreMinimal.h"

/**
 * Improved Perlin gradient noise in doubles, in [-1, 1], with a separate permutation per
 * layer index so octaves and layers are independent. Lattice coordinates are 64-bit, so
 * any metre position on a planet (scaled by a frequency) is exact.
 */
class STELLAR_API FPerlin
{
public:
	/**
	 * A bound on |gradient| of Sample per unit of input. Measured, not derived: hill-climbing
	 * the gradient's magnitude from 20,000 random starts peaks at 3.31; 4 leaves a fifth over
	 * that. Every height bound rests on it, so it errs high: too high only meshes more ground.
	 */
	static constexpr double SlopeBound = 4.0;

	explicit FPerlin(int32 InSeed) : Seed(InSeed) {}

	double Sample(double X, double Y, double Z, int32 Layer) const;

private:
	int32 Seed;
};

/** A closed interval of doubles. */
struct FRange
{
	double Lo = 0.0;
	double Hi = 0.0;
};

/**
 * The planet's height layers at one surface point: what colouring and site picking read.
 * Authored at fixed metre sizes, from tens of kilometres to tens of centimetres (milestone 0
 * step 3): basins and plains, escarpments, mountain ranges cut by traversable valleys, sparse
 * mesas and spire fields as landmarks, hills, rock outcrops, ground detail and fine roughness.
 */
struct FHeightLayers
{
	double Basin = 0, Escarp = 0, RangeMask = 0, Mountains = 0, Hills = 0, Detail = 0;
	/** 1 on a valley floor, falling to 0 on its rim. */
	double Valley = 0;
	/** The valley's own cut (negative), on top of what it removes from mountains and hills. */
	double ValleyCut = 0;
	double Outcrop = 0, Mesa = 0, Spire = 0, Fine = 0;
	double Total() const { return Basin + Escarp + Mountains + Hills + Detail + ValleyCut + Outcrop + Mesa + Spire + Fine; }
};

/** Rock types the terrain is made of (milestone 0 step 3: a first material-layer model). */
enum class EGeoMaterial : uint8
{
	Regolith,
	Sandstone,
	Basalt,
	IronOre,
	CopperOre,
	Quartz,
	/** Ground raised by building or grading (from the edits' material channel). */
	Fill,
	Count
};

/** An authored ore body (a test scenario's deposit): replaces the host rock within Radius. */
struct FDeposit
{
	FVector3d Center = FVector3d::ZeroVector;
	double Radius = 5;
	EGeoMaterial Material = EGeoMaterial::IronOre;
	FString Name;
};

/** What a point under the ground is made of. */
struct FGeoSample
{
	EGeoMaterial Material = EGeoMaterial::Regolith;
	/** Relative resistance to the brush: 1 is sandstone; the bite depth divides by it. */
	float Hardness = 1.f;
};

/**
 * The terrain of a planet far larger than the old voxel bodies (port of the Godot
 * ScaleTerrainField): a sphere of Radius metres plus a height in metres over each direction,
 * built from layers authored at fixed metre sizes - basins and plains (tens of kilometres),
 * escarpments, mountain ranges (ridged, kilometres high), hills, and ground detail - so a
 * bigger planet has more of the same landscape, not stretched landscape.
 *
 * Everything is computed in doubles in the planet's frame (centre at the origin, metres).
 * Callers pass a node origin in doubles and node-local float offsets (see Density), so the
 * density near the surface keeps full precision wherever on the planet it is read.
 *
 * Geology: below the original surface the rock is layered - regolith, banded sandstone,
 * basalt - with ore sheets and pockets, so a cut shows strata and hardness varies.
 *
 * Pure and thread-safe; engine-free apart from UE core types.
 */
class STELLAR_API FPlanetField
{
public:
	/** Bounds of Height over the whole planet (metres). */
	static constexpr double MinHeight = -1100.0;
	static constexpr double MaxHeight = 6000.0;

	FPlanetField(double InRadius, int32 InSeed = 1337);

	double GetRadius() const { return Radius; }
	int32 GetSeed() const { return Seed; }

	/** Height above Radius in the (unit) direction U. */
	double Height(double Ux, double Uy, double Uz) const;
	double Height(const FVector3d& Dir) const { return Height(Dir.X, Dir.Y, Dir.Z); }

	/** The height's layers at the surface point (X, Y, Z) (metres, on the sphere of Radius). */
	FHeightLayers Layers(double X, double Y, double Z) const;

	/**
	 * Bounds on Height over every surface point within Reach metres (straight-line) of the
	 * surface point (X, Y, Z): interval arithmetic through the same layers. Tight where the
	 * layers are broad compared with the reach. What lets meshing skip ground the surface
	 * cannot reach.
	 */
	FRange HeightRange(double X, double Y, double Z, double Reach) const;

	/**
	 * Density (negative inside rock) at the planet-frame origin O plus a float offset L.
	 * Not a true distance: radial distance above the surface.
	 */
	float Density(double Ox, double Oy, double Oz, float Lx, float Ly, float Lz) const;

	/** Density at a planet-frame point, all in doubles. */
	double Density(const FVector3d& P) const;

	/** What the rock is at P, Depth metres under the original (unedited) surface. */
	FGeoSample Geology(const FVector3d& P, double Depth) const;

	/** Linear colour of a material at P (for cut faces); regolith uses the surface colour. */
	FLinearColor MaterialColor(EGeoMaterial Material, const FVector3d& P) const;

	/** The surface's colour: dusty ochre plain, darker rock where steep, paler stone high up. */
	FLinearColor SurfaceColor(const FHeightLayers& L, double AboveRadius, float Slope) const;

	static const TCHAR* MaterialName(EGeoMaterial Material);

	/** Authored deposits, consulted before the procedural geology. Set once, before any thread
	 * reads the field. */
	void SetDeposits(TArray<FDeposit> InDeposits) { Deposits = MoveTemp(InDeposits); }
	const TArray<FDeposit>& GetDeposits() const { return Deposits; }
	/** The deposit containing P, if any. */
	const FDeposit* DepositAt(const FVector3d& P) const;

private:
	double Fbm(double X, double Y, double Z, double Freq, int32 Octaves, int32 Layer) const;
	double Ridged(double X, double Y, double Z, double Freq, int32 Octaves, int32 Layer) const;
	FRange OctaveRange(double X, double Y, double Z, double Freq, int32 Index, double Reach) const;
	FRange FbmRange(double X, double Y, double Z, double Freq, int32 Octaves, int32 Layer, double Reach) const;
	FRange RidgedRange(double X, double Y, double Z, double Freq, int32 Octaves, int32 Layer, double Reach) const;

	double Radius;
	int32 Seed;
	FPerlin Noise;
	TArray<FDeposit> Deposits;
};
