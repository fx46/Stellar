#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "StellarHUD.generated.h"

/**
 * The canvas HUD: crosshair, suit and flight state, the brush (pace, size, progress, the rock
 * under it), pockets, pickup toasts, key hints (H) and the performance overlay (P) with frame
 * percentiles and terrain streaming numbers. No assets needed.
 */
UCLASS()
class STELLAR_API AStellarHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void Line(const FString& Text, float X, float& Y, const FLinearColor& Color = FLinearColor::White, float Scale = 1.f);

	/** Recent frame times (ms), for percentiles. */
	TArray<float> Frames;
	int32 FrameAt = 0;
};
