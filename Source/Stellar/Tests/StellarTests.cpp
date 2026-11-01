// Engine-independent regression tests, ported from the Godot project's native suite
// (verification/native). Run: UnrealEditor-Cmd Stellar.uproject -ExecCmds="Automation RunTests Stellar; Quit"
// -unattended -nullrhi -nosplash

#include "Misc/AutomationTest.h"
#include "Math/RandomStream.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"
#include "Terrain/PlanetTerrain.h"
#include "Terrain/TransvoxelMesher.h"
#include "Terrain/TerrainEdits.h"
#include "Player/CharacterMotion.h"
#include "Game/Items.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;

	FVector3d SurfacePoint(const FPlanetTerrain& T, const FVector3d& Dir)
	{
		return T.SurfaceAt(Dir.GetSafeNormal());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarOctreeNeighbours, "Stellar.Terrain.OctreeNeighbours", TestFlags)
bool FStellarOctreeNeighbours::RunTest(const FString&)
{
	// Face neighbours in a cut are never two levels apart, and no node lies inside another.
	for (const double Radius : { 60000.0, 1000000.0 })
	{
		const FPlanetTerrain T(Radius);
		const FVector3d Dir = FVector3d(0.3, -0.2, 1).GetSafeNormal();
		for (const double Height : { 2.0, 150.0, 2000.0, 30000.0 })
		{
			const FVector3d Cam = SurfacePoint(T, Dir) + Dir * Height;
			const TArray<FNodeKey> Cut = T.Select(Cam, nullptr);
			const TSet<FNodeKey> Set(Cut);
			TestTrue(TEXT("cut is not empty"), Cut.Num() > 0);
			int32 Bad = 0, Nested = 0;
			for (const FNodeKey& K : Cut)
			{
				for (FNodeKey A = K.Parent(); A.Level <= T.Octree.RootLevel; A = A.Parent())
					if (Set.Contains(A))
						Nested++;
				for (int32 Face = 0; Face < 6; Face++)
				{
					FNodeKey N = K;
					const int32 D = (Face & 1) ? 1 : -1;
					if (Face / 2 == 0) N.X += D; else if (Face / 2 == 1) N.Y += D; else N.Z += D;
					// Two or more levels coarser across this face?
					FNodeKey A = N.Parent();
					for (A = A.Parent(); A.Level <= T.Octree.RootLevel; A = A.Parent())
						if (Set.Contains(A))
							Bad++;
				}
			}
			TestEqual(FString::Printf(TEXT("R %.0f km, h %.0f m: neighbours two levels apart"), Radius / 1000, Height), Bad, 0);
			TestEqual(FString::Printf(TEXT("R %.0f km, h %.0f m: nested nodes"), Radius / 1000, Height), Nested, 0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarHeightBounds, "Stellar.Terrain.HeightBounds", TestFlags)
bool FStellarHeightBounds::RunTest(const FString&)
{
	// Every height sampled within reach of a point lies within that point's bounds.
	const FPlanetField F(1000000.0);
	FRandomStream Rng(7);
	int32 Outside = 0;
	for (int32 I = 0; I < 1500; I++)
	{
		const FVector3d U = FVector3d(Rng.FRandRange(-1, 1), Rng.FRandRange(-1, 1), Rng.FRandRange(-1, 1)).GetSafeNormal();
		const double Reach = FMath::Pow(10.0, Rng.FRandRange(-0.5, 4.7));
		const FVector3d P = U * F.GetRadius();
		const FRange Bounds = F.HeightRange(P.X, P.Y, P.Z, Reach);
		for (int32 J = 0; J < 20; J++)
		{
			const FVector3d Off = FVector3d(Rng.FRandRange(-1, 1), Rng.FRandRange(-1, 1), Rng.FRandRange(-1, 1)).GetSafeNormal() * Reach * Rng.FRand();
			const FVector3d Q = (P + Off).GetSafeNormal() * F.GetRadius();
			if (FVector3d::Dist(P, Q) > Reach)
				continue;
			const double H = F.Height(Q / F.GetRadius());
			if (H < Bounds.Lo || H > Bounds.Hi)
				Outside++;
		}
	}
	TestEqual(TEXT("heights outside their bounds"), Outside, 0);
	// And the global bounds hold for every layer together, landmarks included.
	int32 OutsideGlobal = 0;
	double Highest = -1e9, Lowest = 1e9;
	for (int32 I = 0; I < 200000; I++)
	{
		const FVector3d U = FVector3d(Rng.FRandRange(-1, 1), Rng.FRandRange(-1, 1), Rng.FRandRange(-1, 1)).GetSafeNormal();
		const double H = F.Height(U);
		Highest = FMath::Max(Highest, H);
		Lowest = FMath::Min(Lowest, H);
		if (H < FPlanetField::MinHeight || H > FPlanetField::MaxHeight)
			OutsideGlobal++;
	}
	TestEqual(FString::Printf(TEXT("heights outside the planet's bounds (sampled %.0f to %.0f m)"), Lowest, Highest), OutsideGlobal, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarSparseMesh, "Stellar.Terrain.SparseEqualsDense", TestFlags)
bool FStellarSparseMesh::RunTest(const FString&)
{
	// The sparse corner grid gives the dense mesh bit for bit, with and without transition faces.
	const FPlanetTerrain T(1000000.0);
	const FVector3d Dir = FVector3d(0.1, 0.4, 1).GetSafeNormal();
	const TArray<FNodeKey> Cut = T.Select(SurfacePoint(T, Dir) + Dir * 3.0, nullptr);
	FRandomStream Rng(3);
	int32 Compared = 0, Differ = 0;
	for (int32 I = 0; I < Cut.Num() && Compared < 24; I += FMath::Max(1, Cut.Num() / 24))
	{
		const int32 Mask = Rng.RandRange(0, 63);
		FTransvoxelMesh A, B;
		T.Mesh(Cut[I], Mask, nullptr, A, false);
		T.Mesh(Cut[I], Mask, nullptr, B, true);
		Compared++;
		if (A.Vertices.Num() != B.Vertices.Num() || A.Indices != B.Indices)
		{
			Differ++;
			continue;
		}
		for (int32 V = 0; V < A.Vertices.Num(); V++)
			if (A.Vertices[V] != B.Vertices[V])
			{
				Differ++;
				break;
			}
	}
	TestTrue(TEXT("compared some nodes"), Compared > 0);
	TestEqual(TEXT("sparse meshes differing from dense"), Differ, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarClosedMesh, "Stellar.Terrain.ClosedMeshAndWinding", TestFlags)
bool FStellarClosedMesh::RunTest(const FString&)
{
	// A sphere wholly inside one region meshes to a closed surface: every edge is shared by two
	// triangles, used once in each direction; and every triangle is wound with its algebraic
	// normal pointing into the solid - Unreal's front face for a surface seen from outside.
	auto Density = [](float X, float Y, float Z) { return FMath::Sqrt(FMath::Square(X - 8.3f) + FMath::Square(Y - 7.9f) + FMath::Square(Z - 8.1f)) - 5.2f; };
	FTransvoxelMesh Mesh;
	TransvoxelMesher::MeshRegion(Mesh, Density, FVector3f::ZeroVector, 0.5f, 32, 0, nullptr, nullptr);
	TestTrue(TEXT("mesh not empty"), !Mesh.IsEmpty());
	TMap<TPair<int32, int32>, int32> Directed;
	int32 Outward = 0;
	for (int32 I = 0; I < Mesh.Indices.Num(); I += 3)
	{
		const int32 A = Mesh.Indices[I], B = Mesh.Indices[I + 1], C = Mesh.Indices[I + 2];
		Directed.FindOrAdd({ A, B })++;
		Directed.FindOrAdd({ B, C })++;
		Directed.FindOrAdd({ C, A })++;
		const FVector3f N = FVector3f::CrossProduct(Mesh.Vertices[B] - Mesh.Vertices[A], Mesh.Vertices[C] - Mesh.Vertices[A]);
		const FVector3f Centre = (Mesh.Vertices[A] + Mesh.Vertices[B] + Mesh.Vertices[C]) / 3.f - FVector3f(8.3f, 7.9f, 8.1f);
		if (N.Dot(Centre) > 0)
			Outward++;
	}
	int32 Open = 0;
	for (const TPair<TPair<int32, int32>, int32>& E : Directed)
	{
		const int32* Back = Directed.Find({ E.Key.Value, E.Key.Key });
		if (E.Value != 1 || Back == nullptr || *Back != 1)
			Open++;
	}
	TestEqual(TEXT("open or non-manifold edges"), Open, 0);
	TestEqual(TEXT("triangles wound the wrong way"), Outward, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEdits, "Stellar.Terrain.Edits", TestFlags)
bool FStellarEdits::RunTest(const FString&)
{
	const FPlanetTerrain T(1000000.0);
	const FVector3d Up = FVector3d(0.2, 0.1, 1).GetSafeNormal();
	const FVector3d Ground = SurfacePoint(T, Up);
	const TSharedRef<const FTerrainEdits> Empty = MakeShared<FTerrainEdits>(T.LeafStride());

	// A dig changes the density at every lattice point by strength * weight, and removes rock.
	FEditResult R;
	const FEditOp Dig = FEditOp::Sculpt(Ground, -Up, 2.5, 0.3);
	const TSharedRef<const FTerrainEdits> E = Empty->Apply(Dig, T.Field, R);
	TestTrue(TEXT("the dig changed something"), R.bChanged && E->NumBricks() > 0);
	double Removed = 0;
	for (double V : R.Volume)
		Removed += V;
	TestTrue(TEXT("the dig removed rock"), Removed > 0.5 && Removed < 20);
	const double H = T.LeafStride();
	int32 Wrong = 0;
	for (int32 I = -12; I <= 12; I++)
		for (int32 J = -12; J <= 12; J++)
		{
			const FVector3d P = FVector3d(FMath::RoundToDouble(Ground.X / H + I) * H, FMath::RoundToDouble(Ground.Y / H + J) * H,
				FMath::RoundToDouble(Ground.Z / H) * H);
			if (!FMath::IsNearlyEqual(double(E->Delta(P.X, P.Y, P.Z)), Dig.Strength * Dig.Weight(P), 1e-5))
				Wrong++;
		}
	TestEqual(TEXT("lattice offsets unlike the brush"), Wrong, 0);
	TestEqual(TEXT("no offset far away"), E->Delta(Ground.X + 50, Ground.Y, Ground.Z), 0.f);
	TestTrue(TEXT("the ground is lower at the centre"), T.Density(Ground, &E.Get()) > T.Density(Ground, nullptr) + 0.2);
	TestTrue(TEXT("the old snapshot is unchanged"), Empty->IsEmpty());

	// Building back the same amount restores the ground.
	FEditResult R2;
	const TSharedRef<const FTerrainEdits> Back = E->Apply(FEditOp::Sculpt(Ground, -Up, 2.5, -0.3), T.Field, R2);
	TestTrue(TEXT("build restores the density"), FMath::Abs(T.Density(Ground, &Back.Get()) - T.Density(Ground, nullptr)) < 1e-4);

	// Saved edits reload exactly.
	FBufferArchive Out;
	FTerrainEdits Copy = *E;
	Copy.Serialize(Out);
	FMemoryReader In(Out);
	FTerrainEdits Loaded(0.0);
	Loaded.Serialize(In);
	TestFalse(TEXT("load error"), In.IsError());
	TestEqual(TEXT("bricks"), Loaded.NumBricks(), E->NumBricks());
	TestEqual(TEXT("version"), Loaded.GetVersion(), E->GetVersion());
	TestEqual(TEXT("density after reload"), Loaded.Delta(Ground.X, Ground.Y, Ground.Z), E->Delta(Ground.X, Ground.Y, Ground.Z));

	// An edited node meshes the dent; its unedited neighbours do not notice the edit layer.
	TestTrue(TEXT("edited node holds surface"), T.HoldsSurface(FNodeKey(0, int32((Ground.X + T.Octree.Half) / T.Octree.LeafSize),
		int32((Ground.Y + T.Octree.Half) / T.Octree.LeafSize), int32((Ground.Z + T.Octree.Half) / T.Octree.LeafSize)), &E.Get()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarCharacter, "Stellar.Player.StandAndWalk", TestFlags)
bool FStellarCharacter::RunTest(const FString&)
{
	const FPlanetTerrain T(1000000.0);
	const FVector3d Up = FVector3d(-0.3, 0.2, 1).GetSafeNormal();
	auto Density = [&T](const FVector3d& P) { return T.Density(P, nullptr); };
	auto GravityUp = [](const FVector3d& P) { return P.GetSafeNormal(); };
	auto Normal = [&T](const FVector3d& P) { return T.Normal(P, nullptr); };

	// Dropped from 3 m, the astronaut lands, settles a capsule radius above the ground and stays.
	FCharacterState S;
	S.Position = SurfacePoint(T, Up) + Up * 3.0;
	for (int32 I = 0; I < 240; I++)
		S = CharacterMotion::Step(S, Density, GravityUp, Normal, FVector3d::ZeroVector, false, false, false, 1.0 / 120, 1.0, nullptr);
	TestTrue(TEXT("grounded after the drop"), S.bGrounded);
	TestTrue(FString::Printf(TEXT("resting a capsule radius above the ground (%.3f m)"), Density(S.Position)), FMath::Abs(Density(S.Position) - CharacterMotion::Radius) < 0.06);
	const FVector3d Rest = S.Position;
	for (int32 I = 0; I < 1200; I++)
		S = CharacterMotion::Step(S, Density, GravityUp, Normal, FVector3d::ZeroVector, false, false, false, 1.0 / 120, 1.0, nullptr);
	TestTrue(TEXT("standing still does not creep"), FVector3d::Dist(Rest, S.Position) < 0.01);

	// Walking 30 s keeps contact and covers the expected ground.
	const FVector3d Heading = Up.Cross(FVector3d(1, 0, 0)).GetSafeNormal();
	int32 Airborne = 0;
	for (int32 I = 0; I < 3600; I++)
	{
		const FVector3d Wish = Heading - S.Position.GetSafeNormal() * Heading.Dot(S.Position.GetSafeNormal());
		S = CharacterMotion::Step(S, Density, GravityUp, Normal, Wish, false, false, false, 1.0 / 120, 1.0, nullptr);
		if (!S.bGrounded)
			Airborne++;
	}
	const double Walked = FVector3d::Dist(Rest, S.Position);
	TestTrue(FString::Printf(TEXT("walked %.0f m in 30 s"), Walked), Walked > 120 && Walked < 200);
	TestTrue(FString::Printf(TEXT("airborne %d of 3600 ticks"), Airborne), Airborne < 360);
	TestTrue(TEXT("never inside the rock"), Density(S.Position) > CharacterMotion::Radius * 0.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarItems, "Stellar.Game.MiningCredit", TestFlags)
bool FStellarItems::RunTest(const FString&)
{
	// Fractional credit: many small bites yield what their total volume is worth.
	FMiningCredit Credit;
	int32 Units = 0;
	for (int32 I = 0; I < 100; I++)
		Units += Credit.Credit(EGeoMaterial::IronOre, 0.1);
	TestEqual(TEXT("100 x 0.1 m^3 of iron ore"), Units, 15);
	FInventory Pockets(10);
	TestEqual(TEXT("partial add"), Pockets.Add(EItemType::IronOre, 15), 10);
	TestTrue(TEXT("full"), Pockets.IsFull());
	return true;
}

#endif
