#include "Game/Structures.h"
#include "Game/StellarWorld.h"
#include "Terrain/PlanetTerrainActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Serialization/Archive.h"

namespace
{
	constexpr double UU = 100.0;
}

// ---- engine-free set ----

const TCHAR* FStructureSet::NameOf(EStructureType Type)
{
	switch (Type)
	{
	case EStructureType::FoundationPad: return TEXT("Foundation pad");
	case EStructureType::Depot: return TEXT("Depot");
	case EStructureType::Beacon: return TEXT("Survey beacon");
	default: return TEXT("?");
	}
}

FVector3d FStructureSet::HalfSize(EStructureType Type)
{
	switch (Type)
	{
	case EStructureType::FoundationPad: return FVector3d(4, 4, 0.6);
	case EStructureType::Depot: return FVector3d(3, 3, 4.2);
	case EStructureType::Beacon: return FVector3d(0.5, 0.5, 6.5);
	default: return FVector3d(1, 1, 1);
	}
}

uint32 FStructureSet::Add(EStructureType Type, const FVector3d& Position, const FVector3d& Up, const FVector3d& Forward, int32 BodyId)
{
	FStructure S;
	S.Id = NextId++;
	S.Type = Type;
	S.BodyId = BodyId;
	S.Position = Position;
	S.Up = Up.GetSafeNormal();
	S.Forward = (Forward - S.Up * Forward.Dot(S.Up)).GetSafeNormal();
	Items.Add(S);
	return S.Id;
}

bool FStructureSet::Remove(uint32 Id)
{
	return Items.RemoveAll([Id](const FStructure& S) { return S.Id == Id; }) > 0;
}

const FStructure* FStructureSet::Find(uint32 Id) const
{
	return Items.FindByPredicate([Id](const FStructure& S) { return S.Id == Id; });
}

const FStructure* FStructureSet::At(const FVector3d& P, double Margin) const
{
	for (const FStructure& S : Items)
	{
		const FVector3d Half = HalfSize(S.Type);
		const FVector3d Q = P - S.Position;
		const FVector3d Side = S.Up.Cross(S.Forward);
		const double H = Q.Dot(S.Up);
		if (FMath::Abs(Q.Dot(S.Forward)) <= Half.X + Margin && FMath::Abs(Q.Dot(Side)) <= Half.Y + Margin && H >= -1.0 - Margin && H <= Half.Z * 2 + Margin)
			return &S;
	}
	return nullptr;
}

bool FStructureSet::Overlaps(EStructureType Type, const FVector3d& Position, const FVector3d& Forward) const
{
	// Footprints as circles around their corners: conservative and cheap.
	const double R = HalfSize(Type).Size2D();
	for (const FStructure& S : Items)
		if (FVector3d::Dist(S.Position, Position) < R + HalfSize(S.Type).Size2D() * 0.9)
			return true;
	return false;
}

void FStructureSet::Serialize(FArchive& Ar)
{
	int32 Version = 1;
	Ar << Version;
	Ar << NextId;
	int32 Num = Items.Num();
	Ar << Num;
	if (Ar.IsLoading())
		Items.SetNum(FMath::Clamp(Num, 0, 100000));
	for (FStructure& S : Items)
	{
		uint8 Type = uint8(S.Type);
		Ar << S.Id << Type << S.BodyId << S.Position << S.Up << S.Forward;
		S.Type = EStructureType(FMath::Min<uint8>(Type, uint8(EStructureType::Count) - 1));
	}
}

// ---- actor ----

AStructuresActor::AStructuresActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;
}

void AStructuresActor::Setup(AStellarWorld* InWorld, APlanetTerrainActor* InTerrain)
{
	Planet = InWorld;
	Terrain = InTerrain;
	Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	auto Lin = [](uint8 R, uint8 G, uint8 B) { return FLinearColor::FromSRGBColor(FColor(R, G, B)); };
	// Brutalist kit: restrained concrete, one yellow safety accent, small lights.
	Concrete = Planet->SolidMaterial(Lin(150, 147, 140), 0.92f);
	ConcreteDark = Planet->SolidMaterial(Lin(98, 97, 94), 0.95f);
	Accent = Planet->SolidMaterial(Lin(214, 162, 34), 0.6f, 0.1f);
	Steel = Planet->SolidMaterial(Lin(80, 82, 84), 0.45f, 0.8f);
	Lamp = Planet->SolidMaterial(Lin(255, 220, 160), 0.5f, 0.f, 60.f);
	GhostValid = Planet->GhostMaterial(FLinearColor(0.2f, 0.9f, 0.4f));
	GhostInvalid = Planet->GhostMaterial(FLinearColor(1.f, 0.25f, 0.2f));
	Selected = Planet->GhostMaterial(FLinearColor(0.3f, 0.7f, 1.f));
}

