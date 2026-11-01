#include "Terrain/FluidVolume.h"
#include "HAL/PlatformTime.h"
#include "Serialization/Archive.h"

namespace
{
	/** Sideways and vertical moves smaller than these are not made: what lets a region settle
	 * exactly instead of trading ever smaller amounts forever. */
	constexpr float LateralEpsilon = 1e-4f;
	constexpr float VerticalEpsilon = 1e-5f;
}

// ---- frame ----

FFluidFrame FFluidFrame::Around(const FVector3d& Centre, const FVector3d& Up, const FVector3d& Forward, const FVector3d& Size, double Cell)
{
	FFluidFrame F;
	F.Z = Up.GetSafeNormal();
	F.X = (Forward - F.Z * Forward.Dot(F.Z)).GetSafeNormal();
	F.Y = F.Z.Cross(F.X);
	F.CellSize = Cell;
	F.Nx = FMath::Max(1, int32(FMath::CeilToDouble(Size.X / Cell)));
	F.Ny = FMath::Max(1, int32(FMath::CeilToDouble(Size.Y / Cell)));
	F.Nz = FMath::Max(1, int32(FMath::CeilToDouble(Size.Z / Cell)));
	F.Origin = Centre - F.X * (F.Nx * Cell * 0.5) - F.Y * (F.Ny * Cell * 0.5) - F.Z * (F.Nz * Cell * 0.5);
	return F;
}

FBox3d FFluidFrame::PlanetBounds() const
{
	FBox3d B(ForceInit);
	for (int32 C = 0; C < 8; C++)
		B += FromLocal(FVector3d((C & 1) * Nx * CellSize, ((C >> 1) & 1) * Ny * CellSize, ((C >> 2) & 1) * Nz * CellSize));
	return B;
}

void FFluidFrame::Serialize(FArchive& Ar)
{
	Ar << Origin << X << Y << Z << CellSize << Nx << Ny << Nz;
}

// ---- cellular volume ----

FFluidVolume::FFluidVolume(const FFluidFrame& InFrame) : F(InFrame)
{
	Water.SetNumZeroed(F.Num());
	Next.SetNumZeroed(F.Num());
	Solid.SetNumZeroed(F.Num());
	InActive.SetNumZeroed(F.Num());
	InTouched.SetNumZeroed(F.Num());
}

void FFluidVolume::Wake(int32 I, int32 J, int32 K)
{
	if (I < 0 || J < 0 || K < 0 || I >= F.Nx || J >= F.Ny || K >= F.Nz)
		return;
	const int32 Idx = F.Index(I, J, K);
	if (!InActive[Idx])
	{
		InActive[Idx] = 1;
		Active.Add(Idx);
	}
}

void FFluidVolume::WakeAround(int32 I, int32 J, int32 K)
{
	Wake(I, J, K);
	Wake(I - 1, J, K);
	Wake(I + 1, J, K);
	Wake(I, J - 1, K);
	Wake(I, J + 1, K);
	Wake(I, J, K - 1);
	Wake(I, J, K + 1);
}

void FFluidVolume::MarkDirty(int32 I, int32 J, int32 K)
{
	DirtyMin = FIntVector(FMath::Min(DirtyMin.X, I), FMath::Min(DirtyMin.Y, J), FMath::Min(DirtyMin.Z, K));
	DirtyMax = FIntVector(FMath::Max(DirtyMax.X, I), FMath::Max(DirtyMax.Y, J), FMath::Max(DirtyMax.Z, K));
}

bool FFluidVolume::TakeDirty(FIntVector& OutMin, FIntVector& OutMax)
{
	if (DirtyMin.X > DirtyMax.X)
		return false;
	OutMin = DirtyMin;
	OutMax = DirtyMax;
	DirtyMin = FIntVector(MAX_int32);
	DirtyMax = FIntVector(MIN_int32);
	return true;
}

