#pragma once

#include "CoreMinimal.h"

class AActor;
class USceneComponent;
class UStaticMesh;
class UMaterialInterface;
class UInstancedStaticMeshComponent;

/**
 * A kitbashing helper for scale stand-ins: boxes, cylinders, cones and balls in a machine's local
 * frame (metres; X forward, Y right, Z up, origin on the ground), batched into one instanced
 * component per mesh and material, so a machine of hundreds of parts costs a handful of draws.
 */
class STELLAR_API FKit
{
public:
	struct FMeshes
	{
		UStaticMesh* Cube = nullptr;
		UStaticMesh* Cylinder = nullptr;
		UStaticMesh* Cone = nullptr;
		UStaticMesh* Sphere = nullptr;
	};

	FKit(AActor* InOwner, USceneComponent* InParent, const FMeshes& InMeshes) : Owner(InOwner), Parent(InParent), Meshes(InMeshes) {}

	void Box(UMaterialInterface* M, const FVector& Centre, const FVector& Size, const FRotator& Rotation = FRotator::ZeroRotator);
	/** Axis along local Z before Rotation. */
	void Cyl(UMaterialInterface* M, const FVector& Centre, double Radius, double Length, const FRotator& Rotation = FRotator::ZeroRotator);
	/** Tip along local +Z before Rotation. */
	void Cone(UMaterialInterface* M, const FVector& Centre, double Radius, double Length, const FRotator& Rotation = FRotator::ZeroRotator);
	void Ball(UMaterialInterface* M, const FVector& Centre, double Radius);
	/** Small boxes scattered over a face (centre, outward normal along an axis, extent across). */
	void Greeble(UMaterialInterface* M, const FVector& FaceCentre, const FVector& Normal, const FVector& Across1, const FVector& Across2,
		int32 Count, double MaxSize, FRandomStream& Rng);

private:
	void Add(UStaticMesh* Mesh, UMaterialInterface* M, const FTransform& T);

	AActor* Owner;
	USceneComponent* Parent;
	FMeshes Meshes;
	TMap<TPair<UStaticMesh*, UMaterialInterface*>, UInstancedStaticMeshComponent*> Batches;
};

/** The palette the stand-ins share: worn industrial paint, steel, concrete, lamps. */
struct FStandInPalette
{
	UMaterialInterface* Paint = nullptr;      // worn yellow
	UMaterialInterface* PaintDark = nullptr;  // faded ochre
	UMaterialInterface* Rust = nullptr;
	UMaterialInterface* Steel = nullptr;
	UMaterialInterface* SteelDark = nullptr;
	UMaterialInterface* Rubber = nullptr;
	UMaterialInterface* Concrete = nullptr;
	UMaterialInterface* ConcreteDark = nullptr;
	UMaterialInterface* Window = nullptr;
	UMaterialInterface* Lamp = nullptr;
	UMaterialInterface* Accent = nullptr;     // safety orange
	UMaterialInterface* Suit = nullptr;
	UMaterialInterface* Helmet = nullptr;
	UMaterialInterface* Visor = nullptr;
};

/**
 * The stand-ins at known sizes, after the moodboard's machines: a person (1.8 m), a sensor rover
 * (1.4 m), a tracked drill crawler (34 m), a six-legged walking factory (a mobile factory,
 * 40 m tall), and a brutalist industrial mass (1.6 km long, 340 m tall).
 */
namespace StandIns
{
	STELLAR_API void Person(FKit& K, const FStandInPalette& P);
	STELLAR_API void Rover(FKit& K, const FStandInPalette& P);
	STELLAR_API void DrillCrawler(FKit& K, const FStandInPalette& P);
	STELLAR_API void WalkingFactory(FKit& K, const FStandInPalette& P);
	STELLAR_API void Megastructure(FKit& K, const FStandInPalette& P);
}
