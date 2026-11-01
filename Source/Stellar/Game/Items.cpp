#include "Game/Items.h"
#include "Serialization/Archive.h"

const TCHAR* Items::NameOf(EItemType Item)
{
	switch (Item)
	{
	case EItemType::Stone: return TEXT("Stone");
	case EItemType::IronOre: return TEXT("Iron Ore");
	case EItemType::IronIngot: return TEXT("Iron Ingot");
	case EItemType::CopperOre: return TEXT("Copper Ore");
	case EItemType::Quartz: return TEXT("Quartz");
	case EItemType::CopperIngot: return TEXT("Copper Ingot");
	case EItemType::Silicon: return TEXT("Silicon");
	case EItemType::CrushedIron: return TEXT("Crushed Iron");
	case EItemType::CrushedCopper: return TEXT("Crushed Copper");
	case EItemType::PartCore: return TEXT("Core Part");
	case EItemType::PartFrame: return TEXT("Frame Part");
	case EItemType::PartThruster: return TEXT("Thruster Part");
	case EItemType::PartDrill: return TEXT("Drill Part");
	case EItemType::PartCargo: return TEXT("Cargo Part");
	case EItemType::PartBooster: return TEXT("Booster Part");
	case EItemType::PartWheels: return TEXT("Wheels Part");
	case EItemType::SampleAlloy: return TEXT("Alloy Sample");
	case EItemType::SampleData: return TEXT("Data Core");
	case EItemType::IronPlate: return TEXT("Iron Plate");
	case EItemType::CopperWire: return TEXT("Copper Wire");
	case EItemType::Circuit: return TEXT("Circuit");
	default: return TEXT("?");
	}
}

EItemType Items::ForMaterial(EGeoMaterial Material)
{
	switch (Material)
	{
	case EGeoMaterial::IronOre: return EItemType::IronOre;
	case EGeoMaterial::CopperOre: return EItemType::CopperOre;
	case EGeoMaterial::Quartz: return EItemType::Quartz;
	default: return EItemType::Stone;
	}
}

int32 FInventory::Count(EItemType Item) const
{
	const int32* N = Items.Find(Item);
	return N != nullptr ? *N : 0;
}

int32 FInventory::Total() const
{
	int32 Sum = 0;
	for (const TPair<EItemType, int32>& Pair : Items)
		Sum += Pair.Value;
	return Sum;
}

int32 FInventory::Add(EItemType Item, int32 Amount)
{
	if (Amount <= 0)
		return 0;
	// Total can exceed Capacity after a capacity change; that simply reads as full.
	const int32 Fit = Capacity > 0 ? FMath::Min(Amount, FreeSpace()) : Amount;
	if (Fit > 0)
		Items.FindOrAdd(Item) += Fit;
	return Fit;
}

int32 FInventory::Remove(EItemType Item, int32 Amount)
{
	int32* N = Items.Find(Item);
	if (N == nullptr || Amount <= 0)
		return 0;
	const int32 Taken = FMath::Min(*N, Amount);
	*N -= Taken;
	if (*N == 0)
		Items.Remove(Item);
	return Taken;
}

void FInventory::Serialize(FArchive& Ar)
{
	Ar << Capacity;
	int32 Num = Items.Num();
	Ar << Num;
	if (Ar.IsLoading())
	{
		Items.Reset();
		for (int32 I = 0; I < Num && !Ar.IsError(); I++)
		{
			uint8 Type = 0;
			int32 Amount = 0;
			Ar << Type << Amount;
			if (Amount > 0)
				Items.Add(EItemType(Type), Amount);
		}
	}
	else
	{
		for (TPair<EItemType, int32>& Pair : Items)
		{
			uint8 Type = uint8(Pair.Key);
			Ar << Type << Pair.Value;
		}
	}
}

int32 FMiningCredit::Credit(EGeoMaterial Material, double Volume)
{
	double& P = Pending[int32(Material)];
	P += Volume * Items::ItemsPerCubicMetre;
	if (P < 0)
	{
		// Building pays nothing back and owes nothing: the credit cannot go negative.
		P = 0;
		return 0;
	}
	const int32 Units = int32(FMath::FloorToDouble(P));
	P -= Units;
	return Units;
}

void FMiningCredit::Serialize(FArchive& Ar)
{
	for (double& P : Pending)
		Ar << P;
}
