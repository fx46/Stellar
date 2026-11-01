#include "Player/AstronautPawn.h"
#include "Stellar.h"
#include "Player/SculptBrush.h"
#include "Game/StellarWorld.h"
#include "Terrain/PlanetTerrainActor.h"
#include "Camera/CameraComponent.h"
#include "ProceduralMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"
#include "Kismet/KismetSystemLibrary.h"

namespace
{
	constexpr double MotionHz = 120.0;
	constexpr double EyeOffset = 0.7; // capsule centre to eye (feet are Radius below the centre)
	constexpr uint32 SaveMagic = 0x524C5453; // "STLR"
	constexpr int32 SaveVersion = 1;

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
	if (FParse::Value(FCommandLine::Get(), TEXT("-shots="), ShotsDir))
	{
		Shot = 0;
		bShowHints = false;
		bShowPerf = true;
	}
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
	// From the highest possible ground down, past any cave: the first rock below the sky.
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
	const FVector3d P = bFreeCam ? FreeCamPosition : State.Position;
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

FVector3d AAstronautPawn::EyePosition() const
{
	return bFreeCam ? FreeCamPosition : State.Position + CamUp * (EyeOffset - EyeDip);
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

	if (Shot >= 0)
		UpdateShots(Dt);
	else
		HandleInput(PC, float(Dt));
	if (bFreeCam)
		UpdateFreeCam(PC, Dt);
	else
		StepMotion(PC, Dt);
	UpdateTool(PC, Dt);
	UpdateCamera(Dt);

	const double Now = GetWorld()->GetTimeSeconds();
	Toasts.RemoveAll([Now](const FHudToast& T) { return T.Until < Now; });
}

void AAstronautPawn::HandleInput(APlayerController* PC, float Dt)
{
	const bool bCtrl = PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl);
	const bool bShift = PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift);

	// Look: yaw about the camera's up, pitch clamped short of straight up/down.
	if (!PC->bShowMouseCursor)
	{
		float Dx = 0, Dy = 0;
		PC->GetInputMouseDelta(Dx, Dy);
		const double Yaw = FMath::DegreesToRadians(Dx * 2.5);
		Heading = Orthonormal(RotateAbout(Heading, CamUp, Yaw), CamUp, Heading);
		Pitch = FMath::Clamp(Pitch + FMath::DegreesToRadians(Dy * 2.5), -1.55, 1.55);
	}

	if (PC->WasInputKeyJustPressed(EKeys::Escape))
	{
		PC->bShowMouseCursor = !PC->bShowMouseCursor;
		if (PC->bShowMouseCursor)
			PC->SetInputMode(FInputModeGameAndUI());
		else
			PC->SetInputMode(FInputModeGameOnly());
	}
	if (PC->bShowMouseCursor && PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		PC->bShowMouseCursor = false;
		PC->SetInputMode(FInputModeGameOnly());
	}

	if (PC->WasInputKeyJustPressed(EKeys::G))
	{
		Suit.bGodMode = !Suit.bGodMode;
		Toast(Suit.bGodMode ? TEXT("God mode: infinite suit energy, uncapped pace, 120 m reach") : TEXT("God mode off"),
			FLinearColor(1.f, 0.85f, 0.3f));
	}
	if (PC->WasInputKeyJustPressed(EKeys::H))
		bShowHints = !bShowHints;
	if (PC->WasInputKeyJustPressed(EKeys::P))
		bShowPerf = !bShowPerf;
	if (PC->WasInputKeyJustPressed(EKeys::F))
	{
		if (!bFreeCam)
		{
			bFreeCam = true;
			FreeCamPosition = EyePosition();
			Toast(TEXT("Free camera: speed follows altitude, wheel boosts. F drops you to the ground below."));
		}
		else
		{
			bFreeCam = false;
			State.Position = FreeCamPosition;
			DropToGround();
			CamUp = State.Up;
			Heading = Orthonormal(Heading, CamUp, Heading);
			bFlightActive = false;
			JumpJet.Reset();
		}
	}
	if (PC->WasInputKeyJustPressed(EKeys::Home))
	{
		bFreeCam = false;
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

	// Wheel: pace (Shift: brush size); in the free camera, its speed.
	const int32 Wheel = (PC->WasInputKeyJustPressed(EKeys::MouseScrollUp) ? 1 : 0) - (PC->WasInputKeyJustPressed(EKeys::MouseScrollDown) ? 1 : 0);
	if (Wheel != 0)
	{
		if (bFreeCam)
			FreeCamBoost = FMath::Clamp(FreeCamBoost * (Wheel > 0 ? 1.25 : 0.8), 0.05, 50.0);
		else if (bShift)
			RadiusSetting = SculptBrush::ScrollRadius(RadiusSetting, Wheel, Suit.bGodMode);
		else
			PaceSetting = SculptBrush::ScrollPace(PaceSetting, Wheel, Suit.bGodMode);
	}
}