void AStructuresActor::Box(USceneComponent* Parent, UMaterialInterface* M, const FVector& Center, const FVector& Size)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetMobility(EComponentMobility::Movable);
	C->SetStaticMesh(Cube);
	C->SetMaterial(0, M);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetupAttachment(Parent);
	C->SetRelativeLocation(Center * UU);
	C->SetRelativeScale3D(Size);
	C->RegisterComponent();
}

USceneComponent* AStructuresActor::Build(EStructureType Type, UMaterialInterface* Override)
{
	USceneComponent* Node = NewObject<USceneComponent>(this);
	Node->SetMobility(EComponentMobility::Movable);
	Node->SetupAttachment(Root);
	Node->RegisterComponent();
	auto M = [Override](UMaterialInterface* Default) { return Override != nullptr ? Override : Default; };
	// Local frame: X forward, Y right, Z up (metres), origin at the ground.
	switch (Type)
	{
	case EStructureType::FoundationPad:
		Box(Node, M(Concrete), FVector(0, 0, -0.4), FVector(8, 8, 2.0)); // sunk into the ground
		for (int32 I = 0; I < 4; I++)
		{
			const bool bAlongX = I < 2;
			const double S = (I % 2 == 0 ? 1 : -1) * 3.85;
			Box(Node, M(Accent), bAlongX ? FVector(0, S, 0.61) : FVector(S, 0, 0.61), bAlongX ? FVector(7.6, 0.18, 0.02) : FVector(0.18, 7.6, 0.02));
		}
		break;
	case EStructureType::Depot:
		Box(Node, M(ConcreteDark), FVector(0, 0, -0.2), FVector(6.4, 6.4, 1.0));
		Box(Node, M(Concrete), FVector(0, 0, 2.3), FVector(6, 6, 4.2));
		Box(Node, M(Steel), FVector(3.02, 0, 1.6), FVector(0.06, 3.2, 3.0)); // the loading door
		Box(Node, M(Accent), FVector(0, 0, 3.6), FVector(6.04, 6.04, 0.35));
		Box(Node, M(Lamp), FVector(3.05, 0, 3.25), FVector(0.05, 1.4, 0.12));
		break;
	case EStructureType::Beacon:
		Box(Node, M(ConcreteDark), FVector(0, 0, 0.1), FVector(1.0, 1.0, 0.6));
		Box(Node, M(Steel), FVector(0, 0, 3.2), FVector(0.18, 0.18, 6.0));
		Box(Node, M(Accent), FVector(0, 0, 5.0), FVector(0.2, 0.9, 0.6));
		Box(Node, M(Lamp), FVector(0, 0, 6.35), FVector(0.3, 0.3, 0.3));
		break;
	default:
		break;
	}
	return Node;
}

void AStructuresActor::PlaceComponent(USceneComponent* C, const FVector3d& Position, const FVector3d& Up, const FVector3d& Forward)
{
	const FQuat Frame = FRotationMatrix::MakeFromXZ(Terrain->PlanetDirToWorld(Forward), Terrain->PlanetDirToWorld(Up)).ToQuat();
	C->SetWorldLocationAndRotation(Terrain->PlanetToWorld(Position), Frame);
}