void FFluidVolume::RebuildSolid(FSolidSampler Density, const FBox3d* PlanetBox)
{
	FIntVector Lo(0, 0, 0), Hi(F.Nx - 1, F.Ny - 1, F.Nz - 1);
	if (PlanetBox != nullptr)
	{
		// The box's corners in region cells (the frame is rotated: take every corner).
		FBox3d L(ForceInit);
		for (int32 C = 0; C < 8; C++)
			L += F.ToLocal(FVector3d(C & 1 ? PlanetBox->Max.X : PlanetBox->Min.X, C & 2 ? PlanetBox->Max.Y : PlanetBox->Min.Y,
				C & 4 ? PlanetBox->Max.Z : PlanetBox->Min.Z));
		Lo = FIntVector(FMath::Max(0, int32(L.Min.X / F.CellSize) - 1), FMath::Max(0, int32(L.Min.Y / F.CellSize) - 1), FMath::Max(0, int32(L.Min.Z / F.CellSize) - 1));
		Hi = FIntVector(FMath::Min(F.Nx - 1, int32(L.Max.X / F.CellSize) + 1), FMath::Min(F.Ny - 1, int32(L.Max.Y / F.CellSize) + 1),
			FMath::Min(F.Nz - 1, int32(L.Max.Z / F.CellSize) + 1));
		if (Lo.X > Hi.X || Lo.Y > Hi.Y || Lo.Z > Hi.Z)
			return;
	}
	for (int32 K = Lo.Z; K <= Hi.Z; K++)
		for (int32 J = Lo.Y; J <= Hi.Y; J++)
			for (int32 I = Lo.X; I <= Hi.X; I++)
			{
				const int32 Idx = F.Index(I, J, K);
				const uint8 Now = Density(F.CellCentre(I, J, K)) < 0 ? 1 : 0;
				if (Now == Solid[Idx])
					continue;
				Solid[Idx] = Now;
				MarkDirty(I, J, K);
				WakeAround(I, J, K);
				if (Now && Water[Idx] > 0)
				{
					// Displaced by rock: up to the first open cell (conserved).
					for (int32 Up = K + 1; Up < F.Nz; Up++)
					{
						const int32 U = F.Index(I, J, Up);
						if (!Solid[U])
						{
							Water[U] += Water[Idx];
							Water[Idx] = 0;
							WakeAround(I, J, Up);
							MarkDirty(I, J, Up);
							break;
						}
					}
				}
			}
}

float FFluidVolume::AddAt(const FVector3d& P, float Amount)
{
	const FVector3d L = F.ToLocal(P);
	const int32 I = int32(L.X / F.CellSize), J = int32(L.Y / F.CellSize);
	int32 K = int32(L.Z / F.CellSize);
	if (I < 0 || J < 0 || I >= F.Nx || J >= F.Ny || K < 0 || K >= F.Nz)
		return 0;
	while (K < F.Nz && Solid[F.Index(I, J, K)])
		K++;
	if (K >= F.Nz)
		return 0;
	Water[F.Index(I, J, K)] += Amount;
	WakeAround(I, J, K);
	MarkDirty(I, J, K);
	return Amount;
}

void FFluidVolume::FillSphere(const FVector3d& Centre, double Radius)
{
	const FVector3d C = F.ToLocal(Centre);
	const int32 R = int32(Radius / F.CellSize) + 1;
	const int32 Ci = int32(C.X / F.CellSize), Cj = int32(C.Y / F.CellSize), Ck = int32(C.Z / F.CellSize);
	for (int32 K = FMath::Max(0, Ck - R); K <= FMath::Min(F.Nz - 1, Ck + R); K++)
		for (int32 J = FMath::Max(0, Cj - R); J <= FMath::Min(F.Ny - 1, Cj + R); J++)
			for (int32 I = FMath::Max(0, Ci - R); I <= FMath::Min(F.Nx - 1, Ci + R); I++)
			{
				const int32 Idx = F.Index(I, J, K);
				if (Solid[Idx] || FVector3d::Dist(F.CellCentre(I, J, K), Centre) > Radius)
					continue;
				Water[Idx] = MaxMass;
				WakeAround(I, J, K);
				MarkDirty(I, J, K);
			}
}

