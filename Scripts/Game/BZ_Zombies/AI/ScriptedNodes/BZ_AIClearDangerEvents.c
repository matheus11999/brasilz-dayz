// Clear all danger events from the AI agent
// Used when transitioning to wandering so new danger events can be detected
class BZ_AIClearDangerEvents : AITaskScripted
{
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		if (!owner)
			return ENodeResult.FAIL;

		int dangerCount = owner.GetDangerEventsCount();
		if (dangerCount > 0)
			owner.ClearDangerEvents(dangerCount);

		return ENodeResult.SUCCESS;
	}

	static override bool VisibleInPalette()
	{
		return true;
	}
}

