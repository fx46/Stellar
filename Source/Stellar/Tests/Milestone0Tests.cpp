// Milestone 0 regression tests: edit kinds and storage, precision at 1000 km, structures, the
// test site, and the fluid spike's two representations (which also log the measurements the
// spike's write-up quotes). Run with the rest: Automation RunTests Stellar

#include "Misc/AutomationTest.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"
#include "HAL/PlatformTime.h"
#include "Math/RandomStream.h"
#include "Stellar.h"
#include "Terrain/PlanetTerrain.h"
#include "Terrain/TerrainEdits.h"
#include "Terrain/FluidVolume.h"
#include "Game/Structures.h"
#include "Game/TestSite.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEditKinds, "Stellar.Terrain.EditKinds", TestFlags)
bool FStellarEditKinds::RunTest(const FString&)
{
	const FPlanetTerrain T(1000000.0);
	const FVector3d Up = FVector3d(0.3, 0.3, 1).GetSafeNormal();
	const FVector3d Ground = T.SurfaceAt(Up);
	const FVector3d Fwd = Up.Cross(FVector3d(0, 1, 0)).GetSafeNormal();
	const FVector3d Side = Up.Cross(Fwd);
	const TSharedRef<const FTerrainEdits> Empty = MakeShared<FTerrainEdits>(T.LeafStride());

	// Flatten: a pad at the ground's height is level to the plane in its core.
	FEditResult R;
	const TSharedRef<const FTerrainEdits> Pad = Empty->Apply(FEditOp::Flatten(Ground, Up, Fwd, 10, 10, 4, 0, 2), T.Field, R);
	double Worst = 0;
	for (int32 I = -6; I <= 6; I += 2)
		for (int32 J = -6; J <= 6; J += 2)
		{
			const FVector3d Column = Ground + Fwd * I + Side * J;
			FVector3d Hit;
			if (T.Trace(Column + Up * 5, -Up, 10, &Pad.Get(), Hit))
				Worst = FMath::Max(Worst, FMath::Abs((Hit - Ground).Dot(Up)));
		}
	TestTrue(FString::Printf(TEXT("pad level within 5 cm (%.3f m)"), Worst), Worst < 0.05);

	// A tilted flatten is a ramp: the ground 10 m along it is 2 m lower at slope -0.2.
	FEditResult R2;
	const TSharedRef<const FTerrainEdits> Ramp = Empty->Apply(FEditOp::Flatten(Ground, Up, Fwd, 20, 4, 8, -0.2, 2), T.Field, R2);
	FVector3d Hit;
	TestTrue(TEXT("ramp has ground"), T.Trace(Ground + Fwd * 10 + Up * 6, -Up, 15, &Ramp.Get(), Hit));
	TestTrue(FString::Printf(TEXT("ramp drops 2 m over 10 m (%.2f)"), (Hit - Ground).Dot(Up)), FMath::Abs((Hit - Ground).Dot(Up) + 2.0) < 0.15);

	// Carve: a cavity 10 m down is air inside, rock above it.
	FEditResult R3;
	const FVector3d Cave = Ground - Up * 10;
	const TSharedRef<const FTerrainEdits> Carved = Empty->Apply(FEditOp::Carve(Cave, 3), T.Field, R3);
	TestTrue(TEXT("cavity is air"), T.Density(Cave, &Carved.Get()) > 2.5);
	TestTrue(TEXT("its roof is rock"), T.Density(Ground - Up * 5, &Carved.Get()) < 0);
	double Removed = 0;
	for (double V : R3.Volume)
		Removed += V;
	const double Sphere = 4.0 / 3.0 * UE_DOUBLE_PI * 27;
	TestTrue(FString::Printf(TEXT("removed volume near the sphere's (%.1f of %.1f m3)"), Removed, Sphere), FMath::Abs(Removed - Sphere) < Sphere * 0.08);

	// Fill: raised ground is fill in the material channel; dug ground is natural.
	FEditResult R4;
	const TSharedRef<const FTerrainEdits> Filled = Empty->Apply(FEditOp::Fill(Ground + Up * 1.5, 2), T.Field, R4);
	TestEqual(TEXT("built ground is fill"), int32(Filled->MaterialAt(Ground + Up * 1.2)), int32(EEditMaterial::Fill));
	TestEqual(TEXT("rock below stays natural"), int32(Filled->MaterialAt(Ground - Up * 3)), int32(EEditMaterial::Natural));
	TestEqual(TEXT("rock at the fill is reported as fill"), int32(T.RockAt(Ground + Up * 1.2, &Filled.Get()).Material), int32(EGeoMaterial::Fill));

	// Narrow band: grading stores bricks near the surface, not the whole box - a pad graded with
	// 20 m of headroom stores about what one with 4 m does.
	FEditResult R5;
	const TSharedRef<const FTerrainEdits> Tall = Empty->Apply(FEditOp::Flatten(Ground, Up, Fwd, 10, 10, 20, 0, 2), T.Field, R5);
	TestTrue(FString::Printf(TEXT("a tall grading box stores only near-surface bricks (%d, against %d for a shallow one)"), Tall->NumBricks(),
		Pad->NumBricks()), Tall->NumBricks() <= Pad->NumBricks() * 1.6);

	// Format: the material channel saves and loads; a version 1 brick (offsets only) still loads.
	FBufferArchive Out;
	FTerrainEdits Copy = *Filled;
	Copy.Serialize(Out);
	FMemoryReader In(Out);
	FTerrainEdits Loaded(0);
	Loaded.Serialize(In);
	TestEqual(TEXT("material channel reloads"), int32(Loaded.MaterialAt(Ground + Up * 1.2)), int32(EEditMaterial::Fill));
	FBufferArchive V1;
	int32 FileVersion = 1, Num = 1;
	double Spacing = 0.25;
	uint32 Version = 7, BrickVersion = 7;
	FIntVector Key(3, 4, 5);
	TArray<float> Delta;
	Delta.Init(0.5f, FEditBrick::Count);
	V1 << FileVersion << Spacing << Version << Num << Key << BrickVersion;
	V1.Serialize(Delta.GetData(), Delta.Num() * sizeof(float));
	FMemoryReader In1(V1);
	FTerrainEdits Old(0);
	Old.Serialize(In1);
	TestFalse(TEXT("version 1 loads"), In1.IsError());
	TestEqual(TEXT("version 1 offsets"), Old.Delta(3 * 16 * 0.25 + 1, 4 * 16 * 0.25 + 1, 5 * 16 * 0.25 + 1), 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarPrecision, "Stellar.Terrain.PrecisionAt1000km", TestFlags)
bool FStellarPrecision::RunTest(const FString&)
{
	// 1000 km out, the density is the same to 0.3 mm however a point is split into a node origin
	// (doubles) and a node-local float offset, at every level of the octree.
	const FPlanetTerrain T(1000000.0);
	FRandomStream Rng(11);
	double Worst = 0;
	for (int32 I = 0; I < 2000; I++)
	{
		const FVector3d U = FVector3d(Rng.FRandRange(-1, 1), Rng.FRandRange(-1, 1), Rng.FRandRange(-1, 1)).GetSafeNormal();
		const FVector3d P = T.SurfaceAt(U) + U * Rng.FRandRange(-3, 3);
		const int32 Level = Rng.RandRange(0, 8);
		const double Size = T.Octree.Size(Level);
		const FVector3d Min(T.Octree.Half * -1 + FMath::FloorToDouble((P.X + T.Octree.Half) / Size) * Size,
			T.Octree.Half * -1 + FMath::FloorToDouble((P.Y + T.Octree.Half) / Size) * Size, T.Octree.Half * -1 + FMath::FloorToDouble((P.Z + T.Octree.Half) / Size) * Size);
		const FVector3d Local = P - Min;
		const double Split = T.Field.Density(Min.X, Min.Y, Min.Z, float(Local.X), float(Local.Y), float(Local.Z));
		// The float offset itself rounds: compare against the point it actually names.
		const FVector3d Named = Min + FVector3d(float(Local.X), float(Local.Y), float(Local.Z));
		Worst = FMath::Max(Worst, FMath::Abs(Split - T.Field.Density(Named)));
	}
	TestTrue(FString::Printf(TEXT("split density within 0.3 mm (worst %.4f mm)"), Worst * 1000), Worst < 3e-4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarStructures, "Stellar.Game.Structures", TestFlags)
bool FStellarStructures::RunTest(const FString&)
{
	FStructureSet S;
	const FVector3d Up(0, 0, 1), Fwd(1, 0, 0);
	const uint32 A = S.Add(EStructureType::FoundationPad, FVector3d(0, 0, 100), Up, Fwd);
	const uint32 B = S.Add(EStructureType::Depot, FVector3d(20, 0, 100), Up, Fwd);
	TestTrue(TEXT("ids are distinct"), A != B && A != 0 && B != 0);
	TestTrue(TEXT("a pad beside the first overlaps"), S.Overlaps(EStructureType::FoundationPad, FVector3d(5, 0, 100), Fwd));
	TestFalse(TEXT("a pad 40 m away does not"), S.Overlaps(EStructureType::FoundationPad, FVector3d(-40, 0, 100), Fwd));
	TestEqual(TEXT("found on its footprint"), S.At(FVector3d(3, 3, 100.3)) != nullptr ? S.At(FVector3d(3, 3, 100.3))->Id : 0u, A);
	TestTrue(TEXT("removed"), S.Remove(A) && S.Find(A) == nullptr);
	const uint32 C = S.Add(EStructureType::Beacon, FVector3d(0, 50, 100), Up, Fwd);
	TestTrue(TEXT("ids are never reused"), C > B);
	FBufferArchive Out;
	S.Serialize(Out);
	FMemoryReader In(Out);
	FStructureSet L;
	L.Serialize(In);
	TestEqual(TEXT("structures reload"), L.All().Num(), 2);
	TestTrue(TEXT("with their ids, bodies and frames"), L.Find(C) != nullptr && L.Find(C)->Position == FVector3d(0, 50, 100) && L.Find(C)->BodyId == 0);
	TestTrue(TEXT("and keep issuing fresh ids"), L.Add(EStructureType::Beacon, FVector3d::ZeroVector, Up, Fwd) > C);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarTestSite, "Stellar.Game.TestSite", TestFlags)
bool FStellarTestSite::RunTest(const FString&)
{
	FPlanetTerrain T(1000000.0);
	const FSiteChoice Site = ChooseStartSite(T);
	const FTestSitePlan S = FTestSitePlan::Build(T, Site.Surface, Site.Up, Site.Forward);
	T.Field.SetDeposits(S.Deposits);
	const double T0 = FPlatformTime::Seconds();
	FEditResult R;
	const TSharedRef<const FTerrainEdits> E = MakeShared<FTerrainEdits>(T.LeafStride())->ApplyAll(S.Ops, T.Field, R);
	UE_LOG(LogStellar, Log, TEXT("Test site: %d ops in %.0f ms, %d bricks (%.1f MB)"), S.Ops.Num(), (FPlatformTime::Seconds() - T0) * 1000,
		E->NumBricks(), E->BrickBytes() / (1024.0 * 1024.0));

	auto Ground = [&](const FVector3d& Above) { FVector3d Hit; return T.Trace(Above, -S.Up, 40, &E.Get(), Hit) ? (Hit - S.Centre).Dot(S.Up) : 1e9; };
	double Yard = 0;
	for (int32 I = -25; I <= 25; I += 10)
		for (int32 J = -15; J <= 15; J += 10)
			Yard = FMath::Max(Yard, FMath::Abs(Ground(S.At(I, J, 6))));
	TestTrue(FString::Printf(TEXT("the yard is level (%.2f m)"), Yard), Yard < 0.1);
	const double Pit = (S.RampBottom - S.Centre).Dot(S.Up);
	TestTrue(FString::Printf(TEXT("the ramp's middle is halfway down (%.2f of %.2f m)"), Ground(S.At(0, -57, 6)), Pit / 2), FMath::Abs(Ground(S.At(0, -57, 6)) - Pit / 2) < 0.3);
	TestTrue(FString::Printf(TEXT("the pit is at the ramp's foot (%.2f m)"), Pit), FMath::Abs(Ground(S.At(0, -100, 6)) - Pit) < 0.3 && Pit <= -14);
	TestTrue(TEXT("the chamber is open"), T.Density(S.Chamber + S.Up * 3, &E.Get()) > 1);
	TestTrue(TEXT("under a rock roof"), T.Density(S.Chamber + S.Up * 13, &E.Get()) < 0);
	TestTrue(TEXT("the pocket is sealed from the ramp"), T.Density(S.Pocket + S.Forward * 4.0, &E.Get()) < 0);
	TestTrue(TEXT("the pocket is open inside"), T.Density(S.Pocket, &E.Get()) > 1);
	TestTrue(TEXT("the basin is dug"), T.Density(S.Basin, &E.Get()) > 0);
	TestEqual(TEXT("deposit A is iron"), int32(T.RockAt(S.Deposits[0].Center, &E.Get()).Material), int32(EGeoMaterial::IronOre));
	TestEqual(TEXT("deposit B is copper"), int32(T.RockAt(S.Deposits[1].Center, &E.Get()).Material), int32(EGeoMaterial::CopperOre));
	// Iron sits in sandstone, copper in basalt: what the procedural geology is just beside them.
	auto Host = [&](const FDeposit& D) { const FVector3d P = D.Center + S.Forward * (D.Radius + 0.75); return T.Field.Geology(P, -T.Field.Density(P)).Material; };
	TestEqual(TEXT("deposit A's host is sandstone"), int32(Host(S.Deposits[0])), int32(EGeoMaterial::Sandstone));
	TestEqual(TEXT("deposit B's host is basalt"), int32(Host(S.Deposits[1])), int32(EGeoMaterial::Basalt));
	return true;
}

namespace
{
	/** A flat test world in a region frame: rock below z = Floor, plus extra shapes. */
	struct FBoxWorld
	{
		FFluidFrame F;
		TFunction<double(const FVector3d&)> Extra;
		double Floor = 2;
		double operator()(const FVector3d& P) const
		{
			const FVector3d L = F.ToLocal(P);
			double D = L.Z - Floor;
			if (Extra)
				D = Extra(L) < D ? Extra(L) : D;
			return D;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarFluidCellular, "Stellar.Fluid.Cellular", TestFlags)
bool FStellarFluidCellular::RunTest(const FString&)
{
	// A 24 x 24 x 12 m region at 0.5 m cells, ground at 2 m, with a basin dug in it.
	const FFluidFrame F = FFluidFrame::Around(FVector3d(0, 0, 1000000), FVector3d(0, 0, 1), FVector3d(1, 0, 0), FVector3d(24, 24, 12), 0.5);
	FBoxWorld World;
	World.F = F;
	World.Floor = 4;
	// Basin: a 4 m radius bowl 2 m deep at (8, 12); a sealed pocket at (18, 12, 2.5).
	auto Shapes = [](const FVector3d& L)
	{
		const double Bowl = -((FVector3d(L.X, L.Y, L.Z) - FVector3d(8, 12, 6)).Length() - 4.5); // carve: air inside
		const double Pocket = -((L - FVector3d(18, 12, 2)).Length() - 1.5);
		return FMath::Max(Bowl, Pocket);
	};
	auto Density = [&](const FVector3d& P) { const FVector3d L = F.ToLocal(P); return FMath::Max(L.Z - 4, Shapes(L)); };
	FFluidVolume V(F);
	V.RebuildSolid(Density);

	// Pour 20 m3 into the basin: it settles, conserves its volume, and costs nothing once settled.
	const double Cell = 0.125;
	const FVector3d Source = F.FromLocal(FVector3d(8, 12, 5));
	double Poured = 0;
	int32 Steps = 0;
	double Busy = 0;
	while (Steps < 6000)
	{
		if (Poured < 20)
		{
			V.AddAt(Source, float(0.25 / Cell));
			Poured += 0.25;
		}
		const FFluidStats St = V.Step();
		Busy += St.StepMs;
		Steps++;
		if (Poured >= 20 && V.IsSettled())
			break;
	}
	TestTrue(FString::Printf(TEXT("the basin settled (%d steps, %.1f ms total)"), Steps, Busy), V.IsSettled());
	TestTrue(FString::Printf(TEXT("volume conserved (%.4f of %.1f m3)"), V.CubicMetres(), Poured), FMath::Abs(V.CubicMetres() - Poured) < 0.01);
	const double T0 = FPlatformTime::Seconds();
	for (int32 I = 0; I < 1000; I++)
		V.Step();
	const double Idle = (FPlatformTime::Seconds() - T0) * 1000;
	TestTrue(FString::Printf(TEXT("a settled region costs nothing (1000 steps in %.3f ms)"), Idle), Idle < 5 && V.NumActive() == 0);
	// The water stays in the basin: none above the plain's level outside it.
	float Outside = 0;
	for (int32 J = 0; J < F.Ny; J++)
		for (int32 I = 0; I < F.Nx; I++)
			if (FVector3d::Dist(FVector3d((I + 0.5) * 0.5, (J + 0.5) * 0.5, 0), FVector3d(8, 12, 0)) > 5)
				for (int32 K = 0; K < F.Nz; K++)
					Outside += V.Mass(I, J, K);
	TestTrue(FString::Printf(TEXT("the water stays in the basin (%.3f cells outside)"), Outside), Outside < 0.5);

	// A sealed pocket filled with water stays put; breaching it into a trench drains it there.
	V.FillSphere(F.FromLocal(FVector3d(18, 12, 2)), 1.5);
	const double Before = V.CubicMetres();
	for (int32 I = 0; I < 400; I++)
		V.Step();
	TestTrue(TEXT("the sealed pocket holds still"), V.IsSettled());
	// Breach: a trench open to the sky, floored at 2.4 m, from the pocket to a sump 2 m lower -
	// what digging into the pocket from a cut beside it does.
	auto Breached = [&](const FVector3d& P)
	{
		const FVector3d L = F.ToLocal(P);
		const bool bTrench = L.Y > 11 && L.Y < 13 && L.X > 17.5 && L.X < 21 && L.Z > 2.4;
		const bool bSump = L.Y > 10 && L.Y < 14 && L.X >= 21 && L.X < 23.5 && L.Z > 0.2;
		return bTrench || bSump ? 1.0 : Density(P);
	};
	const FBox3d Box(F.FromLocal(FVector3d(17.5, 10, 0.2)), F.FromLocal(FVector3d(23.5, 14, 12)));
	V.RebuildSolid(Breached, &Box);
	int32 Drain = 0;
	while (!V.IsSettled() && Drain < 8000)
	{
		V.Step();
		Drain++;
	}
	float InPocket = 0;
	for (int32 K = 0; K < F.Nz; K++)
		for (int32 J = 0; J < F.Ny; J++)
			for (int32 I = 0; I < F.Nx; I++)
				if (FVector3d::Dist(F.CellCentre(I, J, K), F.FromLocal(FVector3d(18, 12, 2))) < 1.5 && (K + 0.5) * 0.5 > 2.4)
					InPocket += V.Mass(I, J, K);
	TestTrue(FString::Printf(TEXT("the breach drained the pocket's top (%.2f cells left above the trench floor, %d steps)"), InPocket, Drain), InPocket < 1.0);
	TestTrue(TEXT("volume conserved through the breach"), FMath::Abs(V.CubicMetres() - Before) < 0.02);
	UE_LOG(LogStellar, Log, TEXT("Fluid A (cellular): %d cells, %.1f MB; basin of 20 m3 settled in %d steps (%.1f ms total, %.3f ms/step); settled cost %.4f ms/step"),
		F.Num(), V.Bytes() / (1024.0 * 1024.0), Steps, Busy, Busy / FMath::Max(1, Steps), Idle / 1000);

	// Saved and reloaded: the same water.
	FBufferArchive Out;
	V.Serialize(Out);
	FMemoryReader In(Out);
	FFluidVolume L;
	L.Serialize(In);
	L.RebuildSolid(Breached);
	TestTrue(TEXT("water reloads"), FMath::Abs(L.CubicMetres() - V.CubicMetres()) < 1e-6);
	UE_LOG(LogStellar, Log, TEXT("Fluid A save: %d bytes for %.1f m3"), Out.Num(), V.CubicMetres());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarFluidHeightfield, "Stellar.Fluid.Heightfield", TestFlags)
bool FStellarFluidHeightfield::RunTest(const FString&)
{
	// The same basin as the cellular test, in a 2.5D water layer.
	const FFluidFrame F = FFluidFrame::Around(FVector3d(0, 0, 1000000), FVector3d(0, 0, 1), FVector3d(1, 0, 0), FVector3d(24, 24, 12), 0.5);
	auto Density = [&](const FVector3d& P)
	{
		const FVector3d L = F.ToLocal(P);
		const double Bowl = -((L - FVector3d(8, 12, 6)).Length() - 4.5);
		const double Pocket = -((L - FVector3d(18, 12, 2)).Length() - 1.5);
		return FMath::Max(L.Z - 4, FMath::Max(Bowl, Pocket));
	};
	FFluidHeightfield H(F);
	H.RebuildGround(Density);
	double Busy = 0;
	int32 Steps = 0;
	for (; Steps < 20000; Steps++)
	{
		if (Steps < 80)
			H.AddAt(F.FromLocal(FVector3d(8, 12, 5)), 0.25);
		const double T0 = FPlatformTime::Seconds();
		const double Change = H.Step(1.0 / 30);
		Busy += (FPlatformTime::Seconds() - T0) * 1000;
		if (Steps >= 80 && Change < 1e-6)
			break;
	}
	TestTrue(FString::Printf(TEXT("volume conserved (%.4f of 20 m3)"), H.CubicMetres()), FMath::Abs(H.CubicMetres() - 20) < 0.01);
	TestTrue(FString::Printf(TEXT("settles (%d steps)"), Steps), Steps < 20000);
	// Its blind spot: the sealed pocket under the plain does not exist for it - the column over
	// the pocket reads the plain's surface, so water can never be put in, or drain from, a cavity.
	TestTrue(TEXT("the pocket is invisible to a heightfield"), FMath::Abs(H.Ground(36, 24) - 4.0) < 0.01);
	UE_LOG(LogStellar, Log, TEXT("Fluid B (heightfield): %d columns, %.2f MB; basin of 20 m3 settled in %d steps (%.1f ms total, %.3f ms/step, every column every step)"),
		F.Nx * F.Ny, H.Bytes() / (1024.0 * 1024.0), Steps, Busy, Busy / FMath::Max(1, Steps));
	return true;
}

#endif

