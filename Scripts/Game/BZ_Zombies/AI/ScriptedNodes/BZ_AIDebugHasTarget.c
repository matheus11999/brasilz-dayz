class BZ_AIDebugHasTarget : AITaskScripted
{
	protected static const string PORT_IN_TARGET = "Target";
	
	//------------------------------------------------------------------------------------------------
	override void OnInit(AIAgent owner)
	{
	}

	//------------------------------------------------------------------------------------------------
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		IEntity target;
		if (!GetVariableIn(PORT_IN_TARGET, target))
		{
			return ENodeResult.FAIL;
		}

		if (target)
		{
			return ENodeResult.SUCCESS;
		}
		else
		{
			return ENodeResult.FAIL;
		}
	}
	
	//------------------------------------------------------------------------------------------------
	static override bool VisibleInPalette()
	{
		return true;
	}

	//------------------------------------------------------------------------------------------------
	static override string GetOnHoverDescription()
	{
		return "Debug node to check if target exists";
	}
}

