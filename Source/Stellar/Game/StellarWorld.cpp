#include "Game/StellarWorld.h"
#include "Stellar.h"
#include "Terrain/PlanetTerrainActor.h"
#include "Terrain/FluidActor.h"
#include "Terrain/RockScatter.h"
#include "Game/Structures.h"
#include "Game/StandIns.h"
#include "Components/PostProcessComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "HAL/PlatformTime.h"

using Stellar::UU;

namespace
{

	void Tangents(const FVector3d& Up, FVector3d& North, FVector3d& East)
	{
		const FVector3d Reference = FMath::Abs(Up.Z) > 0.99 ? FVector3d(1, 0, 0) : FVector3d(0, 0, 1);
		East = Reference.Cross(Up).GetSafeNormal();
		North = Up.Cross(East);
	}

	/** The direction Metres along the surface from U, at Azimuth from North. */
	FVector3d Walk(const FVector3d& U, const FVector3d& North, const FVector3d& East, double Azimuth, double Metres, double Radius)
	{
		const FVector3d T = North * FMath::Cos(Azimuth) + East * FMath::Sin(Azimuth);
		const double Theta = Metres / Radius;
		return (U * FMath::Cos(Theta) + T * FMath::Sin(Theta)).GetSafeNormal();
	}
}

AStellarWorld::AStellarWorld()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;

	// Light from the black hole's accretion disk: warm, low, shadow casting - the scene's main
	// light, as in the Godot game (a star versus black hole setting is still open).
	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("AccretionLight"));
	Sun->SetupAttachment(Root);
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->SetLightColor(FLinearColor(1.f, 0.7944f, 0.5464f));
	Sun->SetIntensity(10.f);
	Sun->SetAtmosphereSunLight(true);
	Sun->SetDynamicShadowDistanceMovableLight(40000.f * float(UU));
	Sun->SetCastShadows(true);

	Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Atmosphere"));
	Atmosphere->SetupAttachment(Root);

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(Root);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->bRealTimeCapture = true;
	SkyLight->SetIntensity(1.f);

	// Clouds over the plain: scale cues kilometres up (milestone 0 step 4).
	Clouds = CreateDefaultSubobject<UVolumetricCloudComponent>(TEXT("Clouds"));
	Clouds->SetupAttachment(Root);
	Clouds->SetLayerBottomAltitude(6.f);
	Clouds->SetLayerHeight(5.f);

	// Exposure kept to a daylight band, so a shadowed pit reads dark instead of being lifted to
	// noon, and cave darkness is real (the helmet lamp is for that).
	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	PostProcess->SetupAttachment(Root);
	PostProcess->bUnbound = true;
	FPostProcessSettings& S = PostProcess->Settings;
	S.bOverride_AutoExposureMinBrightness = true;
	S.bOverride_AutoExposureMaxBrightness = true;
	S.bOverride_AutoExposureBias = true;
	S.AutoExposureMinBrightness = 0.3f;
	S.AutoExposureMaxBrightness = 4.f;
	S.AutoExposureBias = -0.3f;
}

AStellarWorld* AStellarWorld::Get(UWorld* World)
{
	if (World == nullptr)
		return nullptr;
	AStellarWorld* Found = nullptr;
	for (TActorIterator<AStellarWorld> It(World); It; ++It)
	{
		Found = *It;
		break;
	}
	if (Found == nullptr)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Found = World->SpawnActor<AStellarWorld>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}
	if (Found != nullptr)
		Found->EnsureBuilt();
	return Found;
}

void AStellarWorld::BeginPlay()
{
	Super::BeginPlay();
	EnsureBuilt();
}

