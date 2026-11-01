#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Game/TestSite.h"
#include "StellarWorld.generated.h"

class APlanetTerrainActor;
class AStructuresActor;
class AFluidActor;
class UPostProcessComponent;
class UVolumetricCloudComponent;
class UDirectionalLightComponent;
class USkyAtmosphereComponent;
class USkyLightComponent;
class UMaterialInterface;
class UStaticMesh;

/**
 * The home planet and its sky: spawns the terrain, chooses the start site, lights it and places
 * the milestone 0 scale stand-ins. Built on demand (EnsureBuilt), so it does not depend on the
 * order in which actors begin play.
 *
 * The site (port of the Godot scale study's choice): open plain - flat within 2.5 m over 30 m
 * and off the escarpments - facing the highest ridge 15-42 km away. The planet is turned so the
 * site's up is the world's +Z and its view is +X, with the site's ground at the world origin:
 * the sky atmosphere's planet then sits exactly under it.
 */
UCLASS()
class STELLAR_API AStellarWorld : public AActor
{
	GENERATED_BODY()

public:
	AStellarWorld();

	/** Finds the world's instance, spawning and building it if needed. */
	static AStellarWorld* Get(UWorld* World);

	void EnsureBuilt();

	APlanetTerrainActor* GetTerrain() const { return Terrain; }
	AStructuresActor* GetStructures() const { return Structures; }
	AFluidActor* GetFluids() const { return Fluids; }

	/** The industrial test site (milestone 0b), laid out at startup unless -notestsite. */
	const FTestSitePlan& GetTestSite() const { return TestSite; }
	bool HasTestSite() const { return bTestSite; }
	/** Lay the test site out on the current terrain (edits, deposits, structures, water). */
	void BuildTestSite();

	/** A landmark (mesa or spire field) visible from the site, for views: its ground point and
	 * kind; false if none is within 40 km. */
	bool FindLandmark(FVector3d& OutPoint, FString& OutKind) const;

	/** The start site (planet frame): its ground point, up and view direction. */
	FVector3d SiteSurface = FVector3d::ZeroVector;
	FVector3d SiteUp = FVector3d(0, 0, 1);
	FVector3d SiteForward = FVector3d(1, 0, 0);
	double SiteHeight = 0;
	double RidgeDistance = 0;
	double RidgeElevationDeg = 0;

	/** Towards the light (planet frame). */
	FVector3d SunDirection = FVector3d(0, 0, 1);

	/** A shared solid-colour material instance for stand-ins and gadgets. */
	UMaterialInterface* SolidMaterial(const FLinearColor& Color, float Roughness, float Metallic = 0.f, float Emissive = 0.f);

	/** A translucent unlit colour, for placement previews and selection. */
	UMaterialInterface* GhostMaterial(const FLinearColor& Color);

	/** The unlit, vertex-coloured material the tool cursor uses. */
	UMaterialInterface* CursorMaterial() const { return Cursor; }

	/** Water (translucent). */
	UMaterialInterface* WaterMaterial() const { return Water; }

protected:
	virtual void BeginPlay() override;

private:
	void ChooseSite();
	void PlaceStandIns();
	USceneComponent* Group(double Forward, double Side, float YawDeg, double Sink = 0.05);
	void Box(USceneComponent* Parent, UMaterialInterface* M, const FVector3f& At, const FVector3f& Size);
	void Sphere(USceneComponent* Parent, UMaterialInterface* M, const FVector3f& At, float Radius);
	void Cylinder(USceneComponent* Parent, UMaterialInterface* M, const FVector3f& At, float TopRadius, float BottomRadius, float Height, const FRotator& Rotation);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UDirectionalLightComponent> Sun;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkyAtmosphereComponent> Atmosphere;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkyLightComponent> SkyLight;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UVolumetricCloudComponent> Clouds;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPostProcessComponent> PostProcess;

	UPROPERTY(Transient)
	TObjectPtr<AStructuresActor> Structures;

	UPROPERTY(Transient)
	TObjectPtr<AFluidActor> Fluids;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> Ghost;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> Water;

	UPROPERTY(Transient)
	TObjectPtr<APlanetTerrainActor> Terrain;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> Solid;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> Cursor;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> Instances;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> ConeMesh;

	bool bBuilt = false;
	bool bTestSite = false;
	FTestSitePlan TestSite;
};
