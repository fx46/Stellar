#include "Game/StellarHUD.h"
#include "Player/AstronautPawn.h"
#include "Game/StellarWorld.h"
#include "Game/Structures.h"
#include "Terrain/PlanetTerrainActor.h"
#include "Terrain/FluidActor.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMemory.h"

namespace
{
	FString Distance(double Metres)
	{
		return FMath::Abs(Metres) >= 1000 ? FString::Printf(TEXT("%.1f km"), Metres / 1000) : FString::Printf(TEXT("%.1f m"), Metres);
	}
}

void AStellarHUD::Line(const FString& Text, float X, float& Y, const FLinearColor& Color, float Scale)
{
	UFont* Font = GEngine->GetSmallFont();
	// A dark offset copy keeps text readable over bright sky and pale rock.
	Canvas->SetDrawColor(FColor(0, 0, 0, 180));
	Canvas->DrawText(Font, Text, X + 1, Y + 1, Scale, Scale);
	Canvas->SetDrawColor(Color.ToFColor(true));
	Canvas->DrawText(Font, Text, X, Y, Scale, Scale);
	Y += 16.f * Scale;
}

void AStellarHUD::DrawHUD()
{
	Super::DrawHUD();
	const AAstronautPawn* P = Cast<AAstronautPawn>(GetOwningPawn());
	if (P == nullptr || Canvas == nullptr)
		return;
	const APlanetTerrainActor* T = P->GetTerrain();
	const AStellarWorld* W = P->GetPlanet();

	const float Wd = Canvas->ClipX, H = Canvas->ClipY;
	const float Cx = Wd * 0.5f, Cy = H * 0.5f;
	const EViewMode Mode = P->GetViewMode();

	// Crosshair (not in the planning view, which has a mouse cursor).
	if (Mode != EViewMode::Planning)
	{
		const FLinearColor Cross = P->IsBrushInReach() ? FLinearColor(0.5f, 0.8f, 1.f) : FLinearColor(1.f, 1.f, 1.f, 0.6f);
		DrawLine(Cx - 8, Cy, Cx - 3, Cy, Cross, 1.5f);
		DrawLine(Cx + 3, Cy, Cx + 8, Cy, Cross, 1.5f);
		DrawLine(Cx, Cy - 8, Cx, Cy - 3, Cross, 1.5f);
		DrawLine(Cx, Cy + 3, Cx, Cy + 8, Cross, 1.5f);
	}

	// Status, top left.
	float Y = 16;
	const FCharacterState& S = P->GetMotionState();
	const FString ModeText = Mode == EViewMode::Planning ? TEXT("PLANNING") : Mode == EViewMode::FreeCam ? TEXT("FREE CAMERA")
		: P->IsFlying() ? TEXT("JETPACK") : S.bGrounded ? TEXT("WALKING") : TEXT("AIRBORNE");
	Line(FString::Printf(TEXT("%s   %s %s   speed %.1f m/s%s"), *ModeText, Mode == EViewMode::Planning ? TEXT("view") : TEXT("altitude"),
		*Distance(P->AltitudeAboveGround()), Mode == EViewMode::FreeCam ? P->FreeCamSpeed() : P->GroundSpeed(),
		P->IsLampOn() ? TEXT("   LAMP") : TEXT("")), 16, Y);
	Line(FString::Printf(TEXT("%s from the start site"), *Distance(P->DistanceFromSite())), 16, Y, FLinearColor(0.8f, 0.8f, 0.8f));
	const FSuit& Suit = P->GetSuit();
	Line(Suit.bGodMode ? FString(TEXT("SUIT  GOD MODE")) : FString::Printf(TEXT("SUIT  %.0f%%"), Suit.Power), 16, Y,
		Suit.bGodMode ? FLinearColor(1.f, 0.85f, 0.3f) : Suit.Power < 15 ? FLinearColor(1.f, 0.4f, 0.3f) : FLinearColor(0.7f, 1.f, 0.7f));

	// Bottom centre: the build panel, or the brush.
	float By = H - 74;
	if (P->IsBuildMode() && Mode != EViewMode::FreeCam)
	{
		const FPlacementCheck& C = P->GetPlacement();
		Line(FString::Printf(TEXT("BUILD  %s   (1 pad  2 depot  3 beacon,  R / wheel rotate)"), FStructureSet::NameOf(P->GetBuildType())), Cx - 170, By,
			FLinearColor(0.95f, 0.8f, 0.4f));
		Line(P->HasPlacementTarget() ? (C.bValid ? TEXT("LMB place - ") : TEXT("blocked - ")) + C.Reason : FString(TEXT("aim at the ground")),
			Cx - 170, By, C.bValid ? FLinearColor(0.6f, 1.f, 0.6f) : FLinearColor(1.f, 0.5f, 0.35f));
		Line(TEXT("RMB / X remove the structure under the cursor   T leave build mode"), Cx - 170, By, FLinearColor(0.6f, 0.6f, 0.6f));
	}
	else if (Mode == EViewMode::Walk)
	{
		const double Pace = P->BrushPace();
		const FString PaceText = Pace >= 1000 ? FString::Printf(TEXT("%.1e"), Pace) : FString::SanitizeFloat(FMath::RoundToDouble(Pace * 100) / 100);
		Line(FString::Printf(TEXT("SCULPT  %s %s%s  R%.2f m"), *PaceText, Pace == 1 ? TEXT("bite/s") : TEXT("bites/s"),
			!Suit.bGodMode && Pace >= 1 ? TEXT(" MAX") : TEXT(""), P->BrushRadius()), Cx - 150, By, FLinearColor(0.6f, 0.85f, 1.f));
		Line(P->ToolStatus(), Cx - 150, By, FLinearColor(0.85f, 0.85f, 0.85f));
		Line(TEXT("RMB sculpt  Ctrl+RMB grade  LMB build  wheel pace  Shift+wheel size"), Cx - 150, By, FLinearColor(0.6f, 0.6f, 0.6f));
	}
	else if (Mode == EViewMode::Planning)
		Line(TEXT("WASD pan  Q/E or middle-drag turn  wheel zoom  LMB select  T build  O / Esc back"), Cx - 210, By, FLinearColor(0.7f, 0.7f, 0.7f));

	// Inspect, beside the cursor (planning) or under the crosshair (building).
	if (P->GetInspect().Num() > 0)
	{
		float Mx = Cx + 24, My = Cy + 18;
		if (Mode == EViewMode::Planning)
			if (APlayerController* PC = GetOwningPlayerController())
			{
				float X = 0, Yp = 0;
				if (PC->GetMousePosition(X, Yp))
				{
					Mx = X + 22;
					My = Yp + 12;
				}
			}
		for (int32 I = 0; I < P->GetInspect().Num(); I++)
			Line(P->GetInspect()[I], Mx, My, I == 0 ? FLinearColor(1.f, 1.f, 1.f) : FLinearColor(0.8f, 0.85f, 0.9f));
	}
	if (P->GetSelected() != 0 && W != nullptr)
		if (const FStructure* Sel = W->GetStructures()->GetSet().Find(P->GetSelected()))
		{
			float Sy = 70;
			Line(FString::Printf(TEXT("SELECTED  %s #%u"), FStructureSet::NameOf(Sel->Type), Sel->Id), 16, Sy, FLinearColor(0.5f, 0.8f, 1.f));
			Line(FString::Printf(TEXT("body %d, planet frame (%.1f, %.1f, %.1f) km"), Sel->BodyId, Sel->Position.X / 1000, Sel->Position.Y / 1000,
				Sel->Position.Z / 1000), 16, Sy, FLinearColor(0.75f, 0.8f, 0.85f));
		}

	// Pockets, top right.
	float Iy = 16;
	const FInventory& Inv = P->GetInventory();
	Line(FString::Printf(TEXT("POCKETS %d / %d"), Inv.Total(), Inv.Capacity), Wd - 200, Iy);
	for (const TPair<EItemType, int32>& Pair : Inv.Contents())
		Line(FString::Printf(TEXT("%4d  %s"), Pair.Value, Items::NameOf(Pair.Key)), Wd - 200, Iy, FLinearColor(0.85f, 0.85f, 0.85f));

	// Toasts, above the bottom panel.
	float Ty = H - 110 - 16.f * P->GetToasts().Num();
	for (const FHudToast& Toast : P->GetToasts())
		Line(Toast.Text, Cx - 150, Ty, Toast.Color);

	// Frame times.
	const float Ms = GetWorld()->GetDeltaSeconds() * 1000.f;
	if (Frames.Num() < 600)
		Frames.Add(Ms);
	else
		Frames[FrameAt] = Ms;
	FrameAt = (FrameAt + 1) % 600;

	if (P->ShowPerf() && T != nullptr)
	{
		TArray<float> Sorted = Frames;
		Sorted.Sort();
		auto Pct = [&Sorted](float Q) { return Sorted.Num() > 0 ? Sorted[FMath::Min(Sorted.Num() - 1, int32(Sorted.Num() * Q))] : 0.f; };
		float Py = 110;
		const FLinearColor C(1.f, 1.f, 0.6f);
		const FPlatformMemoryStats Mem = FPlatformMemory::GetStats();
		Line(FString::Printf(TEXT("frame %.1f ms   p50 %.1f  p95 %.1f  p99 %.1f"), Ms, Pct(0.5f), Pct(0.95f), Pct(0.99f)), 16, Py, C);
		Line(FString::Printf(TEXT("terrain %d nodes, %.2f M triangles, %d edited bricks (%.1f MB)"), T->NumNodes(), T->NumTriangles() / 1e6,
			T->GetEdits()->NumBricks(), T->GetEdits()->BrickBytes() / (1024.0 * 1024.0)), 16, Py, C);
		Line(FString::Printf(TEXT("last cut: select %.1f ms, meshed %d nodes in %.0f ms%s%s"), T->LastSelectMs(), T->LastMeshedNodes(),
			T->LastMeshMs(), T->IsJobRunning() ? TEXT("  [meshing]") : TEXT(""), T->bFreezeLod ? TEXT("  [LOD frozen]") : TEXT("")), 16, Py, C);
		if (W != nullptr && W->GetFluids() != nullptr)
		{
			const AFluidActor* F = W->GetFluids();
			Line(FString::Printf(TEXT("water %.1f m3 in %d regions, %d active cells, step %.2f ms, %d springs, %.1f MB"), F->CubicMetres(),
				F->NumRegions(), F->ActiveCells(), F->LastStepMs(), F->NumSprings(), F->Bytes() / (1024.0 * 1024.0)), 16, Py, C);
			Line(FString::Printf(TEXT("structures %d"), W->GetStructures()->GetSet().All().Num()), 16, Py, C);
		}
		Line(FString::Printf(TEXT("memory %.0f MB used"), Mem.UsedPhysical / (1024.0 * 1024.0)), 16, Py, C);
	}

	if (P->ShowHints())
	{
		static const TCHAR* Hints[] = {
			TEXT("WASD walk, Shift sprint, Space jump (hold / press again in air: jetpack)"),
			TEXT("In flight: WASD view-relative, Space up, Ctrl down, Shift boost; release to brake"),
			TEXT("RMB sculpt, Ctrl+RMB grade, LMB build, wheel pace, Shift+wheel brush size"),
			TEXT("O planning view   T build mode   L helmet lamp   Y water spring (test site)"),
			TEXT("G god mode   F free camera / fast travel   Home back to the site"),
			TEXT("Ctrl+S save   Ctrl+L load   P performance   Ctrl+1 wireframe   Ctrl+2 freeze LOD"),
			TEXT("Esc release mouse   H hide hints"),
		};
		float Hy = H - 16.f * (UE_ARRAY_COUNT(Hints) + 1);
		for (const TCHAR* Hint : Hints)
			Line(Hint, 16, Hy, FLinearColor(0.75f, 0.75f, 0.75f));
	}
}
