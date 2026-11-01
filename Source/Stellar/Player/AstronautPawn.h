#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Player/CharacterMotion.h"
#include "Game/Items.h"
#include "Game/Structures.h"
#include "Terrain/PlanetField.h"
#include "AstronautPawn.generated.h"

class UCameraComponent;
class USpotLightComponent;
class UProceduralMeshComponent;
class APlanetTerrainActor;
class AStellarWorld;

/** A short message on the HUD ("+3 Iron Ore", "Saved"). */
struct FHudToast
{
	FString Text;
	double Until = 0;
	FLinearColor Color = FLinearColor::White;
};

/** How the player sees the world. */
enum class EViewMode : uint8
{
	/** First person, walking and flying. */
	Walk,
	/** Development free camera: fast travel. */
	FreeCam,
	/** Overhead planning view (milestone 0b): pan, zoom, inspect, select and place. */
	Planning,
};

/**
 * The player: a first-person astronaut on the planet (port of the Godot PlayerController's
 * walking, jetpack and terrain-tool parts), plus the milestone 0b planning view and structure
 * placement. Motion runs against the density field in the planet frame at a fixed 120 Hz and
 * keeps running in every view, so switching views never pauses the simulation.
 *
 * Input is read straight from the player controller (no input assets); see the README's controls.
 */
UCLASS()
class STELLAR_API AAstronautPawn : public APawn
{
	GENERATED_BODY()

public:
	AAstronautPawn();

	virtual void Tick(float DeltaSeconds) override;

	// ---- state the HUD reads ----
	const FCharacterState& GetMotionState() const { return State; }
	const FSuit& GetSuit() const { return Suit; }
	const FInventory& GetInventory() const { return Inventory; }
	const TArray<FHudToast>& GetToasts() const { return Toasts; }
	EViewMode GetViewMode() const { return Mode; }
	bool IsFreeCam() const { return Mode == EViewMode::FreeCam; }
	bool IsFlying() const { return bFlightActive; }
	bool ShowHints() const { return bShowHints; }
	bool ShowPerf() const { return bShowPerf; }
	double AltitudeAboveGround() const { return Altitude; }
	double GroundSpeed() const { return State.Velocity.Length(); }
	double FreeCamSpeed() const { return LastFreeCamSpeed; }
	double DistanceFromSite() const;
	double BrushPace() const;
	double BrushRadius() const;
	double BiteProgress() const { return FMath::Clamp(DigWork, 0.0, 1.0); }
	bool IsBrushAimed() const { return bAimed; }
	bool IsBrushInReach() const { return bInReach; }
	const FGeoSample& AimedRock() const { return AimRock; }
	const FString& ToolStatus() const { return Status; }
	bool IsBuildMode() const { return bBuildMode; }
	EStructureType GetBuildType() const { return BuildType; }
	const FPlacementCheck& GetPlacement() const { return Placement; }
	bool HasPlacementTarget() const { return bPlacementTarget; }
	/** What is under the planning cursor (or the crosshair, when inspecting), line by line. */
	const TArray<FString>& GetInspect() const { return Inspect; }
	uint32 GetSelected() const { return Selected; }
	bool IsLampOn() const { return bLamp; }
	void SetLampOn(bool bOn);
	APlanetTerrainActor* GetTerrain() const { return Terrain; }
	AStellarWorld* GetPlanet() const { return Planet; }

