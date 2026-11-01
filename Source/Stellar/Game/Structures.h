#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Structures.generated.h"

class AStellarWorld;
class APlanetTerrainActor;
class UMaterialInterface;
class UStaticMesh;

/** What can be built (milestone 0b: test structures for the placement flow and the art kit). */
enum class EStructureType : uint8
{
	/** 8 x 8 m concrete foundation slab with yellow edge bands. */
	FoundationPad,
	/** 6 x 6 x 4 m concrete depot block with a loading door. */
	Depot,
	/** A survey mast with a lamp: marks deposits and sites. */
	Beacon,
	Count
};

/**
 * A placed structure. World position alone is not identity (development plan): every structure
 * has a stable ID, the ID of the body it stands on, and a transform local to that body (here the
 * planet frame: position, up and forward).
 */
struct FStructure
{
	uint32 Id = 0;
	EStructureType Type = EStructureType::FoundationPad;
	/** The body it stands on (0: the home planet). */
	int32 BodyId = 0;
	FVector3d Position = FVector3d::ZeroVector;
	FVector3d Up = FVector3d(0, 0, 1);
	FVector3d Forward = FVector3d(1, 0, 0);
};

/** Engine-free structure bookkeeping: IDs, footprints, overlap, persistence. */
class STELLAR_API FStructureSet
{
public:
	static const TCHAR* NameOf(EStructureType Type);
	/** Footprint half sizes along forward and across it (metres), and height. */
	static FVector3d HalfSize(EStructureType Type);

	/** Adds a structure with a fresh ID; returns the ID. */
	uint32 Add(EStructureType Type, const FVector3d& Position, const FVector3d& Up, const FVector3d& Forward, int32 BodyId = 0);
	bool Remove(uint32 Id);
	const FStructure* Find(uint32 Id) const;
	/** The structure whose footprint contains P (within its height), if any. */
	const FStructure* At(const FVector3d& P, double Margin = 0.3) const;
	/** Whether a footprint at Position would overlap an existing structure. */
	bool Overlaps(EStructureType Type, const FVector3d& Position, const FVector3d& Forward) const;

	const TArray<FStructure>& All() const { return Items; }
	void Serialize(FArchive& Ar);

private:
	TArray<FStructure> Items;
	uint32 NextId = 1;
};

/** Why a placement is (in)valid, for the preview and the HUD. */
struct FPlacementCheck
{
	bool bValid = false;
	FString Reason;
	/** Where the structure would stand (planet frame) and its frame. */
	FVector3d Position = FVector3d::ZeroVector;
	FVector3d Up = FVector3d(0, 0, 1);
	FVector3d Forward = FVector3d(1, 0, 0);
};

/** Renders the structure set and the placement preview. */
UCLASS()
class STELLAR_API AStructuresActor : public AActor
{
	GENERATED_BODY()

public:
	AStructuresActor();

	void Setup(AStellarWorld* InWorld, APlanetTerrainActor* InTerrain);

	FStructureSet& GetSet() { return Set; }
	const FStructureSet& GetSet() const { return Set; }

	/** Ground check for a structure at a planet-frame point, turned to Forward. */
	FPlacementCheck Check(EStructureType Type, const FVector3d& Point, const FVector3d& Forward) const;

	uint32 Place(const FPlacementCheck& Where, EStructureType Type);
	bool Remove(uint32 Id);
	/** Rebuild every structure's visuals (after loading). */
	void Rebuild();

	/** The translucent preview: hidden when Type is Count. */
	void ShowPreview(EStructureType Type, const FPlacementCheck& Where);
	void HidePreview();
	/** Outline a structure (selection); 0 clears. */
	void Highlight(uint32 Id);

private:
	USceneComponent* Build(EStructureType Type, UMaterialInterface* Override);
	void Box(USceneComponent* Parent, UMaterialInterface* M, const FVector& Center, const FVector& Size);
	void PlaceComponent(USceneComponent* C, const FVector3d& Position, const FVector3d& Up, const FVector3d& Forward);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(Transient)
	TObjectPtr<AStellarWorld> Planet;
	UPROPERTY(Transient)
	TObjectPtr<APlanetTerrainActor> Terrain;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> Cube;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> Concrete;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> ConcreteDark;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> Accent;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> Steel;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> Lamp;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> GhostValid;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> GhostInvalid;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> Selected;

	UPROPERTY(Transient)
	TMap<uint32, TObjectPtr<USceneComponent>> Visuals;
	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> Previews;
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> Selection;

	FStructureSet Set;
	uint32 HighlightId = 0;
};
