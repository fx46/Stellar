#include "Terrain/TerrainEdits.h"
#include "Terrain/PlanetField.h"
#include "Serialization/Archive.h"

FSculptOp FSculptOp::Make(const FVector3d& Surface, const FVector3d& Inward, double Radius, double Depth)
{
	FSculptOp Op;
	Op.Center = Surface;
	Op.Inward = Inward.GetSafeNormal();
	Op.Radius = Radius;
	Op.Support = Radius + FMath::Abs(Depth) * 2;
	Op.Strength = Depth;
	return Op;
}

double FSculptOp::Weight(const FVector3d& P) const
{
	const FVector3d Q = P - Center;
	const double Axial = Q.Dot(Inward);
	const double RadialSq = FMath::Max(0.0, Q.SizeSquared() - Axial * Axial);
	const double W = FMath::Max(0.0, 1 - RadialSq / (Radius * Radius));
	const double T = FMath::Clamp((FMath::Abs(Axial) - Support * .5) / (Support * .5), 0.0, 1.0);
	// Quintic fade has zero first and second derivatives at both ends.
	const double Fade = 1 - T * T * T * (T * (T * 6 - 15) + 10);
	// A broad working core with a C2 edge, rather than a narrow pointed dent.
	return W * W * W * (W * (W * 6 - 15) + 10) * Fade;
}

float FTerrainEdits::FSampler::At(int32 I, int32 J, int32 K) const
{
	const FIntVector Key(I >> FEditBrick::Bits, J >> FEditBrick::Bits, K >> FEditBrick::Bits);
	if (Key != LastKey)
	{
		LastKey = Key;
		const TSharedRef<FEditBrick>* Found = Edits->Bricks.Find(Key);
		Last = Found != nullptr ? &Found->Get() : nullptr;
	}
	if (Last == nullptr)
		return 0.f;
	constexpr int32 M = FEditBrick::Size - 1;
	return Last->Delta[FEditBrick::Index(I & M, J & M, K & M)];
}

float FTerrainEdits::FSampler::Delta(double X, double Y, double Z) const
{
	if (Edits == nullptr || Edits->Bricks.Num() == 0)
		return 0.f;
	const double H = Edits->Spacing;
	const double Gx = X / H, Gy = Y / H, Gz = Z / H;
	const double Fx = FMath::FloorToDouble(Gx), Fy = FMath::FloorToDouble(Gy), Fz = FMath::FloorToDouble(Gz);
	const int32 I = int32(Fx), J = int32(Fy), K = int32(Fz);
	const float Tx = float(Gx - Fx), Ty = float(Gy - Fy), Tz = float(Gz - Fz);
	const float C000 = At(I, J, K), C100 = At(I + 1, J, K), C010 = At(I, J + 1, K), C110 = At(I + 1, J + 1, K);
	const float C001 = At(I, J, K + 1), C101 = At(I + 1, J, K + 1), C011 = At(I, J + 1, K + 1), C111 = At(I + 1, J + 1, K + 1);
	if ((C000 == 0.f) & (C100 == 0.f) & (C010 == 0.f) & (C110 == 0.f) & (C001 == 0.f) & (C101 == 0.f) & (C011 == 0.f) & (C111 == 0.f))
		return 0.f;
	const float X00 = C000 + Tx * (C100 - C000), X10 = C010 + Tx * (C110 - C010);
	const float X01 = C001 + Tx * (C101 - C001), X11 = C011 + Tx * (C111 - C011);
	const float Y0 = X00 + Ty * (X10 - X00), Y1 = X01 + Ty * (X11 - X01);
	return Y0 + Tz * (Y1 - Y0);
}

float FTerrainEdits::Delta(double X, double Y, double Z) const
{
	return FSampler(this).Delta(X, Y, Z);
}

FBox3d FTerrainEdits::BrickBox(const FIntVector& Key) const
{
	const double S = FEditBrick::Size * Spacing;
	const FVector3d Lo(Key.X * S - Spacing, Key.Y * S - Spacing, Key.Z * S - Spacing);
	return FBox3d(Lo, Lo + FVector3d(S + Spacing));
}

template <typename Fn>
void FTerrainEdits::ForEachBrickIn(const FBox3d& Box, Fn&& Visit) const
{
	if (Bricks.Num() == 0)
		return;
	const double S = FEditBrick::Size * Spacing;
	// Brick keys whose influence (a cell beyond the low side) can reach the box.
	const FIntVector Lo(int32(FMath::FloorToDouble(Box.Min.X / S)), int32(FMath::FloorToDouble(Box.Min.Y / S)), int32(FMath::FloorToDouble(Box.Min.Z / S)));
	const FIntVector Hi(int32(FMath::FloorToDouble((Box.Max.X + Spacing) / S)), int32(FMath::FloorToDouble((Box.Max.Y + Spacing) / S)),
		int32(FMath::FloorToDouble((Box.Max.Z + Spacing) / S)));
	const int64 Span = int64(Hi.X - Lo.X + 1) * int64(Hi.Y - Lo.Y + 1) * int64(Hi.Z - Lo.Z + 1);
	if (Span <= int64(Bricks.Num()) * 2)
	{
		for (int32 X = Lo.X; X <= Hi.X; X++)
			for (int32 Y = Lo.Y; Y <= Hi.Y; Y++)
				for (int32 Z = Lo.Z; Z <= Hi.Z; Z++)
					if (const TSharedRef<FEditBrick>* B = Bricks.Find(FIntVector(X, Y, Z)))
						Visit(FIntVector(X, Y, Z), B->Get());
	}
	else
	{
		for (const TPair<FIntVector, TSharedRef<FEditBrick>>& Pair : Bricks)
		{
			const FIntVector& K = Pair.Key;
			if (K.X >= Lo.X && K.X <= Hi.X && K.Y >= Lo.Y && K.Y <= Hi.Y && K.Z >= Lo.Z && K.Z <= Hi.Z)
				Visit(K, Pair.Value.Get());
		}
	}
}

