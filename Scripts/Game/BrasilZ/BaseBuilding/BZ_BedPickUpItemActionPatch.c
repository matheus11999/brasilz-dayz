// BrasilZ safety patch for BuildingSystem2 sleeping bag pickup action.
// Prevents null-pointer crashes when the action runs while ownership/RPL/persistence
// components are not fully available.
modded class SCR_BedPickUpItemAction
{
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		if (!pOwnerEntity || !pUserEntity)
		{
			Print("[BrasilZ][BedPickup] Missing owner/user entity, aborting pickup action.", LogLevel.WARNING);
			return;
		}

		if (Replication.IsServer())
		{
			PersistenceSystem persistence = PersistenceSystem.GetInstance();
			if (persistence && BLD_BedManagerComponent.instance)
			{
				string bedId = persistence.GetId(pOwnerEntity);
				if (!bedId.IsEmpty())
					BLD_BedManagerComponent.instance.RemoveBed(bedId);
			}

			BLD_OwnershipComponent ownership = BLD_OwnershipComponent.Cast(pOwnerEntity.FindComponent(BLD_OwnershipComponent));
			if (ownership)
				ownership.csiod(pUserEntity);
		}

		RplComponent rpl = RplComponent.Cast(pUserEntity.FindComponent(RplComponent));
		if (!rpl || !rpl.IsOwner())
			return;

		super.PerformAction(pOwnerEntity, pUserEntity);
	}

	override protected void PerformActionInternal(SCR_InventoryStorageManagerComponent manager, IEntity pOwnerEntity, IEntity pUserEntity)
	{
		if (!manager || !pOwnerEntity || !pUserEntity)
			return;

		RplComponent rpl = RplComponent.Cast(pUserEntity.FindComponent(RplComponent));
		if (!rpl || !rpl.IsOwner())
			return;

		manager.InsertItem(pOwnerEntity);
	}
}
