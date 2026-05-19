//------------------------------------------------------------------------------------------------
//! Safety node to detect if zombie is stuck (no movement) while chasing and recover gracefully
class BZ_AISafetyUnstuck : AITaskScripted
{
	[Attribute("0.25", UIWidgets.EditBox, "Distance considered as no progress (meters)")]
	protected float m_fStuckDistanceThreshold;

	[Attribute("5", UIWidgets.EditBox, "Seconds of no movement before clearing target")]
	protected float m_fStuckTimeSeconds;

	[Attribute("10", UIWidgets.EditBox, "Cooldown after clearing target (seconds)")]
	protected float m_fCooldownSeconds;

	[Attribute("1", UIWidgets.CheckBox, "Enable debug logging")]
	protected bool m_bDebugMode;

	protected BZ_ZombieCharacter m_ZombieCharacter;
	protected vector m_vLastPos;
	protected float m_fAccumulatedStuck;
	protected float m_fCooldownUntil;
	protected float m_fDoorMeleeTimer;

	//------------------------------------------------------------------------------------------------
	override void OnInit(AIAgent owner)
	{
		GenericEntity entity = GenericEntity.Cast(owner.GetControlledEntity());
		if (!entity)
			return;

		m_ZombieCharacter = BZ_ZombieCharacter.Cast(entity);
		if (m_ZombieCharacter)
			m_vLastPos = m_ZombieCharacter.GetOrigin();

		m_fAccumulatedStuck = 0;
		m_fCooldownUntil = 0;
	}

	//------------------------------------------------------------------------------------------------
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		if (!m_ZombieCharacter)
		{
			GenericEntity entity = GenericEntity.Cast(owner.GetControlledEntity());
			if (entity)
				m_ZombieCharacter = BZ_ZombieCharacter.Cast(entity);

			if (!m_ZombieCharacter)
				return ENodeResult.SUCCESS; // Non-blocking safeguard
		}

		float now = GetGame().GetWorld().GetWorldTime() * 0.001; // seconds
		if (now < m_fCooldownUntil)
			return ENodeResult.SUCCESS; // In cooldown after a recovery

		IEntity currentTarget = m_ZombieCharacter.GetCurrentTarget();
		vector pos = m_ZombieCharacter.GetOrigin();
		float moved = vector.Distance(pos, m_vLastPos);

		// Update last position each tick
		m_vLastPos = pos;

		if (currentTarget)
		{
			if (moved < m_fStuckDistanceThreshold)
			{
				m_fAccumulatedStuck += dt;

				// While stuck, try to find and melee a nearby door
				m_fDoorMeleeTimer += dt;
				if (m_fDoorMeleeTimer >= 0.3)
				{
					FindAndMeleeDoor();
					m_fDoorMeleeTimer = 0;
				}
			}
			else
			{
				m_fAccumulatedStuck = 0;
			}

			if (m_fAccumulatedStuck >= m_fStuckTimeSeconds)
			{
				// Considered stuck - clear target to force investigate/wander and repath later
				m_ZombieCharacter.SetCurrentTarget(null);
				// Also clear the BT output variable to force selector to drop the chase branch
				ClearVariable("DetectedTarget");
				m_fCooldownUntil = now + m_fCooldownSeconds;
				m_fAccumulatedStuck = 0;
			}
		}
		else
		{
			// Not chasing - make sure counter is reset
			m_fAccumulatedStuck = 0;
		}

		return ENodeResult.SUCCESS; // This node only monitors and nudges state
	}

	//------------------------------------------------------------------------------------------------
	protected void FindAndMeleeDoor()
	{
		if (!m_ZombieCharacter)
			return;

		vector zombiePos = m_ZombieCharacter.GetOrigin();
		float searchRadius = 2.0; // Search for doors within 2 meters

		IEntity closestDoor = null;
		float closestDistance = searchRadius;

		// Search through all entities in the world
		IEntity entity = GetGame().GetWorld().FindEntityByName("");
		while (entity)
		{
			if (entity)
			{
				// Check if entity has a DoorComponent
				DoorComponent doorComp = DoorComponent.Cast(entity.FindComponent(DoorComponent));
				if (doorComp)
				{
					// Check if door is not fully open
					float doorState = doorComp.GetDoorState();
					float angleRange = doorComp.GetAngleRange();
					if (Math.AbsFloat(angleRange - doorState) >= 0.1) // Door is not fully open
					{
						// Find closest door
						float distance = vector.Distance(zombiePos, entity.GetOrigin());
						if (distance < closestDistance)
						{
							closestDistance = distance;
							closestDoor = entity;
						}
					}
				}
			}

			entity = entity.GetSibling();
		}

		// If we found a door, aim at it and melee
		if (closestDoor)
		{
			// Aim at the door
			vector doorPos = closestDoor.GetOrigin();
			vector dirToTarget = doorPos - zombiePos;
			dirToTarget.Normalize();

			// Set the door as target for aiming
			m_ZombieCharacter.SetCurrentTarget(closestDoor);
		}
	}

	//------------------------------------------------------------------------------------------------
	static override bool VisibleInPalette()
	{
		return true;
	}
}

