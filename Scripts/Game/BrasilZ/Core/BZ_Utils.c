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

		SCR_DamageManagerComponent dmgMgr = SCR_DamageManagerComponent.GetDamageManager(character);
		if (dmgMgr && (dmgMgr.IsDestroyed() || dmgMgr.GetHealth() <= 0))
			return true;

		CharacterControllerComponent charCtrl = CharacterControllerComponent.Cast(character.FindComponent(CharacterControllerComponent));
		if (charCtrl)
		{
			ECharacterLifeState lifeState = charCtrl.GetLifeState();
			if (lifeState == ECharacterLifeState.DEAD || lifeState == ECharacterLifeState.INCAPACITATED)
				return true;
		}

		return false;
	}
}
