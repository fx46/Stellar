// Fill out your copyright notice in the Description page of Project Settings.
#include "World/Chunk.h"
#include "DrawDebugHelpers.h"

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
	
}

// Called every frame
void AChunk::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bOldDrawDebugInfo != bDrawDebugInfo)
	{
		bNeedsToUpdate = true;
		bOldDrawDebugInfo = bDrawDebugInfo;
	}

	if (bNeedsToUpdate)
	{
		RedrawMesh();
		bNeedsToUpdate = false;

		if (bDrawDebugInfo)
			DrawDebugInfo();
	}

}

void AChunk::RedrawMesh()
{
	vertices.Reset();
	UV0.Reset();
	normals.Reset();
	triangles.Reset();
	tangents.Reset();
	vertexColors.Reset();

	for (int x = 0; x < viewableChunkDotSize; x += Lod)
	{
		for (int z = 0; z < viewableChunkDotSize; z += Lod)
		{
			for (int y = 0; y < viewableChunkDotSize; y += Lod)
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
				bool hasTransCell = false;
				for (int i = triangulation_class.GetVertexCount() - 1; i > -1; --i)
				{
					unsigned short regularVertex = regularVertexData[index][i];
					int firstVertexIndex = ((regularVertex >> 4) & 0x0F);
					int secndVertexIndex = (regularVertex & 0x0F);
					FVector firstVertex = CornerIndex[firstVertexIndex];
					FVector secondVertex = CornerIndex[secndVertexIndex];
					FVector middlePoint = (firstVertex + secondVertex) * (0.5f * Lod);
					if (Lod != LodLevel::CellWidthOf1)
					{	//make place for the trans cells
						if (IsHigherLodTowardsNegX && (x == 0)) //first loop
						{
							middlePoint.X = middlePoint.X * 0.5f + Lod / 2;
							hasTransCell = true;
						}
						else if (IsHigherLodTowardsPosX && (x + Lod >= viewableChunkDotSize)) //last loop
						{
							middlePoint.X = middlePoint.X * 0.5f;
							hasTransCell = true;
						}
						if (IsHigherLodTowardsNegY && (y == 0)) //first loop
						{
							middlePoint.Y = middlePoint.Y * 0.5f + Lod / 2;
							hasTransCell = true;
						}
						else if (IsHigherLodTowardsPosY && (y + Lod >= viewableChunkDotSize)) //last loop
						{
							middlePoint.Y = middlePoint.Y * 0.5f;
							hasTransCell = true;
						}
						if (IsHigherLodTowardsNegZ && (z == 0)) //first loop
						{
							middlePoint.Z = middlePoint.Z * 0.5f + Lod / 2;
							hasTransCell = true;
						}
						else if (IsHigherLodTowardsPosZ && (z + Lod >= viewableChunkDotSize)) //last loop
						{
							middlePoint.Z = middlePoint.Z * 0.5f;
							hasTransCell = true;
						}
					}
					vertices.Add((middlePoint + FVector(x, y, z)) * 100);
				}
				//add trans cells, https://transvoxel.org/Lengyel-VoxelTerrain.pdf page 30
				int numTrianglesAddedFromTransCells = 0;
				if (hasTransCell && x == 6 && y == 10 && z == 0)
				{
					TransCell transCell;
					if (IsHigherLodTowardsPosX && (x + Lod >= viewableChunkDotSize)) transCell = TransCellAt(x, y, z, PosX);
					if (IsHigherLodTowardsNegX && (x == 0))                          transCell = TransCellAt(x, y, z, NegX);
					if (IsHigherLodTowardsPosY && (y + Lod >= viewableChunkDotSize)) transCell = TransCellAt(x, y, z, PosY);
					if (IsHigherLodTowardsNegY && (y == 0))                          transCell = TransCellAt(x, y, z, NegY);
					if (IsHigherLodTowardsPosZ && (z + Lod >= viewableChunkDotSize)) transCell = TransCellAt(x, y, z, PosZ);
					if (IsHigherLodTowardsNegZ && (z == 0))                          transCell = TransCellAt(x, y, z, NegZ);
					int transIndex = transCell.TransCellCaseCode();
					if (transIndex != 0 && transIndex != 511)
					{
						// Marching Cubes case index to an equivalence class index
						unsigned char equivalence_classTrans = transitionCellClass[transIndex];
						// Triangulation data for all 16 distinct classes
						TransitionCellData triangulation_classTrans = transitionCellData[equivalence_classTrans & 0x7F];
						// Vertex locations for every one of the 256 possible cases in modified Marching Cubes
						const unsigned short* vertex_classTrans = transitionVertexData[transIndex];
						numTrianglesAddedFromTransCells = triangulation_classTrans.GetTriangleCount();
						currIndex = vertices.Num();

						//winding order of each triangle should be reversed if the high bit is set
						if ((equivalence_classTrans & 0x80) != 0x80)
							for (int i = (triangulation_classTrans.GetTriangleCount() * 3) - 1; i > -1; --i)
							{
								triangles.Add(currIndex + triangulation_classTrans.vertexIndex[i]);
							}
						else
							for (int i = 0; i < triangulation_classTrans.GetTriangleCount() * 3; ++i)
							{
								triangles.Add(currIndex + triangulation_classTrans.vertexIndex[i]);
							}
						for (int i = 0; i < triangulation_classTrans.GetTriangleCount(); ++i)
						{
							UV0.Add(FVector2D(0, 0));
							UV0.Add(FVector2D(1, 0));
							UV0.Add(FVector2D(0, 1));
						}
						for (int i = triangulation_classTrans.GetVertexCount() - 1; i > -1; --i)
						{
							unsigned short regularVertex = transitionVertexData[transIndex][i];
							int firstVertexIndex = ((regularVertex >> 4) & 0x0F);
							int secndVertexIndex = (regularVertex & 0x0F);
							FVector firstVertex = CornerIndexTrans[firstVertexIndex];
							FVector secondVertex = CornerIndexTrans[secndVertexIndex];
							FVector middlePoint = (firstVertex + secondVertex) * (0.5f * Lod);
							vertices.Add((middlePoint + FVector(x, y, z)) * 100);

							if (bDrawDebugInfo)
								DrawDebugSphere(GetWorld(), GetActorLocation() + vertices.Last(), 7.5f, 6, FColor::Yellow, true);
						}
					}
				}
				for (int i = 0; i < triangulation_class.GetTriangleCount() + numTrianglesAddedFromTransCells; ++i)
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
	Cell cell{ chunkDots[cellCoordX      ][cellCoordY      ][cellCoordZ      ],
			   chunkDots[cellCoordX + Lod][cellCoordY      ][cellCoordZ      ],
			   chunkDots[cellCoordX      ][cellCoordY + Lod][cellCoordZ      ],
			   chunkDots[cellCoordX + Lod][cellCoordY + Lod][cellCoordZ      ],
			   chunkDots[cellCoordX      ][cellCoordY      ][cellCoordZ + Lod],
			   chunkDots[cellCoordX + Lod][cellCoordY      ][cellCoordZ + Lod],
			   chunkDots[cellCoordX      ][cellCoordY + Lod][cellCoordZ + Lod],
			   chunkDots[cellCoordX + Lod][cellCoordY + Lod][cellCoordZ + Lod] };

	return cell;
}

TransCell AChunk::TransCellAt(int cellCoordX, int cellCoordY, int cellCoordZ, Direction directionOfHigherLod)
{
	//     n---------n
	//    /|        /|
	//   / |       / |
	//  /  |      /  |
	// n---------n   |
	// |   |     |   |
	// |   n-----|---n
	// z  /      |  /
	// | y       | /
	// |/        |/
	// 0----x----n
	// Layout of the vertices
	// Full-resolution face has 9 vertices (4 for quadrants + 5 shared with half-resolution)
	//  6---7---8
	//  |   |   |
	//  3---4---5
	//  |   |   |
	//  0---1---2
	// 
	//  Half - resolution face has 4 vertices:
	//  B-------C
	//  |       |
	//  |       |
	//  |       |
	//  9-------A
	//  Half-resolution samples 9, A, B, C are the same as Full-resolution samples 0, 2, 6, 8

	if (directionOfHigherLod == PosX)
		return TransCell { chunkDots[cellCoordX + Lod][cellCoordY + Lod    ][cellCoordZ          ],
						   chunkDots[cellCoordX + Lod][cellCoordY + Lod / 2][cellCoordZ          ],
						   chunkDots[cellCoordX + Lod][cellCoordY          ][cellCoordZ          ],
						   chunkDots[cellCoordX + Lod][cellCoordY + Lod    ][cellCoordZ + Lod / 2],
						   chunkDots[cellCoordX + Lod][cellCoordY + Lod / 2][cellCoordZ + Lod / 2],
						   chunkDots[cellCoordX + Lod][cellCoordY          ][cellCoordZ + Lod / 2],
						   chunkDots[cellCoordX + Lod][cellCoordY + Lod    ][cellCoordZ + Lod    ],
						   chunkDots[cellCoordX + Lod][cellCoordY + Lod / 2][cellCoordZ + Lod    ],
						   chunkDots[cellCoordX + Lod][cellCoordY          ][cellCoordZ + Lod    ]};
	else if (directionOfHigherLod == NegX)
		return TransCell { chunkDots[cellCoordX][cellCoordY + Lod    ][cellCoordZ          ],
						   chunkDots[cellCoordX][cellCoordY + Lod / 2][cellCoordZ          ],
						   chunkDots[cellCoordX][cellCoordY          ][cellCoordZ          ],
						   chunkDots[cellCoordX][cellCoordY + Lod    ][cellCoordZ + Lod / 2],
						   chunkDots[cellCoordX][cellCoordY + Lod / 2][cellCoordZ + Lod / 2],
						   chunkDots[cellCoordX][cellCoordY          ][cellCoordZ + Lod / 2],
						   chunkDots[cellCoordX][cellCoordY + Lod    ][cellCoordZ + Lod    ],
						   chunkDots[cellCoordX][cellCoordY + Lod / 2][cellCoordZ + Lod    ],
						   chunkDots[cellCoordX][cellCoordY          ][cellCoordZ + Lod    ]};
	else if (directionOfHigherLod == PosY)
		return TransCell { chunkDots[cellCoordX          ][cellCoordY + Lod][cellCoordZ          ],
						   chunkDots[cellCoordX + Lod / 2][cellCoordY + Lod][cellCoordZ          ],
						   chunkDots[cellCoordX + Lod    ][cellCoordY + Lod][cellCoordZ          ],
						   chunkDots[cellCoordX          ][cellCoordY + Lod][cellCoordZ + Lod / 2],
						   chunkDots[cellCoordX + Lod / 2][cellCoordY + Lod][cellCoordZ + Lod / 2],
						   chunkDots[cellCoordX + Lod    ][cellCoordY + Lod][cellCoordZ + Lod / 2],
						   chunkDots[cellCoordX          ][cellCoordY + Lod][cellCoordZ + Lod    ],
						   chunkDots[cellCoordX + Lod / 2][cellCoordY + Lod][cellCoordZ + Lod    ],
						   chunkDots[cellCoordX + Lod    ][cellCoordY + Lod][cellCoordZ + Lod    ]};
	else if (directionOfHigherLod == NegY)
		return TransCell { chunkDots[cellCoordX          ][cellCoordY][cellCoordZ          ],
						   chunkDots[cellCoordX + Lod / 2][cellCoordY][cellCoordZ          ],
						   chunkDots[cellCoordX + Lod    ][cellCoordY][cellCoordZ          ],
						   chunkDots[cellCoordX          ][cellCoordY][cellCoordZ + Lod / 2],
						   chunkDots[cellCoordX + Lod / 2][cellCoordY][cellCoordZ + Lod / 2],
						   chunkDots[cellCoordX + Lod    ][cellCoordY][cellCoordZ + Lod / 2],
						   chunkDots[cellCoordX          ][cellCoordY][cellCoordZ + Lod    ],
						   chunkDots[cellCoordX + Lod / 2][cellCoordY][cellCoordZ + Lod    ],
						   chunkDots[cellCoordX + Lod    ][cellCoordY][cellCoordZ + Lod    ]};
	else if (directionOfHigherLod == PosZ)
		return TransCell { chunkDots[cellCoordX          ][cellCoordY + Lod    ][cellCoordZ + Lod],
						   chunkDots[cellCoordX + Lod / 2][cellCoordY + Lod    ][cellCoordZ + Lod],
						   chunkDots[cellCoordX + Lod    ][cellCoordY + Lod    ][cellCoordZ + Lod],
						   chunkDots[cellCoordX          ][cellCoordY + Lod / 2][cellCoordZ + Lod],
						   chunkDots[cellCoordX + Lod / 2][cellCoordY + Lod / 2][cellCoordZ + Lod],
						   chunkDots[cellCoordX + Lod    ][cellCoordY + Lod / 2][cellCoordZ + Lod],
						   chunkDots[cellCoordX          ][cellCoordY          ][cellCoordZ + Lod],
						   chunkDots[cellCoordX + Lod / 2][cellCoordY          ][cellCoordZ + Lod],
						   chunkDots[cellCoordX + Lod    ][cellCoordY          ][cellCoordZ + Lod]};
	else
		return TransCell { chunkDots[cellCoordX          ][cellCoordY + Lod    ][cellCoordZ],
						   chunkDots[cellCoordX + Lod / 2][cellCoordY + Lod    ][cellCoordZ],
						   chunkDots[cellCoordX + Lod    ][cellCoordY + Lod    ][cellCoordZ],
						   chunkDots[cellCoordX          ][cellCoordY + Lod / 2][cellCoordZ],
						   chunkDots[cellCoordX + Lod / 2][cellCoordY + Lod / 2][cellCoordZ],
						   chunkDots[cellCoordX + Lod    ][cellCoordY + Lod / 2][cellCoordZ],
						   chunkDots[cellCoordX          ][cellCoordY          ][cellCoordZ],
						   chunkDots[cellCoordX + Lod / 2][cellCoordY          ][cellCoordZ],
						   chunkDots[cellCoordX + Lod    ][cellCoordY          ][cellCoordZ]};
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
					DrawDebugSphere(GetWorld(), GetActorLocation() + FVector(x, y, z) * 100.0f, 7.5f, 6, FColor::Red, true);
				//else
				//	DrawDebugSphere(GetWorld(), GetActorLocation() + FVector(x, y, z) * 100.0f, 5.0f, 6, FColor::Blue, true);
			}
		}
	}
}