void AStellarWorld::EnsureBuilt()
{
	if (bBuilt)
		return;
	bBuilt = true;

	Solid = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Solid.M_Solid"));
	if (Solid == nullptr)
		Solid = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	Ghost = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Ghost.M_Ghost"));
	Water = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Water.M_Water"));
	if (UMaterialInterface* CloudMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst.m_SimpleVolumetricCloud_Inst")))
		Clouds->SetMaterial(CloudMaterial);
	Cursor = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_BrushCursor.M_BrushCursor"));
	if (Cursor == nullptr)
		Cursor = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
	CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	ConeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = this;
	Terrain = GetWorld()->SpawnActor<APlanetTerrainActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	Terrain->Initialize();

	const double T0 = FPlatformTime::Seconds();
	ChooseSite();
	UE_LOG(LogStellar, Log, TEXT("Site height %.0f m, ridge %.1f deg at %.1f km (chosen in %.0f ms)"), SiteHeight, RidgeElevationDeg,
		RidgeDistance / 1000.0, (FPlatformTime::Seconds() - T0) * 1000.0);

	// Planet axes -> world axes: the site's view to +X, its up to +Z, its ground at the origin.
	const FVector3d Right = SiteUp.Cross(SiteForward).GetSafeNormal();
	const FMatrix WorldToPlanet(FPlane(SiteForward, 0), FPlane(Right, 0), FPlane(SiteUp, 0), FPlane(0, 0, 0, 1));
	const FQuat Rotation = FQuat(WorldToPlanet).Inverse();
	const FVector Centre = -Rotation.RotateVector(SiteSurface * UU);
	Terrain->SetPlanetTransform(Centre, Rotation);

	// The sky's planet: concentric with the terrain, its ground at the site (world origin). Dusty
	// desert air, after the moodboard's machines in haze (images 9-11, 22): less Rayleigh (a paler
	// sky), more Mie dust that absorbs blue more than red (warm haze), and aerial perspective that
	// builds over kilometres.
	Atmosphere->SetBottomRadius(float((Terrain->RadiusMeters + SiteHeight) / 1000.0));
	Atmosphere->SetAtmosphereHeight(80.f);
	Atmosphere->SetRayleighScatteringScale(0.024f);
	Atmosphere->SetMieScatteringScale(0.012f);
	Atmosphere->SetMieAbsorptionScale(0.006f);
	Atmosphere->SetMieAbsorption(FLinearColor(0.3f, 0.5f, 0.85f));
	Atmosphere->SetMieExponentialDistribution(1.8f);
	Atmosphere->SetAerialPespectiveViewDistanceScale(2.0f);

	// Afternoon light, low and to the right of the view.
	SetSun(17.0, 65.0);

	PlaceStandIns();

	Structures = GetWorld()->SpawnActor<AStructuresActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	Structures->Setup(this, Terrain);
	Fluids = GetWorld()->SpawnActor<AFluidActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	Fluids->Setup(Terrain, Water != nullptr ? Water.Get() : SolidMaterial(FLinearColor(0.02f, 0.08f, 0.12f), 0.05f));
	Rocks = GetWorld()->SpawnActor<ARockScatter>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	Rocks->Setup(Terrain, Terrain->TerrainMaterial);
	{
		// Clear ground where the astronaut starts and where the stand-ins stand.
		const FVector3d SiteRight = SiteUp.Cross(SiteForward).GetSafeNormal();
		auto Clear = [&](double F, double S, double R) { Rocks->AddClearing(Terrain->GetTerrain().SurfaceAt(SiteSurface + SiteForward * F + SiteRight * S), R); };
		Clear(0, 0, 25);
		Clear(140, 38, 26);
		Clear(330, -150, 30);
		Rocks->AddClearing(MegastructureBase, 900);
	}

	if (!FParse::Param(FCommandLine::Get(), TEXT("notestsite")))
		BuildTestSite();
}

void AStellarWorld::BuildTestSite()
{
	const double T0 = FPlatformTime::Seconds();
	TestSite = FTestSitePlan::Build(Terrain->GetTerrain(), SiteSurface, SiteUp, SiteForward);
	Terrain->MutableTerrain().Field.SetDeposits(TestSite.Deposits);
	Terrain->ApplyNow(TestSite.Ops);
	for (const FTestSitePlan::FMarker& M : TestSite.Structures)
	{
		FPlacementCheck Where = Structures->Check(M.Type, M.Position, M.Forward);
		Where.bValid = true; // authored: placed even where the check would ask for grading
		Structures->Place(Where, M.Type);
	}
	const int32 Surface = Fluids->AddRegion(TestSite.SurfaceWater);
	FFluidSpring Spring;
	Spring.Position = TestSite.Spring;
	Spring.Rate = 0.8;
	Spring.Remaining = 160;
	Fluids->AddSpring(Surface, Spring);
	const int32 Aquifer = Fluids->AddRegion(TestSite.Aquifer);
	Fluids->Region(Aquifer).FillSphere(TestSite.Pocket, TestSite.PocketRadius);
	bTestSite = true;
	UE_LOG(LogStellar, Log, TEXT("Test site built in %.0f ms: %d edits, %d bricks (%.1f MB), %.0f m3 of water in the pocket"),
		(FPlatformTime::Seconds() - T0) * 1000.0, TestSite.Ops.Num(), Terrain->GetEdits()->NumBricks(),
		Terrain->GetEdits()->BrickBytes() / (1024.0 * 1024.0), Fluids->Region(Aquifer).CubicMetres());
}

bool AStellarWorld::FindLandmark(FVector3d& OutPoint, FString& OutKind) const
{
	// Rings of samples around the site, out to 40 km: the nearest mesa or spire.
	const FPlanetField& F = Terrain->GetTerrain().Field;
	const double R = F.GetRadius();
	FVector3d North, East;
	Tangents(SiteUp, North, East);
	for (double D = 2000; D <= 40000; D += 1000)
		for (int32 A = 0; A < 72; A++)
		{
			const FVector3d U = Walk(SiteUp, North, East, A * UE_DOUBLE_PI / 36, D, R);
			const FHeightLayers L = F.Layers(U.X * R, U.Y * R, U.Z * R);
			if (L.Mesa > 300 || L.Spire > 150)
			{
				OutPoint = U * (R + L.Total());
				OutKind = L.Mesa > 300 ? TEXT("mesa") : TEXT("spire field");
				return true;
			}
		}
	return false;
}

UMaterialInterface* AStellarWorld::GhostMaterial(const FLinearColor& Color)
{
	if (Ghost == nullptr)
		return SolidMaterial(Color, 0.5f, 0.f, 2.f);
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Ghost, this);
	M->SetVectorParameterValue(TEXT("Color"), Color);
	Instances.Add(M);
	return M;
}

