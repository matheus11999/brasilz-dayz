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
	// Vida alvo pós-init. Vanilla = 100. Lower = morre mais rápido com armas.
	//
	// IMPORTANTE: não reduzir Blood ou Resilience aqui. As vanilla DamageStateThresholds
	// definem que abaixo de ~33% Blood/Resilience o char vai pra estado WOUNDED/INCAP.
	// Zumbi com Blood 2000/6000 ou Resilience 30/100 nasce já em "critical" e cai morto
	// no init. Só Health é seguro reduzir — não tem regen e cap em 0 mata.
	protected static const float BZ_ZOMBIE_HEALTH_TARGET = 30.0;

	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);

		if (!Replication.IsServer())
			return;

		SCR_CharacterDamageManagerComponent dmg = SCR_CharacterDamageManagerComponent.Cast(GetDamageManager());
		if (!dmg)
			return;

		// Só Health. Blood + Resilience ficam no max (6000/100) pra evitar damage state
		// crítico que mata o zombie na hora do spawn.
		HitZone healthZone = dmg.GetHitZoneByName("Health");
		if (healthZone)
			healthZone.SetHealth(BZ_ZOMBIE_HEALTH_TARGET);

		Print(string.Format("[BrasilZ][Zombie] Health set to %1 (blood/resilience untouched)", BZ_ZOMBIE_HEALTH_TARGET), LogLevel.DEBUG);
	}
}
