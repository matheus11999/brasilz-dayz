class BZ_ZombieAlertSystemClass : ScriptComponentClass
{
}

class BZ_ZombieAlertSystem : ScriptComponent
{
	[Attribute("25", UIWidgets.Slider, "Alert radius - how far to alert other zombies (meters)", params: "10 300 1")]
	protected float m_fAlertRadius;
	
	[Attribute("2", UIWidgets.Slider, "Alert cooldown - minimum time between alerts (seconds)", params: "0.25 30 0.25")]
	protected float m_fAlertCooldown;
	
	[Attribute("1", UIWidgets.CheckBox, "Enable debug messages")]
	protected bool m_bDebugMode;
	
	// Internal state
	protected float m_fLastAlertTime = -999;
	protected IEntity m_Owner;

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		m_Owner = owner;
		
		SetEventMask(owner, EntityEvent.INIT);
	}

	override void EOnInit(IEntity owner)
	{
		// Only run on server
		if (!Replication.IsServer())
			return;
	}

	void AlertNearbyZombies(IEntity target, float overrideRadius = -1)
	{
		if (!target)
		{
			return;
		}

		// Only alert on server
		if (!Replication.IsServer())
		{
			return;
		}

		float currentTime = GetGame().GetWorld().GetWorldTime() * 0.001;
		if (currentTime - m_fLastAlertTime < m_fAlertCooldown)
		{
			return;
		}

		m_fLastAlertTime = currentTime;
		float radius = m_fAlertRadius;
		if (overrideRadius > 0)
			radius = overrideRadius;

		// Play scream sound
		BZ_ZombieScreamComponent screamComp = BZ_ZombieScreamComponent.Cast(m_Owner.FindComponent(BZ_ZombieScreamComponent));
		if (screamComp)
		{
			screamComp.PlayScream();
		}

		// Get the zombie character from owner
		BZ_ZombieCharacter alerterZombie = BZ_ZombieCharacter.Cast(m_Owner);
		if (!alerterZombie)
		{
			return;
		}

		if (alerterZombie.IsZombieEntity(target))
			return;

		// Use the horde manager to alert all nearby zombies
		BZ_ZombieHordeManager hordeManager = BZ_ZombieHordeManager.GetInstance();
		if (hordeManager)
		{
			hordeManager.AlertHorde(alerterZombie, target, radius);
		}
	}

	float GetAlertRadius()
	{
		return m_fAlertRadius;
	}

	void SetAlertRadius(float radius)
	{
		m_fAlertRadius = radius;
	}

	float GetAlertCooldown()
	{
		return m_fAlertCooldown;
	}

	void SetAlertCooldown(float cooldown)
	{
		m_fAlertCooldown = cooldown;
	}
}
