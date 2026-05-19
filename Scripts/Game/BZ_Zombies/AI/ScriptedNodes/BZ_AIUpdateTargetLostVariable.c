//------------------------------------------------------------------------------------------------
//! Updates the BT variable "isTargetLost" based on zombie character state
//! This bridges the gap between zombie character state and BT variables
class BZ_AIUpdateTargetLostVariable : AITaskScripted
{
	protected static const string PORT_OUT_IS_TARGET_LOST = "isTargetLost";
	
	protected static ref TStringArray s_aVarsOut = {
		PORT_OUT_IS_TARGET_LOST
	};
	
	[Attribute("1", UIWidgets.CheckBox, "Enable debug logging")]
	protected bool m_bDebugMode;
	
	protected BZ_ZombieCharacter m_ZombieCharacter;
	
	override TStringArray GetVariablesOut()
	{
		return s_aVarsOut;
	}
	
	override void OnInit(AIAgent owner)
	{
		GenericEntity entity = GenericEntity.Cast(owner.GetControlledEntity());
		if (!entity)
			return;
		
		m_ZombieCharacter = BZ_ZombieCharacter.Cast(entity);
	}
	
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		if (!m_ZombieCharacter)
		{
			return ENodeResult.FAIL;
		}

		// Get the target lost state from zombie character
		bool isTargetLost = m_ZombieCharacter.IsTargetLost();

		// If investigation is suppressed, don't report target as lost
		if (isTargetLost && m_ZombieCharacter.IsInvestigationSuppressedActive())
		{
			isTargetLost = false;
		}

		// Set the output variable
		SetVariableOut(PORT_OUT_IS_TARGET_LOST, isTargetLost);

		return ENodeResult.SUCCESS;
	}
	
	static override bool VisibleInPalette()
	{
		return true;
	}
}