void AStellarWorld::ChooseSite()
{
	const FSiteChoice C = ChooseStartSite(Terrain->GetTerrain());
	SiteUp = C.Up;
	SiteForward = C.Forward;
	SiteHeight = C.Height;
	SiteSurface = C.Surface;
	RidgeDistance = C.RidgeDistance;
	RidgeElevationDeg = C.RidgeElevationDeg;
}

UMaterialInterface* AStellarWorld::SolidMaterial(const FLinearColor& Color, float Roughness, float Metallic, float Emissive)
{
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Solid, this);
	M->SetVectorParameterValue(TEXT("Color"), Color);
	M->SetScalarParameterValue(TEXT("Roughness"), Roughness);
	M->SetScalarParameterValue(TEXT("Metallic"), Metallic);
	M->SetScalarParameterValue(TEXT("Emissive"), Emissive);
	Instances.Add(M);
	return M;
}

USceneComponent* AStellarWorld::Group(double Forward, double Side, float YawDeg, double Sink)
{
	const FPlanetTerrain& T = Terrain->GetTerrain();
	const FVector3d Right = SiteUp.Cross(SiteForward).GetSafeNormal();
	auto Ground = [&](double F, double S, double& Height)
	{
		return T.SurfaceAt(SiteSurface + SiteForward * F + Right * S, &Height);
	};
	double Height;
	FVector3d Point = Ground(Forward, Side, Height);
	const FVector3d Up = Point.GetSafeNormal();
	if (Sink > 1)
	{
		// A big footprint: rest on its lowest ground, sunk in.
		double Low = TNumericLimits<double>::Max();
		for (int32 I = -2; I <= 2; I++)
			for (int32 J = -2; J <= 2; J++)
			{
				double H;
				Ground(Forward + I * 200, Side + J * 400, H);
				Low = FMath::Min(Low, H);
			}
		Point = Up * (T.Field.GetRadius() + Low);
	}
	Point -= Up * Sink;
	const FVector3d Fwd = (SiteForward - Up * SiteForward.Dot(Up)).GetSafeNormal();

	USceneComponent* Node = NewObject<USceneComponent>(this);
	Node->SetMobility(EComponentMobility::Movable);
	Node->SetupAttachment(Root);
	Node->RegisterComponent();
	const FQuat Frame = FRotationMatrix::MakeFromXZ(Terrain->PlanetDirToWorld(Fwd), Terrain->PlanetDirToWorld(Up)).ToQuat();
	Node->SetWorldLocationAndRotation(Terrain->PlanetToWorld(Point), Frame * FQuat(FRotator(0, -YawDeg, 0)));
	return Node;
}

void AStellarWorld::SetSun(double ElevationDeg, double AzimuthDeg)
{
	SunElevationDeg = FMath::Clamp(ElevationDeg, -10.0, 90.0);
	SunAzimuthDeg = FMath::Fmod(AzimuthDeg + 360.0, 360.0);
	const FVector3d Right = SiteUp.Cross(SiteForward).GetSafeNormal();
	const double E = FMath::DegreesToRadians(SunElevationDeg), A = FMath::DegreesToRadians(SunAzimuthDeg);
	SunDirection = (SiteUp * FMath::Sin(E) + (SiteForward * FMath::Cos(A) + Right * FMath::Sin(A)) * FMath::Cos(E)).GetSafeNormal();
	Sun->SetWorldRotation((-Terrain->PlanetDirToWorld(SunDirection)).Rotation());
}

