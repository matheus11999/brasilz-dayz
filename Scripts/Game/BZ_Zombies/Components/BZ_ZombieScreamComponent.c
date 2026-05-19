class BZ_ZombieScreamComponentClass : ScriptComponentClass
{
}

class BZ_ZombieScreamComponent : ScriptComponent
{
	[Attribute("", UIWidgets.ResourceNamePicker, "Sound project file (.acp) for zombie screams", "acp")]
	protected ResourceName m_sScreamSoundFile;

	[Attribute("spot", UIWidgets.EditBox, "Sound event name for zombie scream (defined in .acp file)")]
	protected string m_sScreamSoundEvent;
	
	[Attribute("3", UIWidgets.Slider, "Minimum time between screams (seconds)", params: "1 30 0.5")]
	protected float m_fScreamCooldown;

	[Attribute("1", UIWidgets.CheckBox, "Enable ambient growl sounds")]
	protected bool m_bEnableAmbientGrowls;

	[Attribute("idle", UIWidgets.EditBox, "Ambient growl sound event (defined in scream ACP)")]
	protected string m_sAmbientGrowlEvent;

	[Attribute("6", UIWidgets.EditBox, "Minimum seconds between ambient growls")]
	protected float m_fGrowlIntervalMin;

	[Attribute("18", UIWidgets.EditBox, "Maximum seconds between ambient growls")]
	protected float m_fGrowlIntervalMax;

	[Attribute("1", UIWidgets.CheckBox, "Enable debug messages")]
	protected bool m_bDebugMode;
	
	// Internal state
	protected IEntity m_Owner;
	protected float m_fLastScreamTime = -999;
	protected SoundComponent m_SoundComponent;
	protected SCR_CommunicationSoundComponent m_CommunicationSoundComponent;
	protected float m_fNextGrowlTime;
	protected BZ_ZombieCharacter m_ZombieCharacter;
	protected bool m_bFirstFrameLogged;

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		m_Owner = owner;

