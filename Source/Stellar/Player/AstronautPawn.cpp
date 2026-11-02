#include "Player/AstronautPawn.h"
#include "Stellar.h"
#include "Player/SculptBrush.h"
#include "Game/StellarWorld.h"
#include "Game/Structures.h"
#include "Terrain/PlanetTerrainActor.h"
#include "Terrain/FluidActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SpotLightComponent.h"
#include "ProceduralMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"
#include "Misc/Compression.h"

namespace
{
	constexpr double MotionHz = 120.0;
	constexpr double EyeOffset = 0.7; // capsule centre to eye (feet are Radius below the centre)
	constexpr uint32 SaveMagic = 0x524C5453; // "STLR"
	/** Version 2: tagged sections (so systems can be added without breaking older saves), the
	 * whole body compressed. */
	constexpr int32 SaveVersion = 2;
	/** How far a structure can be placed from the astronaut in the walking view. */
	constexpr double BuildReach = 16.0;

	constexpr uint32 Tag(const char (&S)[5]) { return uint32(S[0]) | uint32(S[1]) << 8 | uint32(S[2]) << 16 | uint32(S[3]) << 24; }

	double Axis(const APlayerController* PC, const FKey& Plus, const FKey& Minus)
	{
		return (PC->IsInputKeyDown(Plus) ? 1.0 : 0.0) - (PC->IsInputKeyDown(Minus) ? 1.0 : 0.0);
	}

	FVector3d Orthonormal(const FVector3d& V, const FVector3d& Up, const FVector3d& Fallback)
	{
		const FVector3d T = V - Up * V.Dot(Up);
		if (T.SizeSquared() > 1e-10)
			return T.GetSafeNormal();
		const FVector3d F = Fallback - Up * Fallback.Dot(Up);
		return F.SizeSquared() > 1e-10 ? F.GetSafeNormal() : Up.Cross(FVector3d(0, 1, 0)).GetSafeNormal();
	}

	FVector3d RotateAbout(const FVector3d& V, const FVector3d& Axis, double Angle)
	{
		const double C = FMath::Cos(Angle), S = FMath::Sin(Angle);
		return V * C + Axis.Cross(V) * S + Axis * (Axis.Dot(V) * (1 - C));
	}
}

AAstronautPawn::AAstronautPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	AutoPossessPlayer = EAutoReceiveInput::Player0;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetFieldOfView(80.f);
	Camera->bUsePawnControlRotation = false;
	RootComponent = Camera;

	// The helmet lamp: caves and cuts in shadow are really dark.
	Lamp = CreateDefaultSubobject<USpotLightComponent>(TEXT("HelmetLamp"));
	Lamp->SetupAttachment(Camera);
	Lamp->SetRelativeLocation(FVector(10, 0, 15));
	Lamp->SetIntensityUnits(ELightUnits::Lumens);
	Lamp->SetIntensity(1800.f);
	Lamp->SetAttenuationRadius(5000.f);
	Lamp->SetInnerConeAngle(14.f);
	Lamp->SetOuterConeAngle(34.f);
	Lamp->SetLightColor(FLinearColor(1.f, 0.95f, 0.88f));
	Lamp->SetVisibility(false);

	Cursor = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BrushCursor"));
	Cursor->SetupAttachment(Camera);
	Cursor->SetUsingAbsoluteLocation(true);
	Cursor->SetUsingAbsoluteRotation(true);
	Cursor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Cursor->SetCastShadow(false);
}

void AAstronautPawn::BeginPlay()
{
	Super::BeginPlay();
	Planet = AStellarWorld::Get(GetWorld());
	Terrain = Planet != nullptr ? Planet->GetTerrain() : nullptr;
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}
	PlaceAtSite();
}

void AAstronautPawn::PlaceAtSite()
{
	if (Terrain == nullptr || Planet == nullptr)
		return;
	State = FCharacterState();
	State.Position = Planet->SiteSurface;
	DropToGround();
	Heading = Planet->SiteForward;
	CamUp = State.Up = Planet->SiteUp;
	Pitch = 0;
	bFlightActive = false;
	JumpJet.Reset();
	bPlaced = true;
	UpdateCamera(0);
}

void AAstronautPawn::DropToGround()
{
	const FPlanetTerrain& T = Terrain->GetTerrain();
	const FVector3d Up = State.Position.GetSafeNormal();
	const double R = T.Field.GetRadius();
	// From just above the point, or - when inside rock - from above the highest ground: the
	// first rock below.
	const double Top = R + FPlanetField::MaxHeight + 10;
	const double Above = FMath::Min(Top, State.Position.Length() + 2.0);
	FVector3d Hit;
	const FVector3d From = Up * (Terrain->Density(State.Position) < 0 ? Top : Above);
	if (T.Trace(From, -Up, From.Length() - (R + FPlanetField::MinHeight - 10), Terrain->GetEdits(), Hit))
		State.Position = Hit + Up * (CharacterMotion::Radius + 0.02);
	State.Velocity = FVector3d::ZeroVector;
	State.bGrounded = true;
	State.Up = Up;
}

double AAstronautPawn::DistanceFromSite() const
{
	if (Planet == nullptr || Terrain == nullptr)
		return 0;
	const FVector3d P = Mode == EViewMode::FreeCam ? FreeCamPosition : State.Position;
	const double Angle = FMath::Acos(FMath::Clamp(P.GetSafeNormal().Dot(Planet->SiteUp), -1.0, 1.0));
	return Angle * Terrain->GetTerrain().Field.GetRadius();
}

double AAstronautPawn::BrushPace() const { return SculptBrush::Pace(PaceSetting, Suit.bGodMode); }
double AAstronautPawn::BrushRadius() const { return SculptBrush::Radius(RadiusSetting, Suit.bGodMode); }

FVector3d AAstronautPawn::ViewForward() const
{
	return (Heading * FMath::Cos(Pitch) + CamUp * FMath::Sin(Pitch)).GetSafeNormal();
}

FVector3d AAstronautPawn::ViewRight() const
{
	return CamUp.Cross(Heading).GetSafeNormal();
}

FVector3d AAstronautPawn::PlanningEye() const
{
	const FVector3d Up = PlanFocus.GetSafeNormal();
	return PlanFocus + Up * (PlanDistance * FMath::Sin(-PlanPitch)) - PlanHeading * (PlanDistance * FMath::Cos(PlanPitch));
}