	// ---- director: what the automated trial and screenshot tours drive ----
	/** Hand control to automation (no mouse look or keys) or back. */
	void SetAutomated(bool bOn);
	/** The astronaut standing at a ground point, facing Forward, the camera pitched. */
	void DirectWalk(const FVector3d& Ground, const FVector3d& Forward, double PitchDeg);
	/** The free camera at Eye, looking at Target. */
	void DirectFreeCam(const FVector3d& Eye, const FVector3d& Target);
	/** The planning view around Focus. */
	void DirectPlanning(const FVector3d& Focus, double Distance, double PitchDeg, const FVector3d& Heading);
	/** Leave the free camera or planning view, dropping the astronaut to the ground below the view. */
	void DirectReturn();
	/** Dig where the view points (one god-mode bite); false if the terrain is busy or nothing is aimed. */
	bool DirectBite(double Radius, double Depth);
	bool SaveTo(const FString& Path, FString& OutError);
	bool LoadFrom(const FString& Path, FString& OutError);
	FVector3d EyePosition() const;
	FVector3d ViewForward() const;

protected:
	virtual void BeginPlay() override;

private:
	void PlaceAtSite();
	void HandleInput(APlayerController* PC, float Dt);
	void StepMotion(APlayerController* PC, double Dt, bool bInput);
	void UpdateFreeCam(APlayerController* PC, double Dt);
	void UpdatePlanning(APlayerController* PC, double Dt);
	void UpdateTool(APlayerController* PC, double Dt);
	void UpdateBuild(APlayerController* PC, const FVector3d& Hit, bool bHit, bool bInReachToPlace);
	void UpdateInspect(const FVector3d& Hit, bool bHit);
	void UpdateCamera(double Dt);
	void DrawCursor(const FVector3d& Center, const FVector3d& Normal, double Radius, double Progress, bool bReady);
	void HideCursor();
	void Toast(const FString& Text, const FLinearColor& Color = FLinearColor::White);
	void DropToGround();
	void SetMode(EViewMode NewMode, APlayerController* PC);
	void PayOut(const struct FEditResult& R);
	FVector3d PlanningEye() const;

	void SaveGame();
	void LoadGame();
	FString SavePath() const;

	FVector3d ViewRight() const;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USpotLightComponent> Lamp;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProceduralMeshComponent> Cursor;

	UPROPERTY(Transient)
	TObjectPtr<APlanetTerrainActor> Terrain;

	UPROPERTY(Transient)
	TObjectPtr<AStellarWorld> Planet;

	FCharacterState State;
	FJumpJetInput JumpJet;
	FSuit Suit;
	FInventory Inventory = FInventory(400);
	FMiningCredit Credit;

	EViewMode Mode = EViewMode::Walk;
	bool bAutomated = false;

	/** Camera frame: smoothed up, a heading in its tangent plane, and pitch (radians). */
	FVector3d CamUp = FVector3d(0, 0, 1);
	FVector3d Heading = FVector3d(1, 0, 0);
	double Pitch = 0;
	double EyeDip = 0;

	bool bFlightActive = false;
	double StepAccumulator = 0;
	double Altitude = 0;

	FVector3d FreeCamPosition = FVector3d::ZeroVector;
	double FreeCamBoost = 1;
	double LastFreeCamSpeed = 0;

	// ---- planning view ----
	FVector3d PlanFocus = FVector3d::ZeroVector;
	FVector3d PlanHeading = FVector3d(1, 0, 0);
	double PlanDistance = 45;
	double PlanPitch = -0.95;

	// ---- the sculpt brush ----
	double PaceSetting = 1;
	double RadiusSetting = 2.5;
	double DigWork = 0;
	bool bAimed = false;
	bool bInReach = false;
	FVector3d BrushNormal = FVector3d(0, 0, 1);
	FGeoSample AimRock;
	FString Status;
	/** Grading: the plane point fixed when Ctrl+RMB went down. */
	bool bGrading = false;
	FVector3d GradePoint = FVector3d::ZeroVector;

	// ---- building ----
	bool bBuildMode = false;
	EStructureType BuildType = EStructureType::FoundationPad;
	double BuildYawDeg = 0;
	FPlacementCheck Placement;
	bool bPlacementTarget = false;
	uint32 Selected = 0;
	uint32 Hovered = 0;
	TArray<FString> Inspect;

	bool bLamp = false;
	bool bShowHints = true;
	bool bShowPerf = false;
	bool bWireframe = false;
	TArray<FHudToast> Toasts;
	bool bPlaced = false;
};
