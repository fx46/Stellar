#include "Game/StellarAutomation.h"
#include "Stellar.h"
#include "Game/StellarWorld.h"
#include "Game/Structures.h"
#include "Player/AstronautPawn.h"
#include "Terrain/PlanetTerrainActor.h"
#include "Terrain/FluidActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "UnrealClient.h"

namespace
{
	FVector3d Tangent(const FVector3d& V, const FVector3d& Up) { return (V - Up * V.Dot(Up)).GetSafeNormal(); }

	/** The direction Metres along the surface from U, toward T (unit, tangent). */
	FVector3d Along(const FVector3d& U, const FVector3d& T, double Metres, double Radius)
	{
		const double Theta = Metres / Radius;
		return (U * FMath::Cos(Theta) + T * FMath::Sin(Theta)).GetSafeNormal();
	}

	double Percentile(TArray<float> V, double Q)
	{
		if (V.Num() == 0)
			return 0;
		V.Sort();
		return V[FMath::Min(V.Num() - 1, int32(V.Num() * Q))];
	}
}

AStellarAutomation::AStellarAutomation()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
}

void AStellarAutomation::StartIfRequested(UWorld* World)
{
	FString Shots, Trial;
	const bool bShots = FParse::Value(FCommandLine::Get(), TEXT("-shots="), Shots);
	const bool bTrialRun = FParse::Value(FCommandLine::Get(), TEXT("-trial="), Trial);
	if (!bShots && !bTrialRun)
		return;
	AStellarAutomation* A = World->SpawnActor<AStellarAutomation>();
	A->bTrial = bTrialRun;
	A->Folder = bTrialRun ? Trial : Shots;
	IFileManager::Get().MakeDirectory(*A->Folder, true);
	A->Started = FPlatformTime::Seconds();
}

AAstronautPawn* AStellarAutomation::Pawn() const
{
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	return PC != nullptr ? Cast<AAstronautPawn>(PC->GetPawn()) : nullptr;
}

AStellarAutomation::FStep AStellarAutomation::Do(TFunction<void()> Fn)
{
	return [Fn](double) { Fn(); return true; };
}

AStellarAutomation::FStep AStellarAutomation::WaitIdle(double MinSeconds, double* OutWaited)
{
	// Done once the terrain has been up to date (no job, no pending edit, the last edit meshed)
	// for half a second, and at least MinSeconds have passed; gives up after a minute.
	return [this, MinSeconds, OutWaited](double Dt)
	{
		APlanetTerrainActor* T = Pawn()->GetTerrain();
		IdleTime = T->IsUpToDate() && T->HasTerrain() ? IdleTime + Dt : 0;
		const bool bDone = (StepTime >= MinSeconds && IdleTime >= 0.5) || StepTime > 60;
		if (bDone && OutWaited != nullptr)
			*OutWaited = StepTime - IdleTime;
		return bDone;
	};
}

AStellarAutomation::FStep AStellarAutomation::Shot(const FString& Name)
{
	// Done when the file exists: a screenshot is only taken when the window renders a frame (not
	// while it is minimised), so ask again every two seconds; give up after twenty.
	return [this, Name](double)
	{
		const FString Path = Folder / Name + TEXT(".png");
		static double LastAsked = -1;
		if (StepTime == 0)
		{
			IFileManager::Get().Delete(*Path);
			LastAsked = -10;
			APlanetTerrainActor* T = Pawn()->GetTerrain();
			UE_LOG(LogStellar, Log, TEXT("SHOT %s: %d nodes, %.2f M triangles, %d bricks"), *Path, T->NumNodes(), T->NumTriangles() / 1e6,
				T->GetEdits()->NumBricks());
		}
		if (StepTime - LastAsked >= 2.0)
		{
			FScreenshotRequest::RequestScreenshot(Path, true, false);
			LastAsked = StepTime;
		}
		const bool bTaken = IFileManager::Get().FileSize(*Path) > 0;
		if (!bTaken && StepTime > 20)
			UE_LOG(LogStellar, Warning, TEXT("SHOT %s was not taken (is the window minimised?)"), *Path);
		return (bTaken && StepTime > 0.3) || StepTime > 20;
	};
}

