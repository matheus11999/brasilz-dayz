// BrasilZ Zombies - AI Attack Door Node
// Makes zombies melee attack doors that are blocking their path

class BZ_AIAttackDoor : AITaskScripted
{
	// Input port names
	static const string PORT_IN_DOOR = "DoorTarget";
	
	[Attribute("25.0", UIWidgets.EditBox, "Damage per attack")]
	protected float m_fAttackDamage;
	
	[Attribute("0.8", UIWidgets.EditBox, "Attack cooldown in seconds")]
	protected float m_fAttackCooldown;
	
	[Attribute("2.5", UIWidgets.EditBox, "Maximum attack range")]
	protected float m_fMaxAttackRange;
	
	[Attribute("0", UIWidgets.CheckBox, "Enable debug logging")]
	protected bool m_bDebugMode;
	
	protected BZ_ZombieCharacter m_ZombieCharacter;
	protected IEntity m_TargetDoor;
	protected float m_fLastAttackTime;
	protected bool m_bIsAttacking;
	
	//------------------------------------------------------------------------------------------------
	override void OnInit(AIAgent owner)
	{
		IEntity controlledEntity = owner.GetControlledEntity();
		if (controlledEntity)
			m_ZombieCharacter = BZ_ZombieCharacter.Cast(controlledEntity);
		
		m_fLastAttackTime = -m_fAttackCooldown;
		m_bIsAttacking = false;
	}
	
	//------------------------------------------------------------------------------------------------
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		if (!m_ZombieCharacter)
			return ENodeResult.FAIL;

		// Get door from input variable
		IEntity doorEntity;
		if (!GetVariableIn(PORT_IN_DOOR, doorEntity) || !doorEntity)
			return ENodeResult.FAIL;
		
		m_TargetDoor = doorEntity;
		
		// Check if door is still valid and in range
		if (!IsValidTarget(m_TargetDoor))
			return ENodeResult.FAIL;
		
		// Check distance to door
		vector zombiePos = m_ZombieCharacter.GetOrigin();
		vector doorPos = m_TargetDoor.GetOrigin();
		float distance = vector.Distance(zombiePos, doorPos);
		
		if (distance > m_fMaxAttackRange)
		{
			return ENodeResult.FAIL;
		}

		// Check attack cooldown
		float currentTime = GetGame().GetWorld().GetWorldTime() / 1000.0;
		float timeSinceLastAttack = currentTime - m_fLastAttackTime;

		if (timeSinceLastAttack < m_fAttackCooldown)
			return ENodeResult.RUNNING;

		// Perform attack
		bool attackSuccess = AttackDoor(m_TargetDoor);

		if (attackSuccess)
		{
			m_fLastAttackTime = currentTime;

			return ENodeResult.RUNNING;
		}
		
		return ENodeResult.FAIL;
	}
	
	//------------------------------------------------------------------------------------------------
	protected bool AttackDoor(IEntity door)
	{
		if (!door)
			return false;
		
		// Try to damage the door using SCR_DestructibleEntity
		SCR_DestructibleEntity destructible = SCR_DestructibleEntity.Cast(door);
		if (destructible)
		{
			return DamageDestructibleDoor(destructible);
		}
		
		// Try to damage door with multi-phase destruction
		SCR_DestructionMultiPhaseComponent multiPhase = SCR_DestructionMultiPhaseComponent.Cast(
			door.FindComponent(SCR_DestructionMultiPhaseComponent)
		);
		if (multiPhase)
		{
			return DamageMultiPhaseDoor(door, multiPhase);
		}
		
		// Try to damage building door
		SCR_DestructibleBuildingEntity building = SCR_DestructibleBuildingEntity.Cast(door);
		if (building)
		{
			return DamageBuildingDoor(building);
		}
		
		return false;
	}
	
	//------------------------------------------------------------------------------------------------
	protected bool DamageDestructibleDoor(SCR_DestructibleEntity destructible)
	{
		vector zombiePos = m_ZombieCharacter.GetOrigin();
		vector doorPos = destructible.GetOrigin();
		
		// Create damage context
		vector hitTransform[3];
		hitTransform[0] = doorPos; // Hit position
		hitTransform[1] = (doorPos - zombiePos).Normalized(); // Hit direction
		hitTransform[2] = vector.Up; // Hit normal
		
		// Apply damage
		destructible.HandleDamage(EDamageType.MELEE, m_fAttackDamage, hitTransform);

		return true;
	}
	
	//------------------------------------------------------------------------------------------------
	protected bool DamageMultiPhaseDoor(IEntity door, SCR_DestructionMultiPhaseComponent multiPhase)
	{
		// Get damage manager
		DamageManagerComponent damageManager = DamageManagerComponent.Cast(
			door.FindComponent(DamageManagerComponent)
		);
		
		if (!damageManager)
			return false;
		
		vector zombiePos = m_ZombieCharacter.GetOrigin();
		vector doorPos = door.GetOrigin();
		
		// Create damage context
		vector hitTransform[3];
		hitTransform[0] = doorPos;
		hitTransform[1] = (doorPos - zombiePos).Normalized();
		hitTransform[2] = vector.Up;
		
		// Get default hit zone
		HitZone hitZone = damageManager.GetDefaultHitZone();
		
		// Create damage context
		SCR_DamageContext context = new SCR_DamageContext(
			EDamageType.MELEE,
			m_fAttackDamage,
			hitTransform,
			door,
			hitZone,
			Instigator.CreateInstigator(m_ZombieCharacter),
			null,
			-1,
			-1
		);
		
		damageManager.HandleDamage(context);

		return true;
	}
	
	//------------------------------------------------------------------------------------------------
	protected bool DamageBuildingDoor(SCR_DestructibleBuildingEntity building)
	{
		vector zombiePos = m_ZombieCharacter.GetOrigin();
		vector doorPos = building.GetOrigin();
		
		// Create damage context
		vector hitTransform[3];
		hitTransform[0] = doorPos;
		hitTransform[1] = (doorPos - zombiePos).Normalized();
		hitTransform[2] = vector.Up;
		
		// Apply damage to building
		building.OnDamage(
			m_fAttackDamage,
			EDamageType.MELEE,
			m_ZombieCharacter,
			hitTransform,
			m_ZombieCharacter,
			Instigator.CreateInstigator(m_ZombieCharacter),
			-1,
			0
		);

		return true;
	}
	
	//------------------------------------------------------------------------------------------------
	protected bool IsValidTarget(IEntity door)
	{
		if (!door)
			return false;

		// Check if door still exists
		if (door.IsDeleted())
			return false;

		// Check if door is already destroyed
		DamageManagerComponent damageManager = DamageManagerComponent.Cast(door.FindComponent(DamageManagerComponent));
		if (damageManager)
		{
			if (damageManager.GetState() == EDamageState.DESTROYED)
				return false;
		}

		// Check if door is open (no need to attack open doors)
		DoorComponent doorComp = DoorComponent.Cast(door.FindComponent(DoorComponent));
		if (doorComp && doorComp.IsOpen())
			return false;

		return true;
	}
	
	//------------------------------------------------------------------------------------------------
	override void OnAbort(AIAgent owner, Node nodeCausingAbort)
	{
		m_bIsAttacking = false;
		m_TargetDoor = null;
	}
	
	//------------------------------------------------------------------------------------------------
	static override bool VisibleInPalette()
	{
		return true;
	}

	//------------------------------------------------------------------------------------------------
	static override string GetOnHoverDescription()
	{
		return "BZ_AIAttackDoor: Makes zombie melee attack a door";
	}
}

