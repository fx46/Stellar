#include "Terrain/RockScatter.h"
#include "Stellar.h"
#include "Terrain/PlanetTerrainActor.h"
#include "ProceduralMeshComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

using Stellar::UU;

namespace
{
	constexpr int32 ShapeCount = 8;
	/** Tiles built per frame, so walking into new ground never hitches. */
	constexpr int32 TilesPerFrame = 6;

}

ARockScatter::ARockScatter()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("PlanetCentre"));
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;
}

void ARockScatter::Setup(APlanetTerrainActor* InTerrain, UMaterialInterface* InMaterial)
{
	Terrain = InTerrain;
	Material = InMaterial;
	SetActorLocationAndRotation(Terrain->GetActorLocation(), Terrain->GetActorQuat());
	EditHandle = Terrain->OnEdited.AddUObject(this, &ARockScatter::OnTerrainEdited);

	// A few lumpy rock shapes: a subdivided icosahedron, displaced by noise, unit radius.
	FPerlin Noise(77);
	for (int32 S = 0; S < ShapeCount; S++)
	{
		FShape Shape;
		const double T = (1.0 + FMath::Sqrt(5.0)) / 2.0;
		TArray<FVector3d> V = { { -1, T, 0 }, { 1, T, 0 }, { -1, -T, 0 }, { 1, -T, 0 }, { 0, -1, T }, { 0, 1, T }, { 0, -1, -T }, { 0, 1, -T },
			{ T, 0, -1 }, { T, 0, 1 }, { -T, 0, -1 }, { -T, 0, 1 } };
		TArray<int32> F = { 0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
			3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1 };
		for (FVector3d& P : V)
			P.Normalize();
		for (int32 Level = 0; Level < 2; Level++)
		{
			TMap<TPair<int32, int32>, int32> Mid;
			auto Middle = [&](int32 A, int32 B)
			{
				const TPair<int32, int32> Key(FMath::Min(A, B), FMath::Max(A, B));
				if (const int32* Found = Mid.Find(Key))
					return *Found;
				const int32 I = V.Add(((V[A] + V[B]) * 0.5).GetSafeNormal());
				Mid.Add(Key, I);
				return I;
			};
			TArray<int32> Next;
			for (int32 I = 0; I < F.Num(); I += 3)
			{
				const int32 A = F[I], B = F[I + 1], C = F[I + 2];
				const int32 AB = Middle(A, B), BC = Middle(B, C), CA = Middle(C, A);
				Next.Append({ A, AB, CA, B, BC, AB, C, CA, BC, AB, BC, CA });
			}
			F = MoveTemp(Next);
		}
		const double O = S * 17.3;
		for (const FVector3d& P : V)
		{
			const double R = 1.0 + 0.28 * Noise.Sample(P.X * 1.3 + O, P.Y * 1.3, P.Z * 1.3, 0) + 0.09 * Noise.Sample(P.X * 3.7, P.Y * 3.7 + O, P.Z * 3.7, 1);
			Shape.Vertices.Add(FVector3f(P * R));
		}
		Shape.Normals.SetNumZeroed(Shape.Vertices.Num());
		for (int32 I = 0; I < F.Num(); I += 3)
		{
			int32 A = F[I], B = F[I + 1], C = F[I + 2];
			const FVector3f& Pa = Shape.Vertices[A];
			FVector3f N = FVector3f::CrossProduct(Shape.Vertices[B] - Pa, Shape.Vertices[C] - Pa);
			// Unreal's front faces: the algebraic normal points inward (as the terrain mesher's).
			if (N.Dot(Pa + Shape.Vertices[B] + Shape.Vertices[C]) > 0)
			{
				Swap(B, C);
				N = -N;
			}
			Shape.Indices.Append({ A, B, C });
			// Outward normals for lighting, area-weighted.
			Shape.Normals[A] -= N;
			Shape.Normals[B] -= N;
			Shape.Normals[C] -= N;
		}
		for (FVector3f& N : Shape.Normals)
			N.Normalize();
		Shapes.Add(MoveTemp(Shape));
	}
}

void ARockScatter::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Terrain != nullptr)
		Terrain->OnEdited.Remove(EditHandle);
	Super::EndPlay(Reason);
}

void ARockScatter::OnTerrainEdited(const FEditResult& Result)
{
	if (!Result.bChanged)
		return;
	const FBox3d Box = Result.Changed.ExpandBy(3.0);
	for (const TPair<FIntVector, FTile>& Pair : Tiles)
	{
		const FVector3d Lo = FVector3d(Pair.Key) * TileSize;
		if (Box.Intersect(FBox3d(Lo, Lo + FVector3d(TileSize))))
			Dirty.Add(Pair.Key);
	}
}

