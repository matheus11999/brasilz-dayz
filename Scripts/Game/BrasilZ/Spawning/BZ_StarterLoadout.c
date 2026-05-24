class BZ_StarterLoadout
{
	static const bool ENABLE_STARTER_LOADOUT = false;
	static const string LOADOUT_VERSION = "brasilz-civil-starter-v15";

	// Guard against duplicate Apply calls. Spawn handler + character controller both schedule
	// Apply on a callqueue, race condition lets the same item be inserted twice if the inventory
	// index hasn't replicated the previous insert yet. Tracking by entity id ensures one-shot.
	protected static ref set<int> s_AppliedEntities = new set<int>();

	// Starter kit BrasilZ. Edite esta lista para mudar o loadout inicial.
	static const ResourceName STARTER_MAP = "{983B57B8E95C1F52}Prefabs/Items/Equipment/Maps/Map_Paper_01/PaperMap_01_folded_FIA.et";
	static const ResourceName STARTER_FLASHLIGHT = "{575EA58E67448C2A}Prefabs/Items/Equipment/Flashlights/Flashlight_Soviet_01/Flashlight_Soviet_01.et";
	static const ResourceName STARTER_KNIFE = "{E5FF2886246E0797}Prefabs/Knifes/HuntsmanKnife/HuntsmanKnifeAC.et";
	static const ResourceName STARTER_APPLE = "{619D10551472D7DC}Prefabs/FoodDrink/Apple.et";
	static const ResourceName STARTER_WATER = "{2DA40953CC5C6D88}Prefabs/FoodDrink/WaterBottler.et";
	static const ResourceName STARTER_WALLET = "{F539F8AD9A0D2249}Prefabs/Items/WalletFisicalValue.et";
	static const ResourceName STARTER_BIKE = "{B70300000000D100}Prefabs/Items/Deployables/BrasilZ_DeployableBike_01.et";
	static const ResourceName STARTER_BANDAGE = "{3BD9B80FAAF5E8B5}Prefabs/Items/Medicine/Gauze.et";

	//------------------------------------------------------------------------------------------------
	static void Apply(IEntity character)
	{
		if (!ENABLE_STARTER_LOADOUT)
			return;

		if (!character)
			return;

		RplComponent rpl = RplComponent.Cast(character.FindComponent(RplComponent));
		if (GetGame().InPlayMode() && rpl && !rpl.IsMaster())
			return;

		// Duplicate-Apply guard. Multiple call sites schedule Apply on a callqueue (spawn handler
		// fires 3 calls at 250/1250/3000ms, character controller fires 3 more at 1000/3000/6000ms).
		// Without this check, a race in HasItemInInventory (inventory index lag) lets the second
		// pass insert the same item again. One-shot per entity instance fixes that.
		int entityId;
		if (rpl)
			entityId = rpl.Id();
		else
			entityId = character.GetID();

		if (s_AppliedEntities.Contains(entityId))
		{
			Print(string.Format("[BrasilZ] Starter loadout already applied for entity %1, skipping.", entityId));
			return;
		}

		InventoryStorageManagerComponent storageManager = InventoryStorageManagerComponent.Cast(character.FindComponent(InventoryStorageManagerComponent));
		if (!storageManager)
			return;

		if (HasProgressInventory(character))
		{
			Print("[BrasilZ] Starter loadout skipped; character already has persisted progress.");
			s_AppliedEntities.Insert(entityId);
			return;
		}

		StripMilitaryItems(character);

		TryAddItem(character, storageManager, STARTER_MAP);
		TryAddItem(character, storageManager, STARTER_FLASHLIGHT);
		TryAddItem(character, storageManager, STARTER_KNIFE);
		TryAddItem(character, storageManager, STARTER_APPLE);
		TryAddItem(character, storageManager, STARTER_WATER);
		TryAddItem(character, storageManager, STARTER_WALLET);
		TryAddItem(character, storageManager, STARTER_BIKE);
		TryAddItem(character, storageManager, STARTER_BANDAGE);

		s_AppliedEntities.Insert(entityId);
		Print(string.Format("[BrasilZ] Custom starter loadout applied. Version=%1 entity=%2", LOADOUT_VERSION, entityId));
	}

	//------------------------------------------------------------------------------------------------
	protected static bool TryAddItem(IEntity character, InventoryStorageManagerComponent storageManager, ResourceName prefab)
	{
		if (HasItemInInventory(storageManager, prefab) || HasStarterResourceInInventory(character, prefab))
			return true;

		Resource resource = Resource.Load(prefab);
		if (!resource || !resource.IsValid())
		{
			Print(string.Format("[BrasilZ] Starter item resource failed: %1", prefab), LogLevel.WARNING);
			return false;
		}

		vector mat[4];
		BuildSpawnTransform(character, mat);

		EntitySpawnParams spawnParams();
		spawnParams.TransformMode = ETransformMode.WORLD;
		for (int i = 0; i < 4; i++)
			spawnParams.Transform[i] = mat[i];

		IEntity item = GetGame().SpawnEntityPrefab(resource, character.GetWorld(), spawnParams);
		if (!item)
			return false;

		if (storageManager.TryInsertItem(item) && HasVisibleInventoryParent(item))
		{
			Print(string.Format("[BrasilZ] Starter item added: %1", prefab));
			return true;
		}

		if (TryInsertIntoAnyCharacterStorage(character, storageManager, item))
		{
			Print(string.Format("[BrasilZ] Starter item added to fallback storage: %1", prefab));
			return true;
		}

		Print(string.Format("[BrasilZ] Starter item did not fit inventory, deleting overflow item: %1", prefab), LogLevel.WARNING);
		SCR_EntityHelper.DeleteEntityAndChildren(item);
		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool TryInsertIntoAnyCharacterStorage(IEntity character, InventoryStorageManagerComponent storageManager, IEntity item)
	{
		array<Managed> storages();
		character.FindComponents(BaseInventoryStorageComponent, storages);

		foreach (Managed storageRef : storages)
		{
			BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(storageRef);
			if (!storage)
				continue;

			if (storageManager.TryInsertItemInStorage(item, storage) && HasVisibleInventoryParent(item))
				return true;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool HasItemInInventory(InventoryStorageManagerComponent storageManager, ResourceName prefab)
	{
		array<IEntity> foundItems();
		SCR_PrefabNamePredicate prefabNamePredicate();
		prefabNamePredicate.prefabName = prefab;
		storageManager.FindItems(foundItems, prefabNamePredicate);
		return !foundItems.IsEmpty();
	}

	//------------------------------------------------------------------------------------------------
	protected static bool HasStarterResourceInInventory(IEntity character, ResourceName prefab)
	{
		array<Managed> storages();
		character.FindComponents(BaseInventoryStorageComponent, storages);

		foreach (Managed storageRef : storages)
		{
			BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(storageRef);
			if (!storage)
				continue;

			array<IEntity> items();
			storage.GetAll(items);

			foreach (IEntity item : items)
			{
				if (IsEntityMatchingStarterPrefab(item, prefab))
					return true;
			}
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool HasVisibleInventoryParent(IEntity item)
	{
		InventoryItemComponent inventoryItemComponent = InventoryItemComponent.Cast(item.FindComponent(InventoryItemComponent));
		if (!inventoryItemComponent)
			return false;

		return inventoryItemComponent.GetParentSlot() != null;
	}

	//------------------------------------------------------------------------------------------------
	protected static void BuildSpawnTransform(IEntity character, out vector itemMat[4])
	{
		vector charMat[4];
		character.GetWorldTransform(charMat);
		vector itemPos = charMat[3] + charMat[2] * 1.5;
		itemPos[1] = itemPos[1] + 0.35;

		Math3D.MatrixIdentity4(itemMat);
		itemMat[0] = charMat[0];
		itemMat[1] = charMat[1];
		itemMat[2] = charMat[2];
		itemMat[3] = itemPos;
	}

	//------------------------------------------------------------------------------------------------
	protected static void StripMilitaryItems(IEntity character)
	{
		CharacterControllerComponent controller = CharacterControllerComponent.Cast(character.FindComponent(CharacterControllerComponent));
		if (controller)
		{
			IEntity leftHandGadget = controller.GetAttachedGadgetAtLeftHandSlot();
			if (leftHandGadget && ShouldRemoveFromStarterLoadout(leftHandGadget))
			{
				controller.RemoveGadgetFromHand(true);
				Print(string.Format("[BrasilZ] Removing hand gadget: %1", GetEntityPrefabName(leftHandGadget)));
				SCR_EntityHelper.DeleteEntityAndChildren(leftHandGadget);
			}

			auto weaponManager = controller.GetWeaponManagerComponent();
			if (weaponManager)
			{
				BaseWeaponComponent currentWeapon = weaponManager.GetCurrent();
				if (currentWeapon)
				{
					IEntity currentWeaponEntity = currentWeapon.GetOwner();
					if (!IsStarterItem(currentWeaponEntity))
					{
						controller.SelectWeapon(null);
						Print(string.Format("[BrasilZ] Removing equipped weapon: %1", GetEntityPrefabName(currentWeaponEntity)));
						SCR_EntityHelper.DeleteEntityAndChildren(currentWeaponEntity);
					}
				}
			}
		}

		InventoryStorageManagerComponent storageManager = InventoryStorageManagerComponent.Cast(character.FindComponent(InventoryStorageManagerComponent));
		if (!storageManager)
			return;

		array<Managed> storages();
		character.FindComponents(BaseInventoryStorageComponent, storages);

		foreach (Managed storageRef : storages)
		{
			BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(storageRef);
			if (!storage)
				continue;

			array<IEntity> items();
			storage.GetAll(items);

			foreach (IEntity item : items)
			{
				if (!ShouldRemoveFromStarterLoadout(item))
					continue;

				Print(string.Format("[BrasilZ] Removing starter prefab item: %1", GetEntityPrefabName(item)));
				storageManager.TryRemoveItemFromStorage(item, storage);
				SCR_EntityHelper.DeleteEntityAndChildren(item);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static bool ShouldRemoveFromStarterLoadout(IEntity item)
	{
		if (!item)
			return false;

		if (IsStarterItem(item))
			return false;

		if (item.FindComponent(BaseWeaponComponent))
			return true;

		string prefabName = GetEntityPrefabName(item);
		if (prefabName.IsEmpty())
			return false;

		if (prefabName.Contains("Prefabs/Weapons/") || prefabName.Contains("Prefabs/weapons/"))
			return true;

		if (prefabName.Contains("Prefabs/Magazines/") || prefabName.Contains("Prefabs/magazines/"))
			return true;

		if (prefabName.Contains("Prefabs/Items/Equipment/Backpacks/") || prefabName.Contains("/Backpacks/"))
			return true;

		if (prefabName.Contains("/Rucksacks/") || prefabName.Contains("Backpack_"))
			return true;

		if (prefabName.Contains("Rucksack_"))
			return true;

		if (prefabName.Contains("ALICEPack") || prefabName.Contains("AssaultPack"))
			return true;

		if (prefabName.Contains("Bergen"))
			return true;

		if (prefabName.Contains("Prefabs/Items/Equipment/Binoculars/") || prefabName.Contains("Binoculars"))
			return true;

		if (prefabName.Contains("binoculars"))
			return true;

		if (prefabName.Contains("/Magazines/") || prefabName.Contains("/Ammo/"))
			return true;

		if (prefabName.Contains("/Grenades/"))
			return true;

		if (prefabName.Contains("Magazine_") || prefabName.Contains("Ammo_"))
			return true;

		if (prefabName.Contains("Grenade_"))
			return true;

		if (prefabName.Contains("Handgun_") || prefabName.Contains("Rifle_"))
			return true;

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool HasProgressInventory(IEntity character)
	{
		array<Managed> storages();
		character.FindComponents(BaseInventoryStorageComponent, storages);

		foreach (Managed storageRef : storages)
		{
			BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(storageRef);
			if (!storage)
				continue;

			array<IEntity> items();
			storage.GetAll(items);

			foreach (IEntity item : items)
			{
				if (IsProgressItem(item))
					return true;
			}
		}

		CharacterControllerComponent controller = CharacterControllerComponent.Cast(character.FindComponent(CharacterControllerComponent));
		if (!controller)
			return false;

		auto weaponManager = controller.GetWeaponManagerComponent();
		if (!weaponManager)
			return false;

		BaseWeaponComponent currentWeapon = weaponManager.GetCurrent();
		return currentWeapon != null;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool HasAnyStarterItem(IEntity character)
	{
		array<Managed> storages();
		character.FindComponents(BaseInventoryStorageComponent, storages);

		foreach (Managed storageRef : storages)
		{
			BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(storageRef);
			if (!storage)
				continue;

			array<IEntity> items();
			storage.GetAll(items);

			foreach (IEntity item : items)
			{
				if (IsStarterItem(item))
					return true;
			}
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsProgressItem(IEntity item)
	{
		if (!item || IsStarterItem(item))
			return false;

		if (item.FindComponent(BaseWeaponComponent))
			return true;

		string prefabName = GetEntityPrefabName(item);
		if (prefabName.IsEmpty())
			return false;

		if (prefabName.Contains("Prefabs/Weapons/") || prefabName.Contains("Prefabs/weapons/"))
			return true;

		if (prefabName.Contains("Prefabs/Magazines/") || prefabName.Contains("/Magazines/"))
			return true;

		if (prefabName.Contains("Prefabs/Items/Equipment/Wallet") || prefabName.Contains("Prefabs/Items/Money"))
			return true;

		if (prefabName.Contains("Wallet") || prefabName.Contains("Money"))
			return true;

		if (prefabName.Contains("Prefabs/Items/Equipment/Backpacks/") || prefabName.Contains("/Backpacks/"))
			return true;

		if (prefabName.Contains("/Rucksacks/") || prefabName.Contains("Backpack_"))
			return true;

		if (prefabName.Contains("Rucksack_"))
			return true;

		if (prefabName.Contains("NVG") || prefabName.Contains("GPNVG") || prefabName.Contains("NightVision"))
			return true;

		if (prefabName.Contains("Magazine_") || prefabName.Contains("Ammo_"))
			return true;

		if (prefabName.Contains("Grenade_"))
			return true;

		if (prefabName.Contains("Handgun_") || prefabName.Contains("Rifle_"))
			return true;

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsStarterItem(IEntity item)
	{
		if (IsEntityMatchingStarterPrefab(item, STARTER_MAP))
			return true;

		if (IsEntityMatchingStarterPrefab(item, STARTER_FLASHLIGHT))
			return true;

		if (IsEntityMatchingStarterPrefab(item, STARTER_KNIFE))
			return true;

		if (IsEntityMatchingStarterPrefab(item, STARTER_APPLE))
			return true;

		if (IsEntityMatchingStarterPrefab(item, STARTER_WATER))
			return true;

		if (IsEntityMatchingStarterPrefab(item, STARTER_WALLET))
			return true;

		if (IsEntityMatchingStarterPrefab(item, STARTER_BIKE))
			return true;

		if (IsEntityMatchingStarterPrefab(item, STARTER_BANDAGE))
			return true;

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsEntityMatchingStarterPrefab(IEntity item, ResourceName starterPrefab)
	{
		string prefabName = GetEntityPrefabName(item);
		if (prefabName.IsEmpty())
			return false;

		string starterName = starterPrefab;
		if (prefabName == starterName)
			return true;

		if (starterName.Contains("PaperMap_01_folded_FIA.et") && prefabName.Contains("PaperMap_01_folded_FIA.et"))
			return true;

		if (starterName.Contains("Flashlight_Soviet_01.et") && prefabName.Contains("Flashlight_Soviet_01.et"))
			return true;

		if (starterName.Contains("HuntsmanKnifeAC.et") && prefabName.Contains("HuntsmanKnifeAC.et"))
			return true;

		if (starterName.Contains("Apple.et") && prefabName.Contains("Apple.et"))
			return true;

		if (starterName.Contains("WaterBottler.et") && prefabName.Contains("WaterBottler.et"))
			return true;

		if (starterName.Contains("WalletFisicalValue.et") && prefabName.Contains("WalletFisicalValue.et"))
			return true;

		if ((starterName.Contains("DeployableBike_01.et") || starterName.Contains("BrasilZ_DeployableBike_01.et")) && (prefabName.Contains("DeployableBike_01.et") || prefabName.Contains("BrasilZ_DeployableBike_01.et")))
			return true;

		if (starterName.Contains("Medicine/Gauze.et") && prefabName.Contains("Medicine/Gauze.et"))
			return true;

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected static string GetEntityPrefabName(IEntity entity)
	{
		if (!entity)
			return string.Empty;

		auto prefabData = entity.GetPrefabData();
		if (!prefabData)
			return string.Empty;

		return prefabData.GetPrefabName();
	}

	//------------------------------------------------------------------------------------------------
	protected static void DumpInventory(IEntity character, string label)
	{
		if (!character)
			return;

		Print(string.Format("[BrasilZ] Inventory dump %1:", label));

		array<Managed> storages();
		character.FindComponents(BaseInventoryStorageComponent, storages);

		foreach (Managed storageRef : storages)
		{
			BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(storageRef);
			if (!storage)
				continue;

			array<IEntity> items();
			storage.GetAll(items);

			foreach (IEntity item : items)
			{
				if (!item)
					continue;

				Print(string.Format("[BrasilZ]   %1", GetEntityPrefabName(item)));
			}
		}
	}
}

modded class SCR_CharacterControllerComponent
{
	//------------------------------------------------------------------------------------------------
	override void OnControlledByPlayer(IEntity owner, bool controlled)
	{
		super.OnControlledByPlayer(owner, controlled);

		if (!controlled)
			return;

		GetGame().GetCallqueue().CallLater(BZ_StarterLoadout.Apply, 1000, false, owner);
		GetGame().GetCallqueue().CallLater(BZ_StarterLoadout.Apply, 3000, false, owner);
		GetGame().GetCallqueue().CallLater(BZ_StarterLoadout.Apply, 6000, false, owner);
	}
}
