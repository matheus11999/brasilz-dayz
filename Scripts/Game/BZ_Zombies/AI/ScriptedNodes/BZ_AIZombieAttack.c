// BrasilZ Zombies - AI Zombie Attack Node
// Handles zombie melee attacks on targets

class BZ_AIZombieAttack : AITaskScripted
{
	// Input port names
	static const string PORT_IN_TARGET = "DetectedTarget";
	
	[Attribute("0", UIWidgets.CheckBox, "Enable debug logging")]
	protected bool m_bDebugMode;
	
	protected BZ_ZombieCharacter m_ZombieCharacter;
	protected IEntity m_CurrentTarget;
	
	//------------------------------------------------------------------------------------------------
	override void OnInit(AIAgent owner)
	{
		IEntity controlledEntity = owner.GetControlledEntity();
		if (controlledEntity)
			m_ZombieCharacter = BZ_ZombieCharacter.Cast(controlledEntity);
	}
	
	//------------------------------------------------------------------------------------------------
	override void OnEnter(AIAgent owner)
	{
		// Get target from input variable
		GetVariableIn(PORT_IN_TARGET, m_CurrentTarget);
	}
	
	//------------------------------------------------------------------------------------------------
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		if (!m_ZombieCharacter)
			return ENodeResult.FAIL;
		
		// Try to get target from input if not set
		if (!m_CurrentTarget)
		{
			GetVariableIn(PORT_IN_TARGET, m_CurrentTarget);
		}
		
		// If still no target, try to get from zombie character
		if (!m_CurrentTarget)
		{
			m_CurrentTarget = m_ZombieCharacter.GetCurrentTarget();
		}
		
		// No target available
		if (!m_CurrentTarget)
		{
			return ENodeResult.FAIL;
		}

		// Check if target is still valid
		if (!m_ZombieCharacter.IsTargetValid(m_CurrentTarget))
		{
			m_CurrentTarget = null;
			return ENodeResult.FAIL;
		}

		// Check if target is in attack range
		float attackRange = m_ZombieCharacter.GetAttackRange();
		if (!m_ZombieCharacter.IsTargetInRange(m_CurrentTarget, attackRange))
		{
			return ENodeResult.RUNNING;
		}

		// Try to attack
		bool attackSuccess = m_ZombieCharacter.TryAttack(m_CurrentTarget);

		if (attackSuccess)
		{
			return ENodeResult.SUCCESS;
		}
		else
		{
			return ENodeResult.RUNNING;
		}
	}
	
	//------------------------------------------------------------------------------------------------
	 void OnExit(AIAgent owner)
	{
		m_CurrentTarget = null;
	}
	
	//------------------------------------------------------------------------------------------------
	protected static override bool VisibleInPalette()
	{
		return true;
	}
	
	//------------------------------------------------------------------------------------------------
	protected static override string GetOnHoverDescription()
	{
		return "Attempts to attack the target if in range and off cooldown";
	}
}