		SetEventMask(owner, EntityEvent.INIT);
	}

	override void EOnInit(IEntity owner)
	{
		m_SoundComponent = SoundComponent.Cast(owner.FindComponent(SoundComponent));
		m_CommunicationSoundComponent = SCR_CommunicationSoundComponent.Cast(owner.FindComponent(SCR_CommunicationSoundComponent));
		m_ZombieCharacter = BZ_ZombieCharacter.Cast(owner);

		if (m_bDebugMode)
		{
			Print(string.Format("[BZ_ZombieScream] Init - EnableGrowls=%1, GrowlEvent='%2', IntervalMin=%3, IntervalMax=%4",
				m_bEnableAmbientGrowls, m_sAmbientGrowlEvent, m_fGrowlIntervalMin, m_fGrowlIntervalMax), LogLevel.NORMAL);
			Print(string.Format("[BZ_ZombieScream] SoundComponent=%1, CommSoundComponent=%2",
				m_SoundComponent != null, m_CommunicationSoundComponent != null), LogLevel.NORMAL);
			Print(string.Format("[BZ_ZombieScream] IsServer=%1, IsClient=%2",
				Replication.IsServer(), Replication.IsClient()), LogLevel.NORMAL);
		}

		// Schedule growl timer instead of using EOnFrame (server only)
		if (Replication.IsServer() && m_bEnableAmbientGrowls)
			StartGrowlTimer();

		if (m_bDebugMode)
			Print(string.Format("[BZ_ZombieScream] After ScheduleNextGrowl - m_fNextGrowlTime=%1", m_fNextGrowlTime), LogLevel.NORMAL);
	}

	protected void StartGrowlTimer()
	{
		float minInterval = Math.Max(0.5, m_fGrowlIntervalMin);
		float maxInterval = Math.Max(minInterval, m_fGrowlIntervalMax);
		float intervalMs = Math.RandomFloat(minInterval, maxInterval) * 1000;
		GetGame().GetCallqueue().CallLater(GrowlTimerTick, intervalMs, false);
	}

	protected void GrowlTimerTick()
	{
		if (!m_bEnableAmbientGrowls)
			return;

		ProcessAmbientGrowl();

		// Schedule next tick with randomized interval
		StartGrowlTimer();
	}

	void PlayScream()
	{
		float currentTime = GetGame().GetWorld().GetWorldTime() * 0.001;
		if (currentTime - m_fLastScreamTime < m_fScreamCooldown)
			return;

		m_fLastScreamTime = currentTime;
		PlaySoundEvent(m_sScreamSoundEvent);
	}

	bool IsOnCooldown()
	{
		float currentTime = GetGame().GetWorld().GetWorldTime() * 0.001;
		return (currentTime - m_fLastScreamTime) < m_fScreamCooldown;
	}

	float GetCooldownRemaining()
	{
		float currentTime = GetGame().GetWorld().GetWorldTime() * 0.001;
		float remaining = m_fScreamCooldown - (currentTime - m_fLastScreamTime);
		if (remaining < 0)
			return 0;
		return remaining;
	}

	void SetScreamSoundEvent(string eventName)
	{
		m_sScreamSoundEvent = eventName;
	}

	string GetScreamSoundEvent()
	{
		return m_sScreamSoundEvent;
	}

	void SetScreamCooldown(float cooldown)
	{
		m_fScreamCooldown = cooldown;
	}

	float GetScreamCooldown()
	{
		return m_fScreamCooldown;
	}

	void PlaySoundEvent(string eventName, bool preferVoice = false)
	{
		if (eventName.IsEmpty())
			return;

		if (preferVoice)
		{
			if (m_CommunicationSoundComponent)
			{
				m_CommunicationSoundComponent.SoundEvent(eventName);
				return;
			}

			if (m_SoundComponent)
			{
				m_SoundComponent.SoundEvent(eventName);
				return;
			}
		}
		else
		{
			if (m_SoundComponent)
			{
				m_SoundComponent.SoundEvent(eventName);
				return;
			}

			if (m_CommunicationSoundComponent)
			{
				m_CommunicationSoundComponent.SoundEvent(eventName);
				return;
			}
		}
	}

	protected void ProcessAmbientGrowl()
	{
		if (m_sAmbientGrowlEvent.IsEmpty())
			return;

		if (!IsZombieAlive())
			return;

		// Only growl during IDLE (wandering) or CHASING states
		if (m_ZombieCharacter)
		{
			EZombieThreatState currentState = m_ZombieCharacter.GetThreatState();
			if (currentState != EZombieThreatState.IDLE && currentState != EZombieThreatState.CHASING)
				return;
		}

		if (m_bDebugMode)
			Print(string.Format("[BZ_ZombieScream] Playing growl sound: %1", m_sAmbientGrowlEvent), LogLevel.NORMAL);

		// Broadcast growl to all clients via RPC
		Rpc(RPC_PlayGrowlBroadcast);
	}

	//------------------------------------------------------------------------------------------------
	//! RPC to broadcast growl sound to all clients
	[RplRpc(RplChannel.Unreliable, RplRcver.Broadcast)]
	protected void RPC_PlayGrowlBroadcast()
	{
		if (m_bDebugMode)
			Print(string.Format("[BZ_ZombieScream] RPC received - playing growl: %1", m_sAmbientGrowlEvent), LogLevel.VERBOSE);

		PlaySoundEvent(m_sAmbientGrowlEvent, false);
	}

	protected bool IsZombieAlive()
	{
		if (!m_ZombieCharacter)
			return true;

		CharacterControllerComponent controller = m_ZombieCharacter.GetCharacterController();
		if (!controller)
			return true;

		return controller.GetLifeState() == ECharacterLifeState.ALIVE;
	}

	void ~BZ_ZombieScreamComponent()
	{
		GetGame().GetCallqueue().Remove(GrowlTimerTick);
	}
}
