// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MarchingTables.h"
#include "ProceduralMeshComponent.h"
#include "Chunk.generated.h"

UCLASS()
class STELLAR_API AChunk : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AChunk();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	void RedrawMesh();
	static const int actualChunkDotSize = 13;
	static const int viewableChunkDotSize = actualChunkDotSize - 1;
	bool bNeedsToUpdate = true;
	uint8_t chunkDots[actualChunkDotSize][actualChunkDotSize][actualChunkDotSize];

	bool IsHigherLodTowardsPosX = false;
	bool IsHigherLodTowardsNegX = false;
	bool IsHigherLodTowardsPosY = false;
	bool IsHigherLodTowardsNegY = false;
	bool IsHigherLodTowardsPosZ = false;
	bool IsHigherLodTowardsNegZ = false;

	LodLevel Lod = LodLevel::CellWidthOf1;

private:
	Cell CellAt(int cellCoordX, int cellCoordY, int cellCoordZ);
	TransCell TransCellAt(int cellCoordX, int cellCoordY, int cellCoordZ, Direction directionOfHigherLod);
	void DrawDebugInfo();

	UPROPERTY(VisibleAnywhere)
	UProceduralMeshComponent* mesh;
	UPROPERTY(EditAnywhere, Category = ProcMesh)
	UMaterial* material;
	UPROPERTY(EditAnywhere, Category = ProcMesh)
	bool bDrawDebugInfo = false;
	bool bOldDrawDebugInfo = false;

	TArray<FVector> vertices;
	TArray<int32> triangles;
	TArray<FVector> normals;
	TArray<FVector2D> UV0;
	TArray<FProcMeshTangent> tangents;
	TArray<FLinearColor> vertexColors;
};
