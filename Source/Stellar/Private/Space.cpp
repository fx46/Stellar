// Fill out your copyright notice in the Description page of Project Settings.


#include "Space.h"

// Sets default values
ASpace::ASpace()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void ASpace::BeginPlay()
{
	Super::BeginPlay();
	
	for (int x = 0; x < NumChunkOnAxis; ++x)
	{
		for (int z = 0; z < NumChunkOnAxis; ++z)
		{
			for (int y = 0; y < NumChunkOnAxis; ++y)
			{
				FActorSpawnParameters SpawnParameters;
				chunks[x][y][z] = 
					GetWorld()->SpawnActor<AChunk>(AChunk::StaticClass(), FVector(x, y, z) * 100.0f * AChunk::viewableChunkDotSize, FRotator::ZeroRotator);
			}
		}
	}


	SetDot(1, 1, 3, 0xFF);
}

// Called every frame
void ASpace::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void ASpace::SetDotInChunk(int chunkX, int chunkY, int chunkZ, int dotX, int dotY, int dotZ, uint8_t value)
{
	AChunk* chunk = chunks[chunkX][chunkY][chunkZ];
	chunk->chunkDots[dotX][dotY][dotZ] = value;
	chunk->bNeedsToUpdate = true;
}

void ASpace::SetDot(int x, int y, int z, uint8_t value)
{
	int chunkX = x / AChunk::viewableChunkDotSize;
	int chunkY = y / AChunk::viewableChunkDotSize;
	int chunkZ = z / AChunk::viewableChunkDotSize;

	int dotXinChunk = x % AChunk::viewableChunkDotSize;
	int dotYinChunk = y % AChunk::viewableChunkDotSize;
	int dotZinChunk = z % AChunk::viewableChunkDotSize;

	SetDotInChunk(chunkX, chunkY, chunkZ, dotXinChunk, dotYinChunk, dotZinChunk, value);

	if (x % AChunk::viewableChunkDotSize == 0 && y % AChunk::viewableChunkDotSize == 0 && z % AChunk::viewableChunkDotSize == 0)
	{
		SetDotInChunk(chunkX - 1, chunkY - 1, chunkZ - 1, AChunk::viewableChunkDotSize, AChunk::viewableChunkDotSize, AChunk::viewableChunkDotSize, value);
		SetDotInChunk(chunkX - 1, chunkY - 1, chunkZ, AChunk::viewableChunkDotSize, AChunk::viewableChunkDotSize, dotZinChunk, value);
		SetDotInChunk(chunkX - 1, chunkY, chunkZ - 1, AChunk::viewableChunkDotSize, dotYinChunk, AChunk::viewableChunkDotSize, value);
		SetDotInChunk(chunkX, chunkY - 1, chunkZ - 1, dotXinChunk, AChunk::viewableChunkDotSize, AChunk::viewableChunkDotSize, value);
		SetDotInChunk(chunkX - 1, chunkY, chunkZ, AChunk::viewableChunkDotSize, dotYinChunk, dotZinChunk, value);
		SetDotInChunk(chunkX, chunkY - 1, chunkZ, dotXinChunk, AChunk::viewableChunkDotSize, dotZinChunk, value);
		SetDotInChunk(chunkX, chunkY, chunkZ - 1, dotXinChunk, dotYinChunk, AChunk::viewableChunkDotSize, value);
	}
	else if (x % AChunk::viewableChunkDotSize == 0 && y % AChunk::viewableChunkDotSize == 0)
	{
		SetDotInChunk(chunkX - 1, chunkY - 1, chunkZ, AChunk::viewableChunkDotSize, AChunk::viewableChunkDotSize, dotZinChunk, value);
		SetDotInChunk(chunkX - 1, chunkY, chunkZ, AChunk::viewableChunkDotSize, dotYinChunk, dotZinChunk, value);
		SetDotInChunk(chunkX, chunkY - 1, chunkZ, dotXinChunk, AChunk::viewableChunkDotSize, dotZinChunk, value);
	}
	else if (x % AChunk::viewableChunkDotSize == 0 && z % AChunk::viewableChunkDotSize == 0)
	{
		SetDotInChunk(chunkX - 1, chunkY, chunkZ - 1, AChunk::viewableChunkDotSize, dotYinChunk, AChunk::viewableChunkDotSize, value);
		SetDotInChunk(chunkX - 1, chunkY, chunkZ, AChunk::viewableChunkDotSize, dotYinChunk, dotZinChunk, value);
		SetDotInChunk(chunkX, chunkY, chunkZ - 1, dotXinChunk, dotYinChunk, AChunk::viewableChunkDotSize, value);
	}
	else if (y % AChunk::viewableChunkDotSize == 0 && z % AChunk::viewableChunkDotSize == 0)
	{
		SetDotInChunk(chunkX, chunkY - 1, chunkZ - 1, dotXinChunk, AChunk::viewableChunkDotSize, AChunk::viewableChunkDotSize, value);
		SetDotInChunk(chunkX, chunkY - 1, chunkZ, dotXinChunk, AChunk::viewableChunkDotSize, dotZinChunk, value);
		SetDotInChunk(chunkX, chunkY, chunkZ - 1, dotXinChunk, dotYinChunk, AChunk::viewableChunkDotSize, value);
	}
	else if (x % AChunk::viewableChunkDotSize == 0)
	{
		SetDotInChunk(chunkX - 1, chunkY, chunkZ, AChunk::viewableChunkDotSize, dotYinChunk, dotZinChunk, value);
	}
	else if (y % AChunk::viewableChunkDotSize == 0)
	{
		SetDotInChunk(chunkX, chunkY - 1, chunkZ, dotXinChunk, AChunk::viewableChunkDotSize, dotZinChunk, value);
	}
	else if (z % AChunk::viewableChunkDotSize == 0)
	{
		SetDotInChunk(chunkX, chunkY, chunkZ - 1, dotXinChunk, dotYinChunk, AChunk::viewableChunkDotSize, value);
	}
}
