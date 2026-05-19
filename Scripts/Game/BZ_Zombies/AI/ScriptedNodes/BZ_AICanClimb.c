// BrasilZ Zombies - AI Can Climb Node
// Always returns FAIL to prevent zombies from climbing

class BZ_AICanClimb : AITaskScripted
{
	[Attribute("0", UIWidgets.CheckBox, "Enable debug logging")]
	protected bool m_bDebugMode;
	
	//------------------------------------------------------------------------------------------------
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		// Always return FAIL to prevent climbing
		return ENodeResult.FAIL;
	}
	
	//------------------------------------------------------------------------------------------------
	protected static override bool VisibleInPalette()
	{
		return true;
	}
	
	//------------------------------------------------------------------------------------------------
	protected static override string GetOnHoverDescription()
	{
		return "Always returns FAIL to prevent zombies from climbing";
	}
}

