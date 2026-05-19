// Converts BaseTarget to IEntity for use with movement/aim tasks
class BZ_AIBaseTargetToEntity : AITaskScripted
{
	[Attribute("0", UIWidgets.CheckBox, "Enable debug logging")]
	protected bool m_bDebugMode;

	protected static const string PORT_IN_BASE_TARGET = "BaseTargetIn";
	protected static const string PORT_OUT_ENTITY = "EntityOut";

	//------------------------------------------------------------------------------------------------
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		BaseTarget baseTarget;
		if (!GetVariableIn(PORT_IN_BASE_TARGET, baseTarget))
		{
			ClearVariable(PORT_OUT_ENTITY);
			return ENodeResult.FAIL;
		}

		if (!baseTarget)
		{
			ClearVariable(PORT_OUT_ENTITY);
			return ENodeResult.FAIL;
		}

		IEntity targetEntity = baseTarget.GetTargetEntity();
		SetVariableOut(PORT_OUT_ENTITY, targetEntity);

		if (targetEntity)
			return ENodeResult.SUCCESS;
		else
			return ENodeResult.FAIL;
	}
	
	//------------------------------------------------------------------------------------------------
	protected static ref TStringArray s_aVarsIn = {PORT_IN_BASE_TARGET};
	protected static ref TStringArray s_aVarsOut = {PORT_OUT_ENTITY};
	
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
		return "Converts BaseTarget to IEntity for movement/aim tasks";
	}
}

