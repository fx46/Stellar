#include "Game/StellarWorld.h"
#include "Stellar.h"
#include "Terrain/PlanetTerrainActor.h"
#include "Terrain/FluidActor.h"
#include "Game/Structures.h"
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

namespace
{
	constexpr double UU = 100.0;

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
	S.AutoExposureMinBrightness = 0.5f;
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

	// The sky's planet: concentric with the terrain, its ground at the site (world origin). A
	// little more dust than Earth's air (Mie) for aerial perspective across tens of kilometres.
	Atmosphere->SetBottomRadius(float((Terrain->RadiusMeters + SiteHeight) / 1000.0));
	Atmosphere->SetAtmosphereHeight(80.f);
	Atmosphere->SetMieScatteringScale(0.0075f);
	Atmosphere->SetMieExponentialDistribution(1.6f);
	Atmosphere->SetAerialPespectiveViewDistanceScale(1.25f);

	// Afternoon light, low and to the right of the view.
	const double Elevation = FMath::DegreesToRadians(17.0), Azimuth = FMath::DegreesToRadians(65.0);
	SunDirection = (SiteUp * FMath::Sin(Elevation) + (SiteForward * FMath::Cos(Azimuth) + Right * FMath::Sin(Azimuth)) * FMath::Cos(Elevation)).GetSafeNormal();
	Sun->SetWorldRotation((-Terrain->PlanetDirToWorld(SunDirection)).Rotation());

	PlaceStandIns();

	Structures = GetWorld()->SpawnActor<AStructuresActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	Structures->Setup(this, Terrain);
	Fluids = GetWorld()->SpawnActor<AFluidActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	Fluids->Setup(Terrain, Water != nullptr ? Water.Get() : SolidMaterial(FLinearColor(0.02f, 0.08f, 0.12f), 0.05f));

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

namespace
{
	/** Godot local (x right, y up, z back) metres -> Unreal local (X forward, Y right, Z up) cm. */
	FVector FromGodot(const FVector3f& V) { return FVector(-V.Z, V.X, V.Y) * UU; }
}

void AStellarWorld::Box(USceneComponent* Parent, UMaterialInterface* M, const FVector3f& At, const FVector3f& Size)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetMobility(EComponentMobility::Movable);
	C->SetStaticMesh(CubeMesh);
	C->SetMaterial(0, M);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetupAttachment(Parent);
	C->SetRelativeLocation(FromGodot(At));
	C->SetRelativeScale3D(FVector(Size.Z, Size.X, Size.Y)); // the cube is 1 m
	C->RegisterComponent();
}

void AStellarWorld::Sphere(USceneComponent* Parent, UMaterialInterface* M, const FVector3f& At, float Radius)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetMobility(EComponentMobility::Movable);
	C->SetStaticMesh(SphereMesh);
	C->SetMaterial(0, M);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetupAttachment(Parent);
	C->SetRelativeLocation(FromGodot(At));
	C->SetRelativeScale3D(FVector(Radius * 2)); // the sphere is 1 m across
	C->RegisterComponent();
}

void AStellarWorld::Cylinder(USceneComponent* Parent, UMaterialInterface* M, const FVector3f& At, float TopRadius, float BottomRadius,
	float Height, const FRotator& Rotation)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetMobility(EComponentMobility::Movable);
	const bool bCone = TopRadius < BottomRadius * 0.5f;
	C->SetStaticMesh(bCone ? ConeMesh : CylinderMesh);
	C->SetMaterial(0, M);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetupAttachment(Parent);
	C->SetRelativeLocation(FromGodot(At));
	C->SetRelativeRotation(Rotation);
	C->SetRelativeScale3D(FVector(BottomRadius * 2, BottomRadius * 2, Height)); // 1 m across, 1 m tall
	C->RegisterComponent();
}

