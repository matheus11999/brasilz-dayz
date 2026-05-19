// Decides zombie behavior based on target presence
// Only sets shouldChase=true when behavior changes (to avoid flickering)
// Persists chase state even when target is temporarily lost (to handle perception flickering)
class BZ_AIDecideZombieBehavior : AITaskScripted
{
	[Attribute("0", UIWidgets.CheckBox, "Enable debug logging")]
	protected bool m_bDebugMode;

	[Attribute("2.0", UIWidgets.EditBox, "Time to wait before giving up chase after losing target (seconds)")]
	protected float m_fGiveUpTime;

	protected static const string PORT_IN_BASE_TARGET = "BaseTargetIn";
	protected static const string PORT_OUT_SHOULD_CHASE = "ShouldChase";

	protected bool m_bIsChasing = false;
	protected float m_fTimeSinceLastSeen = 0;

	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		BaseTarget baseTarget;
		GetVariableIn(PORT_IN_BASE_TARGET, baseTarget);

		bool previousChaseState = m_bIsChasing;

		// Update time since last seen
		if (baseTarget)
		{
			// We have a target - reset timer
			m_fTimeSinceLastSeen = 0;

			if (!m_bIsChasing)
				m_bIsChasing = true;
		}
		else
		{
			// No target this frame
			m_fTimeSinceLastSeen = m_fTimeSinceLastSeen + dt;

			if (m_bIsChasing && m_fTimeSinceLastSeen >= m_fGiveUpTime)
				m_bIsChasing = false;
		}

		if (m_bIsChasing != previousChaseState)
		{
			SetVariableOut(PORT_OUT_SHOULD_CHASE, true);
		}
		else
		{
			// No change - don't trigger behavior switch
			SetVariableOut(PORT_OUT_SHOULD_CHASE, false);
		}

		return ENodeResult.SUCCESS;
	}

	protected static ref TStringArray s_aVarsIn = {PORT_IN_BASE_TARGET};
	protected static ref TStringArray s_aVarsOut = {PORT_OUT_SHOULD_CHASE};
	
	override TStringArray GetVariablesIn()
	{
		return s_aVarsIn;
	}
	
	override TStringArray GetVariablesOut()
	{
		return s_aVarsOut;
	}

	static override bool VisibleInPalette()
	{
		return true;
	}

	static override string GetOnHoverDescription()
	{
		return "Decides zombie behavior: Chase if target exists, Wander otherwise. Only triggers behavior change when state changes.";
	}
}

