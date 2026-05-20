class BZ_Utils
{
	static string GetPlayerUID(int playerId)
	{
		if (playerId <= 0)
			return string.Empty;

		BackendApi api = GetGame().GetBackendApi();
		if (!api)
			return string.Empty;

		return api.GetPlayerIdentityId(playerId);
	}

	static bool IsCharacterDying(IEntity character)
	{
		if (!character)
			return false;

		// Trust health ONLY. CharacterControllerComponent.GetLifeState() returns DEAD or
		// INCAPACITATED transiently on alive characters (e.g. during a disconnect frame
		// before replication finishes), which previously flagged living players as dead
		// on logout. Anti-ALT+F4 must only fire when the damage manager confirms the
		// player is actually destroyed or at 0 health — lifeState alone is not enough.
		SCR_DamageManagerComponent dmgMgr = SCR_DamageManagerComponent.GetDamageManager(character);
		if (!dmgMgr)
			return false;

		return dmgMgr.IsDestroyed() || dmgMgr.GetHealth() <= 0;
	}
}