FVector3d AAstronautPawn::EyePosition() const
{
	switch (Mode)
	{
	case EViewMode::FreeCam: return FreeCamPosition;
	case EViewMode::Planning: return PlanningEye();
	default: return State.Position + CamUp * (EyeOffset - EyeDip);
	}
}

void AAstronautPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Terrain == nullptr || !bPlaced)
		return;
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (PC == nullptr)
		return;
	const double Dt = FMath::Min(double(DeltaSeconds), 0.1);

	if (!bAutomated)
		HandleInput(PC, float(Dt));
	switch (Mode)
	{
	case EViewMode::FreeCam:
		UpdateFreeCam(PC, Dt);
		break;
	case EViewMode::Planning:
		// The astronaut keeps standing (and falling, and being dug under) while you plan.
		StepMotion(PC, Dt, false);
		UpdatePlanning(PC, Dt);
		break;
	default:
		StepMotion(PC, Dt, !bAutomated);
		break;
	}
	UpdateTool(PC, Dt);
	UpdateCamera(Dt);

	const double Now = GetWorld()->GetTimeSeconds();
	Toasts.RemoveAll([Now](const FHudToast& T) { return T.Until < Now; });
}

void AAstronautPawn::SetMode(EViewMode NewMode, APlayerController* PC)
{
	if (NewMode == Mode)
		return;
	if (Mode != EViewMode::Walk && NewMode == EViewMode::Walk && Mode == EViewMode::FreeCam)
	{
		// Leaving the free camera is fast travel: the astronaut drops to the ground below it.
		State.Position = FreeCamPosition;
		DropToGround();
		CamUp = State.Up;
		Heading = Orthonormal(Heading, CamUp, Heading);
		bFlightActive = false;
		JumpJet.Reset();
	}
	if (NewMode == EViewMode::Planning)
	{
		FVector3d Ground;
		const FVector3d Up = State.Position.GetSafeNormal();
		PlanFocus = Terrain->GetTerrain().Trace(State.Position + Up * 2.0, -Up, 50.0, Terrain->GetEdits(), Ground) ? Ground : State.Position;
		PlanHeading = Orthonormal(Heading, PlanFocus.GetSafeNormal(), Heading);
		PlanDistance = 45;
		PlanPitch = FMath::DegreesToRadians(-55.0);
	}
	if (NewMode == EViewMode::FreeCam)
		FreeCamPosition = EyePosition();
	if (Mode == EViewMode::Planning)
	{
		// Back to the astronaut's own view.
		CamUp = State.Up;
		Heading = Orthonormal(Heading, CamUp, Heading);
	}
	Mode = NewMode;
	if (PC != nullptr)
	{
		const bool bCursor = Mode == EViewMode::Planning;
		PC->bShowMouseCursor = bCursor;
		if (bCursor)
		{
			FInputModeGameAndUI Input;
			Input.SetHideCursorDuringCapture(false);
			PC->SetInputMode(Input);
		}
		else
			PC->SetInputMode(FInputModeGameOnly());
	}
	Selected = 0;
	if (Planet != nullptr && Planet->GetStructures() != nullptr)
		Planet->GetStructures()->Highlight(0);
}

