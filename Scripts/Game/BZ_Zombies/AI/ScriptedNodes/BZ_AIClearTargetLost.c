//------------------------------------------------------------------------------------------------
//! Clears the target lost flag after investigation completes
class BZ_AIClearTargetLost : AITaskScripted
{
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
		
		m_ZombieCharacter.ClearTargetLost();
		return ENodeResult.SUCCESS;
	}
	
	//------------------------------------------------------------------------------------------------
	static override bool VisibleInPalette()
	{
		return true;
	}
}

