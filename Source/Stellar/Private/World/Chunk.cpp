// Fill out your copyright notice in the Description page of Project Settings.
#include "DrawDebugHelpers.h"
#include "World/Chunk.h"

// Sets default values
AChunk::AChunk()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GeneratedMesh"));
	RootComponent = mesh;
	mesh->bUseAsyncCooking = true;
}

// Called when the game starts or when spawned
void AChunk::BeginPlay()
{
	Super::BeginPlay();
	
	chunkDots[1][1][1] = 0xFF;
}

// Called every frame
void AChunk::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bNeedsToUpdate)
	{
		RedrawMesh();
		bNeedsToUpdate = false;
	}

	if (bDrawDebugInfo)
		DrawDebugInfo();
}

void AChunk::RedrawMesh()
{
	vertices.Reset();
	UV0.Reset();
	normals.Reset();
	triangles.Reset();
	tangents.Reset();
	vertexColors.Reset();

	for (int x = 0; x < viewableChunkDotSize; ++x)
	{
		for (int z = 0; z < viewableChunkDotSize; ++z)
		{
			for (int y = 0; y < viewableChunkDotSize; ++y)
			{
				Cell cell = CellAt(x, y, z);
				int index = cell.CellCaseCode();
				if (index == 0 || index == 255)
					continue;

				// Marching Cubes case index to an equivalence class index
				unsigned char equivalence_class = regularCellClass[index];
				// Triangulation data for all 16 distinct classes
				RegularCellData triangulation_class = regularCellData[equivalence_class];
				// Vertex locations for every one of the 256 possible cases in modified Marching Cubes
				const unsigned short* vertex_class = regularVertexData[index];

				int currIndex = vertices.Num();
				for (int i = 0; i < triangulation_class.GetTriangleCount() * 3; ++i)
				{
					triangles.Add(currIndex + triangulation_class.vertexIndex[i]);
				}
				for (int i = 0; i < triangulation_class.GetTriangleCount(); ++i)
				{
					UV0.Add(FVector2D(0, 0));
					UV0.Add(FVector2D(1, 0));
					UV0.Add(FVector2D(0, 1));
				}
				for (int i = triangulation_class.GetVertexCount() - 1; i > -1; --i)
				{
					unsigned short regularVertex = regularVertexData[index][i];
					int firstVertexIndex = ((regularVertex >> 4) & 0x0F);
					int secndVertexIndex = (regularVertex & 0x0F);
					FVector firstVertex = CornerIndex[firstVertexIndex];
					FVector secondVertex = CornerIndex[secndVertexIndex];
					FVector middlePoint = (firstVertex + secondVertex) * 0.5f;
					vertices.Add((middlePoint + FVector(x, y, z)) * 100);
				}
				for (int i = 0; i < triangulation_class.GetTriangleCount(); ++i)
				{
					FVector Vertex1 = vertices[i + 0];
					FVector Vertex2 = vertices[i + 1];
					FVector Vertex3 = vertices[i + 2];
					FVector Vector1 = Vertex1 - Vertex2;
					FVector Vector2 = Vertex1 - Vertex3;
					FVector Normal = Vector1.Cross(Vector2).GetUnsafeNormal();
					normals.Add(Normal);
					normals.Add(Normal);
					normals.Add(Normal);
				}
			}
		}
	}

	if (vertices.Num() == 0)
		return;

	mesh->CreateMeshSection_LinearColor(0, vertices, triangles, normals, UV0, vertexColors, tangents, true);
	mesh->SetMaterial(0, material);

	// Enable collision data
	mesh->ContainsPhysicsTriMeshData(true);
}

Cell AChunk::CellAt(int cellCoordX, int cellCoordY, int cellCoordZ)
{
	Cell cell{ chunkDots[cellCoordX][cellCoordY][cellCoordZ],
			   chunkDots[cellCoordX + 1][cellCoordY][cellCoordZ],
			   chunkDots[cellCoordX][cellCoordY + 1][cellCoordZ],
			   chunkDots[cellCoordX + 1][cellCoordY + 1][cellCoordZ],
			   chunkDots[cellCoordX][cellCoordY][cellCoordZ + 1],
			   chunkDots[cellCoordX + 1][cellCoordY][cellCoordZ + 1],
			   chunkDots[cellCoordX][cellCoordY + 1][cellCoordZ + 1],
			   chunkDots[cellCoordX + 1][cellCoordY + 1][cellCoordZ + 1] };

	return cell;
}

void AChunk::DrawDebugInfo()
{
	for (int x = 0; x < actualChunkDotSize; ++x)
	{
		for (int z = 0; z < actualChunkDotSize; ++z)
		{
			for (int y = 0; y < actualChunkDotSize; ++y)
			{
				//FString text = GetName();
				//text.Append(":");
				uint8_t value = chunkDots[x][y][z];
				if (value > 0x00)
				{
					FString text;
					text.Append(FString::FromInt(x));
					text.Append(",");
					text.Append(FString::FromInt(y));
					text.Append(",");
					text.Append(FString::FromInt(z));
					text.Append(",");
					text.Append(FString::FromInt(value));
					DrawDebugString(GetWorld(), FVector(x, y, z) * 100.0f, text, this, FColor::Green);
				}

				if (x == viewableChunkDotSize || y == viewableChunkDotSize || z == viewableChunkDotSize)
					DrawDebugSphere(GetWorld(), GetActorLocation() + FVector(x, y, z) * 100.0f, 7.5f, 12, FColor::Red);
				else
					DrawDebugSphere(GetWorld(), GetActorLocation() + FVector(x, y, z) * 100.0f, 5.0f, 12, FColor::Blue);
			}
		}
	}
}
