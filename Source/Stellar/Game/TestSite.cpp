#include "Game/TestSite.h"
#include "Terrain/PlanetTerrain.h"

namespace
{
	void Tangents(const FVector3d& Up, FVector3d& North, FVector3d& East)
	{
		const FVector3d Reference = FMath::Abs(Up.Z) > 0.99 ? FVector3d(1, 0, 0) : FVector3d(0, 0, 1);
		East = Reference.Cross(Up).GetSafeNormal();
		North = Up.Cross(East);
	}

	/** The direction Metres along the surface from U, at Azimuth from North. */
	FVector3d Walk(const FVector3d& U, const FVector3d& North, const FVector3d& East, double Azimuth, double Metres, double Radius)
	{
		const FVector3d T = North * FMath::Cos(Azimuth) + East * FMath::Sin(Azimuth);
		const double Theta = Metres / Radius;
		return (U * FMath::Cos(Theta) + T * FMath::Sin(Theta)).GetSafeNormal();
	}
}

FSiteChoice ChooseStartSite(const FPlanetTerrain& Terrain)
{
	const FPlanetField& F = Terrain.Field;
	const double R = F.GetRadius();
	const double Cap = FMath::Min(UE_DOUBLE_PI, 200000.0 / R);
	const double Distances[] = { 15000, 19000, 24000, 30000, 36000, 42000 };
	double Best = -TNumericLimits<double>::Max();
	FSiteChoice C;
	constexpr int32 Candidates = 700;
	for (int32 I = 0; I < Candidates; I++)
	{
		// Fibonacci points over the cap around +Z.
		const double CosT = 1 - (1 - FMath::Cos(Cap)) * (I + 0.5) / Candidates;
		const double SinT = FMath::Sqrt(1 - CosT * CosT);
		const double Phi = I * UE_DOUBLE_PI * (3 - FMath::Sqrt(5.0));
		const FVector3d U(SinT * FMath::Cos(Phi), SinT * FMath::Sin(Phi), CosT);
		const FHeightLayers L = F.Layers(U.X * R, U.Y * R, U.Z * R);
		if (L.RangeMask > 0.02 || (L.Escarp > 30 && L.Escarp < 420) || L.Valley > 0.02 || L.Outcrop > 0.5 || L.Mesa > 1 || L.Spire > 1)
			continue; // not open plain (nor on an escarpment's slope, in a valley or on a landmark)
		const double H0 = L.Total();
		FVector3d North, East;
		Tangents(U, North, East);
		// Flat: the ground 30 m around varies by under 2.5 m.
		bool bFlat = true;
		for (int32 B = 0; B < 4 && bFlat; B++)
			bFlat = FMath::Abs(F.Height(Walk(U, North, East, B * UE_DOUBLE_PI / 2, 30, R)) - H0) < 2.5;
		if (!bFlat)
			continue;
		const double Eye = R + H0 + 1.7;
		double Score = -TNumericLimits<double>::Max(), Bearing = 0, Ridge = 0;
		for (int32 B = 0; B < 24; B++)
		{
			const double Az = B * UE_DOUBLE_PI / 12;
			for (double D : Distances)
			{
				const double Rq = R + F.Height(Walk(U, North, East, Az, D, R));
				const double Theta = D / R;
				const double Elev = FMath::Atan2(Rq * FMath::Cos(Theta) - Eye, Rq * FMath::Sin(Theta));
				if (Elev > Score)
				{
					Score = Elev;
					Bearing = Az;
					Ridge = D;
				}
			}
		}
		if (Score > Best)
		{
			Best = Score;
			C.Up = U;
			C.Height = H0;
			C.RidgeElevationDeg = FMath::RadiansToDegrees(Score);
			C.RidgeDistance = Ridge;
			C.Forward = North * FMath::Cos(Bearing) + East * FMath::Sin(Bearing);
		}
	}
	if (Best == -TNumericLimits<double>::Max())
	{
		C.Up = FVector3d(0, 0, 1);
		C.Height = F.Height(C.Up);
		C.Forward = FVector3d(1, 0, 0);
	}
	C.Surface = C.Up * (R + C.Height);
	return C;
}

