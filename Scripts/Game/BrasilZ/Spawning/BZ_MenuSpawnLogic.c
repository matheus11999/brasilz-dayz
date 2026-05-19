class BZ_MenuSpawnLogic : SCR_MenuSpawnLogic
{
	//------------------------------------------------------------------------------------------------
	void BZ_MenuSpawnLogic()
	{
		m_eDisconnectPlayerControllerBehaviour = SCR_ESpawnLogicDisconnectBehaviour.SAVE;
		m_eDisconnectCharacterBehaviour = SCR_ESpawnLogicDisconnectBehaviour.DELETE;
		m_sForcedFaction = "CIV";
		m_bWaitForSpawnPoints = true;
		m_fDeployMenuOpenDelay = 4.0;
	}
}