FPlacementCheck AStructuresActor::Check(EStructureType Type, const FVector3d& Point, const FVector3d& Forward) const
{
	FPlacementCheck R;
	const FPlanetTerrain& T = Terrain->GetTerrain();
	const FTerrainEdits* Edits = Terrain->GetEdits();
	R.Up = Point.GetSafeNormal();
	R.Forward = (Forward - R.Up * Forward.Dot(R.Up)).GetSafeNormal();
	const FVector3d Side = R.Up.Cross(R.Forward);
	const FVector3d Half = FStructureSet::HalfSize(Type);
	// The ground under the footprint: highest and lowest of a 3 x 3 sample, found by tracing down.
	double Highest = -1e30, Lowest = 1e30;
	for (int32 I = -1; I <= 1; I++)
		for (int32 J = -1; J <= 1; J++)
		{
			const FVector3d Column = Point + R.Forward * (I * Half.X) + Side * (J * Half.Y);
			// Rise through open air only (a cave's roof must not count as the ground), then trace down.
			double Rise = 0;
			while (Rise < 12.0 && T.Density(Column + R.Up * (Rise + 0.5), Edits) > 0)
				Rise += 0.5;
			FVector3d Hit;
			if (!T.Trace(Column + R.Up * Rise, -R.Up, Rise + 18.0, Edits, Hit))
			{
				R.Reason = TEXT("no ground under the footprint");
				return R;
			}
			const double H = (Hit - Point).Dot(R.Up);
			Highest = FMath::Max(Highest, H);
			Lowest = FMath::Min(Lowest, H);
		}
	R.Position = Point + R.Up * Highest;
	const double Uneven = Highest - Lowest;
	if (Type != EStructureType::Beacon && Uneven > 1.2)
		R.Reason = FString::Printf(TEXT("ground uneven by %.1f m - grade it first (Ctrl+RMB)"), Uneven);
	else if (Set.Overlaps(Type, R.Position, R.Forward))
		R.Reason = TEXT("overlaps another structure");
	else
	{
		R.bValid = true;
		R.Reason = Uneven > 0.05 ? FString::Printf(TEXT("ok (%.1f m of footing)"), Uneven) : FString(TEXT("ok"));
	}
	return R;
}

uint32 AStructuresActor::Place(const FPlacementCheck& Where, EStructureType Type)
{
	if (!Where.bValid)
		return 0;
	const uint32 Id = Set.Add(Type, Where.Position, Where.Up, Where.Forward);
	USceneComponent* C = Build(Type, nullptr);
	PlaceComponent(C, Where.Position, Where.Up, Where.Forward);
	Visuals.Add(Id, C);
	return Id;
}

bool AStructuresActor::Remove(uint32 Id)
{
	if (!Set.Remove(Id))
		return false;
	if (TObjectPtr<USceneComponent>* C = Visuals.Find(Id))
	{
		TArray<USceneComponent*> Parts;
		(*C)->GetChildrenComponents(true, Parts);
		for (USceneComponent* Child : Parts)
			Child->DestroyComponent();
		(*C)->DestroyComponent();
		Visuals.Remove(Id);
	}
	if (HighlightId == Id)
		Highlight(0);
	return true;
}

void AStructuresActor::Rebuild()
{
	for (TPair<uint32, TObjectPtr<USceneComponent>>& Pair : Visuals)
	{
		TArray<USceneComponent*> Parts;
		Pair.Value->GetChildrenComponents(true, Parts);
		for (USceneComponent* Child : Parts)
			Child->DestroyComponent();
		Pair.Value->DestroyComponent();
	}
	Visuals.Reset();
	for (const FStructure& S : Set.All())
	{
		USceneComponent* C = Build(S.Type, nullptr);
		PlaceComponent(C, S.Position, S.Up, S.Forward);
		Visuals.Add(S.Id, C);
	}
	Highlight(0);
}

void AStructuresActor::ShowPreview(EStructureType Type, const FPlacementCheck& Where)
{
	if (Previews.Num() == 0)
		for (int32 I = 0; I < int32(EStructureType::Count) * 2; I++)
		{
			USceneComponent* C = Build(EStructureType(I / 2), I % 2 == 0 ? GhostValid.Get() : GhostInvalid.Get());
			C->SetVisibility(false, true);
			Previews.Add(C);
		}
	for (int32 I = 0; I < Previews.Num(); I++)
	{
		const bool bShow = I / 2 == int32(Type) && (I % 2 == 0) == Where.bValid;
		Previews[I]->SetVisibility(bShow, true);
		if (bShow)
			PlaceComponent(Previews[I], Where.Position, Where.Up, Where.Forward);
	}
}

void AStructuresActor::HidePreview()
{
	for (USceneComponent* C : Previews)
		C->SetVisibility(false, true);
}

void AStructuresActor::Highlight(uint32 Id)
{
	if (Selection != nullptr)
	{
		TArray<USceneComponent*> Parts;
		Selection->GetChildrenComponents(true, Parts);
		for (USceneComponent* Child : Parts)
			Child->DestroyComponent();
		Selection->DestroyComponent();
		Selection = nullptr;
	}
	HighlightId = Id;
	const FStructure* S = Set.Find(Id);
	if (S == nullptr)
		return;
	// A slightly larger translucent copy around the selected structure.
	Selection = Build(S->Type, Selected);
	Selection->SetRelativeScale3D(FVector(1.04));
	PlaceComponent(Selection, S->Position, S->Up, S->Forward);
}
