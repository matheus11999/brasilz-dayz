// Clear threat state after investigation completes
// Replaces BZ_AIClearTargetLost with threat state system
class BZ_AIClearThreatState : AITaskScripted
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

		// Clear threat state (return to IDLE)
		m_ZombieCharacter.ClearThreatState();

		return ENodeResult.SUCCESS;
	}

	//------------------------------------------------------------------------------------------------
	static override bool VisibleInPalette()
	{
		return true;
	}
}

