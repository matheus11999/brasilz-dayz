// Tests if BaseTarget exists and outputs boolean flag
// Follows base game pattern of using boolean flags for behavior switching
class BZ_AITestHasTarget : AITaskScripted
{
	protected static const string PORT_IN_BASE_TARGET = "BaseTargetIn";
	protected static const string PORT_OUT_HAS_TARGET = "HasTarget";
	
	[Attribute("0", UIWidgets.CheckBox, "Enable debug logging")]
	protected bool m_bDebugMode;
	
	//------------------------------------------------------------------------------------------------
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		BaseTarget baseTarget;
		GetVariableIn(PORT_IN_BASE_TARGET, baseTarget);
		
		bool hasTarget = false;
		if (baseTarget)
			hasTarget = true;
		
		SetVariableOut(PORT_OUT_HAS_TARGET, hasTarget);

		if (hasTarget)
			return ENodeResult.SUCCESS;
		else
			return ENodeResult.FAIL;
	}
	
	//------------------------------------------------------------------------------------------------
	protected static ref TStringArray s_aVarsIn = {PORT_IN_BASE_TARGET};
	protected static ref TStringArray s_aVarsOut = {PORT_OUT_HAS_TARGET};
	
	override TStringArray GetVariablesIn()
	{
		return s_aVarsIn;
	}
	
	override TStringArray GetVariablesOut()
	{
		return s_aVarsOut;
	}
	
	//------------------------------------------------------------------------------------------------
	static override bool VisibleInPalette()
	{
		return true;
	}
	
	//------------------------------------------------------------------------------------------------
	static override string GetOnHoverDescription()
	{
		return "Tests if BaseTarget exists and outputs boolean flag for behavior switching";
	}
}

