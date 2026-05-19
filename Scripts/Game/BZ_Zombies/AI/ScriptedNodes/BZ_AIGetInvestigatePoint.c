//------------------------------------------------------------------------------------------------
//! Compute a small randomized investigate point around the last known position
class BZ_AIGetInvestigatePoint : AITaskScripted
{
	// We purposely write into the same BT variable name used by Move: "lastKnownPosition"
	protected static const string PORT_OUT_POSITION = "lastKnownPosition";

	protected static ref TStringArray s_aVarsOut = {
		PORT_OUT_POSITION
	};

	[Attribute("2.0", UIWidgets.EditBox, "Minimum search radius (meters)")]
	protected float m_fMinRadius;

	[Attribute("8.0", UIWidgets.EditBox, "Maximum search radius (meters)")]
	protected float m_fMaxRadius;

	[Attribute("0", UIWidgets.CheckBox, "Enable debug logging")]
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
			return ENodeResult.FAIL;

		vector center = m_ZombieCharacter.GetLastKnownTargetPosition();
		if (center == vector.Zero)
			return ENodeResult.FAIL;

		if (m_fMaxRadius < m_fMinRadius)
		{
			float t = m_fMinRadius;
			m_fMinRadius = m_fMaxRadius;
			m_fMaxRadius = t;
		}

		float angle = Math.RandomFloat(0, Math.PI2);
		float dist = Math.RandomFloat(m_fMinRadius, m_fMaxRadius);
		vector offset = Vector(Math.Cos(angle) * dist, 0, Math.Sin(angle) * dist);
		vector outPos = center + offset;

		BaseWorld world = GetGame().GetWorld();
		if (world)
		{
			float terrainY = world.GetSurfaceY(outPos[0], outPos[2]);
			outPos[1] = terrainY;
		}
		else
		{
			outPos[1] = center[1];
		}

		SetVariableOut(PORT_OUT_POSITION, outPos);

		return ENodeResult.SUCCESS;
	}

	static override bool VisibleInPalette()
	{
		return true;
	}
}