void AAstronautPawn::HandleInput(APlayerController* PC, float Dt)
{
	const bool bCtrl = PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl);
	const bool bShift = PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift);

	// Look: yaw about the camera's up, pitch clamped short of straight up/down.
	if (Mode != EViewMode::Planning && !PC->bShowMouseCursor)
	{
		float Dx = 0, Dy = 0;
		PC->GetInputMouseDelta(Dx, Dy);
		const double Yaw = FMath::DegreesToRadians(Dx * 2.5);
		Heading = Orthonormal(RotateAbout(Heading, CamUp, Yaw), CamUp, Heading);
		Pitch = FMath::Clamp(Pitch + FMath::DegreesToRadians(Dy * 2.5), -1.55, 1.55);
	}

	if (PC->WasInputKeyJustPressed(EKeys::Escape))
	{
		if (Mode == EViewMode::Planning)
			SetMode(EViewMode::Walk, PC);
		else
		{
			PC->bShowMouseCursor = !PC->bShowMouseCursor;
			if (PC->bShowMouseCursor)
				PC->SetInputMode(FInputModeGameAndUI());
			else
				PC->SetInputMode(FInputModeGameOnly());
		}
	}
	if (Mode != EViewMode::Planning && PC->bShowMouseCursor && PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		PC->bShowMouseCursor = false;
		PC->SetInputMode(FInputModeGameOnly());
	}

	if (PC->WasInputKeyJustPressed(EKeys::O))
		SetMode(Mode == EViewMode::Planning ? EViewMode::Walk : EViewMode::Planning, PC);
	if (PC->WasInputKeyJustPressed(EKeys::F))
	{
		SetMode(Mode == EViewMode::FreeCam ? EViewMode::Walk : EViewMode::FreeCam, PC);
		if (Mode == EViewMode::FreeCam)
			Toast(TEXT("Free camera: speed follows altitude, wheel boosts. F drops you to the ground below."));
	}
	if (PC->WasInputKeyJustPressed(EKeys::T))
	{
		bBuildMode = !bBuildMode;
		Toast(bBuildMode ? TEXT("Build mode: 1-3 type, R rotate, LMB place, RMB or X remove, T to leave") : TEXT("Build mode off"),
			FLinearColor(0.85f, 0.75f, 0.4f));
	}
	if (bBuildMode && !bCtrl)
	{
		if (PC->WasInputKeyJustPressed(EKeys::One)) BuildType = EStructureType::FoundationPad;
		if (PC->WasInputKeyJustPressed(EKeys::Two)) BuildType = EStructureType::Depot;
		if (PC->WasInputKeyJustPressed(EKeys::Three)) BuildType = EStructureType::Beacon;
		if (PC->WasInputKeyJustPressed(EKeys::R)) BuildYawDeg += bShift ? -15 : 15;
	}
	if (PC->WasInputKeyJustPressed(EKeys::G))
	{
		Suit.bGodMode = !Suit.bGodMode;
		Toast(Suit.bGodMode ? TEXT("God mode: infinite suit energy, uncapped pace, 120 m reach") : TEXT("God mode off"),
			FLinearColor(1.f, 0.85f, 0.3f));
	}
	if (PC->WasInputKeyJustPressed(EKeys::L) && !bCtrl)
	{
		SetLampOn(!bLamp);
	}
	if (PC->WasInputKeyJustPressed(EKeys::Y) && Mode == EViewMode::Walk)
	{
		FVector3d Hit;
		if (Planet->GetFluids() != nullptr && Terrain->GetTerrain().Trace(EyePosition(), ViewForward(), 40.0, Terrain->GetEdits(), Hit))
		{
			const FVector3d At = Hit + Terrain->Normal(Hit) * 0.4;
			if (Planet->GetFluids()->RegionAt(At) == INDEX_NONE)
				Toast(TEXT("No water region here (the test site has two)"), FLinearColor(1.f, 0.6f, 0.3f));
			else
				Toast(Planet->GetFluids()->ToggleSpringAt(At) ? TEXT("Spring added") : TEXT("Spring removed"), FLinearColor(0.5f, 0.8f, 1.f));
		}
	}
	// The light: [ and ] lower and raise it, with Shift they turn it around the horizon.
	const int32 SunStep = (PC->WasInputKeyJustPressed(EKeys::RightBracket) ? 1 : 0) - (PC->WasInputKeyJustPressed(EKeys::LeftBracket) ? 1 : 0);
	if (SunStep != 0)
	{
		if (bShift)
			Planet->SetSun(Planet->GetSunElevation(), Planet->GetSunAzimuth() + SunStep * 15.0);
		else
			Planet->SetSun(Planet->GetSunElevation() + SunStep * 3.0, Planet->GetSunAzimuth());
		Toast(FString::Printf(TEXT("Light %.0f deg up, %.0f deg around"), Planet->GetSunElevation(), Planet->GetSunAzimuth()));
	}
	if (PC->WasInputKeyJustPressed(EKeys::H))
		bShowHints = !bShowHints;
	if (PC->WasInputKeyJustPressed(EKeys::P))
		bShowPerf = !bShowPerf;
	if (PC->WasInputKeyJustPressed(EKeys::Home))
	{
		SetMode(EViewMode::Walk, PC);
		PlaceAtSite();
		Toast(TEXT("Back at the start site"));
	}
	if (bCtrl && PC->WasInputKeyJustPressed(EKeys::S))
		SaveGame();
	if (bCtrl && PC->WasInputKeyJustPressed(EKeys::L))
		LoadGame();
	if (bCtrl && PC->WasInputKeyJustPressed(EKeys::One))
	{
		bWireframe = !bWireframe;
		PC->ConsoleCommand(bWireframe ? TEXT("viewmode wireframe") : TEXT("viewmode lit"));
	}
	if (bCtrl && PC->WasInputKeyJustPressed(EKeys::Two))
	{
		Terrain->bFreezeLod = !Terrain->bFreezeLod;
		Toast(Terrain->bFreezeLod ? TEXT("LOD frozen") : TEXT("LOD live"));
	}

	// Wheel: pace (Shift: brush size); free camera speed; planning zoom; build rotation.
	const int32 Wheel = (PC->WasInputKeyJustPressed(EKeys::MouseScrollUp) ? 1 : 0) - (PC->WasInputKeyJustPressed(EKeys::MouseScrollDown) ? 1 : 0);
	if (Wheel != 0)
	{
		if (Mode == EViewMode::FreeCam)
			FreeCamBoost = FMath::Clamp(FreeCamBoost * (Wheel > 0 ? 1.25 : 0.8), 0.05, 50.0);
		else if (Mode == EViewMode::Planning && !(bBuildMode && bShift))
			PlanDistance = FMath::Clamp(PlanDistance * (Wheel > 0 ? 0.85 : 1.0 / 0.85), 8.0, 20000.0);
		else if (bBuildMode)
			BuildYawDeg += Wheel * 15;
		else if (bShift)
			RadiusSetting = SculptBrush::ScrollRadius(RadiusSetting, Wheel, Suit.bGodMode);
		else
			PaceSetting = SculptBrush::ScrollPace(PaceSetting, Wheel, Suit.bGodMode);
	}
}

void AAstronautPawn::StepMotion(APlayerController* PC, double Dt, bool bInput)
{
	const FPlanetTerrain& T = Terrain->GetTerrain();
	const FTerrainEdits* Edits = Terrain->GetEdits();
	auto Density = [&T, Edits](const FVector3d& P) { return T.Density(P, Edits); };
	auto GravityUp = [](const FVector3d& P) { return P.GetSafeNormal(); };
	auto Normal = [&T, Edits](const FVector3d& P) { return T.Normal(P, Edits); };

	const bool bShift = bInput && PC->IsInputKeyDown(EKeys::LeftShift);
	const bool bSpace = bInput && PC->IsInputKeyDown(EKeys::SpaceBar);
	const bool bDown = bInput && (PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::C));
	const double Fwd = bInput ? Axis(PC, EKeys::W, EKeys::S) : 0, Side = bInput ? Axis(PC, EKeys::D, EKeys::A) : 0;

	StepAccumulator = FMath::Min(StepAccumulator + Dt, 8.0 / MotionHz);
	const double Step = 1.0 / MotionHz;
	// Walking directions follow the astronaut's own heading, also while another view is active.
	const FVector3d WalkUp = Mode == EViewMode::Walk ? CamUp : State.Up;
	const FVector3d WalkHeading = Orthonormal(Heading, WalkUp, Heading);
	while (StepAccumulator >= Step)
	{
		StepAccumulator -= Step;
		const FVector3d Right = WalkUp.Cross(WalkHeading).GetSafeNormal();
		const FVector3d Wish = WalkHeading * Fwd + Right * Side;

		bool bJump = false, bJet = false;
		JumpJet.Update(bSpace, State.bGrounded, Suit.CanThrust(), Step, bJump, bJet);
		if (bJet)
			bFlightActive = true;
		if (State.bGrounded || !Suit.CanThrust())
			bFlightActive = false;

		FVector3d FlightWish = ViewForward() * Fwd + Right * Side + WalkUp * ((bSpace ? 1.0 : 0.0) - (bDown ? 1.0 : 0.0));
		const double R = T.Field.GetRadius();
		const double Gravity = FMath::Square(R / FMath::Max(R, State.Position.Length()));
		State = CharacterMotion::Step(State, Density, GravityUp, Normal, Wish, bJump, bJet && !bFlightActive, bShift, Step, Gravity,
			bFlightActive ? &FlightWish : nullptr);
		JumpJet.AfterStep(State.bGrounded, Step);
		Suit.Tick(Step, bFlightActive ? (FlightWish.SizeSquared() > 1e-6 ? ESuitActivity::Jetpack : ESuitActivity::Hovering)
			: CharacterMotion::ActivityOf(State, bJet, Wish));
		if (State.bJustLanded)
			EyeDip = FMath::Min(0.25, State.LandingSpeed * 0.03);
	}
	EyeDip *= FMath::Exp(-6.0 * Dt);
	if (Mode == EViewMode::Walk)
		Altitude = FMath::Max(0.0, Density(State.Position) - CharacterMotion::Radius);
}

