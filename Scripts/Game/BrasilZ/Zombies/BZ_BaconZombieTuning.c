// BrasilZ — reduz HP dos zombies BaconZombies pra escala DayZ.
//
// Vanilla BaconZ DamageManager_Character_Empty_Base.ct usa stats de soldado militar:
//   Health     100
//   Blood      6000
//   Resilience 100
//
// Resultado: player precisa muitos hits de faca pra matar zombie. Não é DayZ-like
// (1 headshot + 2-3 facadas body deveriam bastar).
//
// SDK não expõe SetMaxHealth na HitZone (só GetMaxHealth + GetDamageMultiplier).
// Estratégia: usar SetHealth() pra reduzir HP atual logo no spawn. Health zone
// não regenera (sem m_fFullRegenerationTime), então valor reduzido persiste até
// o zombie morrer.
modded class Bacon_622120A5448725E3_InfectedCharacter
{
	// Vida alvo pós-init. Vanilla = 100. Lower = morre mais rápido.
	protected static const float BZ_ZOMBIE_HEALTH_TARGET = 30.0;
	protected static const float BZ_ZOMBIE_BLOOD_TARGET = 2000.0;
	protected static const float BZ_ZOMBIE_RESILIENCE_TARGET = 30.0;

	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);

		if (!Replication.IsServer())
			return;

		SCR_CharacterDamageManagerComponent dmg = SCR_CharacterDamageManagerComponent.Cast(GetDamageManager());
		if (!dmg)
			return;

		// Lower health/blood/resilience absolute values. Vanilla zone caps em 100/6000/100.
		// SetHealth aceita valor absoluto. Zombie dies normalmente ao chegar em 0.
		HitZone healthZone = dmg.GetHitZoneByName("Health");
		if (healthZone)
			healthZone.SetHealth(BZ_ZOMBIE_HEALTH_TARGET);

		HitZone bloodZone = dmg.GetHitZoneByName("Blood");
		if (bloodZone)
			bloodZone.SetHealth(BZ_ZOMBIE_BLOOD_TARGET);

		HitZone resilienceZone = dmg.GetHitZoneByName("Resilience");
		if (resilienceZone)
			resilienceZone.SetHealth(BZ_ZOMBIE_RESILIENCE_TARGET);

		Print(string.Format("[BrasilZ][Zombie] HP tuned to %1 (blood %2, resilience %3)", BZ_ZOMBIE_HEALTH_TARGET, BZ_ZOMBIE_BLOOD_TARGET, BZ_ZOMBIE_RESILIENCE_TARGET), LogLevel.DEBUG);
	}
}