void AStellarWorld::PlaceStandIns()
{
	// Stand-ins at known sizes (the scale study's): a person, a robot, a large machine and a
	// megastructure. What the milestone 0 visual gate judges the landscape against.
	auto Lin = [](float R, float G, float B) { return FLinearColor::FromSRGBColor(FColor(uint8(R * 255), uint8(G * 255), uint8(B * 255))); };
	UMaterialInterface* Suit = SolidMaterial(Lin(0.86f, 0.42f, 0.12f), 0.7f);
	UMaterialInterface* Visor = SolidMaterial(Lin(0.08f, 0.09f, 0.1f), 0.15f, 0.6f);
	UMaterialInterface* Helmet = SolidMaterial(Lin(0.92f, 0.92f, 0.9f), 0.4f);
	UMaterialInterface* Paint = SolidMaterial(Lin(0.78f, 0.62f, 0.22f), 0.55f, 0.2f);
	UMaterialInterface* Steel = SolidMaterial(Lin(0.36f, 0.37f, 0.38f), 0.45f, 0.7f);
	UMaterialInterface* Dark = SolidMaterial(Lin(0.11f, 0.11f, 0.11f), 0.85f);
	UMaterialInterface* Concrete = SolidMaterial(Lin(0.56f, 0.55f, 0.53f), 0.95f);
	UMaterialInterface* ConcreteDark = SolidMaterial(Lin(0.40f, 0.40f, 0.39f), 0.95f);
	UMaterialInterface* Window = SolidMaterial(Lin(0.05f, 0.06f, 0.07f), 0.2f, 0.4f);
	UMaterialInterface* Lamp = SolidMaterial(Lin(1.f, 0.85f, 0.6f), 0.5f, 0.f, 40.f);

	// A person, 1.8 m.
	USceneComponent* Person = Group(7, 1.6, 0);
	Box(Person, Suit, { 0, 0.45f, 0 }, { 0.36f, 0.9f, 0.24f });
	Box(Person, Suit, { 0, 1.2f, 0 }, { 0.5f, 0.62f, 0.3f });
	Sphere(Person, Helmet, { 0, 1.64f, 0 }, 0.17f);
	Box(Person, Visor, { 0, 1.66f, -0.13f }, { 0.22f, 0.1f, 0.06f });

	// A person-sized robot: a 1.4 m wheeled chassis with a cargo bed.
	USceneComponent* Robot = Group(12, -2.4, 15);
	Box(Robot, Paint, { 0, 0.55f, 0 }, { 0.9f, 0.45f, 1.4f });
	Box(Robot, Steel, { 0, 0.9f, 0.3f }, { 0.8f, 0.25f, 0.6f });
	Box(Robot, Dark, { 0, 0.95f, -0.45f }, { 0.5f, 0.35f, 0.3f });
	for (int32 I = 0; I < 4; I++)
		Cylinder(Robot, Dark, { I % 2 == 0 ? -0.5f : 0.5f, 0.28f, I < 2 ? -0.45f : 0.45f }, 0.28f, 0.28f, 0.18f, FRotator(0, 0, 90));
	Box(Robot, Lamp, { 0, 1.0f, -0.61f }, { 0.3f, 0.06f, 0.02f });

	// A large machine: a tracked drill crawler, 34 m long and 16 m tall.
	USceneComponent* Crawler = Group(140, 38, -25);
	Box(Crawler, Dark, { -6.5f, 2.5f, 0 }, { 4.5f, 5.f, 34.f });
	Box(Crawler, Dark, { 6.5f, 2.5f, 0 }, { 4.5f, 5.f, 34.f });
	Box(Crawler, Paint, { 0, 8.f, 1.f }, { 12.f, 6.f, 26.f });
	Box(Crawler, Steel, { 0, 12.5f, 5.f }, { 9.f, 3.f, 12.f });
	Box(Crawler, Paint, { 3.f, 14.f, -6.f }, { 4.f, 3.5f, 4.f });
	Box(Crawler, Window, { 3.f, 14.5f, -8.05f }, { 3.4f, 1.4f, 0.1f });
	Cylinder(Crawler, Steel, { 0, 7.5f, -18.5f }, 0.4f, 5.5f, 11.f, FRotator(-90, 0, 0));
	for (int32 I = 0; I < 3; I++)
		Box(Crawler, Lamp, { -3.f + I * 3.f, 11.2f, -12.1f }, { 0.8f, 0.4f, 0.1f });

	// A megastructure: a brutalist industrial mass 1.6 km long and 340 m tall, 2.6 km out.
	USceneComponent* Mega = Group(2600, -900, 10, 40);
	Box(Mega, Concrete, { 0, 150, 0 }, { 1600, 300, 420 });
	Box(Mega, ConcreteDark, { -250, 330, 40 }, { 900, 80, 300 });
	Box(Mega, Concrete, { 520, 360, -30 }, { 260, 140, 260 });
	Box(Mega, Paint, { 0, 232, -211 }, { 1600, 14, 4 });
	for (int32 I = 0; I < 6; I++)
		Box(Mega, Window, { 0, 60.f + I * 30.f, -210.5f }, { 1500, 4, 2 });
	for (int32 I = 0; I < 9; I++)
		Box(Mega, ConcreteDark, { -720.f + I * 180.f, 150, -230 }, { 30, 300, 40 });
	for (int32 I = 0; I < 12; I++)
		Box(Mega, Lamp, { -660.f + I * 120.f, 236, -214 }, { 6, 3, 1 });
}
