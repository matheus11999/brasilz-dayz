//------------------------------------------------------------------------------------------------
// BrasilZ Zombies - Set movement type and speed based on entity attributes and day/night
// NOTE: Avoid ternary operators (not supported by Enforce Script in Reforger)
//------------------------------------------------------------------------------------------------

enum EBZ_ZombieSpeedMode
{
	WANDER,
	CHASE
}

class BZ_AISetZombieSpeed : AITaskScripted
{
	protected BZ_ZombieCharacter m_ZombieCharacter;
	protected AICharacterMovementComponent m_MovementComponent;
	protected CharacterControllerComponent m_Controller;
	protected TimeAndWeatherManagerEntity m_TimeManager;
	protected float m_fSunriseTime;
	protected float m_fSunsetTime;

	[Attribute("0", UIWidgets.ComboBox, "Which speed profile to apply", "", ParamEnumArray.FromEnum(EBZ_ZombieSpeedMode))]
	protected EBZ_ZombieSpeedMode m_eMode;

	[Attribute("0", UIWidgets.CheckBox, "Enable debug logging")]
	protected bool m_bDebugMode;

	//----------------------------------------------------------------------------------------------
	override void OnInit(AIAgent owner)
	{
		super.OnInit(owner);

		GenericEntity ent = GenericEntity.Cast(owner.GetControlledEntity());
		if (ent)
		{
			m_ZombieCharacter = BZ_ZombieCharacter.Cast(ent);
			m_MovementComponent = AICharacterMovementComponent.Cast(ent.FindComponent(AICharacterMovementComponent));
			m_Controller = CharacterControllerComponent.Cast(ent.FindComponent(CharacterControllerComponent));
		}

		ChimeraWorld world = owner.GetWorld();
		if (world)
		{
			m_TimeManager = world.GetTimeAndWeatherManager();
			if (m_TimeManager)
			{
				bool okRise = m_TimeManager.GetSunriseHour(m_fSunriseTime);
				bool okSet = m_TimeManager.GetSunsetHour(m_fSunsetTime);
				if (!okRise || !okSet)
					m_TimeManager = null; // mark as invalid
			}
		}

	}

	//----------------------------------------------------------------------------------------------
	protected bool IsNight()
	{
		if (!m_TimeManager)
			return false; // default to day if time manager missing

		float current = m_TimeManager.GetTimeOfTheDay();
		// Night is before sunrise or after sunset
		if (current < m_fSunriseTime)
			return true;
		if (current > m_fSunsetTime)
			return true;
		return false;
	}

	//----------------------------------------------------------------------------------------------
	protected void ApplyMovement(EMovementType movementType)
	{
		if (m_MovementComponent)
			m_MovementComponent.SetMovementTypeWanted(movementType);
	}

	//----------------------------------------------------------------------------------------------
	protected void ApplySpeedScalar(float coef)
	{
		if (m_Controller)
		{
			// Clamp to valid range as per API docs
			if (coef < 0)
				coef = -1; // disable override
			if (coef > 1)
				coef = 1;
			m_Controller.OverrideMaxSpeed(coef);
		}
	}

	//----------------------------------------------------------------------------------------------
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		if (!m_ZombieCharacter)
			return ENodeResult.FAIL;

		bool night = IsNight();

		// Decide movement type and scalar from the character's attributes
		EMovementType desiredMove = EMovementType.WALK;
		float coef = 1.0;

		if (m_eMode == EBZ_ZombieSpeedMode.WANDER)
		{
			desiredMove = m_ZombieCharacter.GetWanderMovementType();
			if (night)
				coef = m_ZombieCharacter.GetWanderSpeedNight();
			else
				coef = m_ZombieCharacter.GetWanderSpeedDay();
		}
		else // CHASE
		{
			desiredMove = m_ZombieCharacter.GetChaseMovementType();
			if (night)
				coef = m_ZombieCharacter.GetChaseSpeedNight();
			else
				coef = m_ZombieCharacter.GetChaseSpeedDay();
		}

		ApplyMovement(desiredMove);
		ApplySpeedScalar(coef);

		return ENodeResult.SUCCESS;
	}

	//----------------------------------------------------------------------------------------------
	static override bool VisibleInPalette()
	{
		return true;
	}
}