FTestSitePlan FTestSitePlan::Build(const FPlanetTerrain& Terrain, const FVector3d& SiteSurface, const FVector3d& SiteUp, const FVector3d& SiteForward)
{
	FTestSitePlan S;
	const FVector3d SiteRight = SiteUp.Cross(SiteForward).GetSafeNormal();
	// The yard: 55 m ahead and 45 m left of where the astronaut starts, at its ground's height.
	double Height = 0;
	const FVector3d Ground = Terrain.SurfaceAt(SiteSurface + SiteForward * 55.0 - SiteRight * 45.0, &Height);
	S.Up = Ground.GetSafeNormal();
	S.Forward = (SiteForward - S.Up * SiteForward.Dot(S.Up)).GetSafeNormal();
	S.Right = S.Up.Cross(S.Forward);
	S.Centre = Ground;

	// The original ground's height (relative to the yard) at local (x, y): every feature is laid
	// out against the ground actually there, so the site works on any plain.
	auto G = [&](double X, double Y) { return (Terrain.SurfaceAt(S.At(X, Y, 0)) - S.Centre).Dot(S.Up); };
	auto& Ops = S.Ops;
	// A graded or cut segment from (x0, y0) at height h0 to (x1, y1) at h1, HalfWidth either side.
	auto Segment = [&](double X0, double Y0, double H0, double X1, double Y1, double H1, double HalfWidth, double HalfHeight, double Blend)
	{
		const FVector3d A = S.At(X0, Y0, H0), B = S.At(X1, Y1, H1);
		const FVector3d Level = (S.At(X1, Y1, 0) - S.At(X0, Y0, 0));
		const double Length = Level.Length();
		Ops.Add(FEditOp::Flatten((A + B) * 0.5, S.Up, Level / Length, Length * 0.5, HalfWidth, HalfHeight, (H1 - H0) / Length, Blend));
	};

	// The yard, graded flat (cutting or filling up to 12 m of relief).
	Ops.Add(FEditOp::Flatten(S.At(0, 0, 0), S.Up, S.Forward, 35, 22, 12, 0, 3));

	// Deep enough that the cavern has at least two metres of rock over it wherever the plain lies.
	const double Lowest = FMath::Min(FMath::Min(G(0, -100), G(0, -117)), FMath::Min(G(0, -133), G(-6, -133)));
	const double Pit = FMath::Min(-14.0, Lowest - 12.0);
	auto RampAt = [Pit](double Y) { return Pit * (-22 - Y) / 70.0; }; // the ramp's floor at local y

	// The ramp: from the yard's left edge down to the pit over 70 m (about 11 degrees), 12 m wide.
	Segment(0, -22, 0, 0, -92, Pit, 6, 30, 2);
	S.RampTop = S.At(0, -22, 0);
	S.RampBottom = S.At(0, -92, Pit);

	// The landing pit at its foot.
	Ops.Add(FEditOp::Flatten(S.At(0, -100, Pit), S.Up, S.Forward, 9, 9, 30, 0, 1.5));

	// The cavern: a tunnel on from the pit's far wall, then a chamber with a level floor.
	for (int32 I = 0; I < 5; I++)
		Ops.Add(FEditOp::Carve(S.At(0, -109 - 4 * I, Pit + 2.6), 3.6));
	Ops.Add(FEditOp::Carve(S.At(0, -133, Pit + 3.5), 6.5));
	Ops.Add(FEditOp::Flatten(S.At(0, -133, Pit - 1.5), S.Up, S.Forward, 5.5, 5.5, 1.5, 0, 1));
	S.Chamber = S.At(0, -133, Pit - 1.5);

	// Deposits: iron in the sandstone of the ramp's right wall - centred in the sandstone band,
	// found by probing the geology there - and copper in the chamber's basalt.
	double SandTop = 1e9, SandBottom = -1e9;
	for (double Depth = 0.5; Depth < 14; Depth += 0.25)
	{
		const FVector3d P = S.At(8, -45, G(8, -45) - Depth);
		const EGeoMaterial M = Terrain.Field.Geology(P, -Terrain.Field.Density(P)).Material;
		if (M == EGeoMaterial::Sandstone || M == EGeoMaterial::IronOre)
		{
			SandTop = FMath::Min(SandTop, Depth);
			SandBottom = FMath::Max(SandBottom, Depth);
		}
	}
	const double IronDepth = SandBottom > SandTop ? (SandTop + SandBottom) * 0.5 : 3.0;
	FDeposit Iron;
	Iron.Center = S.At(8, -45, G(8, -45) - IronDepth);
	Iron.Radius = FMath::Clamp((SandBottom - SandTop) * 0.5 + 2.0, 2.5, 5.0);
	Iron.Material = EGeoMaterial::IronOre;
	Iron.Name = TEXT("Deposit A: iron in sandstone");
	FDeposit Copper;
	Copper.Center = S.At(-6, -133, Pit + 3);
	Copper.Radius = 4.5;
	Copper.Material = EGeoMaterial::CopperOre;
	Copper.Name = TEXT("Deposit B: copper in basalt");
	S.Deposits = { Iron, Copper };

	S.Structures.Add({ EStructureType::Beacon, S.At(10.5, -45, G(10.5, -45)), S.Forward });
	S.Structures.Add({ EStructureType::Beacon, S.At(-2, -131, Pit - 1), S.Forward });
	S.Structures.Add({ EStructureType::Depot, S.At(28, 15, 0), -S.Forward });

	// Surface water: a mound with a spring, a channel cut down its side and across the plain,
	// and a basin dug at the end of it - beyond the yard's front edge.
	const double Mound = G(52, 22), Plain = G(52, 11), Rim = G(52, -4), Basin = G(52, -10);
	Ops.Add(FEditOp::Fill(S.At(52, 22, Mound - 9), 14));
	const double Head = Mound + 4.0, Foot = Plain - 0.5, Lip = FMath::Min(Rim - 1.0, Foot - 0.3);
	Segment(52, 27, Head, 52, 11, Foot, 1.1, 2.5, 0.6);
	Segment(52, 11, Foot, 52, -5, Lip, 1.1, 2.0, 0.6);
	Ops.Add(FEditOp::Carve(S.At(52, -10, Basin + 4), 7.5));
	S.Spring = S.At(52, 26, Head + 0.6);
	S.Basin = S.At(52, -10, Basin - 2);
	const double Low = FMath::Min(Basin - 4, Lip - 1), High = Mound + 6;
	S.SurfaceWater = FFluidFrame::Around(S.At(52, 8, (Low + High) * 0.5), S.Up, S.Forward, FVector3d(18, 48, High - Low), 0.5);

	// The aquifer: a sealed pocket two metres behind the ramp's left wall, low on the ramp.
	const double Floor60 = RampAt(-60);
	S.Pocket = S.At(-11, -60, Floor60 - 0.4);
	S.PocketRadius = 3;
	Ops.Add(FEditOp::Carve(S.Pocket, S.PocketRadius));
	S.BreachPoint = S.At(-3.5, -60, Floor60);
	// Its floor a little below the ramp's, so the pocket drains down to the ramp, not into a sump.
	for (int32 I = 0; I < 4; I++)
		S.Breach.Add(FEditOp::Carve(S.At(-3.5 - 1.6 * I, -60, Floor60 + 1.0), 1.3));
	const double Top = FMath::Max(G(-3, -60), 0.0) + 1, Bottom = Pit - 4;
	S.Aquifer = FFluidFrame::Around(S.At(-3, -95, (Top + Bottom) * 0.5), S.Up, S.Forward, FVector3d(28, 92, Top - Bottom), 0.5);
	return S;
}
