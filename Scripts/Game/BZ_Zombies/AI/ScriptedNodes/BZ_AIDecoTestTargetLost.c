//------------------------------------------------------------------------------------------------
//! Decorator test to check if zombie has recently lost a target
class BZ_AIDecoTestTargetLost : DecoratorTestScripted
{
	[Attribute("30", UIWidgets.EditBox, "Maximum time since target was lost (seconds)")]
	protected float m_fMaxTimeSinceLost;

	[Attribute("1", UIWidgets.CheckBox, "Enable debug logging")]
	protected bool m_bDebugMode;

	//------------------------------------------------------------------------------------------------
	protected override bool TestFunction(AIAgent agent, IEntity controlled)
	{
		if (!controlled)
			return false;

		BZ_ZombieCharacter zombieCharacter = BZ_ZombieCharacter.Cast(controlled);
		if (!zombieCharacter)
			return false;

		// Check if target is lost
		if (!zombieCharacter.IsTargetLost())
		{
			return false;
		}

		// If investigation is temporarily suppressed, do NOT allow Investigate to run
		if (zombieCharacter.IsInvestigationSuppressedActive())
		{
			return false;
		}

		// Check if we have a valid last known position
		vector lastKnownPos = zombieCharacter.GetLastKnownTargetPosition();
		if (lastKnownPos == vector.Zero)
		{
			return false;
		}

		// Check if target was lost recently enough
		float currentTime = GetGame().GetWorld().GetWorldTime() * 0.001; // Convert to seconds
		float timeSinceLost = currentTime - zombieCharacter.GetTargetLostTime();

		if (timeSinceLost > m_fMaxTimeSinceLost)
		{
			return false;
		}

		return true;
	}
}

