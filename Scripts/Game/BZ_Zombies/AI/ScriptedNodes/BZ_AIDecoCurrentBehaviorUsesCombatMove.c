class BZ_AIDecoCurrentBehaviorUsesCombatMove : DecoratorScripted
{
	protected SCR_AIUtilityComponent m_Utility;
	
	override bool TestFunction(AIAgent owner)	
	{
		if (!m_Utility)
		{
			m_Utility = SCR_AIUtilityComponent.Cast(owner.FindComponent(SCR_AIUtilityComponent));
			if (!m_Utility)
				return false;
		}
		
		SCR_AIBehaviorBase behavior = SCR_AIBehaviorBase.Cast(m_Utility.GetExecutedAction());
		
		if (!behavior)
			return false;
			
		return behavior.m_bUseCombatMove;
	}
	
	static override string GetOnHoverDescription() { return "Returns true if current behavior has m_bUseCombatMove = true"; }
	static override bool VisibleInPalette() { return true; }
}

