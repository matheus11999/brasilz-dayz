// BrasilZ — reduz HP dos zombies BaconZombies pra escala DayZ + cap detect range.
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

// Max detect range para BaconZombies (metros). PerceptionComponent vanilla "vê" até ~100m;
// este cap atua via behavior tree decorator + horde broadcast filter. DayZ-like: zombie
// agro só quando player está perto. Tweak conforme gosto:
//   10 = bem cego, só pega muito perto
//   18 = default (DayZ-like)
//   30 = vê mais longe, ainda menos que vanilla
const float BZ_BACON_MAX_DETECT_RANGE = 18.0;

modded class Bacon_622120A5448725E3_InfectedCharacter
{
	// Vida alvo pós-init. Vanilla = 100. Lower = morre mais rápido com armas.
	//
	// IMPORTANTE: não reduzir Blood ou Resilience aqui. As vanilla DamageStateThresholds
	// definem que abaixo de ~33% Blood/Resilience o char vai pra estado WOUNDED/INCAP.
	// Zumbi com Blood 2000/6000 ou Resilience 30/100 nasce já em "critical" e cai morto
	// no init. Só Health é seguro reduzir — não tem regen e cap em 0 mata.
	protected static const float BZ_ZOMBIE_HEALTH_TARGET = 60.0;

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

		Print(string.Format("[BrasilZ][Zombie] Health set to %1, detect range capped at %2m", BZ_ZOMBIE_HEALTH_TARGET, BZ_BACON_MAX_DETECT_RANGE), LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	// Filtro extra: não propaga target pra horde se estiver muito longe.
	// Mesmo se PerceptionComponent detecta, broadcast só vai pra colegas se < BZ_BACON_MAX_DETECT_RANGE.
	override void AIBroadcastTarget(IEntity target)
	{
		if (!target)
			return;

		float distance = vector.Distance(GetOrigin(), target.GetOrigin());
		if (distance > BZ_BACON_MAX_DETECT_RANGE)
			return;

		super.AIBroadcastTarget(target);
	}
}

//------------------------------------------------------------------------------------------------
// Behavior tree decorator: limita visibilidade do zombie a BZ_BACON_MAX_DETECT_RANGE.
// PerceptionComponent vanilla "vê" até ~100m. Mesmo se target está no perception, este decorator
// retorna false se estiver fora do range — zombie behavior tree não considera mais como visível.
//
// NOTA: o crash anterior do Workbench NÃO foi causado por este modded class. Foi do
// wbSettingsDump.ini com Resource Browser apontando pra $BaconZombies: root, fazendo
// Workbench carregar Test.et com Frog.agr quebrada no startup. Já corrigido.
modded class Bacon_622120A5448725E3_AIIsEntityVisible
{
	override bool TestFunction(AIAgent owner)
	{
		bool baseResult = super.TestFunction(owner);
		if (!baseResult)
			return false;

		IEntity zombieEntity = owner.GetControlledEntity();
		if (!zombieEntity)
			return false;

		IEntity targetEntity;
		if (!GetVariableIn(TARGET_PORT, targetEntity))
			return false;

		if (!targetEntity)
			return false;

		float distance = vector.Distance(zombieEntity.GetOrigin(), targetEntity.GetOrigin());
		return distance <= BZ_BACON_MAX_DETECT_RANGE;
	}
}