FFluidStats FFluidVolume::Step()
{
	FFluidStats S;
	const double T0 = FPlatformTime::Seconds();
	S.ActiveCells = Active.Num();
	if (Active.Num() == 0)
		return S;

	TArray<int32> Current = MoveTemp(Active);
	Active.Reset();
	for (int32 Idx : Current)
		InActive[Idx] = 0;
	Touched.Reset();
	auto Touch = [this](int32 Idx)
	{
		if (!InTouched[Idx])
		{
			InTouched[Idx] = 1;
			Touched.Add(Idx);
			Next[Idx] = Water[Idx];
		}
	};
	auto Move = [this, &Touch](int32 From, int32 To, float Flow)
	{
		Touch(From);
		Touch(To);
		Next[From] -= Flow;
		Next[To] += Flow;
	};

	const int32 Plane = F.Nx * F.Ny;

	// Pass 1, vertical: each stacked pair of open cells is settled once, by its lower cell - the
	// lower fills from the upper; a lower cell over full (a spring's cell) overflows upward. One
	// update per pair: updating it from both cells would trade the same amount up and down forever.
	PairDone.SetNumZeroed(F.Num(), EAllowShrinking::No);
	TArray<int32> Pairs;
	auto Pair = [&](int32 Lower)
	{
		const int32 Upper = Lower + Plane;
		if (Lower < 0 || Upper >= F.Num() || PairDone[Lower] || Solid[Lower] || Solid[Upper])
			return;
		PairDone[Lower] = 1;
		Pairs.Add(Lower);
		const float WL = Water[Lower], WU = Water[Upper];
		const float Flow = WL > MaxMass ? MaxMass - WL : FMath::Min(WU, MaxMass - WL); // positive: down
		if (FMath::Abs(Flow) > VerticalEpsilon)
			Move(Upper, Lower, Flow);
	};
	for (int32 Idx : Current)
	{
		Pair(Idx);
		Pair(Idx - Plane);
	}
	for (int32 Lower : Pairs)
		PairDone[Lower] = 0;

	// Pass 2, sideways: water that rests on something (rock, the region's floor, or a full cell)
	// levels out with its four neighbours; the fuller cell of each pair gives a tenth of the
	// difference - small enough that no exchange can overshoot (four tenths at most leave a cell,
	// so a checkerboard decays instead of flipping).
	for (int32 Idx : Current)
	{
		if (Solid[Idx] || Water[Idx] <= MinMass)
			continue;
		const int32 K = Idx / Plane, J = (Idx / F.Nx) % F.Ny, I = Idx % F.Nx;
		const bool bSupported = K == 0 || Solid[Idx - Plane] || Water[Idx - Plane] >= MaxMass - 1e-3f;
		if (!bSupported)
			continue;
		float Remaining = InTouched[Idx] ? Next[Idx] : Water[Idx];
		const int32 Neighbours[4] = { I > 0 ? Idx - 1 : -1, I < F.Nx - 1 ? Idx + 1 : -1, J > 0 ? Idx - F.Nx : -1, J < F.Ny - 1 ? Idx + F.Nx : -1 };
		for (int32 N : Neighbours)
		{
			if (N < 0 || Solid[N] || Remaining <= MinMass)
				continue;
			const float Flow = FMath::Clamp((Water[Idx] - Water[N]) * 0.1f, 0.f, Remaining);
			if (Flow > LateralEpsilon)
			{
				Move(Idx, N, Flow);
				Remaining -= Flow;
			}
		}
	}

	// Commit: what changed wakes itself and its neighbours for the next step.
	for (int32 Idx : Touched)
	{
		InTouched[Idx] = 0;
		if (Next[Idx] == Water[Idx])
			continue;
		Water[Idx] = Next[Idx];
		const int32 K = Idx / Plane, J = (Idx / F.Nx) % F.Ny, I = Idx % F.Nx;
		WakeAround(I, J, K);
		MarkDirty(I, J, K);
	}
	S.Steps = 1;
	S.StepMs = (FPlatformTime::Seconds() - T0) * 1000.0;
	S.bSettled = Active.Num() == 0;
	return S;
}

double FFluidVolume::TotalMass() const
{
	double Sum = 0;
	for (float W : Water)
		Sum += W;
	return Sum;
}

