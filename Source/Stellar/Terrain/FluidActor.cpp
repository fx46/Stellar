#include "Terrain/FluidActor.h"
#include "Terrain/PlanetTerrainActor.h"
#include "Terrain/TransvoxelMesher.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "HAL/PlatformTime.h"
#include "Serialization/Archive.h"

namespace
{
	constexpr double UU = 100.0;
	/** Simulation steps per second, and the most a frame may run (the rest waits). */
	constexpr double StepHz = 30.0;
	constexpr int32 MaxStepsPerFrame = 4;
	/** Cells per mesh chunk along an axis, and how often changed chunks are remeshed. */
	constexpr int32 ChunkCells = 32;
	constexpr double MeshInterval = 0.12;
}

AFluidActor::AFluidActor()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;
}

void AFluidActor::Setup(APlanetTerrainActor* InTerrain, UMaterialInterface* InMaterial)
{
	Terrain = InTerrain;
	Material = InMaterial;
	EditHandle = Terrain->OnEdited.AddUObject(this, &AFluidActor::OnTerrainEdited);
}

void AFluidActor::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Terrain != nullptr)
		Terrain->OnEdited.Remove(EditHandle);
	Super::EndPlay(Reason);
}

int32 AFluidActor::AddRegion(const FFluidFrame& Frame)
{
	FRegion R;
	R.Volume = MakeUnique<FFluidVolume>(Frame);
	R.Volume->RebuildSolid([this](const FVector3d& P) { return Terrain->Density(P); });
	return Regions.Add(MoveTemp(R));
}

int32 AFluidActor::RegionAt(const FVector3d& P) const
{
	for (int32 I = 0; I < Regions.Num(); I++)
	{
		const FFluidFrame& F = Regions[I].Volume->Frame();
		const FVector3d L = F.ToLocal(P);
		if (L.X >= 0 && L.Y >= 0 && L.Z >= 0 && L.X < F.Nx * F.CellSize && L.Y < F.Ny * F.CellSize && L.Z < F.Nz * F.CellSize)
			return I;
	}
	return INDEX_NONE;
}

void AFluidActor::AddSpring(int32 RegionIndex, const FFluidSpring& Spring)
{
	if (Regions.IsValidIndex(RegionIndex))
		Regions[RegionIndex].Springs.Add(Spring);
}

bool AFluidActor::ToggleSpringAt(const FVector3d& P, double Rate)
{
	const int32 Index = RegionAt(P);
	if (Index == INDEX_NONE)
		return false;
	TArray<FFluidSpring>& Springs = Regions[Index].Springs;
	const int32 Near = Springs.IndexOfByPredicate([&P](const FFluidSpring& S) { return FVector3d::Dist(S.Position, P) < 2.0; });
	if (Near != INDEX_NONE)
	{
		Springs.RemoveAt(Near);
		return false;
	}
	FFluidSpring S;
	S.Position = P;
	S.Rate = Rate;
	Springs.Add(S);
	return true;
}

int32 AFluidActor::ActiveCells() const
{
	int32 N = 0;
	for (const FRegion& R : Regions)
		N += R.Volume->NumActive();
	return N;
}

double AFluidActor::CubicMetres() const
{
	double V = 0;
	for (const FRegion& R : Regions)
		V += R.Volume->CubicMetres();
	return V;
}

int64 AFluidActor::Bytes() const
{
	int64 B = 0;
	for (const FRegion& R : Regions)
		B += R.Volume->Bytes();
	return B;
}

int32 AFluidActor::NumSprings() const
{
	int32 N = 0;
	for (const FRegion& R : Regions)
		N += R.Springs.Num();
	return N;
}

void AFluidActor::OnTerrainEdited(const FEditResult& Result)
{
	if (!Result.bChanged)
		return;
	for (FRegion& R : Regions)
		if (R.Volume->Frame().PlanetBounds().Intersect(Result.Changed))
			R.Volume->RebuildSolid([this](const FVector3d& P) { return Terrain->Density(P); }, &Result.Changed);
}

void AFluidActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Terrain == nullptr)
		return;
	const double Dt = 1.0 / StepHz;
	Accumulator = FMath::Min(Accumulator + DeltaSeconds * TimeScale, MaxStepsPerFrame * TimeScale * Dt);
	const double T0 = FPlatformTime::Seconds();
	bool bStepped = false;
	while (Accumulator >= Dt)
	{
		Accumulator -= Dt;
		bStepped = true;
		for (FRegion& R : Regions)
		{
			for (int32 I = R.Springs.Num() - 1; I >= 0; I--)
			{
				FFluidSpring& S = R.Springs[I];
				double Amount = S.Rate * Dt;
				if (S.Remaining >= 0)
				{
					Amount = FMath::Min(Amount, S.Remaining);
					S.Remaining -= Amount;
				}
				const double Cell = FMath::Pow(R.Volume->Frame().CellSize, 3);
				R.Volume->AddAt(S.Position, float(Amount / Cell));
				if (S.Remaining == 0)
					R.Springs.RemoveAt(I);
			}
			R.Volume->Step();
		}
	}
	if (bStepped)
		StepMs = (FPlatformTime::Seconds() - T0) * 1000.0;

	MeshTimer += DeltaSeconds;
	if (MeshTimer >= MeshInterval)
	{
		MeshTimer = 0;
		for (FRegion& R : Regions)
			Remesh(R);
	}
}

