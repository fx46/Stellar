#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Player/CharacterMotion.h"
#include "Game/Items.h"
#include "Terrain/PlanetField.h"
#include "AstronautPawn.generated.h"

class UCameraComponent;
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

/**
 * The player: a first-person astronaut on the planet (port of the Godot PlayerController's
 * walking, jetpack and terrain-tool parts). Motion runs against the density field in the planet
 * frame at a fixed 120 Hz; the camera follows gravity up, smoothed, with the heading parallel
 * transported as up turns so walking around the planet never adds yaw.
 *
 * Input is read straight from the player controller (no input assets): WASD/Shift walk and
 * sprint, Space jumps (hold or press again in the air for the jetpack), Ctrl descends in flight,
 * RMB sculpts, LMB builds, wheel sets pace, Shift+wheel brush size, G god mode, F the
 * development free camera (fast travel: leaving it drops the astronaut to the ground below),
 * Home returns to the start site, Ctrl+S / Ctrl+L save and load, H hints, P performance,
 * Ctrl+1 wireframe, Ctrl+2 freeze LOD.
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
	bool IsFreeCam() const { return bFreeCam; }
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
	APlanetTerrainActor* GetTerrain() const { return Terrain; }

protected:
	virtual void BeginPlay() override;

private:
	void PlaceAtSite();
	void HandleInput(APlayerController* PC, float Dt);
	void StepMotion(APlayerController* PC, double Dt);
	void UpdateFreeCam(APlayerController* PC, double Dt);
	void UpdateTool(APlayerController* PC, double Dt);
	void UpdateCamera(double Dt);
	void DrawCursor(const FVector3d& Center, const FVector3d& Normal, double Radius, double Progress, bool bReady);
	void HideCursor();
	void Toast(const FString& Text, const FLinearColor& Color = FLinearColor::White);
	void DropToGround();

	/** Development: -shots=<dir> flies to preset views, digs a test pit, saves screenshots, quits. */
	void UpdateShots(double Dt);

	void SaveGame();
	void LoadGame();
	FString SavePath() const;

	/** View basis in the planet frame. */
	FVector3d ViewForward() const;
	FVector3d ViewRight() const;
	FVector3d EyePosition() const;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;

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

	/** Camera frame: smoothed up, a heading in its tangent plane, and pitch (radians). */
	FVector3d CamUp = FVector3d(0, 0, 1);
	FVector3d Heading = FVector3d(1, 0, 0);
	double Pitch = 0;
	double EyeDip = 0;

	bool bFlightActive = false;
	double StepAccumulator = 0;
	double Altitude = 0;

	bool bFreeCam = false;
	FVector3d FreeCamPosition = FVector3d::ZeroVector;
	double FreeCamBoost = 1;
	double LastFreeCamSpeed = 0;

	// ---- the sculpt brush ----
	double PaceSetting = 1;
	double RadiusSetting = 2.5;
	double DigWork = 0;
	bool bAimed = false;
	bool bInReach = false;
	FVector3d BrushNormal = FVector3d(0, 0, 1);
	FGeoSample AimRock;
	FString Status;

	bool bShowHints = true;
	bool bShowPerf = false;
	bool bWireframe = false;
	TArray<FHudToast> Toasts;
	bool bPlaced = false;

	FString ShotsDir;
	int32 Shot = -1;
	int32 ShotStage = 0;
	double ShotWait = 0;
	int32 ShotBites = 0;
};
