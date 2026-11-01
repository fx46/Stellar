#include "Player/SculptBrush.h"

double SculptBrush::Pace(double Setting, bool bGod)
{
	const double S = FMath::IsFinite(Setting) && Setting > 0 ? Setting : 1.0;
	return FMath::Clamp(S, MinimumPace, bGod ? 1e6 : 1.0);
}

double SculptBrush::ScrollPace(double Setting, int32 Direction, bool bGod)
{
	return Pace(Pace(Setting, bGod) * (Direction > 0 ? PaceStep : 1.0 / PaceStep), bGod);
}

double SculptBrush::Radius(double Setting, bool bGod)
{
	return FMath::Clamp(Setting, 0.5, bGod ? 64.0 : 4.0);
}

double SculptBrush::ScrollRadius(double Setting, int32 Direction, bool bGod)
{
	return Radius(Radius(Setting, bGod) * (Direction > 0 ? 1.15 : 1.0 / 1.15), bGod);
}

double SculptBrush::Depth(double InRadius, double Hardness, bool bGod)
{
	const double R = Radius(InRadius, bGod);
	// God mode unlocks pace, not long drilling tubes: a shallow bite is the same shape in both
	// modes; larger brushes spread work over their area.
	const double D = StepDepth * FMath::Min(1.0, FMath::Square(DefaultRadius / R)) / FMath::Max(0.25, Hardness);
	return FMath::Clamp(D, 0.005, StepDepth);
}

double SculptBrush::Charge(double Work, double Dt, double InPace, int32 Capacity)
{
	if (!FMath::IsFinite(Dt) || Dt <= 0)
		return FMath::Clamp(Work, 0.0, double(Capacity));
	return FMath::Min(double(Capacity), FMath::Max(0.0, Work) + Dt * InPace);
}

bool SculptBrush::ProjectSurface(TFunctionRef<double(const FVector3d&)> Density, const FVector3d& Point, const FVector3d& Outward,
	double Span, FVector3d& OutHit)
{
	FVector3d High = Point + Outward * Span;
	double Previous = Density(High);
	for (int32 I = 1; I <= 24; I++)
	{
		FVector3d Low = Point + Outward * (Span * (1 - 2.0 * I / 24));
		const double Value = Density(Low);
		if (Previous >= 0 && Value <= 0)
		{
			for (int32 J = 0; J < 10; J++)
			{
				const FVector3d Middle = (High + Low) * 0.5;
				if (Density(Middle) > 0) High = Middle; else Low = Middle;
			}
			OutHit = (High + Low) * 0.5;
			return true;
		}
		High = Low;
		Previous = Value;
	}
	return false;
}
