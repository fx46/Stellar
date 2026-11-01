#pragma once

#include "CoreMinimal.h"
#include "Terrain/TerrainEdits.h"
#include "Terrain/FluidVolume.h"
#include "Game/Structures.h"

class FPlanetTerrain;

/** The start site (port of the Godot scale study's choice): open plain - flat within 2.5 m over
 * 30 m, off escarpments, valleys and landmarks - facing the highest ridge 15-42 km away. */
struct FSiteChoice
{
	FVector3d Surface = FVector3d::ZeroVector;
	FVector3d Up = FVector3d(0, 0, 1);
	FVector3d Forward = FVector3d(1, 0, 0);
	double Height = 0;
	double RidgeDistance = 0;
	double RidgeElevationDeg = 0;
};

STELLAR_API FSiteChoice ChooseStartSite(const FPlanetTerrain& Terrain);

/**
 * The milestone 0b industrial test site, as data: a repeatable surface scenario authored beside
 * the start site on the planet itself, with the scale the stand-ins establish.
 *
 * - a graded yard, 70 x 44 m: room for a road loop;
 * - an incline: a 70 m ramp at about 11 degrees, cut 14 m down to a landing pit;
 * - a small cavern: a tunnel from the pit into rock under the plain, ending in a chamber;
 * - two deposits in different rock: iron in the sandstone of the ramp's wall, copper in the
 *   basalt of the chamber's wall, each marked by a beacon; a depot in the yard;
 * - for the 0c fluid spike: a spring on a mound, a channel down its side and across the plain into
 *   a dug basin; and a sealed water pocket two metres behind the ramp's wall, which drains down
 *   the ramp into the pit and the cavern when the wall is breached.
 *
 * Coordinates are a local level frame at the yard's centre: X forward (the site's view), Y right,
 * Z up, metres; the planet's curvature across the site is about a centimetre.
 */
struct STELLAR_API FTestSitePlan
{
	FVector3d Centre = FVector3d::ZeroVector;
	FVector3d Up = FVector3d(0, 0, 1), Forward = FVector3d(1, 0, 0), Right = FVector3d(0, 1, 0);

	TArray<FEditOp> Ops;
	TArray<FDeposit> Deposits;

	struct FMarker
	{
		EStructureType Type = EStructureType::Beacon;
		FVector3d Position = FVector3d::ZeroVector;
		FVector3d Forward = FVector3d(1, 0, 0);
	};
	TArray<FMarker> Structures;

	FFluidFrame SurfaceWater;
	FVector3d Spring = FVector3d::ZeroVector;
	FFluidFrame Aquifer;
	FVector3d Pocket = FVector3d::ZeroVector;
	double PocketRadius = 3;
	/** Where to dig to breach the pocket: on the ramp's flat floor beside it. */
	FVector3d BreachPoint = FVector3d::ZeroVector;
	/** The breach itself: a short tunnel from the ramp's floor into the pocket. */
	TArray<FEditOp> Breach;

	// Named places, for the views and the trial.
	FVector3d RampTop = FVector3d::ZeroVector, RampBottom = FVector3d::ZeroVector, Chamber = FVector3d::ZeroVector, Basin = FVector3d::ZeroVector;

	/** Planet-frame point at local (x, y, z) from the yard's centre. */
	FVector3d At(double X, double Y, double Z) const { return Centre + Forward * X + Right * Y + Up * Z; }

	/** Lay the site out beside the start site (SiteSurface, SiteUp, SiteForward). */
	static FTestSitePlan Build(const FPlanetTerrain& Terrain, const FVector3d& SiteSurface, const FVector3d& SiteUp, const FVector3d& SiteForward);
};
