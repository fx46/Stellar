#include "Terrain/PlanetField.h"

namespace
{
	double SmoothStep(double A, double B, double V)
	{
		const double T = FMath::Clamp((V - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	double Fade(double T) { return T * T * T * (T * (T * 6.0 - 15.0) + 10.0); }

	uint32 Hash(int64 X, int64 Y, int64 Z, int32 Seed)
	{
		uint64 H = uint64(X) * 0x9E3779B97F4A7C15ull ^ uint64(Y) * 0xC2B2AE3D27D4EB4Full ^ uint64(Z) * 0x165667B19E3779F9ull
			^ uint64(uint32(Seed)) * 0x27D4EB2F165667C5ull;
		H ^= H >> 31;
		H *= 0x7FB5D329728EA185ull;
		H ^= H >> 27;
		return uint32(H >> 32);
	}

	// Ken Perlin's twelve edge gradients (and four repeats), picked by the hash's low bits.
	double Grad(uint32 H, double X, double Y, double Z)
	{
		switch (H & 15)
		{
		case 0: return X + Y;
		case 1: return -X + Y;
		case 2: return X - Y;
		case 3: return -X - Y;
		case 4: return X + Z;
		case 5: return -X + Z;
		case 6: return X - Z;
		case 7: return -X - Z;
		case 8: return Y + Z;
		case 9: return -Y + Z;
		case 10: return Y - Z;
		case 11: return -Y - Z;
		case 12: return X + Y;
		case 13: return -Y + Z;
		case 14: return -X + Y;
		default: return -Y - Z;
		}
	}

	FRange Scale(FRange A, double S) { return { A.Lo * S, A.Hi * S }; }

	FRange Product(FRange A, FRange B)
	{
		const double P1 = A.Lo * B.Lo, P2 = A.Lo * B.Hi, P3 = A.Hi * B.Lo, P4 = A.Hi * B.Hi;
		return { FMath::Min(FMath::Min(P1, P2), FMath::Min(P3, P4)), FMath::Max(FMath::Max(P1, P2), FMath::Max(P3, P4)) };
	}

	/** c - 0.6 c^3 over an interval of c in [-1, 1]: it rises to c = 1/sqrt(1.8) and falls after
	 * (and symmetrically below zero). */
	FRange BasinShape(FRange C)
	{
		auto G = [](double V) { return V - 0.6 * V * V * V; };
		double Lo = FMath::Min(G(C.Lo), G(C.Hi)), Hi = FMath::Max(G(C.Lo), G(C.Hi));
		const double Peak = 1.0 / FMath::Sqrt(1.8);
		if (C.Lo <= Peak && Peak <= C.Hi) Hi = FMath::Max(Hi, G(Peak));
		if (C.Lo <= -Peak && -Peak <= C.Hi) Lo = FMath::Min(Lo, G(-Peak));
		return { Lo, Hi };
	}
}

double FPerlin::Sample(double X, double Y, double Z, int32 Layer) const
{
	const double Fx = FMath::FloorToDouble(X), Fy = FMath::FloorToDouble(Y), Fz = FMath::FloorToDouble(Z);
	const int64 Ix = int64(Fx), Iy = int64(Fy), Iz = int64(Fz);
	const double Tx = X - Fx, Ty = Y - Fy, Tz = Z - Fz;
	const double U = Fade(Tx), V = Fade(Ty), W = Fade(Tz);
	const int32 S = int32(uint32(Seed) * 977u + uint32(Layer) * 7919u);

	const double G000 = Grad(Hash(Ix, Iy, Iz, S), Tx, Ty, Tz);
	const double G100 = Grad(Hash(Ix + 1, Iy, Iz, S), Tx - 1, Ty, Tz);
	const double G010 = Grad(Hash(Ix, Iy + 1, Iz, S), Tx, Ty - 1, Tz);
	const double G110 = Grad(Hash(Ix + 1, Iy + 1, Iz, S), Tx - 1, Ty - 1, Tz);
	const double G001 = Grad(Hash(Ix, Iy, Iz + 1, S), Tx, Ty, Tz - 1);
	const double G101 = Grad(Hash(Ix + 1, Iy, Iz + 1, S), Tx - 1, Ty, Tz - 1);
	const double G011 = Grad(Hash(Ix, Iy + 1, Iz + 1, S), Tx, Ty - 1, Tz - 1);
	const double G111 = Grad(Hash(Ix + 1, Iy + 1, Iz + 1, S), Tx - 1, Ty - 1, Tz - 1);

	const double X00 = G000 + U * (G100 - G000);
	const double X10 = G010 + U * (G110 - G010);
	const double X01 = G001 + U * (G101 - G001);
	const double X11 = G011 + U * (G111 - G011);
	const double Y0 = X00 + V * (X10 - X00);
	const double Y1 = X01 + V * (X11 - X01);
	return FMath::Clamp((Y0 + W * (Y1 - Y0)) * 0.97, -1.0, 1.0);
}

FPlanetField::FPlanetField(double InRadius, int32 InSeed)
	: Radius(InRadius), Seed(InSeed), Noise(InSeed)
{
}

double FPlanetField::Height(double Ux, double Uy, double Uz) const
{
	return Layers(Ux * Radius, Uy * Radius, Uz * Radius).Total();
}

FHeightLayers FPlanetField::Layers(double X, double Y, double Z) const
{
	FHeightLayers L;
	// Basins and plains: broad, gentle, flattened around sea level so most of the ground
	// between ranges is open plain.
	const double C = Fbm(X, Y, Z, 1.0 / 70000, 4, 0);
	L.Basin = 700 * (C - 0.6 * C * C * C);
	// Escarpments: a step of ~450 m where a 25 km field crosses a level - long cliff lines
	// between plateau and plain.
	const double E = Fbm(X, Y, Z, 1.0 / 25000, 3, 1);
	L.Escarp = 450 * SmoothStep(0.10, 0.16, E);
	// Mountain ranges: ridged crests where a 45 km mask is high; squared so ranges rise out
	// of the plain with sharp tops.
	L.RangeMask = SmoothStep(0.02, 0.32, Fbm(X, Y, Z, 1.0 / 45000, 3, 2));
	const double Ridge = L.RangeMask > 0 ? Ridged(X, Y, Z, 1.0 / 14000, 6, 3) : 0;
	L.Mountains = L.RangeMask * Ridge * Ridge * 3900;
	// Hills, stronger in the ranges' foothills.
	L.Hills = Fbm(X, Y, Z, 1.0 / 2600, 3, 4) * 140 * (0.35 + 0.65 * L.RangeMask);
	// Ground detail: what you walk over.
	L.Detail = Fbm(X, Y, Z, 1.0 / 140, 3, 5) * 3.0 + Fbm(X, Y, Z, 1.0 / 19, 2, 6) * 0.45;
	return L;
}

FRange FPlanetField::HeightRange(double X, double Y, double Z, double Reach) const
{
	if (Reach >= Radius)
		return { MinHeight, MaxHeight };
	const FRange C = FbmRange(X, Y, Z, 1.0 / 70000, 4, 0, Reach);
	const FRange Basin = Scale(BasinShape(C), 700);
	const FRange E = FbmRange(X, Y, Z, 1.0 / 25000, 3, 1, Reach);
	const FRange Escarp = { 450 * SmoothStep(0.10, 0.16, E.Lo), 450 * SmoothStep(0.10, 0.16, E.Hi) };
	const FRange M = FbmRange(X, Y, Z, 1.0 / 45000, 3, 2, Reach);
	const FRange Mask = { SmoothStep(0.02, 0.32, M.Lo), SmoothStep(0.02, 0.32, M.Hi) };
	const FRange Ridge = Mask.Hi > 0 ? RidgedRange(X, Y, Z, 1.0 / 14000, 6, 3, Reach) : FRange{ 0.0, 0.0 };
	const FRange Mountains = { Mask.Lo * Ridge.Lo * Ridge.Lo * 3900, Mask.Hi * Ridge.Hi * Ridge.Hi * 3900 };
	const FRange Hills = Scale(Product(FbmRange(X, Y, Z, 1.0 / 2600, 3, 4, Reach), { 0.35 + 0.65 * Mask.Lo, 0.35 + 0.65 * Mask.Hi }), 140);
	const FRange D1 = Scale(FbmRange(X, Y, Z, 1.0 / 140, 3, 5, Reach), 3.0);
	const FRange D2 = Scale(FbmRange(X, Y, Z, 1.0 / 19, 2, 6, Reach), 0.45);
	// A millimetre for rounding: the exact sum is computed in another order.
	return { Basin.Lo + Escarp.Lo + Mountains.Lo + Hills.Lo + D1.Lo + D2.Lo - 1e-3,
	         Basin.Hi + Escarp.Hi + Mountains.Hi + Hills.Hi + D1.Hi + D2.Hi + 1e-3 };
}

FRange FPlanetField::OctaveRange(double X, double Y, double Z, double Freq, int32 Index, double Reach) const
{
	const double Slack = FPerlin::SlopeBound * Freq * Reach;
	if (Slack >= 2)
		return { -1, 1 };
	const double V = Noise.Sample(X * Freq, Y * Freq, Z * Freq, Index);
	return { FMath::Max(-1.0, V - Slack), FMath::Min(1.0, V + Slack) };
}

FRange FPlanetField::FbmRange(double X, double Y, double Z, double Freq, int32 Octaves, int32 Layer, double Reach) const
{
	double Lo = 0, Hi = 0, Amp = 1, Norm = 0;
	for (int32 O = 0; O < Octaves; O++)
	{
		const FRange R = OctaveRange(X, Y, Z, Freq, Layer * 16 + O, Reach);
		Lo += R.Lo * Amp;
		Hi += R.Hi * Amp;
		Norm += Amp;
		Amp *= 0.5;
		Freq *= 2.0;
	}
	return { Lo / Norm, Hi / Norm };
}

FRange FPlanetField::RidgedRange(double X, double Y, double Z, double Freq, int32 Octaves, int32 Layer, double Reach) const
{
	double Lo = 0, Hi = 0, Amp = 1, Norm = 0;
	for (int32 O = 0; O < Octaves; O++)
	{
		const FRange R = OctaveRange(X, Y, Z, Freq, Layer * 16 + O, Reach);
		// |v| over [lo, hi], then n = 1 - |v| >= 0, then n^2 (increasing for n >= 0).
		const double AbsLo = R.Lo <= 0 && R.Hi >= 0 ? 0 : FMath::Min(FMath::Abs(R.Lo), FMath::Abs(R.Hi));
		const double AbsHi = FMath::Max(FMath::Abs(R.Lo), FMath::Abs(R.Hi));
		Lo += (1 - AbsHi) * (1 - AbsHi) * Amp;
		Hi += (1 - AbsLo) * (1 - AbsLo) * Amp;
		Norm += Amp;
		Amp *= 0.5;
		Freq *= 2.0;
	}
	return { Lo / Norm, Hi / Norm };
}

float FPlanetField::Density(double Ox, double Oy, double Oz, float Lx, float Ly, float Lz) const
{
	const double X = Ox + Lx, Y = Oy + Ly, Z = Oz + Lz;
	const double R = FMath::Sqrt(X * X + Y * Y + Z * Z);
	const double Above = R - Radius;
	// Outside the height bounds the sign is known: skip the noise.
	if (Above > MaxHeight + 1)
		return float(Above - MaxHeight);
	if (Above < MinHeight - 1)
		return float(Above - MinHeight);
	const double Inv = 1.0 / R;
	return float(Above - Height(X * Inv, Y * Inv, Z * Inv));
}

double FPlanetField::Density(const FVector3d& P) const
{
	const double R = P.Length();
	const double Above = R - Radius;
	if (Above > MaxHeight + 1)
		return Above - MaxHeight;
	if (Above < MinHeight - 1)
		return Above - MinHeight;
	return Above - Height(P / R);
}

double FPlanetField::Fbm(double X, double Y, double Z, double Freq, int32 Octaves, int32 Layer) const
{
	double Sum = 0, Amp = 1, Norm = 0;
	for (int32 O = 0; O < Octaves; O++)
	{
		Sum += Noise.Sample(X * Freq, Y * Freq, Z * Freq, Layer * 16 + O) * Amp;
		Norm += Amp;
		Amp *= 0.5;
		Freq *= 2.0;
	}
	return Sum / Norm;
}

double FPlanetField::Ridged(double X, double Y, double Z, double Freq, int32 Octaves, int32 Layer) const
{
	double Sum = 0, Amp = 1, Norm = 0;
	for (int32 O = 0; O < Octaves; O++)
	{
		const double N = 1 - FMath::Abs(Noise.Sample(X * Freq, Y * Freq, Z * Freq, Layer * 16 + O));
		Sum += N * N * Amp;
		Norm += Amp;
		Amp *= 0.5;
		Freq *= 2.0;
	}
	return Sum / Norm;
}

// ---- geology ----

FGeoSample FPlanetField::Geology(const FVector3d& P, double Depth) const
{
	// Noise layers 40+ are reserved for geology (the height uses 0-6 * 16 + octave).
	const double Regolith = 0.7 + 0.5 * Noise.Sample(P.X / 7.0, P.Y / 7.0, P.Z / 7.0, 40);
	if (Depth < Regolith)
		return { EGeoMaterial::Regolith, 0.6f };

	const double SandBase = 6.0 + 4.0 * Noise.Sample(P.X / 45.0, P.Y / 45.0, P.Z / 45.0, 41);
	if (Depth < SandBase)
	{
		// Iron: thin sheets where a field crosses zero, tilted with the strata.
		const double Sheet = Noise.Sample(P.X / 11.0, P.Y / 11.0, P.Z / 11.0, 42);
		if (FMath::Abs(Sheet) < 0.045)
			return { EGeoMaterial::IronOre, 1.4f };
		return { EGeoMaterial::Sandstone, 1.0f };
	}

	// Basalt below, with copper pockets and quartz deeper still.
	const double Pocket = Noise.Sample(P.X / 9.0, P.Y / 9.0, P.Z / 9.0, 43);
	if (Pocket > 0.62)
		return { EGeoMaterial::CopperOre, 1.6f };
	if (Depth > SandBase + 6.0 && Noise.Sample(P.X / 5.0, P.Y / 5.0, P.Z / 5.0, 44) > 0.7)
		return { EGeoMaterial::Quartz, 2.0f };
	return { EGeoMaterial::Basalt, 2.2f };
}

FLinearColor FPlanetField::MaterialColor(EGeoMaterial Material, const FVector3d& P) const
{
	// Authored as sRGB, converted once to linear.
	static const FLinearColor SandDark = FLinearColor::FromSRGBColor(FColor(112, 58, 34));
	static const FLinearColor SandLight = FLinearColor::FromSRGBColor(FColor(168, 112, 72));
	static const FLinearColor Basalt = FLinearColor::FromSRGBColor(FColor(46, 46, 50));
	static const FLinearColor Iron = FLinearColor::FromSRGBColor(FColor(122, 46, 22));
	static const FLinearColor Copper = FLinearColor::FromSRGBColor(FColor(36, 104, 86));
	static const FLinearColor Quartz = FLinearColor::FromSRGBColor(FColor(214, 212, 204));
	static const FLinearColor Regolith = FLinearColor::FromSRGBColor(FColor(120, 84, 56));
	switch (Material)
	{
	case EGeoMaterial::Sandstone:
	{
		// Bands by height on the planet: horizontal strata, gently warped.
		const double Elevation = P.Length() - Radius + 1.5 * Noise.Sample(P.X / 60.0, P.Y / 60.0, P.Z / 60.0, 45);
		const double Band = 0.5 + 0.5 * FMath::Sin(Elevation * 2.1);
		return FMath::Lerp(SandDark, SandLight, float(Band));
	}
	case EGeoMaterial::Basalt: return Basalt;
	case EGeoMaterial::IronOre: return Iron;
	case EGeoMaterial::CopperOre: return Copper;
	case EGeoMaterial::Quartz: return Quartz;
	default: return Regolith;
	}
}

FLinearColor FPlanetField::SurfaceColor(const FHeightLayers& L, double AboveRadius, float Slope) const
{
	const float Tone = float(FMath::Clamp(0.5 + L.Basin / 1400 + L.Hills / 600, 0.0, 1.0));
	// The Godot colours are sRGB; convert once to linear.
	static const FLinearColor PlainA = FLinearColor::FromSRGBColor(FColor(153, 115, 79));
	static const FLinearColor PlainB = FLinearColor::FromSRGBColor(FColor(181, 153, 117));
	static const FLinearColor Rock = FLinearColor::FromSRGBColor(FColor(92, 82, 74));
	static const FLinearColor High = FLinearColor::FromSRGBColor(FColor(148, 140, 133));
	const FLinearColor Plain = FMath::Lerp(PlainA, PlainB, Tone);
	const float Steep = 1.f - FMath::SmoothStep(0.62f, 0.86f, Slope);
	const float Altitude = float(FMath::Clamp((AboveRadius - 1800) / 1500, 0.0, 1.0));
	const FLinearColor Ground = FMath::Lerp(Plain, High, Altitude * 0.7f);
	return FMath::Lerp(Ground, FMath::Lerp(Rock, High, Altitude * 0.5f), Steep);
}

const TCHAR* FPlanetField::MaterialName(EGeoMaterial Material)
{
	switch (Material)
	{
	case EGeoMaterial::Regolith: return TEXT("Regolith");
	case EGeoMaterial::Sandstone: return TEXT("Sandstone");
	case EGeoMaterial::Basalt: return TEXT("Basalt");
	case EGeoMaterial::IronOre: return TEXT("Iron ore");
	case EGeoMaterial::CopperOre: return TEXT("Copper ore");
	case EGeoMaterial::Quartz: return TEXT("Quartz");
	default: return TEXT("?");
	}
}
