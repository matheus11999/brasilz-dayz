// Improved zombie target tracking - maintains target persistence
// This node validates and tracks the current target, only losing it when truly lost
class BZ_AITrackTarget : AITaskScripted
{
	protected static const string PORT_IN_TARGET = "DetectedTarget";
	protected static const string PORT_OUT_TARGET = "TrackedTarget";

	protected static ref TStringArray s_aVarsIn = {
		PORT_IN_TARGET
	};

	protected static ref TStringArray s_aVarsOut = {
		PORT_OUT_TARGET
	};

	[Attribute("90", UIWidgets.EditBox, "Maximum tracking range (meters) - lose target beyond this")]
	protected float m_fMaxTrackingRange;

	[Attribute("8", UIWidgets.EditBox, "Max time without LOS before losing target (seconds)")]
	protected float m_fMaxTimeWithoutLOS;

	[Attribute("5", UIWidgets.EditBox, "Max time since detected before losing target (seconds)")]
	protected float m_fMaxTimeSinceDetected;

	[Attribute("1", UIWidgets.CheckBox, "Enable debug logging")]
	protected bool m_bDebugMode;

	protected PerceptionComponent m_PerceptionComp;
	protected BZ_ZombieCharacter m_ZombieCharacter;

	override TStringArray GetVariablesIn()
	{
		return s_aVarsIn;
	}

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
		m_PerceptionComp = PerceptionComponent.Cast(entity.FindComponent(PerceptionComponent));
	}

	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		if (!m_PerceptionComp || !m_ZombieCharacter)
		{
			AbortChase();
			return ENodeResult.FAIL;
		}

		// Get current target from zombie character
		IEntity currentTarget = m_ZombieCharacter.GetCurrentTarget();

		// Also check input variable for new detections
		IEntity inputTarget;
		GetVariableIn(PORT_IN_TARGET, inputTarget);

		// If we have an input target, it takes priority (new detection)
		if (inputTarget)
		{
			// Validate new target
			BaseTarget newBaseTarget = m_PerceptionComp.FindTargetPerceptionObject(inputTarget);
			if (newBaseTarget && !newBaseTarget.IsDisarmed())
			{
				float distance = newBaseTarget.GetDistance();
				if (distance <= m_fMaxTrackingRange)
				{
					// Accept new target
					m_ZombieCharacter.SetCurrentTarget(inputTarget);
					SetVariableOut(PORT_OUT_TARGET, inputTarget);

					return ENodeResult.SUCCESS;
				}
			}
		}

		// No new target - validate current target
		if (!currentTarget)
		{
			AbortChase();
			return ENodeResult.FAIL;
		}

		// Check if current target is still valid
		BaseTarget currentBaseTarget = m_PerceptionComp.FindTargetPerceptionObject(currentTarget);

		// HORDE ALERT FIX: If target not in perception, it came from ReceiveAlert()
		// Validate it using basic checks instead of requiring perception
		if (!currentBaseTarget)
		{
			// Check if target entity still exists
			if (!currentTarget || currentTarget.IsDeleted())
			{
				m_ZombieCharacter.SetCurrentTarget(null);
				AbortChase();
				return ENodeResult.FAIL;
			}
			
			// CRITICAL FIX: Check if target is still alive (not dead/unconscious)
			if (!m_ZombieCharacter.IsTargetValid(currentTarget))
			{
				m_ZombieCharacter.SetCurrentTarget(null);
				AbortChase();
				return ENodeResult.FAIL;
			}

			// Check distance manually for alert-based targets
			GenericEntity entity = GenericEntity.Cast(owner.GetControlledEntity());
			if (entity)
			{
				float distance = vector.Distance(entity.GetOrigin(), currentTarget.GetOrigin());

				if (distance > m_fMaxTrackingRange)
				{
					m_ZombieCharacter.SetCurrentTarget(null);
					AbortChase();
					return ENodeResult.FAIL;
				}
			}

			// Alert-based target is valid - output it
			SetVariableOut(PORT_OUT_TARGET, currentTarget);
			return ENodeResult.SUCCESS;
		}

		// Target is in perception - use perception-based validation
		if (currentBaseTarget.IsDisarmed())
		{
			m_ZombieCharacter.SetCurrentTarget(null);
			AbortChase();
			return ENodeResult.FAIL;
		}

		float distance = currentBaseTarget.GetDistance();
		if (distance > m_fMaxTrackingRange)
		{
			m_ZombieCharacter.SetCurrentTarget(null);
			AbortChase();
			return ENodeResult.FAIL;
		}

		// Manual world-space distance check for instant drop (perception distance can lag)
		GenericEntity myEntity = GenericEntity.Cast(owner.GetControlledEntity());
		if (myEntity && currentTarget)
		{
			float actualDistance = vector.Distance(myEntity.GetOrigin(), currentTarget.GetOrigin());
			if (actualDistance > m_fMaxTrackingRange)
			{
				m_ZombieCharacter.SetCurrentTarget(null);
				AbortChase();
				return ENodeResult.FAIL;
			}
		}

		float timeSinceSeen = currentBaseTarget.GetTimeSinceSeen();
		float timeSinceDetected = currentBaseTarget.GetTimeSinceDetected();

		if (timeSinceSeen > m_fMaxTimeWithoutLOS && timeSinceDetected > m_fMaxTimeSinceDetected)
		{
			m_ZombieCharacter.SetCurrentTarget(null);
			AbortChase();
			return ENodeResult.FAIL;
		}

		SetVariableOut(PORT_OUT_TARGET, currentTarget);

		return ENodeResult.SUCCESS;
	}

	static override bool VisibleInPalette()
	{
		return true;
	}

	protected void AbortChase()
	{
		ClearVariable(PORT_OUT_TARGET);
		if (m_ZombieCharacter)
			m_ZombieCharacter.ForceReturnToWander(1.0);
	}
}
