#include "Game/StandIns.h"
#include "Stellar.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"

using Stellar::UU;

void FKit::Add(UStaticMesh* Mesh, UMaterialInterface* M, const FTransform& T)
{
	UInstancedStaticMeshComponent*& C = Batches.FindOrAdd(TPair<UStaticMesh*, UMaterialInterface*>(Mesh, M));
	if (C == nullptr)
	{
		C = NewObject<UInstancedStaticMeshComponent>(Owner);
		C->SetMobility(EComponentMobility::Movable);
		C->SetStaticMesh(Mesh);
		C->SetMaterial(0, M);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetupAttachment(Parent);
		C->RegisterComponent();
	}
	C->AddInstance(T, false);
}

void FKit::Box(UMaterialInterface* M, const FVector& Centre, const FVector& Size, const FRotator& Rotation)
{
	Add(Meshes.Cube, M, FTransform(Rotation, Centre * UU, Size)); // the cube is 1 m
}

void FKit::Cyl(UMaterialInterface* M, const FVector& Centre, double Radius, double Length, const FRotator& Rotation)
{
	Add(Meshes.Cylinder, M, FTransform(Rotation, Centre * UU, FVector(Radius * 2, Radius * 2, Length))); // 1 m across and tall
}

void FKit::Cone(UMaterialInterface* M, const FVector& Centre, double Radius, double Length, const FRotator& Rotation)
{
	Add(Meshes.Cone, M, FTransform(Rotation, Centre * UU, FVector(Radius * 2, Radius * 2, Length)));
}

void FKit::Ball(UMaterialInterface* M, const FVector& Centre, double Radius)
{
	Add(Meshes.Sphere, M, FTransform(FRotator::ZeroRotator, Centre * UU, FVector(Radius * 2)));
}

