// BrasilZ FactionManager extension.
//
// Validates the player faction after the vanilla faction manager initializes.
// Zombies are disabled, so BaconZ faction is intentionally not registered or validated.

modded class SCR_FactionManager
{
	protected bool m_bBzFactionInitDone;

	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);

		if (m_bBzFactionInitDone)
			return;
		m_bBzFactionInitDone = true;

		GetGame().GetCallqueue().CallLater(BZ_ValidateFactions, 3000, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_ValidateFactions()
	{
		Print("[BrasilZ][FactionMgr] Validating player faction post-init", LogLevel.NORMAL);

		Faction civFaction = GetFactionByKey("CIV");
		if (civFaction)
			Print(string.Format("[BrasilZ][FactionMgr] CIV registered (entity=%1)", civFaction), LogLevel.NORMAL);
		else
			Print("[BrasilZ][FactionMgr] WARNING: CIV faction NOT registered - players cannot spawn into CIV", LogLevel.WARNING);
	}
}
