#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Terrain/FluidVolume.h"
#include "FluidActor.generated.h"

class APlanetTerrainActor;
class UProceduralMeshComponent;
class UMaterialInterface;
struct FEditResult;

/** A water source in a region (planet frame). */
struct FFluidSpring
{
	FVector3d Position = FVector3d::ZeroVector;
	/** Cubic metres per second. */
	double Rate = 0.5;
	/** Stops after this much (cubic metres); negative: never. */
	double Remaining = -1;
};

/**
 * Terrain-coupled water (milestone 0c spike): cellular regions (FFluidVolume) stepped at a fixed
 * rate, woken by terrain edits that reach them, meshed where their water changed. A settled region
 * is not stepped or meshed at all.
 */
UCLASS()
class STELLAR_API AFluidActor : public AActor
{
	GENERATED_BODY()

public:
	AFluidActor();

	void Setup(APlanetTerrainActor* InTerrain, UMaterialInterface* InMaterial);

	/** Adds a region over the current terrain; returns its index. */
	int32 AddRegion(const FFluidFrame& Frame);
	FFluidVolume& Region(int32 Index) { return *Regions[Index].Volume; }
	int32 NumRegions() const { return Regions.Num(); }
	/** The region containing P, or INDEX_NONE. */
	int32 RegionAt(const FVector3d& P) const;

	void AddSpring(int32 RegionIndex, const FFluidSpring& Spring);
	/** Toggle a spring at P: removes one within 2 m, otherwise adds one. Returns true if added. */
	bool ToggleSpringAt(const FVector3d& P, double Rate = 0.6);

	// ---- diagnostics ----
	int32 ActiveCells() const;
	double LastStepMs() const { return StepMs; }
	double CubicMetres() const;
	int64 Bytes() const;
	int32 NumSprings() const;

	void Serialize(FArchive& Ar);

	/** Simulated seconds per real second (development: the screenshot tour runs springs faster). */
	double TimeScale = 1;

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	struct FRegion
	{
		TUniquePtr<FFluidVolume> Volume;
		TArray<FFluidSpring> Springs;
		TMap<FIntVector, TObjectPtr<UProceduralMeshComponent>> Chunks;
		FIntVector DirtyMin = FIntVector(MAX_int32), DirtyMax = FIntVector(MIN_int32);
	};

	void OnTerrainEdited(const FEditResult& Result);
	void Remesh(FRegion& R);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;
	UPROPERTY(Transient)
	TObjectPtr<APlanetTerrainActor> Terrain;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> Material;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UProceduralMeshComponent>> AllChunks;

	TArray<FRegion> Regions;
	FDelegateHandle EditHandle;
	double Accumulator = 0;
	double MeshTimer = 0;
	double StepMs = 0;
};
