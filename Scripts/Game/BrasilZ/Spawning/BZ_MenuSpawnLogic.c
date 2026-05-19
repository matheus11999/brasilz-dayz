class BZ_MenuSpawnLogic : SCR_MenuSpawnLogic
{
	//------------------------------------------------------------------------------------------------
	void BZ_MenuSpawnLogic()
	{
		// CRITICAL: SAVE both controller and character so players persist across disconnects
		// and can reconnect to their existing entity. Corpse cleanup is handled by
		// BZ_GameMode.TrackCorpseForCleanup so dead bodies don't accumulate.
		m_eDisconnectPlayerControllerBehaviour = SCR_ESpawnLogicDisconnectBehaviour.SAVE;
		m_eDisconnectCharacterBehaviour = SCR_ESpawnLogicDisconnectBehaviour.SAVE;
		m_sForcedFaction = "CIV";
		m_bWaitForSpawnPoints = true;
		m_fDeployMenuOpenDelay = 4.0;
	}
}