void AAstronautPawn::StepMotion(APlayerController* PC, double Dt)
{
	const FPlanetTerrain& T = Terrain->GetTerrain();
	const FTerrainEdits* Edits = Terrain->GetEdits();
	auto Density = [&T, Edits](const FVector3d& P) { return T.Density(P, Edits); };
	auto GravityUp = [](const FVector3d& P) { return P.GetSafeNormal(); };
	auto Normal = [&T, Edits](const FVector3d& P) { return T.Normal(P, Edits); };

	const bool bShift = PC->IsInputKeyDown(EKeys::LeftShift);
	const bool bSpace = PC->IsInputKeyDown(EKeys::SpaceBar);
	const bool bDown = PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::C);
	const double Fwd = Axis(PC, EKeys::W, EKeys::S), Side = Axis(PC, EKeys::D, EKeys::A);

	StepAccumulator = FMath::Min(StepAccumulator + Dt, 8.0 / MotionHz);
	const double Step = 1.0 / MotionHz;
	while (StepAccumulator >= Step)
	{
		StepAccumulator -= Step;
		const FVector3d Right = ViewRight();
		const FVector3d Wish = Heading * Fwd + Right * Side;

		bool bJump = false, bJet = false;
		JumpJet.Update(bSpace, State.bGrounded, Suit.CanThrust(), Step, bJump, bJet);
		if (bJet)
			bFlightActive = true;
		if (State.bGrounded || !Suit.CanThrust())
			bFlightActive = false;

		FVector3d FlightWish = ViewForward() * Fwd + Right * Side + CamUp * ((bSpace ? 1.0 : 0.0) - (bDown ? 1.0 : 0.0));
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
	Altitude = FMath::Max(0.0, Density(State.Position) - CharacterMotion::Radius);
}

void AAstronautPawn::UpdateFreeCam(APlayerController* PC, double Dt)
{
	const FPlanetTerrain& T = Terrain->GetTerrain();
	Altitude = FMath::Max(0.0, T.Density(FreeCamPosition, Terrain->GetEdits()));
	const bool bShift = PC->IsInputKeyDown(EKeys::LeftShift);
	const double Speed = FMath::Clamp(Altitude, 4.0, 300000.0) * (bShift ? 6.0 : 1.0) * FreeCamBoost;
	LastFreeCamSpeed = Speed;
	const FVector3d V = ViewForward() * Axis(PC, EKeys::W, EKeys::S) + ViewRight() * Axis(PC, EKeys::D, EKeys::A)
		+ CamUp * ((PC->IsInputKeyDown(EKeys::SpaceBar) ? 1.0 : 0.0) - (PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::C) ? 1.0 : 0.0));
	if (V.SizeSquared() > 1e-8)
		FreeCamPosition += V.GetSafeNormal() * Speed * Dt;
	// Never below the ground.
	const double D = T.Density(FreeCamPosition, Terrain->GetEdits());
	if (D < 0.5)
		FreeCamPosition += FreeCamPosition.GetSafeNormal() * (0.5 - D);
}