void FKit::Greeble(UMaterialInterface* M, const FVector& FaceCentre, const FVector& Normal, const FVector& Across1, const FVector& Across2,
	int32 Count, double MaxSize, FRandomStream& Rng)
{
	// Panels, boxes and vents standing a little proud of a face: the "readable mechanism" detail.
	const FRotator Frame = FRotationMatrix::MakeFromXZ(Normal, FMath::Abs(Normal.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector).Rotator();
	for (int32 I = 0; I < Count; I++)
	{
		const FVector At = FaceCentre + Across1 * Rng.FRandRange(-1, 1) + Across2 * Rng.FRandRange(-1, 1);
		const double A = Rng.FRandRange(0.2, 1.0) * MaxSize, B = Rng.FRandRange(0.2, 1.0) * MaxSize, D = Rng.FRandRange(0.05, 0.35) * MaxSize;
		// Size along the face's normal is the depth; the frame's X is the normal.
		Box(M, At + Normal.GetSafeNormal() * D * 0.5, FVector(D, A, B), Frame);
	}
}

void StandIns::Person(FKit& K, const FStandInPalette& P)
{
	for (int32 S = -1; S <= 1; S += 2)
	{
		K.Box(P.Suit, FVector(0, 0.1 * S, 0.43), FVector(0.17, 0.16, 0.86));
		K.Box(P.Suit, FVector(0.04, 0.29 * S, 1.12), FVector(0.12, 0.12, 0.6), FRotator(0, 0, 8 * S));
	}
	K.Box(P.Suit, FVector(0, 0, 1.18), FVector(0.3, 0.46, 0.62));
	K.Box(P.SteelDark, FVector(-0.25, 0, 1.22), FVector(0.22, 0.38, 0.48)); // life-support pack
	K.Ball(P.Helmet, FVector(0, 0, 1.63), 0.16);
	K.Box(P.Visor, FVector(0.12, 0, 1.64), FVector(0.07, 0.2, 0.09));
}

void StandIns::Rover(FKit& K, const FStandInPalette& P)
{
	// A six-wheeled survey rover with a sensor mast and headlights (board image 16).
	K.Box(P.Paint, FVector(0, 0, 0.56), FVector(1.3, 0.8, 0.32));
	K.Box(P.SteelDark, FVector(-0.1, 0, 0.86), FVector(0.85, 0.7, 0.28));
	for (int32 I = -1; I <= 1; I++)
		for (int32 S = -1; S <= 1; S += 2)
		{
			K.Cyl(P.Rubber, FVector(0.45 * I, 0.48 * S, 0.25), 0.25, 0.17, FRotator(0, 0, 90));
			K.Box(P.Steel, FVector(0.45 * I, 0.4 * S, 0.42), FVector(0.08, 0.1, 0.3));
		}
	K.Cyl(P.Steel, FVector(0.3, 0, 1.25), 0.035, 0.5);
	K.Cyl(P.SteelDark, FVector(0.3, 0, 1.53), 0.09, 0.09);
	K.Cyl(P.Steel, FVector(-0.45, -0.25, 1.35), 0.01, 0.7);
	for (int32 S = -1; S <= 1; S += 2)
		K.Box(P.Lamp, FVector(0.66, 0.24 * S, 0.62), FVector(0.02, 0.12, 0.06));
	K.Box(P.PaintDark, FVector(-0.4, 0.15, 1.08), FVector(0.3, 0.3, 0.18));
	K.Box(P.Accent, FVector(0.651, 0, 0.5), FVector(0.01, 0.6, 0.05));
}

void StandIns::DrillCrawler(FKit& K, const FStandInPalette& P)
{
	// A tracked drill crawler, 34 m long, 17 m tall (board images 9 and 11): two track units, a
	// worn hull, a spiral-toothed drill head, stacks, tanks, railings and work lights.
	FRandomStream Rng(9);
	for (int32 S = -1; S <= 1; S += 2)
	{
		const double Y = 7 * S;
		K.Box(P.SteelDark, FVector(0, Y, 2.6), FVector(26, 4.4, 4.4));
		for (int32 End = -1; End <= 1; End += 2)
			K.Cyl(P.Rubber, FVector(13 * End, Y, 2.5), 2.45, 4.6, FRotator(0, 0, 90));
		for (int32 I = 0; I < 26; I++)
		{
			K.Box(P.Rubber, FVector(-12.5 + I, Y, 4.95), FVector(0.75, 4.7, 0.32));
			K.Box(P.Rubber, FVector(-12.5 + I, Y, 0.16), FVector(0.75, 4.7, 0.32));
		}
		for (int32 I = 0; I < 7; I++)
			K.Cyl(P.Steel, FVector(-10.5 + I * 3.5, Y + 2.35 * S, 1.3), 0.95, 0.3, FRotator(0, 0, 90));
		K.Greeble(P.Steel, FVector(0, Y + 2.25 * S, 3.4), FVector(0, S, 0), FVector(11, 0, 0), FVector(0, 0, 0.8), 14, 1.2, Rng);
	}
	// Hull.
	K.Box(P.Paint, FVector(-1, 0, 8.2), FVector(24, 11, 6.4));
	K.Box(P.Paint, FVector(12.6, 0, 7.6), FVector(5.5, 11, 5.0), FRotator(-28, 0, 0));
	K.Box(P.PaintDark, FVector(-3, 0, 12.7), FVector(16, 9, 2.6));
	K.Box(P.Accent, FVector(-1, 0, 5.1), FVector(24.1, 11.1, 0.35));
	for (int32 S = -1; S <= 1; S += 2)
	{
		K.Greeble(P.PaintDark, FVector(-1, 5.5 * S, 8.4), FVector(0, S, 0), FVector(11, 0, 0), FVector(0, 0, 2.6), 36, 1.6, Rng);
		K.Box(P.Rust, FVector(-6, 4.6 * S, 11.6), FVector(8, 0.1, 1.0)); // a rust run under the deck
	}
	// Cabin with windows.
	K.Box(P.PaintDark, FVector(5, 3, 15.4), FVector(4.2, 4.2, 3.6));
	K.Box(P.Window, FVector(7.12, 3, 15.8), FVector(0.06, 3.6, 1.3));
	K.Box(P.Window, FVector(5, 5.12, 15.8), FVector(3.6, 0.06, 1.3));
	// Drill head: mount, cone with a helix of teeth.
	K.Cyl(P.SteelDark, FVector(16.5, 0, 7.6), 3.1, 3.4, FRotator(-90, 0, 0));
	K.Cone(P.Steel, FVector(23.6, 0, 7.6), 5.4, 11, FRotator(-90, 0, 0));
	for (int32 I = 0; I < 48; I++)
	{
		const double T = I / 48.0;
		const double X = 18.4 + T * 10.4, R = 5.3 * (1 - T) + 0.25, A = T * 5 * UE_DOUBLE_PI * 2;
		const FVector At(X, R * FMath::Cos(A), 7.6 + R * FMath::Sin(A));
		K.Box(P.SteelDark, At, FVector(0.9, 0.5, 0.9), FRotator(0, 0, FMath::RadiansToDegrees(A)));
	}
	// Stacks, tanks, railings and lights.
	for (int32 S = -1; S <= 1; S += 2)
	{
		K.Cyl(P.Rust, FVector(-9, 3 * S, 16.2), 0.55, 6.5);
		K.Cyl(P.SteelDark, FVector(-9, 3 * S, 19.5), 0.65, 0.4);
		K.Cyl(P.Rust, FVector(-6, 4.6 * S, 13.8), 1.3, 8, FRotator(-90, 0, 0));
	}
	for (int32 I = 0; I <= 8; I++)
		for (int32 S = -1; S <= 1; S += 2)
			K.Box(P.Steel, FVector(-11 + I * 2, 4.4 * S, 14.6), FVector(0.07, 0.07, 1.1));
	for (int32 S = -1; S <= 1; S += 2)
		K.Box(P.Steel, FVector(-3, 4.4 * S, 15.15), FVector(16, 0.06, 0.06));
	for (int32 I = -1; I <= 1; I++)
		K.Box(P.Lamp, FVector(15.2, 3 * I, 11.3), FVector(0.15, 0.9, 0.45));
	K.Box(P.Lamp, FVector(7.2, 1.2, 17.4), FVector(0.15, 0.6, 0.3));
}

void StandIns::WalkingFactory(FKit& K, const FStandInPalette& P)
{
	// A mobile factory on six legs, 40 m to the roof and 52 m to the top of its stack (board
	// image 8): a worn box of a body with rows of windows, plates, a smokestack and lamps under it.
	FRandomStream Rng(8);
	K.Box(P.PaintDark, FVector(0, 0, 26), FVector(34, 22, 16));
	K.Box(P.Rust, FVector(-4, 0, 35.2), FVector(22, 16, 2.4));
	K.Box(P.Concrete, FVector(8, -4, 37.5), FVector(8, 8, 3));
	K.Cyl(P.Rust, FVector(-8, 4, 44), 1.7, 18);
	K.Cyl(P.SteelDark, FVector(-8, 4, 53.2), 2.0, 0.6);
	K.Box(P.Accent, FVector(0, 0, 18.6), FVector(34.1, 22.1, 0.8));
	for (int32 S = -1; S <= 1; S += 2)
	{
		for (int32 Row = 0; Row < 2; Row++)
			for (int32 I = 0; I < 9; I++)
				K.Box(P.Window, FVector(-14 + I * 3.5, 11.02 * S, 24 + Row * 5), FVector(2.2, 0.08, 1.6));
		K.Greeble(P.Rust, FVector(0, 11 * S, 30.5), FVector(0, S, 0), FVector(15, 0, 0), FVector(0, 0, 1.5), 24, 2.4, Rng);
		K.Greeble(P.SteelDark, FVector(0, 11 * S, 20.5), FVector(0, S, 0), FVector(15, 0, 0), FVector(0, 0, 1.0), 20, 1.8, Rng);
	}
	K.Greeble(P.Steel, FVector(17, 0, 26), FVector(1, 0, 0), FVector(0, 9, 0), FVector(0, 0, 6), 22, 2.2, Rng);
	for (int32 I = 0; I < 8; I++)
		K.Box(P.Steel, FVector(-14 + I * 4, 10.8, 37.4), FVector(0.08, 0.08, 1.2));
	// Six legs, splayed: hip, thigh with a piston, shin, foot.
	for (int32 I = -1; I <= 1; I++)
		for (int32 S = -1; S <= 1; S += 2)
		{
			const double X = 12 * I;
			K.Box(P.SteelDark, FVector(X, 11.6 * S, 19.2), FVector(3.4, 3.4, 3.4));
			K.Box(P.Steel, FVector(X, 15.6 * S, 15.8), FVector(2.2, 2.2, 10.5), FRotator(0, 0, 40 * S));
			K.Cyl(P.Steel, FVector(X + 1.4, 14.6 * S, 14.6), 0.35, 8, FRotator(0, 0, 32 * S));
			K.Box(P.SteelDark, FVector(X, 19.6 * S, 6.6), FVector(1.8, 1.8, 12.6), FRotator(0, 0, -6 * S));
			K.Box(P.Rust, FVector(X, 20.3 * S, 0.45), FVector(4.2, 4.2, 0.9));
			K.Box(P.Lamp, FVector(X, 9 * S, 17.9), FVector(1.2, 0.4, 0.15));
		}
}

void StandIns::Megastructure(FKit& K, const FStandInPalette& P)
{
	// A brutalist industrial mass, 1.6 km long and 340 m tall (board images 21-26): few large
	// planes, rows of openings, vertical fins, one orange accent band, small lights for scale.
	FRandomStream Rng(21);
	K.Box(P.Concrete, FVector(0, 0, 150), FVector(420, 1600, 300));
	K.Box(P.ConcreteDark, FVector(40, -250, 330), FVector(300, 900, 80));
	K.Box(P.Concrete, FVector(-30, 520, 360), FVector(260, 260, 140));
	K.Box(P.ConcreteDark, FVector(-260, -500, 250), FVector(100, 380, 60)); // a cantilevered gallery
	const double Face = -210;
	for (int32 Row = 0; Row < 6; Row++)
		for (int32 I = 0; I < 40; I++)
			K.Box(P.Window, FVector(Face - 1, -780 + I * 40, 52 + Row * 30), FVector(3, 26, 11));
	for (int32 I = 0; I < 17; I++)
		K.Box(P.ConcreteDark, FVector(Face - 12, -800 + I * 100, 150), FVector(24, 9, 300));
	K.Box(P.Accent, FVector(Face - 2, 0, 232), FVector(4, 1600, 14));
	K.Box(P.Accent, FVector(Face - 2.5, 560, 300), FVector(4, 90, 64));
	K.Box(P.Concrete, FVector(Face - 3, 560, 300), FVector(4, 50, 24));
	for (int32 I = 0; I < 48; I++)
		K.Box(P.Lamp, FVector(Face - 4.5, -780 + I * 33, 241), FVector(1, 6, 3));
	for (int32 I = 0; I < 6; I++)
		K.Cyl(P.Steel, FVector(Rng.FRandRange(-150, 150), Rng.FRandRange(-700, 700), 320), 1.2, 45);
	K.Greeble(P.ConcreteDark, FVector(0, 0, 300), FVector(0, 0, 1), FVector(180, 0, 0), FVector(0, 760, 0), 60, 40, Rng);
}