UProceduralMeshComponent* ARockScatter::Acquire()
{
	if (Pool.Num() > 0)
		return Pool.Pop();
	UProceduralMeshComponent* C = NewObject<UProceduralMeshComponent>(this);
	C->SetupAttachment(Root);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(true);
	C->RegisterComponent();
	return C;
}

void ARockScatter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Terrain == nullptr || !Terrain->HasTerrain())
		return;
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (PC == nullptr || PC->PlayerCameraManager == nullptr)
		return;
	const FVector3d Cam = Terrain->WorldToPlanet(PC->PlayerCameraManager->GetCameraLocation());

	// Which tiles: those within reach that the ground can cross (the density at the tile's centre
	// is a height, so within half a diagonal of zero). Recomputed when the camera has moved.
	if (FVector3d::Dist(Cam, WantedFor) > 8.0)
	{
		WantedFor = Cam;
		Wanted.Reset();
		const FIntVector C(int32(FMath::FloorToDouble(Cam.X / TileSize)), int32(FMath::FloorToDouble(Cam.Y / TileSize)), int32(FMath::FloorToDouble(Cam.Z / TileSize)));
		const int32 N = int32(FMath::CeilToDouble(Reach / TileSize));
		for (int32 X = -N; X <= N; X++)
			for (int32 Y = -N; Y <= N; Y++)
				for (int32 Z = -N; Z <= N; Z++)
				{
					const FIntVector Key = C + FIntVector(X, Y, Z);
					const FVector3d Centre = (FVector3d(Key) + FVector3d(0.5)) * TileSize;
					if (FVector3d::Dist(Centre, Cam) > Reach)
						continue;
					if (FMath::Abs(Terrain->Density(Centre)) > TileSize * 0.9)
						continue;
					Wanted.Add(Key);
				}
		// Nearest first.
		Wanted.Sort([&Cam](const FIntVector& A, const FIntVector& B)
		{
			return FVector3d::DistSquared((FVector3d(A) + 0.5) * TileSize, Cam) < FVector3d::DistSquared((FVector3d(B) + 0.5) * TileSize, Cam);
		});
		// Release tiles well out of reach.
		TArray<FIntVector> Gone;
		for (const TPair<FIntVector, FTile>& Pair : Tiles)
			if (FVector3d::Dist((FVector3d(Pair.Key) + 0.5) * TileSize, Cam) > Reach * 1.3)
				Gone.Add(Pair.Key);
		for (const FIntVector& K : Gone)
		{
			FTile& T = Tiles[K];
			T.Component->ClearAllMeshSections();
			Rocks -= T.Rocks;
			Pool.Add(T.Component);
			Tiles.Remove(K);
		}
	}

	int32 Built = 0;
	for (const FIntVector& Key : Wanted)
	{
		if (Built >= TilesPerFrame)
			break;
		FTile* T = Tiles.Find(Key);
		if (T != nullptr && !Dirty.Contains(Key))
			continue;
		if (T == nullptr)
		{
			T = &Tiles.Add(Key);
			T->Component = Acquire();
		}
		Rocks -= T->Rocks;
		BuildTile(Key, T->Component, T->Rocks);
		Rocks += T->Rocks;
		Dirty.Remove(Key);
		Built++;
	}
	// Dirty tiles out of the wanted set are rebuilt when they are wanted again.
	for (auto It = Dirty.CreateIterator(); It; ++It)
		if (!Tiles.Contains(*It))
			It.RemoveCurrent();
}

