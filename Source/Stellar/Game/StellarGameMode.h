#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "StellarGameMode.generated.h"

/** Spawns the astronaut and HUD; the planet itself is AStellarWorld, built on first use. */
UCLASS()
class STELLAR_API AStellarGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AStellarGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;
};