void AFluidActor::Remesh(FRegion& R)
{
	FIntVector Lo, Hi;
	if (R.Volume->TakeDirty(Lo, Hi))
	{
		R.DirtyMin = FIntVector(FMath::Min(R.DirtyMin.X, Lo.X), FMath::Min(R.DirtyMin.Y, Lo.Y), FMath::Min(R.DirtyMin.Z, Lo.Z));
		R.DirtyMax = FIntVector(FMath::Max(R.DirtyMax.X, Hi.X), FMath::Max(R.DirtyMax.Y, Hi.Y), FMath::Max(R.DirtyMax.Z, Hi.Z));
	}
	if (R.DirtyMin.X > R.DirtyMax.X)
		return;
	const FFluidFrame& F = R.Volume->Frame();
	const FQuat Frame = Terrain->GetActorQuat() * FQuat(FMatrix(FPlane(F.X, 0), FPlane(F.Y, 0), FPlane(F.Z, 0), FPlane(0, 0, 0, 1)));
	// A cell's change reaches the samples of the cells around it.
	const FIntVector CLo((R.DirtyMin.X - 1) / ChunkCells, (R.DirtyMin.Y - 1) / ChunkCells, (R.DirtyMin.Z - 1) / ChunkCells);
	const FIntVector CHi((R.DirtyMax.X + 1) / ChunkCells, (R.DirtyMax.Y + 1) / ChunkCells, (R.DirtyMax.Z + 1) / ChunkCells);
	R.DirtyMin = FIntVector(MAX_int32);
	R.DirtyMax = FIntVector(MIN_int32);
	for (int32 Cz = FMath::Max(0, CLo.Z); Cz <= FMath::Min(CHi.Z, (F.Nz - 1) / ChunkCells); Cz++)
		for (int32 Cy = FMath::Max(0, CLo.Y); Cy <= FMath::Min(CHi.Y, (F.Ny - 1) / ChunkCells); Cy++)
			for (int32 Cx = FMath::Max(0, CLo.X); Cx <= FMath::Min(CHi.X, (F.Nx - 1) / ChunkCells); Cx++)
			{
				const FIntVector Key(Cx, Cy, Cz);
				const FVector3d ChunkOrigin = FVector3d(Cx, Cy, Cz) * (ChunkCells * F.CellSize);
				auto Density = [&R, &ChunkOrigin](float X, float Y, float Z)
				{
					return 0.5f - R.Volume->SampleLocal(ChunkOrigin + FVector3d(X, Y, Z));
				};
				FTransvoxelMesh Mesh;
				TransvoxelMesher::MeshRegion(Mesh, Density, FVector3f::ZeroVector, float(F.CellSize), ChunkCells, 0, nullptr, nullptr);
				TObjectPtr<UProceduralMeshComponent>* Found = R.Chunks.Find(Key);
				UProceduralMeshComponent* C = Found != nullptr ? Found->Get() : nullptr;
				if (Mesh.IsEmpty())
				{
					if (C != nullptr)
						C->ClearAllMeshSections();
					continue;
				}
				if (C == nullptr)
				{
					C = NewObject<UProceduralMeshComponent>(this);
					C->SetupAttachment(Root);
					C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					C->SetCastShadow(false);
					C->RegisterComponent();
					C->SetWorldLocationAndRotation(Terrain->PlanetToWorld(F.FromLocal(ChunkOrigin)), Frame);
					R.Chunks.Add(Key, C);
					AllChunks.Add(C);
				}
				TArray<FVector> Vertices, Normals;
				Vertices.SetNumUninitialized(Mesh.Vertices.Num());
				Normals.SetNumUninitialized(Mesh.Normals.Num());
				for (int32 V = 0; V < Mesh.Vertices.Num(); V++)
				{
					Vertices[V] = FVector(Mesh.Vertices[V]) * UU;
					// The mesher's normals point out of the solid - here, out of the water.
					Normals[V] = FVector(Mesh.Normals[V]);
				}
				C->CreateMeshSection(0, Vertices, Mesh.Indices, Normals, TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>(), false);
				C->SetMaterial(0, Material);
			}
}

void AFluidActor::Serialize(FArchive& Ar)
{
	int32 Version = 1;
	Ar << Version;
	int32 Num = Regions.Num();
	Ar << Num;
	if (Ar.IsLoading())
	{
		for (TObjectPtr<UProceduralMeshComponent>& C : AllChunks)
			if (C != nullptr)
				C->DestroyComponent();
		AllChunks.Reset();
		Regions.Reset();
		for (int32 I = 0; I < Num && !Ar.IsError(); I++)
		{
			FRegion R;
			R.Volume = MakeUnique<FFluidVolume>();
			R.Volume->Serialize(Ar);
			R.Volume->RebuildSolid([this](const FVector3d& P) { return Terrain->Density(P); });
			int32 Springs = 0;
			Ar << Springs;
			for (int32 S = 0; S < Springs && !Ar.IsError(); S++)
			{
				FFluidSpring Spring;
				Ar << Spring.Position << Spring.Rate << Spring.Remaining;
				R.Springs.Add(Spring);
			}
			Regions.Add(MoveTemp(R));
		}
	}
	else
	{
		for (FRegion& R : Regions)
		{
			R.Volume->Serialize(Ar);
			int32 Springs = R.Springs.Num();
			Ar << Springs;
			for (FFluidSpring& S : R.Springs)
				Ar << S.Position << S.Rate << S.Remaining;
		}
	}
}
