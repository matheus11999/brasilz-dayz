// Zombie target detection using PerceptionComponent (base game pattern)
// Outputs BaseTarget object for use in behavior trees
class BZ_AIGetZombieTarget : AITaskScripted
{
	[Attribute("70", UIWidgets.EditBox, "Maximum detection range (meters)")]
	protected float m_fMaxDetectionRange;
	
	[Attribute("10", UIWidgets.EditBox, "Max time since target was last seen (seconds)")]
	protected float m_fTimeSinceSeenMax;
	
	[Attribute("5", UIWidgets.EditBox, "Max time since target was last detected (seconds)")]
	protected float m_fTimeSinceDetectedMax;
	
	[Attribute("0", UIWidgets.CheckBox, "Enable debug logging")]
	protected bool m_bDebugMode;
	
	protected static const string PORT_OUT_TARGET = "BaseTarget";
	
	protected PerceptionComponent m_PerceptionComp;
	
	//------------------------------------------------------------------------------------------------
	override void OnInit(AIAgent owner)
	{
		GenericEntity ent = GenericEntity.Cast(owner.GetControlledEntity());
		if (!ent)
			return;
		
		m_PerceptionComp = PerceptionComponent.Cast(ent.FindComponent(PerceptionComponent));
	}
	
	//------------------------------------------------------------------------------------------------
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		if (!m_PerceptionComp)
			return ENodeResult.FAIL;

		BaseTarget outTarget = null;

		// Get update interval to ensure we don't miss targets (base game pattern)
		float updateInterval = m_PerceptionComp.GetUpdateInterval();
		float timeSinceSeenMax = Math.Max(m_fTimeSinceSeenMax, updateInterval) + 0.02;
		float timeSinceDetectedMax = Math.Max(m_fTimeSinceDetectedMax, updateInterval) + 0.02;

		// First check DETECTED category (new targets not yet identified) - base game pattern
		outTarget = m_PerceptionComp.GetClosestTarget(ETargetCategory.DETECTED, timeSinceSeenMax, timeSinceDetectedMax);

		// If no detected target, check ENEMY category (identified enemies) - base game pattern
		if (!outTarget)
			outTarget = m_PerceptionComp.GetClosestTarget(ETargetCategory.ENEMY, timeSinceSeenMax, timeSinceDetectedMax);

		// Filter by range (zombie-specific)
		if (outTarget)
		{
			if (outTarget.GetDistance() > m_fMaxDetectionRange)
			{
				outTarget = null;
			}
		}

		// Filter disarmed/unconscious targets (base game pattern)
		if (outTarget && outTarget.IsDisarmed())
			outTarget = null;

		// Output target to BT variable
		SetVariableOut(PORT_OUT_TARGET, outTarget);

		if (outTarget)
			return ENodeResult.SUCCESS;
		else
			return ENodeResult.FAIL;
	}
	
	//------------------------------------------------------------------------------------------------
	protected static ref TStringArray s_aVarsOut = {PORT_OUT_TARGET};
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
		return "Gets closest zombie target using PerceptionComponent (base game pattern)";
	}
}
