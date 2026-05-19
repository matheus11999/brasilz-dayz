class BZ_ShopCurrencyHelper
{
	static const ResourceName FM_PHYSICAL_WALLET = "{F539F8AD9A0D2249}Prefabs/Items/WalletFisicalValue.et";
	static const ResourceName SHOP_SYSTEM_WALLET = "{B0E67230AEEE2DF3}Prefabs/Items/Wallet.et";

	//------------------------------------------------------------------------------------------------
	static bool EnsureCurrencyWallet(IEntity player)
	{
		if (!player)
			return false;

		SCR_InventoryStorageManagerComponent inventory = SCR_InventoryStorageManagerComponent.Cast(player.FindComponent(SCR_InventoryStorageManagerComponent));
		if (!inventory)
			return false;

		array<IEntity> currencyItems = ADM_CurrencyComponent.FindCurrencyInInventory(inventory);
		if (currencyItems && !currencyItems.IsEmpty())
			return true;

		if (SpawnWalletIntoInventory(player, inventory, FM_PHYSICAL_WALLET))
			return true;

		return SpawnWalletIntoInventory(player, inventory, SHOP_SYSTEM_WALLET);
	}

	//------------------------------------------------------------------------------------------------
	protected static bool SpawnWalletIntoInventory(IEntity player, SCR_InventoryStorageManagerComponent inventory, ResourceName walletPrefab)
	{
		Resource resource = Resource.Load(walletPrefab);
		if (!resource || !resource.IsValid())
			return false;

		vector mat[4];
		BuildSpawnTransform(player, mat);

		EntitySpawnParams spawnParams();
		spawnParams.TransformMode = ETransformMode.WORLD;
		for (int i = 0; i < 4; i++)
			spawnParams.Transform[i] = mat[i];

		IEntity wallet = GetGame().SpawnEntityPrefab(resource, player.GetWorld(), spawnParams);
		if (!wallet)
			return false;

		if (inventory.TryInsertItem(wallet) && HasVisibleInventoryParent(wallet))
		{
			Print(string.Format("[BrasilZ][Shop] Currency wallet created for shop payout: %1", walletPrefab), LogLevel.NORMAL);
			return true;
		}

		if (TryInsertIntoAnyStorage(player, inventory, wallet))
		{
			Print(string.Format("[BrasilZ][Shop] Currency wallet created in fallback storage: %1", walletPrefab), LogLevel.NORMAL);
			return true;
		}

		SCR_EntityHelper.DeleteEntityAndChildren(wallet);
		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool TryInsertIntoAnyStorage(IEntity player, SCR_InventoryStorageManagerComponent inventory, IEntity item)
	{
		array<Managed> storages();
		player.FindComponents(BaseInventoryStorageComponent, storages);

		foreach (Managed storageRef : storages)
		{
			BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(storageRef);
			if (!storage)
				continue;

			if (inventory.TryInsertItemInStorage(item, storage) && HasVisibleInventoryParent(item))
				return true;
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
	protected static void BuildSpawnTransform(IEntity player, out vector itemMat[4])
	{
		vector playerMat[4];
		player.GetWorldTransform(playerMat);
		vector itemPos = playerMat[3] + playerMat[2] * 1.5;
		itemPos[1] = itemPos[1] + 0.35;

		Math3D.MatrixIdentity4(itemMat);
		itemMat[0] = playerMat[0];
		itemMat[1] = playerMat[1];
		itemMat[2] = playerMat[2];
		itemMat[3] = itemPos;
	}
}

modded class ADM_PaymentMethodCurrency
{
	//------------------------------------------------------------------------------------------------
	override bool DistributePayment(IEntity player, int quantity = 1)
	{
		BZ_ShopCurrencyHelper.EnsureCurrencyWallet(player);

		bool success = super.DistributePayment(player, quantity);
		if (!success)
			Print("[BrasilZ][Shop] Could not distribute currency payout; player has no compatible currency wallet in inventory.", LogLevel.WARNING);

		return success;
	}
}