void AStellarWorld::PlaceStandIns()
{
	// Stand-ins at known sizes, after the moodboard's machines (Docs/MOODBOARD.md): what the
	// milestone 0 visual gate judges the landscape against.
	auto Lin = [](uint8 R, uint8 G, uint8 B) { return FLinearColor::FromSRGBColor(FColor(R, G, B)); };
	FStandInPalette P;
	P.Paint = SolidMaterial(Lin(186, 146, 58), 0.62f, 0.15f);
	P.PaintDark = SolidMaterial(Lin(132, 104, 62), 0.75f, 0.1f);
	P.Rust = SolidMaterial(Lin(112, 66, 42), 0.88f, 0.2f);
	P.Steel = SolidMaterial(Lin(98, 100, 102), 0.42f, 0.85f);
	P.SteelDark = SolidMaterial(Lin(46, 47, 49), 0.6f, 0.6f);
	P.Rubber = SolidMaterial(Lin(26, 26, 26), 0.92f);
	P.Concrete = SolidMaterial(Lin(150, 147, 140), 0.94f);
	P.ConcreteDark = SolidMaterial(Lin(96, 95, 91), 0.95f);
	P.Window = SolidMaterial(Lin(16, 20, 24), 0.15f, 0.5f);
	P.Lamp = SolidMaterial(Lin(255, 226, 170), 0.5f, 0.f, 60.f);
	P.Accent = SolidMaterial(Lin(214, 92, 30), 0.6f, 0.1f);
	P.Suit = SolidMaterial(Lin(220, 107, 31), 0.7f);
	P.Helmet = SolidMaterial(Lin(235, 235, 230), 0.4f);
	P.Visor = SolidMaterial(Lin(20, 23, 26), 0.15f, 0.6f);
	FKit::FMeshes Meshes;
	Meshes.Cube = CubeMesh;
	Meshes.Cylinder = CylinderMesh;
	Meshes.Cone = ConeMesh;
	Meshes.Sphere = SphereMesh;
	auto Make = [&](double Forward, double Side, float YawDeg, double Sink, void (*Build)(FKit&, const FStandInPalette&))
	{
		USceneComponent* Node = Group(Forward, Side, YawDeg, Sink);
		FKit Kit(this, Node, Meshes);
		Build(Kit, P);
		return Node;
	};
	Make(7, 1.6, 0, 0.05, &StandIns::Person);
	Make(12, -2.4, 15, 0.05, &StandIns::Rover);
	Make(140, 38, -25, 0.3, &StandIns::DrillCrawler);
	Make(330, -150, 35, 0.6, &StandIns::WalkingFactory);
	// The megastructure needs ground flat across 1.6 km, or it stands half buried in a dune: the
	// flattest footprint within a few kilometres of 2.6 km out, front-left of the view.
	double BestF = 2600, BestS = -900, BestScore = TNumericLimits<double>::Max();
	const FVector3d Right = SiteUp.Cross(SiteForward).GetSafeNormal();
	for (double F = 1800; F <= 4200; F += 200)
		for (double S = -2200; S <= 600; S += 200)
		{
			double Lo = TNumericLimits<double>::Max(), Hi = -Lo;
			for (int32 I = -2; I <= 2; I++)
				for (int32 J = -2; J <= 2; J++)
				{
					double H;
					Terrain->GetTerrain().SurfaceAt(SiteSurface + SiteForward * (F + I * 110) + Right * (S + J * 400), &H);
					Lo = FMath::Min(Lo, H);
					Hi = FMath::Max(Hi, H);
				}
			const double Score = (Hi - Lo) + 0.01 * FMath::Abs(FVector2d(F - 2600, S + 900).Size());
			if (Score < BestScore)
			{
				BestScore = Score;
				BestF = F;
				BestS = S;
			}
		}
	const USceneComponent* Mega = Make(BestF, BestS, 10, 40, &StandIns::Megastructure);
	MegastructureBase = Terrain->WorldToPlanet(Mega->GetComponentLocation());
	MegastructureFace = Terrain->WorldDirToPlanet(-Mega->GetForwardVector());
}
