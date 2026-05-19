//------------------------------------------------------------------------------------------------
//! Get the last known position of a lost target
class BZ_AIGetLastKnownPosition : AITaskScripted
{
	// Match BT variable port name
	protected static const string PORT_OUT_POSITION = "lastKnownPosition";
	
	protected static ref TStringArray s_aVarsOut = {
		PORT_OUT_POSITION
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

		// Get the last known position from the zombie character
		vector lastKnownPos = m_ZombieCharacter.GetLastKnownTargetPosition();

		// Check if we have a valid position (not zero vector)
		if (lastKnownPos == vector.Zero)
		{
			return ENodeResult.FAIL;
		}

		// Set the output variable
		SetVariableOut(PORT_OUT_POSITION, lastKnownPos);

		return ENodeResult.SUCCESS;
	}
	
	static override bool VisibleInPalette()
	{
		return true;
	}
}

