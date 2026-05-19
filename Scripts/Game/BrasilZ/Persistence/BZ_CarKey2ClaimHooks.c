modded class GRG_LockCarUserAction
{
	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		super.PerformAction(pOwnerEntity, pUserEntity);

		if (!BZ_CarKey2VehiclePersistence.IsEnabled())
			return;

		BZ_ClaimedVehicleRegistryComponent registry = BZ_ClaimedVehicleRegistryComponent.GetInstance();
		if (!registry)
			return;

		registry.RegisterClaim(pUserEntity, ResolveBrasilZVehicleOwner(pOwnerEntity));
	}

	//------------------------------------------------------------------------------------------------
	protected IEntity ResolveBrasilZVehicleOwner(IEntity ownerEntity)
	{
		if (!ownerEntity)
			return null;

		IEntity root = ownerEntity.GetRootParent();
		if (root)
			return root;

		return ownerEntity;
	}
}

modded class Key_SetCarCodeUserAction
{
	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		super.PerformAction(pOwnerEntity, pUserEntity);
		if (!BZ_CarKey2VehiclePersistence.IsEnabled())
			return;

		BZ_CarKey2VehiclePersistence.TouchActionVehicle(pOwnerEntity);
	}
}

modded class Key_TryCarCodeUserAction
{
	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		super.PerformAction(pOwnerEntity, pUserEntity);
		if (!BZ_CarKey2VehiclePersistence.IsEnabled())
			return;

		BZ_CarKey2VehiclePersistence.TouchActionVehicle(pOwnerEntity);
	}
}

modded class ToggleCarUserAction
{
	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		super.PerformAction(pOwnerEntity, pUserEntity);
		if (!BZ_CarKey2VehiclePersistence.IsEnabled())
			return;

		BZ_CarKey2VehiclePersistence.TouchActionVehicle(pOwnerEntity);
	}
}

class BZ_CarKey2VehiclePersistence
{
	//------------------------------------------------------------------------------------------------
	static bool IsEnabled()
	{
		return false;
	}

	//------------------------------------------------------------------------------------------------
	static void TouchActionVehicle(IEntity ownerEntity)
	{
		BZ_ClaimedVehicleRegistryComponent registry = BZ_ClaimedVehicleRegistryComponent.GetInstance();
		if (!registry || !ownerEntity)
			return;

		IEntity root = ownerEntity.GetRootParent();
		if (root)
			registry.TouchVehicle(root);
		else
			registry.TouchVehicle(ownerEntity);
	}
}