double FFluidVolume::CubicMetresIn(const FVector3d& Centre, double Radius) const
{
	double Sum = 0;
	for (int32 K = 0; K < F.Nz; K++)
		for (int32 J = 0; J < F.Ny; J++)
			for (int32 I = 0; I < F.Nx; I++)
				if (FVector3d::DistSquared(F.CellCentre(I, J, K), Centre) <= Radius * Radius)
					Sum += Water[F.Index(I, J, K)];
	return Sum * F.CellSize * F.CellSize * F.CellSize;
}

float FFluidVolume::SampleLocal(const FVector3d& L) const
{
	const double Gx = L.X / F.CellSize - 0.5, Gy = L.Y / F.CellSize - 0.5, Gz = L.Z / F.CellSize - 0.5;
	const int32 I0 = int32(FMath::FloorToDouble(Gx)), J0 = int32(FMath::FloorToDouble(Gy)), K0 = int32(FMath::FloorToDouble(Gz));
	const float Tx = float(Gx - I0), Ty = float(Gy - J0), Tz = float(Gz - K0);
	auto Cell = [this](int32 I, int32 J, int32 K) -> float
	{
		if (I < 0 || J < 0 || K < 0 || I >= F.Nx || J >= F.Ny || K >= F.Nz)
			return 0.f;
		const int32 Idx = F.Index(I, J, K);
		if (!Solid[Idx])
			return FMath::Min(Water[Idx], 1.f);
		// Rock reads as the water beside or above it, so a pond's surface runs on into its banks
		// (hidden by the terrain) instead of closing off along every wet wall.
		float M = 0.f;
		const int32 Ns[5][3] = { { I - 1, J, K }, { I + 1, J, K }, { I, J - 1, K }, { I, J + 1, K }, { I, J, K + 1 } };
		for (const auto& N : Ns)
			if (N[0] >= 0 && N[1] >= 0 && N[2] >= 0 && N[0] < F.Nx && N[1] < F.Ny && N[2] < F.Nz && !Solid[F.Index(N[0], N[1], N[2])])
				M = FMath::Max(M, FMath::Min(Water[F.Index(N[0], N[1], N[2])], 1.f));
		return M;
	};
	const float C000 = Cell(I0, J0, K0), C100 = Cell(I0 + 1, J0, K0), C010 = Cell(I0, J0 + 1, K0), C110 = Cell(I0 + 1, J0 + 1, K0);
	const float C001 = Cell(I0, J0, K0 + 1), C101 = Cell(I0 + 1, J0, K0 + 1), C011 = Cell(I0, J0 + 1, K0 + 1), C111 = Cell(I0 + 1, J0 + 1, K0 + 1);
	const float X00 = FMath::Lerp(C000, C100, Tx), X10 = FMath::Lerp(C010, C110, Tx), X01 = FMath::Lerp(C001, C101, Tx), X11 = FMath::Lerp(C011, C111, Tx);
	return FMath::Lerp(FMath::Lerp(X00, X10, Ty), FMath::Lerp(X01, X11, Ty), Tz);
}

void FFluidVolume::Serialize(FArchive& Ar)
{
	// Sparse: the frame, then (index, fill) for every wet cell. Rock is not saved: it is rebuilt
	// from the (saved) terrain on load.
	int32 Version = 1;
	Ar << Version;
	F.Serialize(Ar);
	if (Ar.IsLoading())
	{
		*this = FFluidVolume(F);
		int32 Count = 0;
		Ar << Count;
		for (int32 N = 0; N < Count && !Ar.IsError(); N++)
		{
			int32 Idx = 0;
			float W = 0;
			Ar << Idx << W;
			if (Idx >= 0 && Idx < Water.Num() && FMath::IsFinite(W))
			{
				Water[Idx] = W;
				const int32 Plane = F.Nx * F.Ny;
				Wake(Idx % F.Nx, (Idx / F.Nx) % F.Ny, Idx / Plane);
				MarkDirty(Idx % F.Nx, (Idx / F.Nx) % F.Ny, Idx / Plane);
			}
		}
	}
	else
	{
		int32 Count = 0;
		for (float W : Water)
			Count += W > 0 ? 1 : 0;
		Ar << Count;
		for (int32 Idx = 0; Idx < Water.Num(); Idx++)
			if (Water[Idx] > 0)
				Ar << Idx << Water[Idx];
	}
}

