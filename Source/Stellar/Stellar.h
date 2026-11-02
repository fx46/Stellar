#pragma once

#include "CoreMinimal.h"

DECLARE_LOG_CATEGORY_EXTERN(LogStellar, Log, All);

namespace Stellar
{
	/** Unreal units per metre: gameplay and terrain math run in metres, the engine in centimetres. */
	constexpr double UU = 100.0;

	/** A planet-frame position (metres) wrapped to 4096 m: what materials read as position, exact
	 * in floats anywhere on the planet. */
	inline FVector WrapPosition(const FVector3d& P)
	{
		auto W = [](double V) { const double R = FMath::Fmod(V, 4096.0); return R < 0 ? R + 4096.0 : R; };
		return FVector(W(P.X), W(P.Y), W(P.Z));
	}
}