void AStellarAutomation::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AAstronautPawn* P = Pawn();
	if (P == nullptr || P->GetTerrain() == nullptr)
		return;
	if (Steps.Num() == 0)
	{
		P->SetAutomated(true);
		bTrial ? BuildTrial() : BuildShots();
	}
	if (bRecordFrames)
		Frames.Add(DeltaSeconds * 1000.f);
	if (At >= Steps.Num())
		return;
	if (Steps[At](DeltaSeconds))
	{
		At++;
		StepTime = 0;
		IdleTime = 0;
		if (At >= Steps.Num())
			Finish();
	}
	else
		StepTime += DeltaSeconds;
}

void AStellarAutomation::Finish()
{
	if (bTrial)
		WriteReport();
	UE_LOG(LogStellar, Log, TEXT("Automation finished (%s)"), bTrial ? (bAllPass ? TEXT("trial: ALL PASS") : TEXT("trial: FAILURES")) : TEXT("shots"));
	FPlatformMisc::RequestExit(false);
}

// ---------------- the visual gate ----------------

void AStellarAutomation::BuildShots()
{
	AStellarWorld* W = Pawn()->GetPlanet();
	APlanetTerrainActor* T = Pawn()->GetTerrain();
	const FVector3d Site = W->SiteSurface, Up = W->SiteUp, Fwd = W->SiteForward;
	const FTestSitePlan& S = W->GetTestSite();
	auto P = [this]() { return Pawn(); };

	Steps.Add(Do([P, Site, Fwd]() { P()->DirectWalk(Site, Fwd, 0); }));
	Steps.Add(WaitIdle(6));
	Steps.Add(Shot(TEXT("01_eye")));

	Steps.Add(Do([P, Site, Fwd]() { P()->DirectWalk(Site, -Fwd, 0); }));
	Steps.Add(WaitIdle(3));
	Steps.Add(Shot(TEXT("02_eye_back")));

	FVector3d Landmark;
	FString Kind;
	if (W->FindLandmark(Landmark, Kind))
	{
		const double Km = FMath::Acos(FMath::Clamp(Landmark.GetSafeNormal().Dot(Up), -1.0, 1.0)) * T->RadiusMeters / 1000;
		UE_LOG(LogStellar, Log, TEXT("Landmark: a %s %.1f km from the site"), *Kind, Km);
		Steps.Add(Do([P, Site, Up, Landmark]() { P()->DirectWalk(Site, Tangent(Landmark - Site, Up), 2); }));
		Steps.Add(WaitIdle(4));
		Steps.Add(Shot(FString::Printf(TEXT("03_landmark_%s_%.0fkm"), *Kind.Replace(TEXT(" "), TEXT("_")), Km)));
	}

	Steps.Add(Do([P, Site, Up, Fwd]() { P()->DirectFreeCam(Site + Up * 150.0, Site + Fwd * 3000.0); }));
	Steps.Add(WaitIdle(3));
	Steps.Add(Shot(TEXT("04_rise_150m")));
	Steps.Add(Do([P, Site, Up, Fwd]() { P()->DirectFreeCam(Site + Up * 2000.0 - Fwd * 1000.0, Site + Fwd * 9000.0); }));
	Steps.Add(WaitIdle(3));
	Steps.Add(Shot(TEXT("05_rise_2km")));
	Steps.Add(Do([P, Site, Up, Fwd]() { P()->DirectFreeCam(Site + Up * 30000.0, Site + Fwd * 90000.0); }));
	Steps.Add(WaitIdle(4));
	Steps.Add(Shot(TEXT("06_high_30km")));

	if (!W->HasTestSite())
		return;
	// The test site from the planning view, with a pad and a depot placed through the same checks
	// the player's placement uses.
	Steps.Add(Do([P, W, S]()
	{
		AStructuresActor* St = W->GetStructures();
		St->Place(St->Check(EStructureType::FoundationPad, S.At(-14, 6, 0), S.Forward), EStructureType::FoundationPad);
		St->Place(St->Check(EStructureType::FoundationPad, S.At(-4, 6, 0), S.Forward), EStructureType::FoundationPad);
		P()->DirectPlanning(S.At(0, -40, 0), 210, -48, (S.Forward - S.Right * 0.8).GetSafeNormal());
	}));
	Steps.Add(WaitIdle(4));
	Steps.Add(Shot(TEXT("07_test_site_planning")));

	Steps.Add(Do([P, S]() { P()->DirectWalk(S.RampTop + S.Right * 2.0, -S.Right, -12); }));
	Steps.Add(WaitIdle(3));
	Steps.Add(Shot(TEXT("08_ramp")));

	// Down the ramp, a third of the way, facing its right wall (the iron).
	Steps.Add(Do([P, S]()
	{
		P()->DirectWalk(FMath::Lerp(S.RampTop, S.RampBottom, 23.0 / 70.0) - S.Forward * 1.0, S.Forward, -4);
		P()->SetLampOn(true); // the wall faces away from the low light
	}));
	Steps.Add(WaitIdle(3));
	Steps.Add(Shot(TEXT("09_deposit_iron_in_sandstone")));
	Steps.Add(Do([P]() { P()->SetLampOn(false); }));

	Steps.Add(Do([P, S]()
	{
		const FVector3d Eye = S.Chamber + S.Forward * 2.0 + S.Right * 3.0;
		P()->DirectWalk(Eye, Tangent(S.Deposits[1].Center - Eye, S.Up), 6);
		P()->SetLampOn(true);
	}));
	Steps.Add(WaitIdle(4));
	Steps.Add(Shot(TEXT("10_cavern_copper_in_basalt")));
	Steps.Add(Do([P]() { P()->SetLampOn(false); }));

	// Water: the spring runs faster than real time so the channel fills during the tour.
	Steps.Add(Do([P, W, S]()
	{
		W->GetFluids()->TimeScale = 12;
		P()->DirectFreeCam(S.At(34, 10, 13), S.At(52, 4, -1));
	}));
	Steps.Add(WaitIdle(6));
	Steps.Add(Shot(TEXT("11_water_channel")));
	Steps.Add([W](double) { return W->GetFluids()->NumSprings() == 0 && W->GetFluids()->ActiveCells() < 200; });
	Steps.Add(WaitIdle(2));
	Steps.Add(Shot(TEXT("12_water_basin")));

	// The breach: dig through the ramp's wall into the sealed pocket, from inside the ramp's cut.
	// The water is measured, not just looked at: in the pocket, and in the pit at the ramp's foot.
	const FVector3d Pit = S.RampBottom - S.Right * 8.0;
	auto Measure = [W, S, Pit](const TCHAR* When)
	{
		AFluidActor* F = W->GetFluids();
		for (int32 I = 0; I < F->NumRegions(); I++)
			if (F->Region(I).Frame().PlanetBounds().IsInside(S.Pocket))
				UE_LOG(LogStellar, Log, TEXT("Breach %s: %.1f m3 in the pocket, %.1f m3 in and around the pit, %.1f m3 in the region"), When,
					F->Region(I).CubicMetresIn(S.Pocket, S.PocketRadius + 0.5), F->Region(I).CubicMetresIn(Pit, 14), F->Region(I).CubicMetres());
	};
	Steps.Add(Do([P, T, S, Measure]()
	{
		Measure(TEXT("before"));
		P()->DirectFreeCam(FMath::Lerp(S.RampTop, S.RampBottom, 30.0 / 70.0) + S.Up * 3.5 + S.Forward * 3.0, S.BreachPoint - S.Forward * 3.0);
		T->ApplyNow(S.Breach);
	}));
	Steps.Add(Do([W]() { W->GetFluids()->TimeScale = 30; }));
	Steps.Add(WaitIdle(1.5));
	Steps.Add(Shot(TEXT("13_breach_flowing")));
	// A trickle down a 30 m ramp is a few dozen active cells: wait for real rest.
	Steps.Add([this, W](double) { return W->GetFluids()->ActiveCells() == 0 || StepTime > 120; });
	Steps.Add(Do([P, S, Pit, Measure]()
	{
		Measure(TEXT("after"));
		// From a corner of the pit, lamp on: it is in deep shadow under the low light.
		P()->DirectFreeCam(Pit + S.Up * 3.5 + S.Forward * 7.0 + S.Right * 6.0, Pit - S.Forward * 2.0 - S.Right * 4.0);
		P()->SetLampOn(true);
	}));
	Steps.Add(WaitIdle(3));
	Steps.Add(Shot(TEXT("14_breach_pit")));
	Steps.Add(Do([P]() { P()->SetLampOn(false); }));
	Steps.Add(Do([W]() { W->GetFluids()->TimeScale = 1; }));
}