void AAstronautPawn::UpdateFreeCam(APlayerController* PC, double Dt)
{
	const FPlanetTerrain& T = Terrain->GetTerrain();
	Altitude = FMath::Max(0.0, T.Density(FreeCamPosition, Terrain->GetEdits()));
	if (!bAutomated)
	{
		const bool bShift = PC->IsInputKeyDown(EKeys::LeftShift);
		const double Speed = FMath::Clamp(Altitude, 4.0, 300000.0) * (bShift ? 6.0 : 1.0) * FreeCamBoost;
		LastFreeCamSpeed = Speed;
		const FVector3d V = ViewForward() * Axis(PC, EKeys::W, EKeys::S) + ViewRight() * Axis(PC, EKeys::D, EKeys::A)
			+ CamUp * ((PC->IsInputKeyDown(EKeys::SpaceBar) ? 1.0 : 0.0) - (PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::C) ? 1.0 : 0.0));
		if (V.SizeSquared() > 1e-8)
			FreeCamPosition += V.GetSafeNormal() * Speed * Dt;
	}
	// Never below the ground.
	const double D = T.Density(FreeCamPosition, Terrain->GetEdits());
	if (D < 0.5)
		FreeCamPosition += FreeCamPosition.GetSafeNormal() * (0.5 - D);
}

void AAstronautPawn::UpdatePlanning(APlayerController* PC, double Dt)
{
	const FPlanetTerrain& T = Terrain->GetTerrain();
	const FTerrainEdits* Edits = Terrain->GetEdits();
	FVector3d Up = PlanFocus.GetSafeNormal();
	if (!bAutomated)
	{
		const bool bShift = PC->IsInputKeyDown(EKeys::LeftShift);
		// Pan across the ground, faster the higher the view; Q/E and a middle-button drag turn it.
		const FVector3d Right = Up.Cross(PlanHeading).GetSafeNormal();
		const FVector3d Pan = PlanHeading * Axis(PC, EKeys::W, EKeys::S) + Right * Axis(PC, EKeys::D, EKeys::A);
		if (Pan.SizeSquared() > 1e-8)
			PlanFocus += Pan.GetSafeNormal() * PlanDistance * (bShift ? 3.0 : 1.0) * Dt;
		double Turn = Axis(PC, EKeys::E, EKeys::Q) * 1.2 * Dt;
		if (PC->IsInputKeyDown(EKeys::MiddleMouseButton))
		{
			float Dx = 0, Dy = 0;
			PC->GetInputMouseDelta(Dx, Dy);
			Turn += FMath::DegreesToRadians(Dx * 2.0);
			PlanPitch = FMath::Clamp(PlanPitch + FMath::DegreesToRadians(Dy * 2.0), FMath::DegreesToRadians(-88.0), FMath::DegreesToRadians(-12.0));
		}
		PlanHeading = Orthonormal(RotateAbout(PlanHeading, Up, Turn), Up, PlanHeading);
	}
	// Keep the focus on the ground (a cave roof counts, as seen from above).
	Up = PlanFocus.GetSafeNormal();
	FVector3d Ground;
	if (T.Trace(PlanFocus + Up * 400.0, -Up, 1200.0, Edits, Ground))
		PlanFocus = Ground;
	PlanHeading = Orthonormal(PlanHeading, Up, PlanHeading);
	// And the eye above it.
	const FVector3d Eye = PlanningEye();
	const double D = T.Density(Eye, Edits);
	if (D < 2.0)
		PlanDistance = FMath::Min(20000.0, PlanDistance + (2.0 - D) * 1.5);
	Altitude = FMath::Max(0.0, D);
}

void AAstronautPawn::UpdateCamera(double Dt)
{
	if (Mode == EViewMode::Planning)
	{
		const FVector3d Eye = PlanningEye();
		const FVector3d Up = Eye.GetSafeNormal();
		const FVector WorldForward = Terrain->PlanetDirToWorld((PlanFocus - Eye).GetSafeNormal());
		SetActorLocationAndRotation(Terrain->PlanetToWorld(Eye), FRotationMatrix::MakeFromXZ(WorldForward, Terrain->PlanetDirToWorld(Up)).ToQuat());
		return;
	}
	const FVector3d TargetUp = Mode == EViewMode::FreeCam ? FreeCamPosition.GetSafeNormal() : State.Up;
	const FVector3d OldUp = CamUp;
	CamUp = Dt > 0 ? PlayerOrientation::SmoothUp(CamUp, TargetUp, 10.0, Dt) : TargetUp;
	Heading = Orthonormal(PlayerOrientation::Transport(Heading, OldUp, CamUp), CamUp, Heading);

	const FVector WorldForward = Terrain->PlanetDirToWorld(ViewForward());
	const FVector WorldUp = Terrain->PlanetDirToWorld(CamUp);
	SetActorLocationAndRotation(Terrain->PlanetToWorld(EyePosition()), FRotationMatrix::MakeFromXZ(WorldForward, WorldUp).ToQuat());
}

void AAstronautPawn::PayOut(const FEditResult& R)
{
	for (int32 M = 0; M < int32(EGeoMaterial::Count); M++)
	{
		if (R.Volume[M] <= 0)
			continue; // building pays nothing back
		const int32 Units = Credit.Credit(EGeoMaterial(M), R.Volume[M]);
		if (Units <= 0)
			continue;
		const EItemType Item = Items::ForMaterial(EGeoMaterial(M));
		const int32 Added = Inventory.Add(Item, Units);
		if (Added > 0)
			Toast(FString::Printf(TEXT("+%d %s"), Added, Items::NameOf(Item)), FLinearColor(0.6f, 0.9f, 1.f));
		if (Added < Units)
			Toast(TEXT("Pockets full"), FLinearColor(1.f, 0.4f, 0.3f));
	}
}

