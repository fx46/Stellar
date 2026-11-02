#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RockScatter.generated.h"

class APlanetTerrainActor;
class UProceduralMeshComponent;
class UMaterialInterface;
struct FEditResult;

/**
 * Stones, rocks and boulders scattered on the ground around the camera (the moodboard's rubble,
 * images 9, 10 and 14): planet-frame tiles of 24 m within reach of the camera, each filled from a
 * hash of its key, so the same ground always has the same stones. A few lumpy rock shapes are
 * shared; each placement is turned, squashed and sunk into the ground, coloured from the terrain
 * around it, and drawn with the terrain material. Decoration only: the astronaut does not collide
 * with them. Kept off edited ground (cuts, the test site) and steep slopes; tiles an edit reaches
 * are placed again.
 */
UCLASS()
class STELLAR_API ARockScatter : public AActor
{
	GENERATED_BODY()

public:
	ARockScatter();

	void Setup(APlanetTerrainActor* InTerrain, UMaterialInterface* InMaterial);

	/** Tile size and how far from the camera tiles are kept (metres). */
	static constexpr double TileSize = 24.0;
	static constexpr double Reach = 150.0;

	/** Keep rocks out of a circle (planet frame): the start site, machines, buildings. */
	void AddClearing(const FVector3d& Centre, double Radius) { Clearings.Add(TPair<FVector3d, double>(Centre, Radius)); }

	int32 NumTiles() const { return Tiles.Num(); }
	int32 NumRocks() const { return Rocks; }

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	struct FShape
	{
		TArray<FVector3f> Vertices;
		TArray<FVector3f> Normals;
		TArray<int32> Indices;
	};

	void OnTerrainEdited(const FEditResult& Result);
	void BuildTile(const FIntVector& Key, UProceduralMeshComponent* C, int32& OutRocks);
	UProceduralMeshComponent* Acquire();

	UPROPERTY(Transient)
	TObjectPtr<APlanetTerrainActor> Terrain;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> Material;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UProceduralMeshComponent>> Pool;

	struct FTile
	{
		TObjectPtr<UProceduralMeshComponent> Component;
		int32 Rocks = 0;
	};
	TMap<FIntVector, FTile> Tiles;
	TArray<FIntVector> Wanted;
	TSet<FIntVector> Dirty;
	TArray<FShape> Shapes;
	TArray<TPair<FVector3d, double>> Clearings;
	FVector3d WantedFor = FVector3d(TNumericLimits<double>::Max());
	FDelegateHandle EditHandle;
	int32 Rocks = 0;
};