// ---- heightfield (virtual pipes) ----

FFluidHeightfield::FFluidHeightfield(const FFluidFrame& InFrame) : F(InFrame)
{
	const int32 N = F.Nx * F.Ny;
	G.SetNumZeroed(N);
	D.SetNumZeroed(N);
	FluxR.SetNumZeroed(N);
	FluxL.SetNumZeroed(N);
	FluxU.SetNumZeroed(N);
	FluxDn.SetNumZeroed(N);
}

void FFluidHeightfield::RebuildGround(FSolidSampler Density)
{
	// One height per column: the first rock below the region's top. Whatever lies under it -
	// a cave, a sealed pocket - does not exist for this representation.
	for (int32 J = 0; J < F.Ny; J++)
		for (int32 I = 0; I < F.Nx; I++)
		{
			double Ground = 0;
			for (int32 K = F.Nz - 1; K >= 0; K--)
				if (Density(F.CellCentre(I, J, K)) < 0)
				{
					Ground = (K + 1) * F.CellSize;
					break;
				}
			G[J * F.Nx + I] = Ground;
		}
}

void FFluidHeightfield::AddAt(const FVector3d& P, double CubicMetres)
{
	const FVector3d L = F.ToLocal(P);
	const int32 I = int32(L.X / F.CellSize), J = int32(L.Y / F.CellSize);
	if (I >= 0 && J >= 0 && I < F.Nx && J < F.Ny)
		D[J * F.Nx + I] += CubicMetres / (F.CellSize * F.CellSize);
}

double FFluidHeightfield::Step(double Dt)
{
	const double Gravity = 9.81, L = F.CellSize, Area = L * L, Pipe = L * L;
	const int32 Nx = F.Nx, Ny = F.Ny;
	auto H = [&](int32 I) { return G[I] + D[I]; };
	for (int32 J = 0; J < Ny; J++)
		for (int32 I = 0; I < Nx; I++)
		{
			const int32 C = J * Nx + I;
			const double Hc = H(C);
			auto Flux = [&](double Old, int32 N) { return N < 0 ? 0.0 : FMath::Max(0.0, Old * 0.995 + Dt * Pipe * Gravity * (Hc - H(N)) / L); };
			FluxR[C] = Flux(FluxR[C], I < Nx - 1 ? C + 1 : -1);
			FluxL[C] = Flux(FluxL[C], I > 0 ? C - 1 : -1);
			FluxU[C] = Flux(FluxU[C], J < Ny - 1 ? C + Nx : -1);
			FluxDn[C] = Flux(FluxDn[C], J > 0 ? C - Nx : -1);
			// Never send more than the column holds.
			const double Out = (FluxR[C] + FluxL[C] + FluxU[C] + FluxDn[C]) * Dt;
			if (Out > D[C] * Area && Out > 0)
			{
				const double K = D[C] * Area / Out;
				FluxR[C] *= K; FluxL[C] *= K; FluxU[C] *= K; FluxDn[C] *= K;
			}
		}
	double Largest = 0;
	for (int32 J = 0; J < Ny; J++)
		for (int32 I = 0; I < Nx; I++)
		{
			const int32 C = J * Nx + I;
			const double In = (I > 0 ? FluxR[C - 1] : 0) + (I < Nx - 1 ? FluxL[C + 1] : 0) + (J > 0 ? FluxU[C - Nx] : 0) + (J < Ny - 1 ? FluxDn[C + Nx] : 0);
			const double Out = FluxR[C] + FluxL[C] + FluxU[C] + FluxDn[C];
			const double Change = Dt * (In - Out) / Area;
			D[C] = FMath::Max(0.0, D[C] + Change);
			Largest = FMath::Max(Largest, FMath::Abs(Change));
		}
	return Largest;
}

double FFluidHeightfield::CubicMetres() const
{
	double Sum = 0;
	for (double V : D)
		Sum += V;
	return Sum * F.CellSize * F.CellSize;
}
