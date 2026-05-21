// BrasilZ — reduz HP dos zombies BaconZombies pra escala DayZ.
// Vanilla BaconZ usa stats de soldado militar (Health 100, Blood 6000). Player com
// faca precisa muitos golpes pra matar. Modded class detecta zombie BACON e
// reduz HP/Blood no OnInit.
modded class Bacon_622120A5448725E3_InfectedCharacter
{
	protected static const float BZ_ZOMBIE_HEALTH_MAX = 30.0;
	protected static const float BZ_ZOMBIE_BLOOD_MAX = 2000.0;
	protected static const float BZ_ZOMBIE_RESILIENCE_MAX = 30.0;

	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);

		if (!Replication.IsServer())
			return;

		SCR_CharacterDamageManagerComponent dmg = SCR_CharacterDamageManagerComponent.Cast(FindComponent(SCR_CharacterDamageManagerComponent));
		if (!dmg)
			return;

		// HitZones têm getters por categoria. Override MaxHealth e re-seta health corrente
		// pra novo cap (senão zombie spawna com 100 HP mesmo com cap 30).
		HitZone healthZone = dmg.GetHitZoneByName("Health");
		if (healthZone)
		{
			healthZone.SetMaxHealth(BZ_ZOMBIE_HEALTH_MAX);
			healthZone.SetHealth(BZ_ZOMBIE_HEALTH_MAX);
		}

		HitZone bloodZone = dmg.GetHitZoneByName("Blood");
		if (bloodZone)
		{
			bloodZone.SetMaxHealth(BZ_ZOMBIE_BLOOD_MAX);
			bloodZone.SetHealth(BZ_ZOMBIE_BLOOD_MAX);
		}

		HitZone resilienceZone = dmg.GetHitZoneByName("Resilience");
		if (resilienceZone)
		{
			resilienceZone.SetMaxHealth(BZ_ZOMBIE_RESILIENCE_MAX);
			resilienceZone.SetHealth(BZ_ZOMBIE_RESILIENCE_MAX);
		}
	}
}