void AAstronautPawn::UpdateTool(APlayerController* PC, double Dt)
{
	const FPlanetTerrain& T = Terrain->GetTerrain();
	const FTerrainEdits* Edits = Terrain->GetEdits();

	// What the view points at: the crosshair, or in the planning view the mouse cursor.
	FVector3d From = EyePosition(), Dir = ViewForward();
	double Range = Suit.bGodMode ? SculptBrush::GodReach + 20 : SculptBrush::Reach + 20;
	if (Mode == EViewMode::Planning)
	{
		FVector WorldPos, WorldDir;
		if (!bAutomated && PC->DeprojectMousePositionToWorld(WorldPos, WorldDir))
			Dir = Terrain->WorldDirToPlanet(WorldDir).GetSafeNormal();
		else
			Dir = (PlanFocus - From).GetSafeNormal();
		Range = 30000;
	}
	else if (bBuildMode)
		Range = Suit.bGodMode ? 140 : BuildReach + 10;
	FVector3d Hit;
	const bool bHit = Mode != EViewMode::FreeCam && T.Trace(From, Dir, Range, Edits, Hit);

	UpdateInspect(Hit, bHit);

	if (bBuildMode && Mode != EViewMode::FreeCam)
	{
		HideCursor();
		Status.Reset();
		UpdateBuild(PC, Hit, bHit, Mode == EViewMode::Planning || Suit.bGodMode || (bHit && FVector3d::Dist(State.Position, Hit) <= BuildReach));
		return;
	}
	if (Planet->GetStructures() != nullptr)
		Planet->GetStructures()->HidePreview();
	bPlacementTarget = false;

	if (Mode != EViewMode::Walk)
	{
		HideCursor();
		Status.Reset();
		// Selecting in the planning view.
		if (Mode == EViewMode::Planning && !bAutomated && PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))
		{
			Selected = Hovered;
			Planet->GetStructures()->Highlight(Selected);
		}
		return;
	}

	const bool bGod = Suit.bGodMode;
	const double Reach = bGod ? SculptBrush::GodReach : SculptBrush::Reach;
	const double Radius = BrushRadius();
	bAimed = bHit;
	if (!bAimed)
	{
		bInReach = false;
		DigWork = 0;
		HideCursor();
		Status = TEXT("no ground in reach");
		return;
	}
	bInReach = FVector3d::Dist(State.Position, Hit) <= Reach + Radius;

	// A footprint-scale surface frame, smoothed, so the brush does not snap to every facet.
	const FVector3d N = T.Normal(Hit, Edits, FMath::Max(0.3, Radius * 0.4));
	BrushNormal = BrushNormal.Dot(N) < 0.5 ? N : PlayerOrientation::SmoothUp(BrushNormal, N, 12.0, Dt);
	AimRock = T.RockAt(Hit - BrushNormal * 0.3, Edits);

	FVector3d Center = Hit;
	SculptBrush::ProjectSurface([this](const FVector3d& P) { return Terrain->Density(P); }, Hit, BrushNormal, Radius * 0.6, Center);

	const bool bCtrl = !bAutomated && (PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl));
	const bool bDig = !bAutomated && PC->IsInputKeyDown(EKeys::RightMouseButton);
	const bool bBuild = !bAutomated && !bDig && PC->IsInputKeyDown(EKeys::LeftMouseButton) && !PC->bShowMouseCursor;
	const bool bGrade = bDig && bCtrl && State.bGrounded;
	if (bGrade && !bGrading)
		GradePoint = Center; // the pad's height is where the grading started
	bGrading = bGrade;

	if ((bDig || bBuild) && bInReach)
	{
		DigWork = SculptBrush::Charge(DigWork, Dt, BrushPace(), 1);
		Status = bGrade ? FString(TEXT("grading a flat pad"))
			: bDig ? FString::Printf(TEXT("sculpting - %s, hardness x%.1f"), FPlanetField::MaterialName(AimRock.Material), AimRock.Hardness)
			: FString(TEXT("building"));
		if (DigWork >= 1.0 && !Terrain->IsEditPending())
		{
			if (bDig && !Suit.TryPay(FSuit::PowerPerBite))
			{
				Status = TEXT("suit battery flat - stand still to recharge");
				DigWork = 0;
			}
			else
			{
				FEditOp Op;
				if (bGrade)
				{
					// Level to the plane through the starting point, under gravity's up.
					const FVector3d Up = GradePoint.GetSafeNormal();
					const FVector3d OnPlane = Center - Up * (Center - GradePoint).Dot(Up);
					Op = FEditOp::Flatten(OnPlane, Up, Heading, Radius, Radius, FMath::Max(1.5, Radius * 0.8), 0, Radius * 0.35);
				}
				else
				{
					const double Depth = SculptBrush::Depth(Radius, bDig ? AimRock.Hardness : 1.0, bGod);
					Op = FEditOp::Sculpt(Center, -BrushNormal, Radius, bDig ? Depth : -Depth);
				}
				TWeakObjectPtr<AAstronautPawn> Self(this);
				if (Terrain->RequestEdit(Op, [Self](const FEditResult& R)
				{
					if (AAstronautPawn* P = Self.Get())
						P->PayOut(R);
				}))
					DigWork -= 1.0;
			}
		}
	}
	else
	{
		// Releasing cancels unfinished work.
		DigWork = 0;
		Status = bInReach ? FString::Printf(TEXT("%s, hardness x%.1f"), FPlanetField::MaterialName(AimRock.Material), AimRock.Hardness)
			: FString(TEXT("out of reach"));
	}
	DrawCursor(Center, BrushNormal, Radius, BiteProgress(), bInReach);
}

