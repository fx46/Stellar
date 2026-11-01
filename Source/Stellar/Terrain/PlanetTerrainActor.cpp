#include "Terrain/PlanetTerrainActor.h"
#include "Stellar.h"
#include "ProceduralMeshComponent.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"

namespace
{
	/** Unreal units per metre. */
	constexpr double UU = 100.0;
}

APlanetTerrainActor::APlanetTerrainActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("PlanetCentre"));
	RootComponent = Root;
}

void APlanetTerrainActor::Initialize()
{
	Terrain = MakeShared<FPlanetTerrain>(RadiusMeters, Seed, LeafSizeMeters);
	Edits = MakeShared<FTerrainEdits>(Terrain->LeafStride());
	if (TerrainMaterial == nullptr)
	{
		TerrainMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Terrain.M_Terrain"));
		if (TerrainMaterial == nullptr)
			TerrainMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
	}
	UE_LOG(LogStellar, Log, TEXT("Planet radius %.1f km, octree root level %d, leaf %.1f m (cells %.3f m)"),
		RadiusMeters / 1000.0, Terrain->Octree.RootLevel, LeafSizeMeters, Terrain->LeafStride());
}

void APlanetTerrainActor::SetPlanetTransform(const FVector& CenterWorld, const FQuat& Rotation)
{
	SetActorLocationAndRotation(CenterWorld, Rotation);
}

FVector3d APlanetTerrainActor::WorldToPlanet(const FVector& World) const
{
	return GetActorQuat().UnrotateVector(World - GetActorLocation()) / UU;
}

FVector APlanetTerrainActor::PlanetToWorld(const FVector3d& Planet) const
{
	return GetActorLocation() + GetActorQuat().RotateVector(Planet * UU);
}

FVector3d APlanetTerrainActor::WorldDirToPlanet(const FVector& Dir) const
{
	return GetActorQuat().UnrotateVector(Dir);
}

FVector APlanetTerrainActor::PlanetDirToWorld(const FVector3d& Dir) const
{
	return GetActorQuat().RotateVector(Dir);
}

bool APlanetTerrainActor::RequestEdit(const FEditOp& Op, TFunction<void(const FEditResult&)> OnDone)
{
	if (!Terrain.IsValid() || EditJob.IsValid())
		return false;
	TSharedRef<FPlanetTerrain> T = Terrain.ToSharedRef();
	TSharedRef<const FTerrainEdits> Current = Edits;
	EditDone = MoveTemp(OnDone);
	EditJob = Async(EAsyncExecution::ThreadPool, [T, Current, Op]()
	{
		FEditJobResult R;
		R.Edits = Current->Apply(Op, T->Field, R.Result);
		return R;
	});
	return true;
}

FEditResult APlanetTerrainActor::ApplyNow(TConstArrayView<FEditOp> Ops)
{
	WaitForJobs();
	FEditResult Result;
	Edits = Edits->ApplyAll(Ops, Terrain->Field, Result);
	OnEdited.Broadcast(Result);
	return Result;
}

void APlanetTerrainActor::SetEdits(TSharedRef<const FTerrainEdits> NewEdits)
{
	WaitForJobs();
	Edits = NewEdits;
	// Every built node an edit may have touched before or after must be remeshed: forget the
	// versions so the next job compares against the new snapshot.
	for (TPair<FNodeKey, FBuilt>& Pair : Built)
		Pair.Value.EditVersion = MAX_uint32;
	AppliedFor = FVector3d(TNumericLimits<double>::Max());
	AppliedEditVersion = MAX_uint32;
}

bool APlanetTerrainActor::CameraPlanet(FVector3d& Out) const
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (PC == nullptr || PC->PlayerCameraManager == nullptr)
		return false;
	Out = WorldToPlanet(PC->PlayerCameraManager->GetCameraLocation());
	return true;
}

void APlanetTerrainActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Terrain.IsValid())
		return;

	if (EditJob.IsValid() && EditJob.IsReady())
	{
		FEditJobResult R = EditJob.Get();
		EditJob.Reset();
		Edits = R.Edits.ToSharedRef();
		if (EditDone)
		{
			TFunction<void(const FEditResult&)> Done = MoveTemp(EditDone);
			EditDone.Reset();
			Done(R.Result);
		}
		OnEdited.Broadcast(R.Result);
	}

	if (Job.IsValid())
	{
		if (!Job.IsReady())
			return;
		FJobResult Result = Job.Get();
		Job.Reset();
		Apply(Result);
	}

	FVector3d Cam;
	if (!CameraPlanet(Cam))
		return;
	const bool bEditsChanged = Edits->GetVersion() != AppliedEditVersion;
	if (!bEditsChanged && (bFreezeLod || FVector3d::Dist(Cam, AppliedFor) < 3.0))
		return;
	if (bFreezeLod && bHasCut)
		Cam = AppliedFor;

	TMap<FNodeKey, TPair<int32, uint32>> State;
	State.Reserve(Built.Num());
	for (const TPair<FNodeKey, FBuilt>& Pair : Built)
		State.Add(Pair.Key, TPair<int32, uint32>(Pair.Value.Mask, Pair.Value.EditVersion));
	TSharedRef<FPlanetTerrain> T = Terrain.ToSharedRef();
	TSharedRef<const FTerrainEdits> E = Edits;
	Job = Async(EAsyncExecution::ThreadPool, [T, E, Cam, State = MoveTemp(State)]() mutable
	{
		return RunJob(T, E, Cam, MoveTemp(State));
	});
}

APlanetTerrainActor::FJobResult APlanetTerrainActor::RunJob(TSharedRef<FPlanetTerrain> Terrain, TSharedRef<const FTerrainEdits> Edits,
	FVector3d Camera, TMap<FNodeKey, TPair<int32, uint32>> Built)
{
	FJobResult R;
	R.Camera = Camera;
	R.EditVersion = Edits->GetVersion();
	double T0 = FPlatformTime::Seconds();
	R.Cut = Terrain->Select(Camera, &Edits.Get());
	TSet<FNodeKey> Set(R.Cut);
	TArray<FNodeKey> Todo;
	for (const FNodeKey& K : R.Cut)
	{
		const int32 Mask = Terrain->Octree.CoarserFaces(K, Set);
		const uint32 Version = Edits->IsEmpty() ? 0 : Edits->VersionIn(Terrain->ReadBox(K));
		R.Masks.Add(K, Mask);
		R.Versions.Add(K, Version);
		const TPair<int32, uint32>* Had = Built.Find(K);
		if (Had == nullptr || Had->Key != Mask || Had->Value != Version)
			Todo.Add(K);
	}
	const double T1 = FPlatformTime::Seconds();
	R.SelectMs = (T1 - T0) * 1000.0;

	TArray<TSharedPtr<FNodeMeshData>> Meshes;
	Meshes.SetNum(Todo.Num());
	ParallelFor(Todo.Num(), [&](int32 I)
	{
		const FNodeKey& K = Todo[I];
		FTransvoxelMesh Mesh;
		Terrain->Mesh(K, R.Masks[K], &Edits.Get(), Mesh);
		if (Mesh.IsEmpty())
			return;
		TSharedPtr<FNodeMeshData> Data = MakeShared<FNodeMeshData>();
		const int32 Count = Mesh.Vertices.Num();
		Data->Vertices.SetNumUninitialized(Count);
		Data->Normals.SetNumUninitialized(Count);
		Data->UV0.SetNumUninitialized(Count);
		Data->UV1.SetNumUninitialized(Count);
		const FVector3d Min = Terrain->Octree.Min(K);
		auto Wrap = [](double V) { const double W = FMath::Fmod(V, 4096.0); return W < 0 ? W + 4096.0 : W; };
		for (int32 V = 0; V < Count; V++)
		{
			Data->Vertices[V] = FVector(Mesh.Vertices[V]) * UU;
			Data->Normals[V] = FVector(Mesh.Normals[V]);
			const FVector3d P = Min + FVector3d(Mesh.Vertices[V]);
			Data->UV0[V] = FVector2D(Wrap(P.X), Wrap(P.Y));
			Data->UV1[V] = FVector2D(Wrap(P.Z), 0.0);
		}
		Data->Colors = MoveTemp(Mesh.Colors);
		Data->Triangles = MoveTemp(Mesh.Indices);
		Meshes[I] = Data;
	});
	for (int32 I = 0; I < Todo.Num(); I++)
		R.Meshes.Add(Todo[I], Meshes[I]);
	R.MeshMs = (FPlatformTime::Seconds() - T1) * 1000.0;
	return R;
}

