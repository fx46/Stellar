// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "World/Chunk.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Space.generated.h"

UCLASS()
class STELLAR_API ASpace : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ASpace();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	void SetDot(int x, int y, int z, uint8_t value);
	void SetDotInChunk(int chunkX, int chunkY, int chunkZ, int dotX, int dotY, int dotZ, uint8_t value);

private:
	static const int NumChunkOnAxis = 16;
	AChunk* chunks[NumChunkOnAxis][NumChunkOnAxis][NumChunkOnAxis];
};
