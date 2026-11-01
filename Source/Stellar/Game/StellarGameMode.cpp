#include "Game/StellarGameMode.h"
#include "Game/StellarHUD.h"
#include "Game/StellarWorld.h"
#include "Player/AstronautPawn.h"

AStellarGameMode::AStellarGameMode()
{
	DefaultPawnClass = AAstronautPawn::StaticClass();
	HUDClass = AStellarHUD::StaticClass();
}

void AStellarGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	// Build the planet before any player spawns, so the first frame already has its ground.
	AStellarWorld::Get(GetWorld());
}
