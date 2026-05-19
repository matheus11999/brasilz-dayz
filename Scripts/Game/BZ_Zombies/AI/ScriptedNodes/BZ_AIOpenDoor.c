// BrasilZ Zombies - Door Opening Override
// Prevents zombies from opening doors

class BZ_AIOpenDoor : SCR_AIOpenDoor
{
	//------------------------------------------------------------------------------------------------
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		// Block zombies from opening doors
		if (!owner)
			return ENodeResult.FAIL;

		IEntity controlledEntity = owner.GetControlledEntity();
		if (!controlledEntity)
			return ENodeResult.FAIL;

		// Check if it's a zombie
		BZ_ZombieCharacter zombie = BZ_ZombieCharacter.Cast(controlledEntity);
		if (zombie)
		{
			// Zombies cannot open doors
			return ENodeResult.FAIL;
		}

		// Allow non-zombies to open doors normally
		return super.EOnTaskSimulate(owner, dt);
	}
}

