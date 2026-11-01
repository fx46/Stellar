#pragma once

#include "CoreMinimal.h"
#include "Terrain/PlanetField.h"

class FPlanetField;

/** What a terrain edit does. */
enum class EEditKind : uint8
{
	/** The sculpt brush: a smooth displacement along an axis (positive digs, negative builds). */
	Sculpt,
	/** Grade toward a plane inside an oriented box with soft edges: flat pads, road beds, and
	 * ramps when the plane is tilted along the box. */
	Flatten,
	/** Remove a sphere of rock (CSG): caverns and tunnels. */
	Carve,
	/** Add a sphere of rock (CSG). */
	Fill,
};

/**
 * One terrain edit, planet frame, metres. Every kind maps the density before it to the density
 * after it at a point (Apply), so edits compose in order and store as offsets over the
 * procedural field.
 */
struct STELLAR_API FEditOp
{
	EEditKind Kind = EEditKind::Sculpt;
	FVector3d Center = FVector3d::ZeroVector;
	/** Sculpt: the inward axis. Flatten: the plane's normal (up, tilted for a ramp). */
	FVector3d Normal = FVector3d(0, 0, -1);
	/** Flatten: the box's long axis (in the plane). */
	FVector3d Axis = FVector3d(1, 0, 0);
	/** Sculpt: footprint radius. Carve/Fill: sphere radius. */
	double Radius = 2.5;
	/** Sculpt: axial reach. */
	double Support = 3.1;
	/** Sculpt: centre depth (positive digs, negative builds). */
	double Strength = 0.3;
	/** Flatten: half sizes along Axis, across it and along Normal, and the soft rim width. */
	double HalfLength = 0, HalfWidth = 0, HalfHeight = 0, Blend = 1;

	/** The sculpt brush (port of the Godot EditOp.Sculpt). */
	static FEditOp Sculpt(const FVector3d& Surface, const FVector3d& Inward, double Radius, double Depth);
	/** Grade to the plane through Center with normal Up tilted by Slope (rise per metre along
	 * Axis): a pad when Slope is 0, a ramp otherwise. */
	static FEditOp Flatten(const FVector3d& Center, const FVector3d& Up, const FVector3d& Axis, double HalfLength, double HalfWidth,
		double HalfHeight, double Slope = 0, double Blend = 2);
	static FEditOp Carve(const FVector3d& Center, double Radius);
	static FEditOp Fill(const FVector3d& Center, double Radius);

	/** The brush's weight at P, in [0, 1] (sculpt). */
	double Weight(const FVector3d& P) const;

	/** The density at P after this op, given the density D before it. */
	double Apply(double D, const FVector3d& P) const;

	/** A box (planet frame) outside which the op changes nothing. */
	FBox3d Bounds() const;

	void Serialize(FArchive& Ar);
};

/** Kept for code written against the first port. */
using FSculptOp = FEditOp;

/** Materials an edit can leave behind (the brick material channel); 0 = the natural rock. */
enum class EEditMaterial : uint8
{
	Natural = 0,
	/** Ground raised by building or grading: compacted fill. */
	Fill = 1,
};

/** One brick of the edit lattice: 16^3 density offsets and materials, copy-on-write between
 * snapshots. */
struct FEditBrick
{
	static constexpr int32 Bits = 4;
	static constexpr int32 Size = 1 << Bits;
	static constexpr int32 Count = Size * Size * Size;

	float Delta[Count];
	uint8 Material[Count];
	/** The snapshot version that last wrote this brick (what remeshing compares). */
	uint32 Version = 0;

	static int32 Index(int32 Lx, int32 Ly, int32 Lz) { return (Lx * Size + Ly) * Size + Lz; }
	bool IsEmpty() const;
};

/** What one edit did: removed (positive) or added (negative) rock volume per material. */
struct FEditResult
{
	double Volume[int32(EGeoMaterial::Count)] = {};
	/** Planet-frame box the edit changed (what must be remeshed). */
	FBox3d Changed = FBox3d(ForceInit);
	bool bChanged = false;
};

/**
 * The editable layer over a procedural planet (milestone 0 step 2): a SPARSE lattice of density
 * offsets, stored only where the terrain has been edited. The density anywhere is the procedural
 * field plus the trilinear interpolation of the offsets, so storing a brick for the first time
 * changes nothing anyone reads and edited ground meets untouched ground without a seam, at every
 * level of detail. Offsets live in bricks of 16^3 lattice points keyed by integer brick
 * coordinates in the planet frame - a 1000 km planet needs nothing allocated until someone digs.
 *
 * Bricks carry channels: the density offset and a material (what building leaves). The save
 * format records a channel mask per brick, so further per-lattice-point data can be added without
 * migrating saved edits (milestone 0c).
 *
 * Snapshots are immutable: an edit makes a new snapshot sharing every brick it did not touch, so
 * meshing threads read a consistent terrain while the next edit is computed.
 */
class STELLAR_API FTerrainEdits
{
public:
	explicit FTerrainEdits(double InSpacing) : Spacing(InSpacing) {}

	double GetSpacing() const { return Spacing; }
	uint32 GetVersion() const { return Version; }
	int32 NumBricks() const { return Bricks.Num(); }
	bool IsEmpty() const { return Bricks.Num() == 0; }
	/** Bytes held by bricks. */
	int64 BrickBytes() const { return int64(Bricks.Num()) * sizeof(FEditBrick); }

	/** The offset at a planet-frame point: trilinear over the lattice (0 where unedited). */
	float Delta(double X, double Y, double Z) const;

	/** The material left at the lattice point nearest P. */
	EEditMaterial MaterialAt(const FVector3d& P) const;

	/** Whether any stored brick can change the density inside the box. */
	bool Touches(const FBox3d& Box) const;

	/** Space boxes of the stored bricks that can change the density inside Box. */
	void Gather(const FBox3d& Box, TArray<FBox3d>& Out) const;

	/** The highest brick version that can change the density inside the box (0: none). */
	uint32 VersionIn(const FBox3d& Box) const;

	/** A new snapshot with Op applied, reporting the volume moved per material. */
	TSharedRef<const FTerrainEdits> Apply(const FEditOp& Op, const FPlanetField& Field, FEditResult& Result) const;

	/** A new snapshot with every op applied in order (one version step). */
	TSharedRef<const FTerrainEdits> ApplyAll(TConstArrayView<FEditOp> Ops, const FPlanetField& Field, FEditResult& Result) const;

	void Serialize(FArchive& Ar);

	/** A lookup cache for one thread: most samples fall in the brick of the previous one. */
	struct FSampler
	{
		const FTerrainEdits* Edits = nullptr;
		mutable FIntVector LastKey = FIntVector(MAX_int32);
		mutable const FEditBrick* Last = nullptr;

		explicit FSampler(const FTerrainEdits* InEdits) : Edits(InEdits) {}
		float Delta(double X, double Y, double Z) const;
		float At(int32 I, int32 J, int32 K) const;
		const FEditBrick* Brick(int32 I, int32 J, int32 K) const;
	};

private:
	/** Space covered by a brick's influence: a cell beyond its points on the low side. */
	FBox3d BrickBox(const FIntVector& Key) const;
	template <typename Fn> void ForEachBrickIn(const FBox3d& Box, Fn&& Visit) const;
	void ApplyInPlace(const FEditOp& Op, const FPlanetField& Field, FEditResult& Result, TSet<FIntVector>& Owned);

	double Spacing;
	uint32 Version = 0;
	TMap<FIntVector, TSharedRef<FEditBrick>> Bricks;
};
