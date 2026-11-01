#pragma once

#include "CoreMinimal.h"
#include "Terrain/PlanetField.h"

/**
 * Item types (values stable, as in the Godot project): 0-9 raw and refined materials, 10-29
 * robot parts, 30-39 research samples, 40+ manufactured intermediates. Production recipes come
 * with milestone 1's machines with local buffers; the catalogue is kept whole so saves and
 * future systems share one numbering.
 */
enum class EItemType : uint8
{
	Stone = 0,
	IronOre = 1,
	IronIngot = 2,
	CopperOre = 3,
	Quartz = 4,
	CopperIngot = 5,
	Silicon = 6,
	CrushedIron = 7,
	CrushedCopper = 8,

	PartCore = 10,
	PartFrame = 11,
	PartThruster = 12,
	PartDrill = 13,
	PartCargo = 14,
	PartBooster = 15,
	PartWheels = 16,

	SampleAlloy = 30,
	SampleData = 31,

	IronPlate = 40,
	CopperWire = 41,
	Circuit = 42,
};

namespace Items
{
	STELLAR_API const TCHAR* NameOf(EItemType Item);

	/** The item a bite of this rock breaks loose as. */
	STELLAR_API EItemType ForMaterial(EGeoMaterial Material);

	/** Items per cubic metre of removed rock (the Godot erosion's 1.5 per unit volume). */
	constexpr double ItemsPerCubicMetre = 1.5;
}

/** A counted bag of items with an optional capacity (0 = unlimited). Engine-free. */
class STELLAR_API FInventory
{
public:
	explicit FInventory(int32 InCapacity = 0) : Capacity(InCapacity) {}

	int32 Capacity;

	int32 Count(EItemType Item) const;
	int32 Total() const;
	int32 FreeSpace() const { return Capacity > 0 ? FMath::Max(0, Capacity - Total()) : MAX_int32; }
	bool IsFull() const { return Capacity > 0 && Total() >= Capacity; }

	/** Add up to Amount; returns how many fit. */
	int32 Add(EItemType Item, int32 Amount);
	/** Remove up to Amount; returns how many were removed. */
	int32 Remove(EItemType Item, int32 Amount);

	const TMap<EItemType, int32>& Contents() const { return Items; }
	void Serialize(FArchive& Ar);

private:
	TMap<EItemType, int32> Items;
};

/**
 * Fractional mining credit: removed volume accrues per material, whole items are paid out and
 * the remainder is kept (and saved), so many shallow bites yield exactly what they removed.
 */
class STELLAR_API FMiningCredit
{
public:
	/** Credit removed volume (m^3, negative when building) and return the whole items earned. */
	int32 Credit(EGeoMaterial Material, double Volume);

	void Serialize(FArchive& Ar);

private:
	double Pending[int32(EGeoMaterial::Count)] = {};
};