void AAstronautPawn::UpdateCamera(double Dt)
{
	const FVector3d TargetUp = bFreeCam ? FreeCamPosition.GetSafeNormal() : State.Up;
	const FVector3d OldUp = CamUp;
	CamUp = Dt > 0 ? PlayerOrientation::SmoothUp(CamUp, TargetUp, 10.0, Dt) : TargetUp;
	Heading = Orthonormal(PlayerOrientation::Transport(Heading, OldUp, CamUp), CamUp, Heading);

	const FVector WorldForward = Terrain->PlanetDirToWorld(ViewForward());
	const FVector WorldUp = Terrain->PlanetDirToWorld(CamUp);
	SetActorLocationAndRotation(Terrain->PlanetToWorld(EyePosition()), FRotationMatrix::MakeFromXZ(WorldForward, WorldUp).ToQuat());
}

void AAstronautPawn::UpdateTool(APlayerController* PC, double Dt)
{
	if (bFreeCam)
	{
		HideCursor();
		Status.Reset();
		return;
	}
	const FPlanetTerrain& T = Terrain->GetTerrain();
	const bool bGod = Suit.bGodMode;
	const double Reach = bGod ? SculptBrush::GodReach : SculptBrush::Reach;
	const double Radius = BrushRadius();

	FVector3d Hit;
	const FVector3d Eye = EyePosition();
	bAimed = T.Trace(Eye, ViewForward(), Reach + 20.0, Terrain->GetEdits(), Hit);
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
	const FVector3d N = T.Normal(Hit, Terrain->GetEdits(), FMath::Max(0.3, Radius * 0.4));
	BrushNormal = BrushNormal.Dot(N) < 0.5 ? N : PlayerOrientation::SmoothUp(BrushNormal, N, 12.0, Dt);

	const FVector3d Inside = Hit - BrushNormal * 0.3;
	AimRock = T.Field.Geology(Inside, -T.Field.Density(Inside));

	FVector3d Center = Hit;
	SculptBrush::ProjectSurface([this](const FVector3d& P) { return Terrain->Density(P); }, Hit, BrushNormal, Radius * 0.6, Center);

	const bool bDig = PC->IsInputKeyDown(EKeys::RightMouseButton);
	const bool bBuild = !bDig && PC->IsInputKeyDown(EKeys::LeftMouseButton) && !PC->bShowMouseCursor;
	if ((bDig || bBuild) && bInReach)
	{
		DigWork = SculptBrush::Charge(DigWork, Dt, BrushPace(), 1);
		Status = bDig ? FString::Printf(TEXT("sculpting - %s, hardness x%.1f"), FPlanetField::MaterialName(AimRock.Material), AimRock.Hardness)
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
				const double Depth = SculptBrush::Depth(Radius, bDig ? AimRock.Hardness : 1.0, bGod);
				const FSculptOp Op = FSculptOp::Make(Center, -BrushNormal, Radius, bDig ? Depth : -Depth);
				TWeakObjectPtr<AAstronautPawn> Self(this);
				if (Terrain->RequestEdit(Op, [Self](const FEditResult& R)
				{
					AAstronautPawn* P = Self.Get();
					if (P == nullptr)
						return;
					for (int32 M = 0; M < int32(EGeoMaterial::Count); M++)
					{
						if (R.Volume[M] <= 0)
							continue; // building pays nothing back
						const int32 Units = P->Credit.Credit(EGeoMaterial(M), R.Volume[M]);
						if (Units <= 0)
							continue;
						const EItemType Item = Items::ForMaterial(EGeoMaterial(M));
						const int32 Added = P->Inventory.Add(Item, Units);
						if (Added > 0)
							P->Toast(FString::Printf(TEXT("+%d %s"), Added, Items::NameOf(Item)), FLinearColor(0.6f, 0.9f, 1.f));
						if (Added < Units)
							P->Toast(TEXT("Pockets full"), FLinearColor(1.f, 0.4f, 0.3f));
					}
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
	const FColor Main = bReady ? FColor(80, 180, 255) : FColor(200, 60, 50);
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

void AAstronautPawn::UpdateShots(double Dt)
{
	struct FView { const TCHAR* Name; double Height; double PitchDeg; double YawDeg; bool bDig; };
	static const FView Views[] = {
		{ TEXT("eye"), 0, 0, 0, false },
		{ TEXT("aim"), 0, -32, 0, false },
		{ TEXT("dig"), 0, -15, 0, true },
		{ TEXT("rise_150m"), 150, -8, 0, false },
		{ TEXT("rise_2km"), 2000, -12, 0, false },
		{ TEXT("high_30km"), 30000, -25, 0, false },
		{ TEXT("eye_back"), 0, 0, 180, false },
	};
	if (Shot >= int32(UE_ARRAY_COUNT(Views)))
	{
		UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
		Shot = -1;
		return;
	}
	const FView& V = Views[Shot];
	if (ShotStage == 0)
	{
		// Go to the view.
		PlaceAtSite();
		Heading = RotateAbout(Planet->SiteForward, Planet->SiteUp, FMath::DegreesToRadians(V.YawDeg));
		Pitch = FMath::DegreesToRadians(V.PitchDeg);
		if (V.Height > 0)
		{
			bFreeCam = true;
			FreeCamPosition = State.Position + Planet->SiteUp * V.Height;
		}
		else
			bFreeCam = false;
		Suit.bGodMode = V.bDig;
		RadiusSetting = V.bDig ? 2.5 : 2.0;
		ShotBites = V.bDig ? 14 : 0;
		ShotWait = 0;
		ShotStage = 1;
		return;
	}
	if (ShotStage == 1)
	{
		// Dig the test pit: one bite whenever the last has landed.
		if (ShotBites > 0 && !Terrain->IsEditPending())
		{
			FVector3d Hit;
			if (Terrain->GetTerrain().Trace(EyePosition(), ViewForward(), 30, Terrain->GetEdits(), Hit))
			{
				const FVector3d N = Terrain->Normal(Hit);
				const double Depth = SculptBrush::Depth(RadiusSetting, 1.0, true) * 3;
				Terrain->RequestEdit(FSculptOp::Make(Hit, -N, RadiusSetting, Depth), [](const FEditResult&) {});
			}
			if (--ShotBites == 0)
			{
				// Look down into the pit from above and behind.
				bFreeCam = true;
				FreeCamPosition = State.Position + Planet->SiteUp * 6.0 - Heading * 1.0;
				Pitch = FMath::DegreesToRadians(-48.0);
			}
		}
		// Wait for the bites and the terrain to settle, and the lighting to adapt.
		ShotWait += Dt;
		if (ShotBites == 0 && !Terrain->IsEditPending() && !Terrain->IsJobRunning() && Terrain->HasTerrain() && ShotWait > 6.0)
		{
			const FString Path = ShotsDir / FString::Printf(TEXT("%02d_%s.png"), Shot, V.Name);
			FScreenshotRequest::RequestScreenshot(Path, true, false);
			UE_LOG(LogStellar, Log, TEXT("SHOT %s: %d nodes, %.2f M triangles, last mesh %.0f ms"), *Path, Terrain->NumNodes(),
				Terrain->NumTriangles() / 1e6, Terrain->LastMeshMs());
			ShotStage = 2;
			ShotWait = 0;
		}
		return;
	}
	ShotWait += Dt;
	if (ShotWait > 0.5)
	{
		Shot++;
		ShotStage = 0;
	}
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

FString AAstronautPawn::SavePath() const
{
	return FPaths::ProjectSavedDir() / TEXT("SaveGames") / TEXT("stellar_save.bin");
}

void AAstronautPawn::SaveGame()
{
	if (!State.bGrounded && !bFreeCam)
	{
		Toast(TEXT("Save while standing on the ground"), FLinearColor(1.f, 0.6f, 0.3f));
		return;
	}
	const FTerrainEdits EditsCopy = *Terrain->GetEdits();
	FBufferArchive Ar;
	uint32 Magic = SaveMagic;
	int32 Version = SaveVersion;
	double Radius = Terrain->RadiusMeters;
	int32 Seed = Terrain->Seed;
	FVector3d Position = bFreeCam ? FreeCamPosition : State.Position;
	Ar << Magic << Version << Radius << Seed;
	Ar << Position << Heading << Pitch;
	Ar << Suit.Power << Suit.bGodMode << PaceSetting << RadiusSetting;
	Inventory.Serialize(Ar);
	Credit.Serialize(Ar);
	const_cast<FTerrainEdits&>(EditsCopy).Serialize(Ar);

	// Write a temporary file, then replace: the previous save is kept as a backup.
	const FString Path = SavePath(), Temp = Path + TEXT(".tmp"), Backup = Path + TEXT(".bak");
	if (!FFileHelper::SaveArrayToFile(Ar, *Temp))
	{
		Toast(TEXT("Save failed: could not write the file"), FLinearColor(1.f, 0.3f, 0.3f));
		return;
	}
	IFileManager& FM = IFileManager::Get();
	if (FM.FileExists(*Path))
		FM.Move(*Backup, *Path, true);
	FM.Move(*Path, *Temp, true);
	Toast(FString::Printf(TEXT("Saved (%d edited bricks)"), EditsCopy.NumBricks()), FLinearColor(0.6f, 1.f, 0.6f));
}

void AAstronautPawn::LoadGame()
{
	if (!State.bGrounded && !bFreeCam)
	{
		Toast(TEXT("Load while standing on the ground"), FLinearColor(1.f, 0.6f, 0.3f));
		return;
	}
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *SavePath()))
	{
		Toast(TEXT("No save to load"), FLinearColor(1.f, 0.6f, 0.3f));
		return;
	}
	FMemoryReader Ar(Bytes);
	uint32 Magic = 0;
	int32 Version = 0, Seed = 0;
	double Radius = 0;
	Ar << Magic << Version << Radius << Seed;
	if (Magic != SaveMagic || Version != SaveVersion || Radius != Terrain->RadiusMeters || Seed != Terrain->Seed)
	{
		Toast(TEXT("Save is for another planet or version"), FLinearColor(1.f, 0.3f, 0.3f));
		return;
	}
	FVector3d Position, NewHeading;
	double NewPitch = 0, Power = 0, Pace = 1, Brush = 2.5;
	bool bGod = false;
	FInventory NewInventory;
	FMiningCredit NewCredit;
	TSharedRef<FTerrainEdits> NewEdits = MakeShared<FTerrainEdits>(Terrain->GetTerrain().LeafStride());
	Ar << Position << NewHeading << NewPitch;
	Ar << Power << bGod << Pace << Brush;
	NewInventory.Serialize(Ar);
	NewCredit.Serialize(Ar);
	NewEdits->Serialize(Ar);
	// Validate everything before changing the running world.
	if (Ar.IsError() || Position.ContainsNaN() || NewHeading.ContainsNaN() || !FMath::IsFinite(Power)
		|| !FMath::IsNearlyEqual(NewEdits->GetSpacing(), Terrain->GetTerrain().LeafStride()))
	{
		Toast(TEXT("Save is damaged"), FLinearColor(1.f, 0.3f, 0.3f));
		return;
	}
	Terrain->SetEdits(NewEdits);
	bFreeCam = false;
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
	Toast(FString::Printf(TEXT("Loaded (%d edited bricks)"), NewEdits->NumBricks()), FLinearColor(0.6f, 1.f, 0.6f));
}
