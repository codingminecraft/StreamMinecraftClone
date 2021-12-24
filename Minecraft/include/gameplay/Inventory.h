#ifndef MINECRAFT_INVENTORY_H
#define MINECRAFT_INVENTORY_H
#include "core.h"
#include "utils/Constants.h"

namespace Minecraft
{
	struct InventorySlot
	{
		uint16 blockId;
		uint8 count;

		bool operator==(InventorySlot other) const
		{
			return other.blockId == blockId && other.count == count;
		}

		bool operator!=(InventorySlot other) const
		{
			return !(other == *this);
		}
	};

	struct Inventory
	{
		union
		{
			struct
			{
				InventorySlot hotbar[Player::numHotbarSlots];
				InventorySlot mainInventory[Player::numMainInventorySlots];
			};
			InventorySlot slots[Player::numHotbarSlots + Player::numMainInventorySlots];
		};
		int currentHotbarSlot;
	};
}

#endif 
