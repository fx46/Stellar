#pragma once

#include "CoreMinimal.h"

/**
 * The hand tool: a broad, shallow sculpting brush (port of the Godot BeamDrill). Pace controls
 * frequency, never bite depth: normal mode caps at one bite a second, god mode removes the
 * ceiling. Wider brushes take shallower layers; hardness also reduces removal. Engine-free.
 */
namespace SculptBrush
{
	constexpr double DefaultRadius = 2.5;
	constexpr double StepDepth = 0.3;
	constexpr double MinimumPace = 0.125;
	constexpr double PaceStep = 1.25;
	/** Arm's reach from the body (metres); god mode reaches much further. */
	constexpr double Reach = 9.0;
	constexpr double GodReach = 120.0;

	STELLAR_API double Pace(double Setting, bool bGod);
	STELLAR_API double ScrollPace(double Setting, int32 Direction, bool bGod);
	STELLAR_API double Radius(double Setting, bool bGod);
	STELLAR_API double ScrollRadius(double Setting, int32 Direction, bool bGod);

	/** One bite's centre depth for a brush of Radius through rock of Hardness. */
	STELLAR_API double Depth(double Radius, double Hardness, bool bGod);

	/** Accumulate bite work: Pace bites per second, never more than Capacity owed. */
	STELLAR_API double Charge(double Work, double Dt, double Pace, int32 Capacity);

	/** First surface under a footprint point along -Outward, including curved ground and fresh
	 * cuts: steps down from Span above, then bisects the crossing. */
	STELLAR_API bool ProjectSurface(TFunctionRef<double(const FVector3d&)> Density, const FVector3d& Point, const FVector3d& Outward,
		double Span, FVector3d& OutHit);
}
