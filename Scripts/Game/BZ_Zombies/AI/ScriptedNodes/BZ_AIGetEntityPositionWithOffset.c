// Get entity position with random circular offset
class BZ_AIGetEntityPositionWithOffset : AITaskScripted
{
	protected static const string ENTITY_PORT = "Entity";
	protected static const string POSITION_PORT = "Position";
	
	[Attribute("2", UIWidgets.EditBox, "Minimum offset radius (meters)")]
	protected float m_fMinRadius;
	
	[Attribute("5", UIWidgets.EditBox, "Maximum offset radius (meters)")]
	protected float m_fMaxRadius;
	
	[Attribute("10", UIWidgets.EditBox, "Recalculate offset interval (seconds)")]
	protected float m_fRecalculateInterval;

	[Attribute("", UIWidgets.EditBox, "Override output variable name (leave empty for default)")]
	protected string m_sOutputVariableName;
	
	protected float m_fLastRecalculateTime;
	protected vector m_vLastOffset;
	protected IEntity m_LastEntity;
	protected ref TStringArray m_aVarsOut;
	protected string m_sResolvedOutputPortName;
	protected bool m_bOutputPortNameResolved;
	
	//------------------------------------------------------------------------------------------------
	static override bool VisibleInPalette()
	{
		return true;
	}
	
	//------------------------------------------------------------------------------------------------
	static override string GetOnHoverDescription()
	{
		return "Returns position of entity with random circular offset that recalculates periodically";
	}
	
	//------------------------------------------------------------------------------------------------
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		IEntity entity;
		GetVariableIn(ENTITY_PORT, entity);
		if (!entity)
			return ENodeResult.FAIL;
		
		// Get current time
		BaseWorld world = owner.GetWorld();
		if (!world)
			world = GetGame().GetWorld();
		if (!world)
			return ENodeResult.FAIL;
			
		float currentTime = world.GetWorldTime() * 0.001;
		
		// Check if we need to recalculate offset
		bool needsRecalculate = false;
		
		if (m_LastEntity != entity)
		{
			needsRecalculate = true;
			m_LastEntity = entity;
		}
		else if (currentTime - m_fLastRecalculateTime >= m_fRecalculateInterval)
		{
			needsRecalculate = true;
		}
		
		// Generate new offset if needed
		if (needsRecalculate)
		{
			float angle = Math.RandomFloat(0, Math.PI2);
			float distance = Math.RandomFloat(m_fMinRadius, m_fMaxRadius);
			
			m_vLastOffset = Vector(
				Math.Cos(angle) * distance,
				0,
				Math.Sin(angle) * distance
			);
			
			m_fLastRecalculateTime = currentTime;
		}
		
		// Get entity position and add offset
		vector entityPos = entity.GetOrigin();
		vector posOut = entityPos + m_vLastOffset;
		
		// Adjust Y to terrain height
		float terrainY = world.GetSurfaceY(posOut[0], posOut[2]);
		posOut[1] = terrainY;
		
		SetVariableOut(GetOutputPortName(), posOut);
		
		return ENodeResult.SUCCESS;
	}
	
	//------------------------------------------------------------------------------------------------
	protected static ref TStringArray s_aVarsIn = { ENTITY_PORT };
	override TStringArray GetVariablesIn()
	{
		return s_aVarsIn;
	}
	
	//------------------------------------------------------------------------------------------------
	override TStringArray GetVariablesOut()
	{
		if (!m_aVarsOut)
			m_aVarsOut = new TStringArray();
		
		m_aVarsOut.Clear();
		m_aVarsOut.Insert(GetOutputPortName());
		return m_aVarsOut;
	}

	//------------------------------------------------------------------------------------------------
	protected string GetOutputPortName()
	{
		if (!m_bOutputPortNameResolved)
		{
			if (m_sOutputVariableName && m_sOutputVariableName != "")
				m_sResolvedOutputPortName = m_sOutputVariableName;
			else
				m_sResolvedOutputPortName = POSITION_PORT;

			m_bOutputPortNameResolved = true;
		}

		return m_sResolvedOutputPortName;
	}
}