void AAstronautPawn::UpdateBuild(APlayerController* PC, const FVector3d& Hit, bool bHit, bool bInReachToPlace)
{
	AStructuresActor* S = Planet->GetStructures();
	bPlacementTarget = bHit;
	if (!bHit)
	{
		S->HidePreview();
		return;
	}
	const FVector3d Up = Hit.GetSafeNormal();
	const FVector3d Base = Mode == EViewMode::Planning ? PlanHeading : Heading;
	const FVector3d Forward = RotateAbout(Orthonormal(Base, Up, Base), Up, FMath::DegreesToRadians(BuildYawDeg));
	Placement = S->Check(BuildType, Hit, Forward);
	if (Placement.bValid && !bInReachToPlace)
	{
		Placement.bValid = false;
		Placement.Reason = TEXT("too far - walk closer, or plan it from above (O)");
	}
	S->ShowPreview(BuildType, Placement);
	if (bAutomated)
		return;
	if (PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		if (Placement.bValid)
		{
			const uint32 Id = S->Place(Placement, BuildType);
			Toast(FString::Printf(TEXT("Placed %s #%u"), FStructureSet::NameOf(BuildType), Id), FLinearColor(0.6f, 1.f, 0.6f));
		}
		else
			Toast(FString::Printf(TEXT("Can't place: %s"), *Placement.Reason), FLinearColor(1.f, 0.5f, 0.3f));
	}
	if ((PC->WasInputKeyJustPressed(EKeys::RightMouseButton) || PC->WasInputKeyJustPressed(EKeys::X)) && Hovered != 0)
	{
		const FStructure* Found = S->GetSet().Find(Hovered);
		const FString Name = Found != nullptr ? FStructureSet::NameOf(Found->Type) : TEXT("structure");
		if (S->Remove(Hovered))
			Toast(FString::Printf(TEXT("Removed %s #%u"), *Name, Hovered), FLinearColor(1.f, 0.8f, 0.5f));
		Hovered = 0;
	}
}

void AAstronautPawn::UpdateInspect(const FVector3d& Hit, bool bHit)
{
	Inspect.Reset();
	Hovered = 0;
	if (!bHit || (Mode != EViewMode::Planning && !bBuildMode))
		return;
	const FPlanetTerrain& T = Terrain->GetTerrain();
	const FTerrainEdits* Edits = Terrain->GetEdits();
	const double R = T.Field.GetRadius();
	const FVector3d Up = Hit.GetSafeNormal();
	const FVector3d N = T.Normal(Hit, Edits, 0.5);
	double Original = 0;
	T.SurfaceAt(Hit, &Original);
	const double Depth = (R + Original) - Hit.Length();
	const FGeoSample Rock = T.RockAt(Hit - N * 0.3, Edits);

	if (const FStructure* S = Planet->GetStructures()->GetSet().At(Hit))
	{
		Hovered = S->Id;
		Inspect.Add(FString::Printf(TEXT("%s #%u  (body %d)"), FStructureSet::NameOf(S->Type), S->Id, S->BodyId));
	}
	Inspect.Add(FString::Printf(TEXT("%s, hardness x%.1f"), FPlanetField::MaterialName(Rock.Material), Rock.Hardness));
	if (const FDeposit* D = T.Field.DepositAt(Hit - N * 1.0))
		Inspect.Add(D->Name);
	Inspect.Add(FString::Printf(TEXT("ground %+.1f m, slope %.0f deg"), Hit.Length() - R, FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(N.Dot(Up), -1.0, 1.0)))));
	if (Depth > 0.3)
		Inspect.Add(FString::Printf(TEXT("cut %.1f m below the original ground"), Depth));
	else if (Depth < -0.3)
		Inspect.Add(FString::Printf(TEXT("built %.1f m above the original ground"), -Depth));
	Inspect.Add(FString::Printf(TEXT("%.0f m from the astronaut"), FVector3d::Dist(Hit, State.Position)));
	if (Planet->GetFluids() != nullptr)
	{
		const int32 Region = Planet->GetFluids()->RegionAt(Hit + N * 0.3);
		if (Region != INDEX_NONE)
			Inspect.Add(FString::Printf(TEXT("water region %d (Y in the walking view toggles a spring)"), Region));
	}
}

void AAstronautPawn::DrawCursor(const FVector3d& Center, const FVector3d& Normal, double Radius, double Progress, bool bReady)
{
	FVector3d T1 = Normal.Cross(FMath::Abs(Normal.Z) < 0.9 ? FVector3d(0, 0, 1) : FVector3d(1, 0, 0)).GetSafeNormal();
	FVector3d T2 = Normal.Cross(T1);
	const double Distance = FVector3d::Dist(EyePosition(), Center);
	const double Width = FMath::Max(0.025, Distance * 0.0035);
	const FVector Origin = Terrain->PlanetToWorld(Center);
	auto Density = [this](const FVector3d& P) { return Terrain->Density(P); };

	TArray<FVector> Verts;
	TArray<int32> Tris;
	TArray<FColor> Colors;
	TArray<FVector> Normals;
	auto Ring = [&](double R, int32 Segments, double Fraction, const FColor& Color)
	{
		const int32 Base = Verts.Num();
		const int32 Count = FMath::Max(2, int32(Segments * Fraction) + 1);
		for (int32 I = 0; I < Count; I++)
		{
			const double A = 2 * UE_DOUBLE_PI * Fraction * I / (Count - 1);
			const FVector3d Dir = T1 * FMath::Cos(A) + T2 * FMath::Sin(A);
			FVector3d P = Center + Dir * R;
			FVector3d Q;
			if (SculptBrush::ProjectSurface(Density, P, Normal, FMath::Max(0.5, R * 0.6), Q))
				P = Q;
			P += Normal * 0.04;
			for (int32 Side = 0; Side < 2; Side++)
			{
				const FVector3d V = P + Dir * (Side == 0 ? -Width : Width);
				Verts.Add(Terrain->PlanetToWorld(V) - Origin);
				Normals.Add(Terrain->PlanetDirToWorld(Normal));
				Colors.Add(Color);
			}
			if (I > 0)
			{
				const int32 A0 = Base + (I - 1) * 2, A1 = A0 + 1, B0 = A0 + 2, B1 = A0 + 3;
				Tris.Append({ A0, B0, A1, A1, B0, B1, A0, A1, B0, A1, B1, B0 }); // both faces
			}
		}
	};
	const FColor Main = bGrading ? FColor(240, 200, 80) : bReady ? FColor(80, 180, 255) : FColor(200, 60, 50);
	Ring(Radius, 64, 1.0, Main);
	if (Progress > 0.01)
		Ring(Radius * 0.35, 32, Progress, FColor(220, 240, 255));
	Cursor->SetWorldLocationAndRotation(Origin, FQuat::Identity);
	Cursor->CreateMeshSection(0, Verts, Tris, Normals, TArray<FVector2D>(), Colors, TArray<FProcMeshTangent>(), false);
	if (Planet != nullptr)
		Cursor->SetMaterial(0, Planet->CursorMaterial());
	Cursor->SetVisibility(true);
}

