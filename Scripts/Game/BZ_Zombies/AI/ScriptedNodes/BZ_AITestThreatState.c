// Decorator that checks if zombie threat state matches expected state
// Returns SUCCESS if threat state matches, FAIL otherwise
// This replaces BZ_AITestTargetLost with a more flexible threat state system
class BZ_AITestThreatState : DecoratorScripted
{
	[Attribute("1", UIWidgets.ComboBox, "Expected Threat State", "", ParamEnumArray.FromEnum(EZombieThreatState))]
	protected EZombieThreatState m_eExpectedState;

	[Attribute("0", UIWidgets.CheckBox, "Enable debug logging")]
	protected bool m_bDebugMode;

	protected BZ_ZombieCharacter m_ZombieCharacter;

	//------------------------------------------------------------------------------------------------
	override void OnInit(AIAgent owner)
	{
		GenericEntity entity = GenericEntity.Cast(owner.GetControlledEntity());
		if (entity)
			m_ZombieCharacter = BZ_ZombieCharacter.Cast(entity);
	}

	//------------------------------------------------------------------------------------------------
	protected override bool TestFunction(AIAgent owner)
	{
		if (!m_ZombieCharacter)
		{
			GenericEntity entity = GenericEntity.Cast(owner.GetControlledEntity());
			if (entity)
				m_ZombieCharacter = BZ_ZombieCharacter.Cast(entity);

			if (!m_ZombieCharacter)
				return false;
		}

		// Check if zombie threat state matches expected state
		EZombieThreatState currentState = m_ZombieCharacter.GetThreatState();
		bool stateMatches = (currentState == m_eExpectedState);

		// Return true if state matches, false otherwise
		return stateMatches;
	}

	//------------------------------------------------------------------------------------------------
	static override bool VisibleInPalette()
	{
		return true;
	}
}

