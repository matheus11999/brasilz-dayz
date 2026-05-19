// Get threat position from zombie character (for investigation)
// Replaces BZ_AIGetLastKnownPosition with threat state system
class BZ_AIGetThreatPosition : AITaskScripted
{
	protected static const string PORT_OUT_POSITION = "threatPosition";

	protected static ref TStringArray s_aVarsOut = {
		PORT_OUT_POSITION
	};

	[Attribute("0", UIWidgets.CheckBox, "Enable debug logging")]
	protected bool m_bDebugMode;

	[Attribute("5", UIWidgets.EditBox, "Minimum random offset radius (meters)")]
	protected float m_fMinOffsetRadius;

	[Attribute("15", UIWidgets.EditBox, "Maximum random offset radius (meters)")]
	protected float m_fMaxOffsetRadius;

	protected BZ_ZombieCharacter m_ZombieCharacter;

	//------------------------------------------------------------------------------------------------
	override TStringArray GetVariablesOut()
	{
		return s_aVarsOut;
	}

	//------------------------------------------------------------------------------------------------
	override void OnInit(AIAgent owner)
	{
		GenericEntity entity = GenericEntity.Cast(owner.GetControlledEntity());
		if (entity)
			m_ZombieCharacter = BZ_ZombieCharacter.Cast(entity);
	}

	//------------------------------------------------------------------------------------------------
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		if (!m_ZombieCharacter)
		{
			GenericEntity entity = GenericEntity.Cast(owner.GetControlledEntity());
			if (entity)
				m_ZombieCharacter = BZ_ZombieCharacter.Cast(entity);

			if (!m_ZombieCharacter)
				return ENodeResult.FAIL;
		}

		// Get threat position from zombie character
		vector threatPosition = m_ZombieCharacter.GetThreatPosition();

		// Add random offset to spread zombies out
		float angle = Math.RandomFloat(0, Math.PI2);
		float distance = Math.RandomFloat(m_fMinOffsetRadius, m_fMaxOffsetRadius);

		vector offset = Vector(
			Math.Cos(angle) * distance,
			0,
			Math.Sin(angle) * distance
		);

		vector offsetPosition = threatPosition + offset;

		// Adjust Y to terrain height
		BaseWorld world = GetGame().GetWorld();
		if (world)
		{
			float terrainY = world.GetSurfaceY(offsetPosition[0], offsetPosition[2]);
			offsetPosition[1] = terrainY;
		}

		// Set output variable
		SetVariableOut(PORT_OUT_POSITION, offsetPosition);

		return ENodeResult.SUCCESS;
	}

	//------------------------------------------------------------------------------------------------
	static override bool VisibleInPalette()
	{
		return true;
	}
}