void AAstronautPawn::HideCursor()
{
	Cursor->SetVisibility(false);
}

void AAstronautPawn::Toast(const FString& Text, const FLinearColor& Color)
{
	FHudToast T;
	T.Text = Text;
	T.Color = Color;
	T.Until = GetWorld()->GetTimeSeconds() + 3.0;
	// Merge repeated pickups of one item into the newest toast.
	if (Toasts.Num() > 0 && Toasts.Last().Text == Text)
		Toasts.Last().Until = T.Until;
	else
		Toasts.Add(T);
	if (Toasts.Num() > 6)
		Toasts.RemoveAt(0);
}

void AAstronautPawn::SetLampOn(bool bOn)
{
	bLamp = bOn;
	Lamp->SetVisibility(bOn);
}

// ---- director ----

void AAstronautPawn::SetAutomated(bool bOn)
{
	bAutomated = bOn;
	if (bOn)
		bShowHints = false;
}

void AAstronautPawn::DirectWalk(const FVector3d& Ground, const FVector3d& Forward, double PitchDeg)
{
	SetMode(EViewMode::Walk, nullptr);
	State = FCharacterState();
	State.Position = Ground + Ground.GetSafeNormal() * 1.5;
	DropToGround();
	CamUp = State.Up;
	Heading = Orthonormal(Forward, CamUp, Heading);
	Pitch = FMath::DegreesToRadians(PitchDeg);
	bFlightActive = false;
	JumpJet.Reset();
	UpdateCamera(0);
}

void AAstronautPawn::DirectFreeCam(const FVector3d& Eye, const FVector3d& Target)
{
	SetMode(EViewMode::FreeCam, nullptr);
	FreeCamPosition = Eye;
	CamUp = Eye.GetSafeNormal();
	const FVector3d Dir = (Target - Eye).GetSafeNormal();
	Heading = Orthonormal(Dir, CamUp, Heading);
	Pitch = FMath::Asin(FMath::Clamp(Dir.Dot(CamUp), -0.999, 0.999));
	UpdateCamera(0);
}

void AAstronautPawn::DirectPlanning(const FVector3d& Focus, double Distance, double PitchDeg, const FVector3d& InHeading)
{
	SetMode(EViewMode::Planning, nullptr);
	PlanFocus = Focus;
	PlanDistance = Distance;
	PlanPitch = FMath::DegreesToRadians(PitchDeg);
	PlanHeading = Orthonormal(InHeading, Focus.GetSafeNormal(), PlanHeading);
	UpdateCamera(0);
}

void AAstronautPawn::DirectReturn()
{
	if (Mode == EViewMode::FreeCam)
		SetMode(EViewMode::Walk, nullptr);
	else if (Mode == EViewMode::Planning)
		SetMode(EViewMode::Walk, nullptr);
	UpdateCamera(0);
}

bool AAstronautPawn::DirectBite(double Radius, double Depth)
{
	if (Terrain->IsEditPending())
		return false;
	FVector3d Hit;
	if (!Terrain->GetTerrain().Trace(EyePosition(), ViewForward(), 200.0, Terrain->GetEdits(), Hit))
		return false;
	const FVector3d N = Terrain->Normal(Hit);
	TWeakObjectPtr<AAstronautPawn> Self(this);
	return Terrain->RequestEdit(FEditOp::Sculpt(Hit, -N, Radius, Depth), [Self](const FEditResult& R)
	{
		if (AAstronautPawn* P = Self.Get())
			P->PayOut(R);
	});
}

// ---- saves ----

FString AAstronautPawn::SavePath() const
{
	return FPaths::ProjectSavedDir() / TEXT("SaveGames") / TEXT("stellar_save.bin");
}

void AAstronautPawn::SaveGame()
{
	if (Mode == EViewMode::Walk && !State.bGrounded)
	{
		Toast(TEXT("Save while standing on the ground"), FLinearColor(1.f, 0.6f, 0.3f));
		return;
	}
	FString Error;
	if (SaveTo(SavePath(), Error))
		Toast(FString::Printf(TEXT("Saved (%d edited bricks, %d structures)"), Terrain->GetEdits()->NumBricks(),
			Planet->GetStructures()->GetSet().All().Num()), FLinearColor(0.6f, 1.f, 0.6f));
	else
		Toast(FString::Printf(TEXT("Save failed: %s"), *Error), FLinearColor(1.f, 0.3f, 0.3f));
}

void AAstronautPawn::LoadGame()
{
	if (Mode == EViewMode::Walk && !State.bGrounded)
	{
		Toast(TEXT("Load while standing on the ground"), FLinearColor(1.f, 0.6f, 0.3f));
		return;
	}
	FString Error;
	if (LoadFrom(SavePath(), Error))
		Toast(FString::Printf(TEXT("Loaded (%d edited bricks, %d structures)"), Terrain->GetEdits()->NumBricks(),
			Planet->GetStructures()->GetSet().All().Num()), FLinearColor(0.6f, 1.f, 0.6f));
	else
		Toast(Error, FLinearColor(1.f, 0.3f, 0.3f));
}

