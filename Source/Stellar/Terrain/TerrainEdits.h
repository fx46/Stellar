#pragma once

#include "CoreMinimal.h"
#include "Terrain/PlanetField.h"

class FPlanetField;

/**
 * The sculpt brush (port of the Godot EditOp.Sculpt): a compact, C2-continuous displacement.
 * Strength is the centre depth (positive digs, negative builds), Radius the footprint and
 * Support the axial reach along Inward. The outer half of the axial support fades out, so
 * neither the rim nor the far end cuts a seam. Planet frame, metres.
 */
struct STELLAR_API FSculptOp
{
	FVector3d Center = FVector3d::ZeroVector;
	FVector3d Inward = FVector3d(0, 0, -1);
	double Radius = 2.5;
	double Support = 3.1;
	double Strength = 0.3;

	static FSculptOp Make(const FVector3d& Surface, const FVector3d& Inward, double Radius, double Depth);

	/** How far from Center this op can change the density. */
	double Reach() const { return FMath::Sqrt(Radius * Radius + Support * Support); }

	/** The brush's weight at P, in [0, 1]. */
	double Weight(const FVector3d& P) const;
};

/** One brick of the edit lattice: 16^3 density offsets, copy-on-write between snapshots. */
struct FEditBrick
{
	static constexpr int32 Bits = 4;
	static constexpr int32 Size = 1 << Bits;
	static constexpr int32 Count = Size * Size * Size;

	float Delta[Count];
	/** The snapshot version that last wrote this brick (what remeshing compares). */
	uint32 Version = 0;

	static int32 Index(int32 Lx, int32 Ly, int32 Lz) { return (Lx * Size + Ly) * Size + Lz; }
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
 * offsets, stored only where the terrain has been edited. The density anywhere is the
 * procedural field plus the trilinear interpolation of the offsets, so storing a brick for the
 * first time changes nothing anyone reads and the edited ground meets the untouched ground
 * without a seam, at every level of detail. Offsets live in bricks of 16^3 lattice points keyed
 * by integer brick coordinates in the planet frame - a 1000 km planet needs nothing allocated
 * until someone digs, and each dig touches a handful of bricks.
 *
 * Snapshots are immutable: an edit makes a new snapshot sharing every brick it did not touch,
 * so meshing threads read a consistent terrain while the next edit is computed.
 */
class STELLAR_API FTerrainEdits
{
public:
	explicit FTerrainEdits(double InSpacing) : Spacing(InSpacing) {}

	double GetSpacing() const { return Spacing; }
	uint32 GetVersion() const { return Version; }
	int32 NumBricks() const { return Bricks.Num(); }
	bool IsEmpty() const { return Bricks.Num() == 0; }

	/** The offset at a planet-frame point: trilinear over the lattice (0 where unedited). */
	float Delta(double X, double Y, double Z) const;

	/** Whether any stored brick can change the density inside the box. */
	bool Touches(const FBox3d& Box) const;

	/** Space boxes of the stored bricks that can change the density inside Box. */
	void Gather(const FBox3d& Box, TArray<FBox3d>& Out) const;

	/** The highest brick version that can change the density inside the box (0: none). */
	uint32 VersionIn(const FBox3d& Box) const;

	/** A new snapshot with Op applied, reporting the volume moved per material. */
	TSharedRef<const FTerrainEdits> Apply(const FSculptOp& Op, const FPlanetField& Field, FEditResult& Result) const;

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
	};

private:
	/** Space covered by a brick's influence: a cell beyond its points on the low side. */
	FBox3d BrickBox(const FIntVector& Key) const;
	template <typename Fn> void ForEachBrickIn(const FBox3d& Box, Fn&& Visit) const;

	double Spacing;
	uint32 Version = 0;
	TMap<FIntVector, TSharedRef<FEditBrick>> Bricks;
};
