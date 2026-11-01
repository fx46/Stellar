#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Async/Future.h"
#include "Terrain/PlanetTerrain.h"
#include "PlanetTerrainActor.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;

/** One meshed node, ready for upload: Unreal units (cm), node-local. Built off the game thread. */
struct FNodeMeshData
{
	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	TArray<FColor> Colors;
	TArray<int32> Triangles;
};

/**
 * The planet in the world: streams the level-of-detail cut around the camera onto procedural
 * mesh components and applies terrain edits.
 *
 * Coordinates: terrain and gameplay math run in the planet frame (doubles, metres, centre at
 * the origin). The actor sits at the planet's centre, turned so the start site is at the top of
 * the world; Unreal's large world coordinates keep everything in doubles up to rendering, which
 * is camera-relative - the floating origin the Godot scale study needed comes for free.
 *
 * Meshing happens off the game thread: a job selects the octree cut for the camera, meshes only
 * the nodes that are new, whose coarser neighbours changed or that an edit reached, and the game
 * thread swaps the whole cut in one frame (no holes while it updates). Edits run off the game
 * thread too, one at a time; a finished edit triggers a remesh of the nodes it reached.
 */
UCLASS()
class STELLAR_API APlanetTerrainActor : public AActor
{
	GENERATED_BODY()

public:
	APlanetTerrainActor();

	/** Planet radius in metres (the development plan's home planet: 1000 km). */
	UPROPERTY(EditAnywhere, Category = "Planet")
	double RadiusMeters = 1000000.0;

	UPROPERTY(EditAnywhere, Category = "Planet")
	int32 Seed = 1337;

	/** Size of the finest octree nodes (metres); their cells are this / 32. */
	UPROPERTY(EditAnywhere, Category = "Planet")
	double LeafSizeMeters = 8.0;

	UPROPERTY(EditAnywhere, Category = "Planet")
	TObjectPtr<UMaterialInterface> TerrainMaterial;

	/** Stop updating the level of detail (debug). */
	UPROPERTY(EditAnywhere, Category = "Planet")
	bool bFreezeLod = false;

	/** Builds the terrain (field, octree, empty edits). Called once before use. */
	void Initialize();

	bool IsInitialized() const { return Terrain.IsValid(); }
	const FPlanetTerrain& GetTerrain() const { return *Terrain; }
	const FTerrainEdits* GetEdits() const { return &Edits.Get(); }
	TSharedRef<const FTerrainEdits> GetEditsSnapshot() const { return Edits; }

	/** Place the planet: its centre in the world and the rotation from planet axes to world axes. */
	void SetPlanetTransform(const FVector& CenterWorld, const FQuat& Rotation);

	FVector3d WorldToPlanet(const FVector& World) const;
	FVector PlanetToWorld(const FVector3d& Planet) const;
	FVector3d WorldDirToPlanet(const FVector& Dir) const;
	FVector PlanetDirToWorld(const FVector3d& Dir) const;

	/** Density at a planet-frame point, edits included (game thread). */
	double Density(const FVector3d& P) const { return Terrain->Density(P, &Edits.Get()); }
	FVector3d Normal(const FVector3d& P) const { return Terrain->Normal(P, &Edits.Get()); }

	/** Start an edit unless one is still being computed. OnDone runs on the game thread. */
	bool RequestEdit(const FSculptOp& Op, TFunction<void(const FEditResult&)> OnDone);
	bool IsEditPending() const { return EditJob.IsValid(); }

	/** Replace the edits (loading a save). Remeshes whatever they reach. */
	void SetEdits(TSharedRef<const FTerrainEdits> NewEdits);

	/** The first cut has been built (the ground under the player exists). */
	bool HasTerrain() const { return bHasCut; }

	// ---- diagnostics ----
	int32 NumNodes() const { return Built.Num(); }
	int64 NumTriangles() const { return Triangles; }
	double LastSelectMs() const { return SelectMs; }
	double LastMeshMs() const { return MeshMs; }
	int32 LastMeshedNodes() const { return MeshedNodes; }
	bool IsJobRunning() const { return Job.IsValid(); }

	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	struct FBuilt
	{
		int32 Mask = 0;
		uint32 EditVersion = 0;
		UProceduralMeshComponent* Component = nullptr;
		int32 Triangles = 0;
	};

	struct FJobResult
	{
		FVector3d Camera;
		uint32 EditVersion = 0;
		TArray<FNodeKey> Cut;
		TMap<FNodeKey, int32> Masks;
		TMap<FNodeKey, uint32> Versions;
		TMap<FNodeKey, TSharedPtr<FNodeMeshData>> Meshes;
		double SelectMs = 0, MeshMs = 0;
	};

	struct FEditJobResult
	{
		TSharedPtr<const FTerrainEdits> Edits;
		FEditResult Result;
	};

	static FJobResult RunJob(TSharedRef<FPlanetTerrain> Terrain, TSharedRef<const FTerrainEdits> Edits, FVector3d Camera,
		TMap<FNodeKey, TPair<int32, uint32>> Built);
	void Apply(FJobResult& Result);
	UProceduralMeshComponent* AcquireComponent();
	void ReleaseComponent(UProceduralMeshComponent* Component);
	bool CameraPlanet(FVector3d& Out) const;
	void WaitForJobs();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UProceduralMeshComponent>> Pool;

	TSharedPtr<FPlanetTerrain> Terrain;
	TSharedRef<const FTerrainEdits> Edits = MakeShared<FTerrainEdits>(0.25);

	TMap<FNodeKey, FBuilt> Built;
	TFuture<FJobResult> Job;
	TFuture<FEditJobResult> EditJob;
	TFunction<void(const FEditResult&)> EditDone;

	FVector3d AppliedFor = FVector3d(TNumericLimits<double>::Max());
	uint32 AppliedEditVersion = 0;
	bool bHasCut = false;
	int64 Triangles = 0;
	double SelectMs = 0, MeshMs = 0;
	int32 MeshedNodes = 0;
};
