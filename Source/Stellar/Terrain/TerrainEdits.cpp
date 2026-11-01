#include "Terrain/TerrainEdits.h"
#include "Terrain/PlanetField.h"
#include "Async/ParallelFor.h"
#include "Serialization/Archive.h"

namespace
{
	/** How far beyond a carved or filled sphere the CSG keeps the density a distance (the Godot
	 * narrow band): deeper changes are never read by a mesh. */
	constexpr double CsgBand = 2.0;

	double SmoothStep(double A, double B, double V)
	{
		const double T = FMath::Clamp((V - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}
}

// ---- ops ----

FEditOp FEditOp::Sculpt(const FVector3d& Surface, const FVector3d& Inward, double Radius, double Depth)
{
	FEditOp Op;
	Op.Kind = EEditKind::Sculpt;
	Op.Center = Surface;
	Op.Normal = Inward.GetSafeNormal();
	Op.Radius = Radius;
	Op.Support = Radius + FMath::Abs(Depth) * 2;
	Op.Strength = Depth;
	return Op;
}

FEditOp FEditOp::Flatten(const FVector3d& Center, const FVector3d& Up, const FVector3d& Axis, double HalfLength, double HalfWidth,
	double HalfHeight, double Slope, double Blend)
{
	FEditOp Op;
	Op.Kind = EEditKind::Flatten;
	Op.Center = Center;
	const FVector3d U = Up.GetSafeNormal();
	const FVector3d A = (Axis - U * Axis.Dot(U)).GetSafeNormal();
	// The plane rises by Slope per metre along the axis.
	Op.Normal = (U - A * Slope).GetSafeNormal();
	Op.Axis = (A - Op.Normal * A.Dot(Op.Normal)).GetSafeNormal();
	Op.HalfLength = HalfLength;
	Op.HalfWidth = HalfWidth;
	Op.HalfHeight = HalfHeight;
	Op.Blend = FMath::Max(0.01, Blend);
	return Op;
}

FEditOp FEditOp::Carve(const FVector3d& Center, double Radius)
{
	FEditOp Op;
	Op.Kind = EEditKind::Carve;
	Op.Center = Center;
	Op.Radius = Radius;
	return Op;
}

FEditOp FEditOp::Fill(const FVector3d& Center, double Radius)
{
	FEditOp Op;
	Op.Kind = EEditKind::Fill;
	Op.Center = Center;
	Op.Radius = Radius;
	return Op;
}

double FEditOp::Weight(const FVector3d& P) const
{
	const FVector3d Q = P - Center;
	const double Axial = Q.Dot(Normal);
	const double RadialSq = FMath::Max(0.0, Q.SizeSquared() - Axial * Axial);
	const double W = FMath::Max(0.0, 1 - RadialSq / (Radius * Radius));
	const double T = FMath::Clamp((FMath::Abs(Axial) - Support * .5) / (Support * .5), 0.0, 1.0);
	// Quintic fade has zero first and second derivatives at both ends.
	const double Fade = 1 - T * T * T * (T * (T * 6 - 15) + 10);
	// A broad working core with a C2 edge, rather than a narrow pointed dent.
	return W * W * W * (W * (W * 6 - 15) + 10) * Fade;
}

double FEditOp::Apply(double D, const FVector3d& P) const
{
	switch (Kind)
	{
	case EEditKind::Sculpt:
	{
		const double W = Weight(P);
		return W > 0 ? D + Strength * W : D;
	}
	case EEditKind::Flatten:
	{
		const FVector3d Q = P - Center;
		const double H = Q.Dot(Normal);
		if (FMath::Abs(H) >= HalfHeight)
			return D;
		const FVector3d Side = Normal.Cross(Axis);
		const double A = FMath::Abs(Q.Dot(Axis)), S = FMath::Abs(Q.Dot(Side));
		// Narrow band: deep rock that stays rock and open air that stays air keep their values
		// (no mesh reads them), so grading stores only bricks near the old or new surface.
		if (FMath::Min(FMath::Abs(D), FMath::Abs(H)) > CsgBand + 1 && (D < 0) == (H < 0))
			return D;
		const double W = (1 - SmoothStep(HalfLength - Blend, HalfLength, A)) * (1 - SmoothStep(HalfWidth - Blend, HalfWidth, S))
			* (1 - SmoothStep(HalfHeight * 0.6, HalfHeight, FMath::Abs(H)));
		return W > 0 ? D + W * (H - D) : D;
	}
	case EEditKind::Carve:
	{
		const double B = FVector3d::Dist(P, Center) - Radius;
		return B > CsgBand ? D : FMath::Max(D, -B);
	}
	case EEditKind::Fill:
	{
		const double B = FVector3d::Dist(P, Center) - Radius;
		return B > CsgBand ? D : FMath::Min(D, B);
	}
	}
	return D;
}

FBox3d FEditOp::Bounds() const
{
	switch (Kind)
	{
	case EEditKind::Sculpt:
		return FBox3d(Center, Center).ExpandBy(FMath::Sqrt(Radius * Radius + Support * Support));
	case EEditKind::Flatten:
	{
		const FVector3d Side = Normal.Cross(Axis);
		const FVector3d Extent = (Axis * HalfLength).GetAbs() + (Side * HalfWidth).GetAbs() + (Normal * HalfHeight).GetAbs();
		return FBox3d(Center - Extent, Center + Extent);
	}
	default:
		return FBox3d(Center, Center).ExpandBy(Radius + CsgBand);
	}
}

void FEditOp::Serialize(FArchive& Ar)
{
	uint8 K = uint8(Kind);
	Ar << K;
	Kind = EEditKind(K);
	Ar << Center << Normal << Axis << Radius << Support << Strength << HalfLength << HalfWidth << HalfHeight << Blend;
}

bool FEditBrick::IsEmpty() const
{
	for (int32 I = 0; I < Count; I++)
		if (Delta[I] != 0.f || Material[I] != 0)
			return false;
	return true;
}

// ---- sampling ----

const FEditBrick* FTerrainEdits::FSampler::Brick(int32 I, int32 J, int32 K) const
{
	const FIntVector Key(I >> FEditBrick::Bits, J >> FEditBrick::Bits, K >> FEditBrick::Bits);
	if (Key != LastKey)
	{
		LastKey = Key;
		const TSharedRef<FEditBrick>* Found = Edits->Bricks.Find(Key);
		Last = Found != nullptr ? &Found->Get() : nullptr;
	}
	return Last;
}

float FTerrainEdits::FSampler::At(int32 I, int32 J, int32 K) const
{
	const FEditBrick* B = Brick(I, J, K);
	if (B == nullptr)
		return 0.f;
	constexpr int32 M = FEditBrick::Size - 1;
	return B->Delta[FEditBrick::Index(I & M, J & M, K & M)];
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

EEditMaterial FTerrainEdits::MaterialAt(const FVector3d& P) const
{
	if (Bricks.Num() == 0)
		return EEditMaterial::Natural;
	const int32 I = int32(FMath::RoundToDouble(P.X / Spacing)), J = int32(FMath::RoundToDouble(P.Y / Spacing)), K = int32(FMath::RoundToDouble(P.Z / Spacing));
	const FEditBrick* B = FSampler(this).Brick(I, J, K);
	constexpr int32 M = FEditBrick::Size - 1;
	return B != nullptr ? EEditMaterial(B->Material[FEditBrick::Index(I & M, J & M, K & M)]) : EEditMaterial::Natural;
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

// ---- editing ----

TSharedRef<const FTerrainEdits> FTerrainEdits::Apply(const FEditOp& Op, const FPlanetField& Field, FEditResult& Result) const
{
	return ApplyAll(MakeArrayView(&Op, 1), Field, Result);
}

TSharedRef<const FTerrainEdits> FTerrainEdits::ApplyAll(TConstArrayView<FEditOp> Ops, const FPlanetField& Field, FEditResult& Result) const
{
	TSharedRef<FTerrainEdits> Next = MakeShared<FTerrainEdits>(Spacing);
	Next->Bricks = Bricks;
	Next->Version = Version + 1;
	TSet<FIntVector> Owned;
	for (const FEditOp& Op : Ops)
		Next->ApplyInPlace(Op, Field, Result, Owned);
	if (Result.bChanged)
		Result.Changed = Result.Changed.ExpandBy(Spacing);
	return Next;
}

void FTerrainEdits::ApplyInPlace(const FEditOp& Op, const FPlanetField& Field, FEditResult& Result, TSet<FIntVector>& Owned)
{
	const double H = Spacing;
	const FBox3d Box = Op.Bounds().ExpandBy(H);
	const FIntVector Lo(int32(FMath::FloorToDouble(Box.Min.X / H)), int32(FMath::FloorToDouble(Box.Min.Y / H)), int32(FMath::FloorToDouble(Box.Min.Z / H)));
	const FIntVector Hi(int32(FMath::CeilToDouble(Box.Max.X / H)), int32(FMath::CeilToDouble(Box.Max.Y / H)), int32(FMath::CeilToDouble(Box.Max.Z / H)));

	// Phase 1 (serial): every brick the op's box reaches, made writable - fresh, or a copy of the
	// shared one (once per snapshot).
	struct FTarget
	{
		FIntVector Key;
		TSharedPtr<FEditBrick> Original; // the shared brick, kept if the op leaves it unchanged
		FEditBrick* Brick = nullptr;
		bool bFresh = false;
		// Results, merged afterwards.
		bool bChanged = false;
		double Volume[int32(EGeoMaterial::Count)] = {};
		FBox3d Changed = FBox3d(ForceInit);
	};
	TArray<FTarget> Targets;
	for (int32 Bx = Lo.X >> FEditBrick::Bits; Bx <= Hi.X >> FEditBrick::Bits; Bx++)
		for (int32 By = Lo.Y >> FEditBrick::Bits; By <= Hi.Y >> FEditBrick::Bits; By++)
			for (int32 Bz = Lo.Z >> FEditBrick::Bits; Bz <= Hi.Z >> FEditBrick::Bits; Bz++)
			{
				FTarget T;
				T.Key = FIntVector(Bx, By, Bz);
				TSharedRef<FEditBrick>* Slot = Bricks.Find(T.Key);
				if (Slot == nullptr)
				{
					TSharedRef<FEditBrick> Fresh = MakeShared<FEditBrick>();
					FMemory::Memzero(Fresh->Delta, sizeof(Fresh->Delta));
					FMemory::Memzero(Fresh->Material, sizeof(Fresh->Material));
					Slot = &Bricks.Add(T.Key, Fresh);
					Owned.Add(T.Key);
					T.bFresh = true;
				}
				else if (!Owned.Contains(T.Key))
				{
					T.Original = Slot->ToSharedPtr();
					*Slot = MakeShared<FEditBrick>(Slot->Get());
					Owned.Add(T.Key);
				}
				T.Brick = &Slot->Get();
				Targets.Add(MoveTemp(T));
			}

	// Phase 2 (parallel): each brick's points in the box. Each lattice point stands for a cell;
	// its rock is in proportion to how far inside the surface it is (within half a cell), which
	// is what the removed volume counts.
	const uint32 NewVersion = Version;
	const double Cell = H * H * H;
	ParallelFor(Targets.Num(), [&](int32 Index)
	{
		FTarget& T = Targets[Index];
		FEditBrick& B = *T.Brick;
		const FIntVector Base = T.Key * FEditBrick::Size;
		for (int32 Lx = 0; Lx < FEditBrick::Size; Lx++)
		{
			const int32 I = Base.X + Lx;
			if (I < Lo.X || I > Hi.X)
				continue;
			for (int32 Ly = 0; Ly < FEditBrick::Size; Ly++)
			{
				const int32 J = Base.Y + Ly;
				if (J < Lo.Y || J > Hi.Y)
					continue;
				for (int32 Lz = 0; Lz < FEditBrick::Size; Lz++)
				{
					const int32 K = Base.Z + Lz;
					if (K < Lo.Z || K > Hi.Z)
						continue;
					const FVector3d P(I * H, J * H, K * H);
					// Cheap rejection before the procedural field is evaluated.
					if (Op.Kind == EEditKind::Sculpt && Op.Weight(P) <= 0)
						continue;
					if ((Op.Kind == EEditKind::Carve || Op.Kind == EEditKind::Fill) && FVector3d::Dist(P, Op.Center) > Op.Radius + CsgBand)
						continue;
					const int32 At = FEditBrick::Index(Lx, Ly, Lz);
					const double Proc = Field.Density(P);
					const double Old = Proc + B.Delta[At];
					const float NewDelta = float(Op.Apply(Old, P) - Proc);
					if (NewDelta == B.Delta[At])
						continue;
					const double New = Proc + NewDelta;
					const double Before = FMath::Clamp(0.5 - Old / H, 0.0, 1.0);
					const double After = FMath::Clamp(0.5 - New / H, 0.0, 1.0);
					if (Before != After)
					{
						const EGeoMaterial M = B.Material[At] == uint8(EEditMaterial::Fill) || After > Before ? EGeoMaterial::Fill
							: Field.Geology(P, -Proc).Material;
						T.Volume[int32(M)] += (Before - After) * Cell;
					}
					// Ground that turns to rock by building is fill; ground cut open is what it was.
					if (New < 0 && Old >= 0)
						B.Material[At] = uint8(EEditMaterial::Fill);
					else if (New >= 0 && Old < 0)
						B.Material[At] = uint8(EEditMaterial::Natural);
					B.Delta[At] = NewDelta;
					T.bChanged = true;
					T.Changed += P;
				}
			}
		}
	});

	// Phase 3 (serial): merge, drop bricks that hold nothing, keep originals the op left alone.
	for (FTarget& T : Targets)
	{
		if (T.bChanged)
		{
			T.Brick->Version = NewVersion;
			for (int32 M = 0; M < int32(EGeoMaterial::Count); M++)
				Result.Volume[M] += T.Volume[M];
			Result.Changed += T.Changed;
			Result.bChanged = true;
		}
		if (T.bFresh && (!T.bChanged || T.Brick->IsEmpty()))
		{
			Bricks.Remove(T.Key);
			Owned.Remove(T.Key);
		}
		else if (!T.bChanged && T.Original.IsValid())
		{
			Bricks.Add(T.Key, T.Original.ToSharedRef());
			Owned.Remove(T.Key);
		}
	}
}

void FTerrainEdits::Serialize(FArchive& Ar)
{
	// Version 2: a channel mask per brick (1 density offset, 2 material). Version 1 bricks held
	// the offset only.
	enum : uint8 { ChannelDelta = 1, ChannelMaterial = 2 };
	int32 FileVersion = 2;
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
			FMemory::Memzero(Brick->Delta, sizeof(Brick->Delta));
			FMemory::Memzero(Brick->Material, sizeof(Brick->Material));
			Ar << Brick->Version;
			uint8 Channels = ChannelDelta;
			if (FileVersion >= 2)
				Ar << Channels;
			if (Channels & ChannelDelta)
				Ar.Serialize(Brick->Delta, sizeof(Brick->Delta));
			if (Channels & ChannelMaterial)
				Ar.Serialize(Brick->Material, sizeof(Brick->Material));
			Bricks.Add(Key, Brick);
		}
	}
	else
	{
		for (TPair<FIntVector, TSharedRef<FEditBrick>>& Pair : Bricks)
		{
			FEditBrick& B = Pair.Value.Get();
			uint8 Channels = ChannelDelta;
			for (int32 I = 0; I < FEditBrick::Count; I++)
				if (B.Material[I] != 0)
				{
					Channels |= ChannelMaterial;
					break;
				}
			Ar << Pair.Key;
			Ar << B.Version;
			Ar << Channels;
			Ar.Serialize(B.Delta, sizeof(B.Delta));
			if (Channels & ChannelMaterial)
				Ar.Serialize(B.Material, sizeof(B.Material));
		}
	}
}
