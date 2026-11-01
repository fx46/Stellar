#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StellarAutomation.generated.h"

class AAstronautPawn;
class AStellarWorld;

/**
 * Development automation, started from the command line (milestone 0's gates):
 *
 * -shots=<folder>  the visual gate: the same compositions every run - eye height and looking
 *                  back, a landmark, 150 m / 2 km / 30 km up, the test site from the planning
 *                  view, the ramp, the cavern by lamplight, both deposits, the spring's channel and
 *                  basin, and the aquifer breach - written as PNGs, then quits.
 * -trial=<folder>  the technical gate: fast travel to stops out to the far side of the planet; at
 *                  each, streaming latency, edit latency, footing, precision, resident nodes and
 *                  memory, frame percentiles and a screenshot; revisits after leaving render range;
 *                  save, clear and reload; a report (report.md) with pass/fail checks, then quits.
 */
UCLASS()
class STELLAR_API AStellarAutomation : public AActor
{
	GENERATED_BODY()

public:
	AStellarAutomation();

	/** Spawns the automation if the command line asks for it. */
	static void StartIfRequested(UWorld* World);

	virtual void Tick(float DeltaSeconds) override;

private:
	using FStep = TFunction<bool(double Dt)>;

	void BuildShots();
	void BuildTrial();
	/** Steps that wait until the terrain (and edits) are idle for a while. */
	FStep WaitIdle(double MinSeconds, double* OutWaited = nullptr);
	FStep Shot(const FString& Name);
	FStep Do(TFunction<void()> Fn);
	void Finish();

	AAstronautPawn* Pawn() const;

	FString Folder;
	bool bTrial = false;
	TArray<FStep> Steps;
	int32 At = 0;
	double StepTime = 0;
	double IdleTime = 0;
	double Started = 0;

	// ---- trial records ----
	struct FStop
	{
		FString Name;
		FVector3d Ground = FVector3d::ZeroVector;
		FVector3d Dug = FVector3d::ZeroVector;
		double DistanceKm = 0;
		double StreamSeconds = 0;
		double EditSeconds = 0;
		double DensityBefore = 0, DensityAfter = 0;
		int32 Nodes = 0;
		double Triangles = 0;
		int32 Bricks = 0;
		double MemoryMB = 0;
		double P50 = 0, P95 = 0, P99 = 0;
		bool bGrounded = false;
		double FootingError = 0;
		double SplitErrorMm = 0;
		bool bRevisitKept = false;
		bool bReloadKept = false;
		/** Resident memory at this stop on every lap (the first is MemoryMB). */
		TArray<double> Laps;
	};
	TArray<FStop> Stops;
	TArray<float> Frames;
	bool bRecordFrames = false;
	double StartupSeconds = 0;
	double MaxNodes = 0, MaxTriangles = 0;
	TArray<FString> Checks;
	bool bAllPass = true;
	int32 SavedStructures = 0;
	double SavedWater = 0;

	void Check(bool bOk, const FString& What);
	void WriteReport();
};