bool FTerrainEdits::Touches(const FBox3d& Box) const
{
	bool bAny = false;
	ForEachBrickIn(Box, [&bAny](const FIntVector&, const FEditBrick&) { bAny = true; });
	return bAny;
}

void FTerrainEdits::Gather(const FBox3d& Box, TArray<FBox3d>& Out) const
{
	ForEachBrickIn(Box, [this, &Out](const FIntVector& K, const FEditBrick&) { Out.Add(BrickBox(K)); });
}

uint32 FTerrainEdits::VersionIn(const FBox3d& Box) const
{
	uint32 V = 0;
	ForEachBrickIn(Box, [&V](const FIntVector&, const FEditBrick& B) { V = FMath::Max(V, B.Version); });
	return V;
}

TSharedRef<const FTerrainEdits> FTerrainEdits::Apply(const FSculptOp& Op, const FPlanetField& Field, FEditResult& Result) const
{
	TSharedRef<FTerrainEdits> Next = MakeShared<FTerrainEdits>(Spacing);
	Next->Bricks = Bricks;
	Next->Version = Version + 1;

	const double H = Spacing;
	const double Reach = Op.Reach() + H;
	const FIntVector Lo(int32(FMath::FloorToDouble((Op.Center.X - Reach) / H)), int32(FMath::FloorToDouble((Op.Center.Y - Reach) / H)),
		int32(FMath::FloorToDouble((Op.Center.Z - Reach) / H)));
	const FIntVector Hi(int32(FMath::CeilToDouble((Op.Center.X + Reach) / H)), int32(FMath::CeilToDouble((Op.Center.Y + Reach) / H)),
		int32(FMath::CeilToDouble((Op.Center.Z + Reach) / H)));

	TSet<FIntVector> Cloned;
	constexpr int32 M = FEditBrick::Size - 1;
	const double Cell = H * H * H;
	for (int32 I = Lo.X; I <= Hi.X; I++)
		for (int32 J = Lo.Y; J <= Hi.Y; J++)
			for (int32 K = Lo.Z; K <= Hi.Z; K++)
			{
				const FVector3d P(I * H, J * H, K * H);
				const double W = Op.Weight(P);
				if (W <= 0.0)
					continue;
				const float Change = float(Op.Strength * W);
				if (Change == 0.f)
					continue;
				const FIntVector Key(I >> FEditBrick::Bits, J >> FEditBrick::Bits, K >> FEditBrick::Bits);
				TSharedRef<FEditBrick>* Slot = Next->Bricks.Find(Key);
				if (Slot == nullptr)
				{
					TSharedRef<FEditBrick> Fresh = MakeShared<FEditBrick>();
					FMemory::Memzero(Fresh->Delta, sizeof(Fresh->Delta));
					Slot = &Next->Bricks.Add(Key, Fresh);
					Cloned.Add(Key);
				}
				else if (!Cloned.Contains(Key))
				{
					*Slot = MakeShared<FEditBrick>(Slot->Get());
					Cloned.Add(Key);
				}
				FEditBrick& Brick = Slot->Get();
				Brick.Version = Next->Version;
				float& Value = Brick.Delta[FEditBrick::Index(I & M, J & M, K & M)];

				// Removed volume: each lattice point stands for a cell, rock in proportion to how
				// far inside the surface it is (within half a cell).
				const double Proc = Field.Density(P);
				const double Before = FMath::Clamp(0.5 - (Proc + Value) / H, 0.0, 1.0);
				Value += Change;
				const double After = FMath::Clamp(0.5 - (Proc + Value) / H, 0.0, 1.0);
				if (Before != After)
				{
					const FGeoSample Geo = Field.Geology(P, -Proc);
					Result.Volume[int32(Geo.Material)] += (Before - After) * Cell;
				}
				Result.Changed += P;
				Result.bChanged = true;
			}
	if (Result.bChanged)
		Result.Changed = Result.Changed.ExpandBy(H);
	return Next;
}

void FTerrainEdits::Serialize(FArchive& Ar)
{
	int32 FileVersion = 1;
	Ar << FileVersion;
	Ar << Spacing;
	Ar << Version;
	int32 Num = Bricks.Num();
	Ar << Num;
	if (Ar.IsLoading())
	{
		Bricks.Reset();
		for (int32 I = 0; I < Num && !Ar.IsError(); I++)
		{
			FIntVector Key;
			Ar << Key;
			TSharedRef<FEditBrick> Brick = MakeShared<FEditBrick>();
			Ar << Brick->Version;
			Ar.Serialize(Brick->Delta, sizeof(Brick->Delta));
			Bricks.Add(Key, Brick);
		}
	}
	else
	{
		for (TPair<FIntVector, TSharedRef<FEditBrick>>& Pair : Bricks)
		{
			Ar << Pair.Key;
			Ar << Pair.Value->Version;
			Ar.Serialize(Pair.Value->Delta, sizeof(Pair.Value->Delta));
		}
	}
}
