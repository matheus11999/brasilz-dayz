// BrasilZ Zombies - Door Melee Attack Node
// Triggers melee attacks when zombie is stuck at a door

class BZ_AIDoorMelee : AITaskScripted
{
	[Attribute("2.0", UIWidgets.EditBox, "Search radius for doors (meters)")]
	protected float m_fSearchRadius;

	[Attribute("0.5", UIWidgets.EditBox, "Melee attack interval (seconds)")]
	protected float m_fMeleeInterval;

	[Attribute("25", UIWidgets.EditBox, "Damage per melee attack")]
	protected float m_fDoorDamage;

	[Attribute("60", UIWidgets.EditBox, "Timeout - give up after this many seconds at door")]
	protected float m_fDoorTimeout;

	[Attribute("2.0", UIWidgets.EditBox, "Delay before first melee (seconds)")]
	protected float m_fBeginDelay;

	[Attribute("0", UIWidgets.CheckBox, "Enable debug logging")]
	protected bool m_bDebugMode;

	protected BZ_ZombieCharacter m_ZombieCharacter;
	protected float m_fMeleeTimer;
	protected IEntity m_CurrentDoor;
	protected float m_fTimeAtDoor;
    protected ref array<IEntity> m_aNearbyEntities = {};

    // Return a stable entity to represent the door (top-most entity that still has DoorComponent)
    protected IEntity CanonicalizeDoorEntity(IEntity doorLike)
    {
        if (!doorLike) return null;
        IEntity candidate = doorLike;
        // Climb up while parent still has DoorComponent to get a stable reference
        IEntity p = candidate.GetParent();
        while (p)
        {
            DoorComponent pDoor = DoorComponent.Cast(p.FindComponent(DoorComponent));
            if (!pDoor) break;
            candidate = p;
            p = candidate.GetParent();
        }
        return candidate;
    }

	protected IEntity GetDamageTargetFromDoor(IEntity doorEntity)
	{
		if (!doorEntity) return null;
		// Prefer entity with DamageManager on the door or its immediate relatives
		DamageManagerComponent dmg = DamageManagerComponent.Cast(doorEntity.FindComponent(DamageManagerComponent));
		if (dmg) return doorEntity;

		IEntity parent = doorEntity.GetParent();
		if (parent)
		{
			DamageManagerComponent pdmg = DamageManagerComponent.Cast(parent.FindComponent(DamageManagerComponent));
			if (pdmg) return parent;
			// Check children of parent (sibling nodes)
			IEntity child = parent.GetChildren();
			while (child)
			{
				DamageManagerComponent cdmg = DamageManagerComponent.Cast(child.FindComponent(DamageManagerComponent));
				if (cdmg) return child;
				child = child.GetSibling();
			}
		}

		// As a last resort, accept the door entity even without DMC (no-op damage)
		return doorEntity;
	}

	protected bool QueryEntitiesCallback(IEntity entity)
	{
		if (entity)
			m_aNearbyEntities.Insert(entity);
		return true;
	}

	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		if (!m_ZombieCharacter)
		{
			GenericEntity entity = GenericEntity.Cast(owner.GetControlledEntity());
			if (entity)
				m_ZombieCharacter = BZ_ZombieCharacter.Cast(entity);

			if (!m_ZombieCharacter)
				return ENodeResult.FAIL;
		}

        // Having a target is not required to attack a blocking door
        IEntity target = m_ZombieCharacter.GetCurrentTarget();

        vector zombiePos = m_ZombieCharacter.GetOrigin();
        // target may be NULL; only read if present (not required for door melee)
        // vector targetPos = target ? target.GetOrigin() : zombiePos;

		// Find closest door within search radius
		IEntity closestDoor = null;
		float closestDistance = m_fSearchRadius;

		m_aNearbyEntities.Clear();
		GetGame().GetWorld().QueryEntitiesBySphere(zombiePos, m_fSearchRadius, QueryEntitiesCallback, null, EQueryEntitiesFlags.ALL);

        foreach (IEntity entity : m_aNearbyEntities)
        {
            DoorComponent doorComp = DoorComponent.Cast(entity.FindComponent(DoorComponent));
            if (doorComp)
            {
                // Only care if door is closed or closing
                if (doorComp.IsOpen() && !doorComp.IsClosing())
                    continue;
                IEntity canonical = CanonicalizeDoorEntity(entity);
                IEntity orgEnt = canonical;
                if (!orgEnt)
                    orgEnt = entity;
                float distance = vector.Distance(zombiePos, orgEnt.GetOrigin());
                if (distance < closestDistance)
                {
                    closestDistance = distance;
                    closestDoor = orgEnt;
                }
            }
        }

		// If we found a door, attack it
		if (closestDoor)
		{
			// Track if we're at the same door or a new one
			if (m_CurrentDoor != closestDoor)
			{
				m_CurrentDoor = closestDoor;
				m_fTimeAtDoor = 0;
				m_fMeleeTimer = 0;
			}

			// Increment time at door
			m_fTimeAtDoor += dt;

			// Check timeout - if stuck at door for too long, give up
			if (m_fTimeAtDoor >= m_fDoorTimeout)
			{
				// Clear target - this will trigger investigation then wander
				m_ZombieCharacter.SetCurrentTarget(null);
				m_fTimeAtDoor = 0;
				m_CurrentDoor = null;
				return ENodeResult.FAIL;
			}

			// Wait briefly at the door before first hit to avoid false positives
            if (m_fTimeAtDoor < m_fBeginDelay)
            {
                return ENodeResult.SUCCESS;
            }

			// Attack the door at intervals
			m_fMeleeTimer += dt;

				if (m_fMeleeTimer >= m_fMeleeInterval)
				{
					// Damage the door (or its destructible sibling) using DamageManagerComponent
					IEntity damageTarget = GetDamageTargetFromDoor(closestDoor);
					DamageManagerComponent dmg = null;
					if (damageTarget)
						dmg = DamageManagerComponent.Cast(damageTarget.FindComponent(DamageManagerComponent));
					if (dmg)
					{
						vector doorPos = damageTarget.GetOrigin();
						vector hitTransform[3];
						hitTransform[0] = doorPos;
						hitTransform[1] = (doorPos - zombiePos).Normalized();
						hitTransform[2] = vector.Up;

						HitZone hitZone = dmg.GetDefaultHitZone();
						SCR_DamageContext ctx = new SCR_DamageContext(EDamageType.MELEE, m_fDoorDamage, hitTransform, damageTarget, hitZone, Instigator.CreateInstigator(m_ZombieCharacter), null, -1, -1);
						dmg.HandleDamage(ctx);
                }

                m_fMeleeTimer = 0;

                // Always trigger character melee input so animation plays even if door isn't damageable
                CharacterControllerComponent ctrl = CharacterControllerComponent.Cast(m_ZombieCharacter.FindComponent(CharacterControllerComponent));
                if (ctrl)
                {
                    ctrl.SetMeleeAttack(true);
                    ctrl.SetMeleeAttack(false);
                }
                }

			// Keep attacking - return SUCCESS to stay in this state
			return ENodeResult.SUCCESS;
		}
		else
		{
			// No door nearby - not stuck at door, continue to chase
			m_fTimeAtDoor = 0;
			m_CurrentDoor = null;
			m_fMeleeTimer = 0;
			return ENodeResult.FAIL;
		}
	}

	static override bool VisibleInPalette()
	{
		return true;
	}
}