bool AAstronautPawn::SaveTo(const FString& Path, FString& OutError)
{
	FBufferArchive Ar;
	uint32 Magic = SaveMagic;
	int32 Version = SaveVersion;
	double Radius = Terrain->RadiusMeters;
	int32 Seed = Terrain->Seed;
	Ar << Magic << Version << Radius << Seed;

	FBufferArchive Body;
	auto Section = [&Body](uint32 Id, TFunctionRef<void(FArchive&)> Write)
	{
		FBufferArchive Payload;
		Write(Payload);
		int64 Size = Payload.Num();
		Body << Id << Size;
		Body.Serialize(Payload.GetData(), Payload.Num());
	};
	Section(Tag("PLYR"), [this](FArchive& A)
	{
		FVector3d Position = Mode == EViewMode::Walk ? State.Position : Mode == EViewMode::FreeCam ? FreeCamPosition : State.Position;
		A << Position << Heading << Pitch << Suit.Power << Suit.bGodMode << PaceSetting << RadiusSetting;
	});
	Section(Tag("INVT"), [this](FArchive& A)
	{
		Inventory.Serialize(A);
		Credit.Serialize(A);
	});
	Section(Tag("EDIT"), [this](FArchive& A)
	{
		FTerrainEdits Copy = *Terrain->GetEdits();
		Copy.Serialize(A);
	});
	Section(Tag("STRC"), [this](FArchive& A) { Planet->GetStructures()->GetSet().Serialize(A); });
	Section(Tag("FLUD"), [this](FArchive& A) { Planet->GetFluids()->Serialize(A); });
	Section(Tag("SITE"), [this](FArchive& A)
	{
		bool bSite = Planet->HasTestSite();
		A << bSite;
	});

	// Compress the body (edit bricks are mostly smooth offsets: several times smaller).
	int64 RawSize = Body.Num();
	int32 Packed = FCompression::CompressMemoryBound(NAME_Oodle, int32(RawSize));
	TArray<uint8> Compressed;
	Compressed.SetNumUninitialized(Packed);
	if (!FCompression::CompressMemory(NAME_Oodle, Compressed.GetData(), Packed, Body.GetData(), int32(RawSize)))
	{
		OutError = TEXT("could not compress the save");
		return false;
	}
	Ar << RawSize << Packed;
	Ar.Serialize(Compressed.GetData(), Packed);

	// Write a temporary file, then replace: the previous save is kept as a backup.
	const FString Temp = Path + TEXT(".tmp"), Backup = Path + TEXT(".bak");
	if (!FFileHelper::SaveArrayToFile(Ar, *Temp))
	{
		OutError = TEXT("could not write the file");
		return false;
	}
	IFileManager& FM = IFileManager::Get();
	if (FM.FileExists(*Path))
		FM.Move(*Backup, *Path, true);
	FM.Move(*Path, *Temp, true);
	return true;
}

bool AAstronautPawn::LoadFrom(const FString& Path, FString& OutError)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		OutError = TEXT("No save to load");
		return false;
	}
	FMemoryReader Ar(Bytes);
	uint32 Magic = 0;
	int32 Version = 0, Seed = 0;
	double Radius = 0;
	Ar << Magic << Version << Radius << Seed;
	if (Magic != SaveMagic || Version < 1 || Version > SaveVersion || Radius != Terrain->RadiusMeters || Seed != Terrain->Seed)
	{
		OutError = TEXT("Save is for another planet or version");
		return false;
	}

	// Everything is read and validated before the running world changes.
	FVector3d Position, NewHeading;
	double NewPitch = 0, Power = 0, Pace = 1, Brush = 2.5;
	bool bGod = false;
	FInventory NewInventory;
	FMiningCredit NewCredit;
	TSharedRef<FTerrainEdits> NewEdits = MakeShared<FTerrainEdits>(Terrain->GetTerrain().LeafStride());
	FStructureSet NewStructures;
	TArray<uint8> Fluids;
	bool bHasStructures = false, bSite = false;
	auto ReadPlayer = [&](FArchive& A) { A << Position << NewHeading << NewPitch << Power << bGod << Pace << Brush; };
	if (Version == 1)
	{
		// Version 1: one fixed layout, no structures or water.
		ReadPlayer(Ar);
		NewInventory.Serialize(Ar);
		NewCredit.Serialize(Ar);
		NewEdits->Serialize(Ar);
	}
	else
	{
		int64 RawSize = 0;
		int32 Packed = 0;
		Ar << RawSize << Packed;
		if (Ar.IsError() || RawSize < 0 || RawSize > (int64(1) << 31) || Packed < 0 || Ar.Tell() + Packed > Ar.TotalSize())
		{
			OutError = TEXT("Save is damaged");
			return false;
		}
		TArray<uint8> Raw;
		Raw.SetNumUninitialized(int32(RawSize));
		if (!FCompression::UncompressMemory(NAME_Oodle, Raw.GetData(), int32(RawSize), Bytes.GetData() + Ar.Tell(), Packed))
		{
			OutError = TEXT("Save is damaged");
			return false;
		}
		FMemoryReader Body(Raw);
		while (!Body.AtEnd() && !Body.IsError())
		{
			uint32 Id = 0;
			int64 Size = 0;
			Body << Id << Size;
			const int64 End = Body.Tell() + Size;
			if (Size < 0 || End > Body.TotalSize())
			{
				OutError = TEXT("Save is damaged");
				return false;
			}
			if (Id == Tag("PLYR")) ReadPlayer(Body);
			else if (Id == Tag("INVT")) { NewInventory.Serialize(Body); NewCredit.Serialize(Body); }
			else if (Id == Tag("EDIT")) NewEdits->Serialize(Body);
			else if (Id == Tag("STRC")) { NewStructures.Serialize(Body); bHasStructures = true; }
			else if (Id == Tag("FLUD")) { Fluids.SetNumUninitialized(int32(Size)); Body.Serialize(Fluids.GetData(), Size); }
			else if (Id == Tag("SITE")) Body << bSite;
			// Unknown sections (from newer builds) are skipped.
			Body.Seek(End);
		}
		if (Body.IsError())
		{
			OutError = TEXT("Save is damaged");
			return false;
		}
	}
	if (Ar.IsError() || Position.ContainsNaN() || NewHeading.ContainsNaN() || !FMath::IsFinite(Power)
		|| !FMath::IsNearlyEqual(NewEdits->GetSpacing(), Terrain->GetTerrain().LeafStride()))
	{
		OutError = TEXT("Save is damaged");
		return false;
	}

	Terrain->SetEdits(NewEdits);
	if (bSite && !Planet->HasTestSite())
		Terrain->MutableTerrain().Field.SetDeposits(FTestSitePlan::Build(Terrain->GetTerrain(), Planet->SiteSurface, Planet->SiteUp, Planet->SiteForward).Deposits);
	AStructuresActor* S = Planet->GetStructures();
	S->GetSet() = bHasStructures ? NewStructures : FStructureSet();
	S->Rebuild();
	if (Fluids.Num() > 0)
	{
		FMemoryReader FA(Fluids);
		Planet->GetFluids()->Serialize(FA);
	}

	SetMode(EViewMode::Walk, Cast<APlayerController>(GetController()));
	State = FCharacterState();
	State.Position = Position;
	DropToGround();
	Heading = Orthonormal(NewHeading, State.Up, Heading);
	CamUp = State.Up;
	Pitch = NewPitch;
	Suit.Power = Power;
	Suit.bGodMode = bGod;
	PaceSetting = Pace;
	RadiusSetting = Brush;
	Inventory = NewInventory;
	Credit = NewCredit;
	bFlightActive = false;
	JumpJet.Reset();
	return true;
}