// ---------------- the technical gate ----------------

void AStellarAutomation::Check(bool bOk, const FString& What)
{
	Checks.Add(FString::Printf(TEXT("- [%s] %s"), bOk ? TEXT("x") : TEXT(" "), *What));
	bAllPass &= bOk;
	UE_LOG(LogStellar, Log, TEXT("TRIAL %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
}

void AStellarAutomation::BuildTrial()
{
	AStellarWorld* W = Pawn()->GetPlanet();
	APlanetTerrainActor* T = Pawn()->GetTerrain();
	const double R = T->RadiusMeters;
	const FVector3d Up = W->SiteUp, Fwd = W->SiteForward;
	auto P = [this]() { return Pawn(); };

	Steps.Add(WaitIdle(1));
	Steps.Add(Do([this]() { StartupSeconds = FPlatformTime::Seconds() - GStartTime; }));

	struct FPlan { const TCHAR* Name; double Km; };
	Stops.Reserve(8); // steps keep pointers into it
	const FPlan Plans[] = { { TEXT("start site"), 0 }, { TEXT("25 km"), 25 }, { TEXT("120 km"), 120 }, { TEXT("400 km"), 400 },
		{ TEXT("far side"), UE_DOUBLE_PI * R / 1000 - 0.5 } };
	for (const FPlan& Plan : Plans)
	{
		const int32 Index = Stops.AddDefaulted();
		FStop& S0 = Stops[Index];
		S0.Name = Plan.Name;
		S0.DistanceKm = Plan.Km;
		const FVector3d Dir = Along(Up, Fwd, Plan.Km * 1000, R);
		S0.Ground = T->GetTerrain().SurfaceAt(Dir);
		const FVector3d Heading = Tangent(Fwd, Dir).IsNearlyZero() ? Tangent(FVector3d(1, 0, 0), Dir) : Tangent(Fwd, Dir);

		// Fast travel, then wait for the cut around the new place.
		Steps.Add(Do([P, this, Index, Heading]()
		{
			const FStop& S = Stops[Index];
			P()->DirectFreeCam(S.Ground + S.Ground.GetSafeNormal() * 60.0, S.Ground + Heading * 500.0);
		}));
		Steps.Add(WaitIdle(0.5, &Stops[Index].StreamSeconds));
		Steps.Add(Do([P, Index, this, Heading]() { P()->DirectWalk(Stops[Index].Ground, Heading, -40); }));
		Steps.Add(WaitIdle(1));
		// Six bites; latency from the first request to the last bite meshed.
		Steps.Add([P, T, this, Index](double)
		{
			FStop& S = Stops[Index];
			static int32 Bites = 0;
			static double T0 = 0;
			if (StepTime == 0)
			{
				Bites = 0;
				T0 = FPlatformTime::Seconds();
				FVector3d Hit;
				if (T->GetTerrain().Trace(P()->EyePosition(), P()->ViewForward(), 30, T->GetEdits(), Hit))
				{
					S.Dug = Hit - T->Normal(Hit) * 0.3;
					S.DensityBefore = T->Density(S.Dug);
				}
			}
			if (Bites < 6)
			{
				if (P()->DirectBite(2.5, 0.35))
					Bites++;
				return false;
			}
			if (!T->IsUpToDate())
				return false;
			S.EditSeconds = FPlatformTime::Seconds() - T0;
			S.DensityAfter = T->Density(S.Dug);
			return true;
		});
		Steps.Add(WaitIdle(0.5));
		Steps.Add(Do([P, T, this, Index]()
		{
			FStop& S = Stops[Index];
			const FCharacterState& M = P()->GetMotionState();
			S.bGrounded = M.bGrounded;
			S.FootingError = FMath::Abs(T->Density(M.Position) - CharacterMotion::Radius);
			// Precision: the density at the dig point evaluated from the node origin plus a float
			// offset, against all doubles.
			const FPlanetOctree& O = T->GetTerrain().Octree;
			const FNodeKey Leaf(0, int32((S.Dug.X + O.Half) / O.LeafSize), int32((S.Dug.Y + O.Half) / O.LeafSize), int32((S.Dug.Z + O.Half) / O.LeafSize));
			const FVector3d Min = O.Min(Leaf);
			const FVector3f Local(S.Dug - Min);
			S.SplitErrorMm = FMath::Abs(T->GetTerrain().Field.Density(Min.X, Min.Y, Min.Z, Local.X, Local.Y, Local.Z) - T->GetTerrain().Field.Density(S.Dug)) * 1000;
			Frames.Reset();
			bRecordFrames = true;
		}));
		Steps.Add([this](double) { return StepTime >= 3.0; });
		Steps.Add(Do([T, this, Index]()
		{
			FStop& S = Stops[Index];
			bRecordFrames = false;
			S.P50 = Percentile(Frames, 0.5);
			S.P95 = Percentile(Frames, 0.95);
			S.P99 = Percentile(Frames, 0.99);
			S.Nodes = T->NumNodes();
			S.Triangles = T->NumTriangles() / 1e6;
			S.Bricks = T->GetEdits()->NumBricks();
			S.MemoryMB = FPlatformMemory::GetStats().UsedPhysical / (1024.0 * 1024.0);
			MaxNodes = FMath::Max(MaxNodes, double(S.Nodes));
			MaxTriangles = FMath::Max(MaxTriangles, S.Triangles);
		}));
		Steps.Add(Shot(FString::Printf(TEXT("trial_%d_%s"), Index, *FString(Plan.Name).Replace(TEXT(" "), TEXT("_")))));
	}

	// More laps over the same stops: the first lap's memory includes first-use growth (pools and
	// caches reaching their working size); later laps tell whether residency keeps growing with
	// the area explored or levels off.
	constexpr int32 Laps = 5;
	for (int32 Lap = 2; Lap <= Laps; Lap++)
		for (int32 Index = 0; Index < Stops.Num(); Index++)
		{
			Steps.Add(Do([P, this, Index]() { P()->DirectWalk(Stops[Index].Ground, Tangent(FVector3d(1, 0, 0), Stops[Index].Ground.GetSafeNormal()), -10); }));
			Steps.Add(WaitIdle(2));
			Steps.Add(Do([this, Index]() { Stops[Index].Laps.Add(FPlatformMemory::GetStats().UsedPhysical / (1024.0 * 1024.0)); }));
		}

	// Revisit two stops after leaving their render range: their cuts must be there.
	for (const int32 Index : { 1, 3 })
	{
		Steps.Add(Do([P, this, Index]() { P()->DirectWalk(Stops[Index].Ground, Tangent(FVector3d(1, 0, 0), Stops[Index].Ground.GetSafeNormal()), -40); }));
		Steps.Add(WaitIdle(1));
		Steps.Add(Do([T, this, Index]() { Stops[Index].bRevisitKept = FMath::Abs(T->Density(Stops[Index].Dug) - Stops[Index].DensityAfter) < 1e-4; }));
		Steps.Add(Shot(FString::Printf(TEXT("trial_revisit_%d"), Index)));
	}

	// Save, clear every edit, reload.
	Steps.Add(Do([P, W, T, this]()
	{
		P()->DirectWalk(W->SiteSurface, W->SiteForward, 0);
		FString Error;
		Check(P()->SaveTo(Folder / TEXT("trial_save.bin"), Error), TEXT("saved the game ") + Error);
		SavedStructures = W->GetStructures()->GetSet().All().Num();
		SavedWater = W->GetFluids()->CubicMetres();
		T->SetEdits(MakeShared<FTerrainEdits>(T->GetTerrain().LeafStride()));
	}));
	Steps.Add(WaitIdle(1));
	Steps.Add(Do([T, this]()
	{
		bool bCleared = true;
		for (const FStop& S : Stops)
			bCleared &= FMath::Abs(T->Density(S.Dug) - T->GetTerrain().Field.Density(S.Dug)) < 1e-6;
		Check(bCleared, TEXT("clearing the edits restores the procedural ground at every stop"));
	}));
	Steps.Add(Do([P, W, T, this]()
	{
		FString Error;
		Check(P()->LoadFrom(Folder / TEXT("trial_save.bin"), Error), TEXT("loaded the game ") + Error);
		for (FStop& S : Stops)
			S.bReloadKept = FMath::Abs(T->Density(S.Dug) - S.DensityAfter) < 1e-4;
		Check(W->GetStructures()->GetSet().All().Num() == SavedStructures, FString::Printf(TEXT("structures survive save/load (%d)"), SavedStructures));
		Check(FMath::Abs(W->GetFluids()->CubicMetres() - SavedWater) <= 0.01 * FMath::Max(1.0, SavedWater),
			FString::Printf(TEXT("water survives save/load (%.1f m3)"), SavedWater));
	}));
	Steps.Add(WaitIdle(1));
	Steps.Add(Shot(TEXT("trial_reloaded_site")));
}

void AStellarAutomation::WriteReport()
{
	for (const FStop& S : Stops)
	{
		Check(S.StreamSeconds < 15, FString::Printf(TEXT("%s: the terrain streamed in within 15 s (%.2f s)"), *S.Name, S.StreamSeconds));
		Check(S.DensityAfter > S.DensityBefore + 0.2, FString::Printf(TEXT("%s: digging removed ground (%.2f m)"), *S.Name, S.DensityAfter - S.DensityBefore));
		Check(S.bGrounded && S.FootingError < 0.08, FString::Printf(TEXT("%s: the astronaut stands on the ground (footing error %.3f m)"), *S.Name, S.FootingError));
		Check(S.SplitErrorMm < 1.0, FString::Printf(TEXT("%s: node-local density matches doubles (%.3f mm)"), *S.Name, S.SplitErrorMm));
		Check(S.bReloadKept, FString::Printf(TEXT("%s: the cut survives save, clear and load"), *S.Name));
	}
	for (const int32 Index : { 1, 3 })
		if (Stops.IsValidIndex(Index))
			Check(Stops[Index].bRevisitKept, FString::Printf(TEXT("%s: the cut is there after leaving render range and returning"), *Stops[Index].Name));
	// The view's residency budget: set from the first trial (2,500 nodes was a guess made before
	// measuring; rugged ground needs about 3,400 at 4 ms p95 on the reference machine).
	Check(MaxNodes <= 4000 && MaxTriangles <= 4.5, FString::Printf(TEXT("resident terrain within the budget of 4,000 nodes / 4.5 M triangles (max %.0f / %.2f M)"), MaxNodes, MaxTriangles));
	// Levelling off: the last lap uses no more than 50 MB over the one before at any stop.
	double Growth = -1e9;
	for (const FStop& S : Stops)
		if (S.Laps.Num() >= 2)
			Growth = FMath::Max(Growth, S.Laps.Last() - S.Laps[S.Laps.Num() - 2]);
	Check(Growth <= 50, FString::Printf(TEXT("memory levels off as the same ground is revisited: the last lap uses at most %+.0f MB more than the one before at any stop"), Growth));
	double WorstP95 = 0;
	for (const FStop& S : Stops)
		WorstP95 = FMath::Max(WorstP95, S.P95);
	Check(WorstP95 < 16.7, FString::Printf(TEXT("p95 frame time under 16.7 ms at every stop (worst %.1f ms)"), WorstP95));

	FString Md;
	Md += TEXT("# Milestone 0 technical trial\n\n");
	Md += FString::Printf(TEXT("Generated by `-trial` on %s. Hardware: %s, %s, %d logical cores, %.0f GB RAM. Development build, 1600x900 window.\n\n"),
		*FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M")), *GRHIAdapterName, *FPlatformMisc::GetCPUBrand().TrimStartAndEnd(),
		FPlatformMisc::NumberOfCoresIncludingHyperthreads(), FPlatformMemory::GetConstants().TotalPhysicalGB * 1.0);
	Md += FString::Printf(TEXT("Startup (process start to the first complete terrain cut): %.1f s.\n\n"), StartupSeconds);
	Md += TEXT("| stop | distance | streaming | 6 bites to meshed | nodes | triangles | edited bricks | p50 / p95 / p99 frame | footing | density split |\n");
	Md += TEXT("|---|---|---|---|---|---|---|---|---|---|\n");
	for (const FStop& S : Stops)
		Md += FString::Printf(TEXT("| %s | %.0f km | %.2f s | %.2f s | %d | %.2f M | %d | %.1f / %.1f / %.1f ms | %.3f m | %.3f mm |\n"), *S.Name,
			S.DistanceKm, S.StreamSeconds, S.EditSeconds, S.Nodes, S.Triangles, S.Bricks, S.P50, S.P95, S.P99, S.FootingError, S.SplitErrorMm);
	const int32 Extra = Stops.Num() > 0 ? Stops[0].Laps.Num() : 0;
	Md += TEXT("\nResident memory (MB) at each stop, lap by lap over the same ground:\n\n| stop | lap 1");
	for (int32 L = 0; L < Extra; L++)
		Md += FString::Printf(TEXT(" | lap %d"), L + 2);
	Md += TEXT(" |\n|---|---");
	for (int32 L = 0; L < Extra; L++)
		Md += TEXT("|---");
	Md += TEXT("|\n");
	for (const FStop& S : Stops)
	{
		Md += FString::Printf(TEXT("| %s | %.0f"), *S.Name, S.MemoryMB);
		for (double M : S.Laps)
			Md += FString::Printf(TEXT(" | %.0f"), M);
		Md += TEXT(" |\n");
	}
	Md += TEXT("\n## Checks\n\n");
	for (const FString& C : Checks)
		Md += C + TEXT("\n");
	Md += FString::Printf(TEXT("\n%s\n"), bAllPass ? TEXT("**ALL PASS**") : TEXT("**FAILURES** - see the unchecked items."));
	FFileHelper::SaveStringToFile(Md, *(Folder / TEXT("report.md")));
}