void ARockScatter::BuildTile(const FIntVector& Key, UProceduralMeshComponent* C, int32& OutRocks)
{
	const FPlanetTerrain& T = Terrain->GetTerrain();
	const FTerrainEdits* Edits = Terrain->GetEdits();
	const FVector3d Origin = FVector3d(Key) * TileSize;
	const uint32 Seed = HashCombineFast(HashCombineFast(GetTypeHash(Key.X), GetTypeHash(Key.Y)), GetTypeHash(Key.Z));
	FRandomStream Rng{ int32(Seed) };

	const FVector3d Mid = Origin + FVector3d(TileSize * 0.5);
	const FVector3d MidUp = Mid.GetSafeNormal();
	const FHeightLayers L = T.Field.Layers(MidUp.X * T.Field.GetRadius(), MidUp.Y * T.Field.GetRadius(), MidUp.Z * T.Field.GetRadius());
	// More and bigger rock near outcrops and in the ranges' foothills; a scatter everywhere else.
	const double Rocky = FMath::Clamp(L.Outcrop / 6.0 + L.RangeMask, 0.0, 1.0);
	const int32 Count = 7 + Rng.RandRange(0, 6) + int32(Rocky * 22);

	TArray<FVector> Vertices, Normals;
	TArray<int32> Triangles;
	TArray<FColor> Colors;
	OutRocks = 0;
	for (int32 I = 0; I < Count; I++)
	{
		const FVector3d Try = Origin + FVector3d(Rng.FRand(), Rng.FRand(), Rng.FRand()) * TileSize;
		const double Kind = Rng.FRand();
		const double Size = Kind < 0.7 ? Rng.FRandRange(0.12, 0.4) : Kind < 0.95 - Rocky * 0.1 ? Rng.FRandRange(0.4, 1.0) : Rng.FRandRange(1.2, 2.8);
		const FVector3d Up = Try.GetSafeNormal();
		FVector3d Hit;
		if (!T.Trace(Try + Up * 30.0, -Up, 60.0, Edits, Hit))
			continue;
		// Each ground point belongs to one tile.
		if (FIntVector(int32(FMath::FloorToDouble(Hit.X / TileSize)), int32(FMath::FloorToDouble(Hit.Y / TileSize)), int32(FMath::FloorToDouble(Hit.Z / TileSize))) != Key)
			continue;
		const FVector3d N = T.Normal(Hit, Edits, 0.5);
		if (N.Dot(Up) < 0.8)
			continue;
		if (Edits != nullptr && !Edits->IsEmpty() && Edits->Touches(FBox3d(Hit, Hit).ExpandBy(Size + 1.0)))
			continue;
		if (Clearings.ContainsByPredicate([&Hit](const TPair<FVector3d, double>& C) { return FVector3d::DistSquared(Hit, C.Key) < C.Value * C.Value; }))
			continue;

		// Colour: the ground's, a little darker and greyer - bare stone.
		const FHeightLayers Here = T.Field.Layers(Up.X * T.Field.GetRadius(), Up.Y * T.Field.GetRadius(), Up.Z * T.Field.GetRadius());
		FLinearColor Stone = FMath::Lerp(T.Field.SurfaceColor(Here, Hit.Length() - T.Field.GetRadius(), 1.f), T.Field.CliffColor(Hit, Here, 0.25), 0.55f);
		Stone *= Rng.FRandRange(0.7f, 0.95f);
		FColor Col = Stone.ToFColor(true);
		Col.A = 235;

		const FShape& Shape = Shapes[Rng.RandRange(0, ShapeCount - 1)];
		// Sizes are diameters; the shapes have unit radius.
		const FVector3d Scale = FVector3d(Rng.FRandRange(0.8, 1.3), Rng.FRandRange(0.8, 1.3), Rng.FRandRange(0.45, 0.85)) * (Size * 0.5);
		// Frame: up along the ground's normal, a random turn about it.
		const FVector3d Side = (FMath::Abs(N.Z) < 0.9 ? FVector3d(0, 0, 1) : FVector3d(1, 0, 0)).Cross(N).GetSafeNormal();
		const double Turn = Rng.FRandRange(0, UE_DOUBLE_PI * 2);
		const FVector3d Ax = Side * FMath::Cos(Turn) + N.Cross(Side) * FMath::Sin(Turn);
		const FVector3d Ay = N.Cross(Ax);
		const FVector3d Centre = Hit + N * (Scale.Z * 0.35);
		const int32 Base = Vertices.Num();
		for (int32 V = 0; V < Shape.Vertices.Num(); V++)
		{
			const FVector3f& S = Shape.Vertices[V];
			const FVector3d P = Centre + Ax * (S.X * Scale.X) + Ay * (S.Y * Scale.Y) + N * (S.Z * Scale.Z);
			const FVector3f& SN = Shape.Normals[V];
			const FVector3d PN = (Ax * (SN.X / Scale.X) + Ay * (SN.Y / Scale.Y) + N * (SN.Z / Scale.Z)).GetSafeNormal();
			Vertices.Add(FVector(P - Origin) * UU);
			Normals.Add(FVector(PN));
			Colors.Add(Col);
		}
		// The frame (Ax, Ay, N) is right-handed in the algebra, as the shape's was: windings hold.
		for (int32 Index : Shape.Indices)
			Triangles.Add(Base + Index);
		OutRocks++;
	}
	C->SetRelativeLocation(Origin * UU);
	if (Triangles.Num() > 0)
	{
		C->CreateMeshSection(0, Vertices, Triangles, Normals, TArray<FVector2D>(), Colors, TArray<FProcMeshTangent>(), false);
		C->SetMaterial(0, Material);
		C->SetCustomPrimitiveDataVector3(0, Stellar::WrapPosition(Origin));
	}
	else
		C->ClearAllMeshSections();
}
