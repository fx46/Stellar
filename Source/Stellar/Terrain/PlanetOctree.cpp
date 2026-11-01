#include "Terrain/PlanetOctree.h"
#include "Async/ParallelFor.h"

FPlanetOctree::FPlanetOctree(double BoundRadius, double InLeafSize, double InSplitFactor)
	: LeafSize(InLeafSize), RootLevel(0), Half(0), SplitFactor(InSplitFactor)
{
	check(SplitFactor > FMath::Sqrt(3.0)); // keeps neighbours within a level (see the class comment)
	while (LeafSize * double(1ll << RootLevel) < 2 * BoundRadius)
		RootLevel++;
	Half = LeafSize * double(1ll << RootLevel) * 0.5;
}

FVector3d FPlanetOctree::Min(const FNodeKey& K) const
{
	const double S = Size(K.Level);
	return FVector3d(-Half + K.X * S, -Half + K.Y * S, -Half + K.Z * S);
}

double FPlanetOctree::Distance(const FNodeKey& K, const FVector3d& P) const
{
	const FVector3d M = Min(K);
	const double S = Size(K.Level);
	const double Dx = FMath::Max(0.0, FMath::Max(M.X - P.X, P.X - (M.X + S)));
	const double Dy = FMath::Max(0.0, FMath::Max(M.Y - P.Y, P.Y - (M.Y + S)));
	const double Dz = FMath::Max(0.0, FMath::Max(M.Z - P.Z, P.Z - (M.Z + S)));
	return FMath::Sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
}

FRange FPlanetOctree::RadialRange(const FNodeKey& K) const
{
	const FVector3d M = Min(K);
	return RadialRange(M.X, M.Y, M.Z, Size(K.Level));
}

FRange FPlanetOctree::RadialRange(double X0, double Y0, double Z0, double S)
{
	double Near = 0, Far = 0;
	auto Axis = [S, &Near, &Far](double Lo)
	{
		const double Hi = Lo + S;
		const double N = Lo > 0 ? Lo : Hi < 0 ? Hi : 0;
		const double F = FMath::Max(FMath::Abs(Lo), FMath::Abs(Hi));
		Near += N * N;
		Far += F * F;
	};
	Axis(X0);
	Axis(Y0);
	Axis(Z0);
	return { FMath::Sqrt(Near), FMath::Sqrt(Far) };
}

TArray<FNodeKey> FPlanetOctree::Select(const FVector3d& C, TFunctionRef<bool(const FNodeKey&)> Keep, bool bParallel) const
{
	TArray<FNodeKey> Cut;
	TArray<FNodeKey> Frontier;
	Frontier.Add(Root());
	TArray<uint8> Holds;
	while (Frontier.Num() > 0)
	{
		Holds.SetNumUninitialized(Frontier.Num());
		if (bParallel && Frontier.Num() > 4)
			ParallelFor(Frontier.Num(), [&](int32 I) { Holds[I] = Keep(Frontier[I]) ? 1 : 0; });
		else
			for (int32 I = 0; I < Frontier.Num(); I++)
				Holds[I] = Keep(Frontier[I]) ? 1 : 0;
		TArray<FNodeKey> Next;
		for (int32 I = 0; I < Frontier.Num(); I++)
		{
			if (!Holds[I])
				continue;
			const FNodeKey& K = Frontier[I];
			if (K.Level > 0 && Distance(K, C) < SplitFactor * Size(K.Level))
				for (int32 Ch = 0; Ch < 8; Ch++)
					Next.Add(K.Child(Ch));
			else
				Cut.Add(K);
		}
		Frontier = MoveTemp(Next);
	}
	return Cut;
}

int32 FPlanetOctree::CoarserFaces(const FNodeKey& K, const TSet<FNodeKey>& Cut) const
{
	int32 Mask = 0;
	if (K.Level >= RootLevel)
		return 0;
	const int32 Count = 1 << (RootLevel - K.Level);
	for (int32 Axis = 0; Axis < 3; Axis++)
		for (int32 Side = 0; Side < 2; Side++)
		{
			const int32 D = Side == 0 ? -1 : 1;
			FNodeKey N = K;
			if (Axis == 0) N.X += D;
			else if (Axis == 1) N.Y += D;
			else N.Z += D;
			if (N.X < 0 || N.Y < 0 || N.Z < 0 || N.X >= Count || N.Y >= Count || N.Z >= Count)
				continue;
			if (Cut.Contains(N.Parent()))
				Mask |= 1 << (Axis * 2 + Side);
		}
	return Mask;
}
