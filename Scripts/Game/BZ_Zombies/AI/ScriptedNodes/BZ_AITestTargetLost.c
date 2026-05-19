// Decorator that checks if zombie is investigating (target lost)
// Returns SUCCESS if zombie should investigate, FAIL otherwise
class BZ_AITestTargetLost : AITaskScripted
{
	[Attribute("0", UIWidgets.CheckBox, "Enable debug logging")]
	protected bool m_bDebugMode;

	protected BZ_ZombieCharacter m_ZombieCharacter;

	//------------------------------------------------------------------------------------------------
	override void OnInit(AIAgent owner)
	{
		GenericEntity entity = GenericEntity.Cast(owner.GetControlledEntity());
		if (entity)
			m_ZombieCharacter = BZ_ZombieCharacter.Cast(entity);
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
				return ENodeResult.FAIL;
		}

		// Check if zombie is investigating (target lost)
		bool isTargetLost = m_ZombieCharacter.IsTargetLost();

		// Return SUCCESS if investigating, FAIL otherwise
		if (isTargetLost)
			return ENodeResult.SUCCESS;
		else
			return ENodeResult.FAIL;
	}

	//------------------------------------------------------------------------------------------------
	static override bool VisibleInPalette()
	{
		return true;
	}
}

