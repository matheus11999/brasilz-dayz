// BrasilZ Zombies - Check Melee Range Decorator
// Checks if target is still within melee attack range
// Used to break out of attack loop if target moves away

class BZ_AICheckMeleeRange : DecoratorScripted
{
	static const string PORT_IN_TARGET = "TargetEntity";
	
	[Attribute("1.5", UIWidgets.EditBox, "Maximum melee attack range (meters)")]
	protected float m_fMeleeRange;
	
	[Attribute("0", UIWidgets.CheckBox, "Enable debug logging")]
	protected bool m_bDebugMode;
	
	protected BZ_ZombieCharacter m_ZombieCharacter;
	
	//------------------------------------------------------------------------------------------------
	override void OnInit(AIAgent owner)
	{
		IEntity controlledEntity = owner.GetControlledEntity();
		if (controlledEntity)
			m_ZombieCharacter = BZ_ZombieCharacter.Cast(controlledEntity);
	}
	
	//------------------------------------------------------------------------------------------------
	override bool TestFunction(AIAgent owner)
	{
		if (!m_ZombieCharacter)
		{
			GenericEntity entity = GenericEntity.Cast(owner.GetControlledEntity());
			if (entity)
				m_ZombieCharacter = BZ_ZombieCharacter.Cast(entity);
			if (!m_ZombieCharacter)
				return false;
		}

		// Get target from zombie character (current target being chased)
		IEntity target = m_ZombieCharacter.GetCurrentTarget();
		if (!target)
		{
			return false;
		}

		// Calculate distance to target
		float distance = vector.Distance(m_ZombieCharacter.GetOrigin(), target.GetOrigin());

		// Use zombie's attack range if decorator range is 0 (not set)
		float attackRange = m_fMeleeRange;
		if (attackRange <= 0)
			attackRange = m_ZombieCharacter.GetAttackRange();

		bool inRange = distance <= attackRange;

		return inRange;
	}
	
	//------------------------------------------------------------------------------------------------
	override TStringArray GetVariablesIn()
	{
		auto vars = new TStringArray();
		vars.Insert(PORT_IN_TARGET);
		return vars;
	}
}