void APlanetTerrainActor::Apply(FJobResult& Result)
{
	TSet<FNodeKey> InCut(Result.Cut);
	for (TPair<FNodeKey, TSharedPtr<FNodeMeshData>>& Pair : Result.Meshes)
	{
		const FNodeKey& K = Pair.Key;
		if (FBuilt* Old = Built.Find(K))
		{
			if (Old->Component != nullptr)
			{
				Triangles -= Old->Triangles;
				ReleaseComponent(Old->Component);
			}
		}
		FBuilt B;
		B.Mask = Result.Masks[K];
		B.EditVersion = Result.Versions[K];
		if (Pair.Value.IsValid())
		{
			FNodeMeshData& Data = *Pair.Value;
			UProceduralMeshComponent* C = AcquireComponent();
			C->SetRelativeLocation(Terrain->Octree.Min(K) * UU);
			C->CreateMeshSection(0, Data.Vertices, Data.Triangles, Data.Normals, Data.UV0, Data.UV1, TArray<FVector2D>(),
				TArray<FVector2D>(), Data.Colors, TArray<FProcMeshTangent>(), false);
			C->SetMaterial(0, TerrainMaterial);
			C->SetVisibility(true);
			B.Component = C;
			B.Triangles = Data.Triangles.Num() / 3;
			Triangles += B.Triangles;
		}
		Built.Add(K, B);
	}
	// Nodes whose mask and edits did not change keep their mesh; record the version anyway.
	for (const TPair<FNodeKey, uint32>& Pair : Result.Versions)
		if (FBuilt* B = Built.Find(Pair.Key))
			B->EditVersion = Pair.Value;

	TArray<FNodeKey> Gone;
	for (const TPair<FNodeKey, FBuilt>& Pair : Built)
		if (!InCut.Contains(Pair.Key))
			Gone.Add(Pair.Key);
	for (const FNodeKey& K : Gone)
	{
		const FBuilt& B = Built[K];
		if (B.Component != nullptr)
		{
			Triangles -= B.Triangles;
			ReleaseComponent(B.Component);
		}
		Built.Remove(K);
	}
	AppliedFor = Result.Camera;
	SelectMs = Result.SelectMs;
	MeshMs = Result.MeshMs;
	MeshedNodes = Result.Meshes.Num();
	bHasCut = true;
	// The job compared against the snapshot it was given; a newer one triggers another job.
	AppliedEditVersion = Result.EditVersion;
}

UProceduralMeshComponent* APlanetTerrainActor::AcquireComponent()
{
	if (Pool.Num() > 0)
		return Pool.Pop();
	UProceduralMeshComponent* C = NewObject<UProceduralMeshComponent>(this, NAME_None, RF_Transient);
	C->SetupAttachment(Root);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCollisionResponseToAllChannels(ECR_Ignore);
	C->bUseAsyncCooking = true;
	C->SetCastShadow(true);
	C->RegisterComponent();
	return C;
}

void APlanetTerrainActor::ReleaseComponent(UProceduralMeshComponent* Component)
{
	Component->ClearAllMeshSections();
	Component->SetVisibility(false);
	Pool.Add(Component);
}

void APlanetTerrainActor::WaitForJobs()
{
	if (Job.IsValid())
	{
		Job.Wait();
		FJobResult Result = Job.Get();
		Job.Reset();
		Apply(Result);
	}
	if (EditJob.IsValid())
	{
		EditJob.Wait();
		EditJob.Reset();
		EditDone.Reset();
	}
}

void APlanetTerrainActor::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Job.IsValid())
		Job.Wait();
	if (EditJob.IsValid())
		EditJob.Wait();
	Super::EndPlay(Reason);
}
