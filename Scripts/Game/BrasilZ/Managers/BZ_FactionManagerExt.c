// BrasilZ FactionManager extension.
//
// Before: chernarus.layer declared `SCR_FactionManager BrasilZ_FactionManager` as a SEPARATE
// entity, which duplicated the vanilla SCR_FactionManager that GameModeSF.et spawns as a child
// of BrasilZ_GameMode2. Engine logged:
//   "Multiple faction managers present!"
// and the vanilla one (without BrasilZ Factions list) won the singleton registration —
// breaking CIV playability and BaconZ_Faction registration.
//
// After: chernarus.layer no longer declares BrasilZ_FactionManager. This modded class hooks
// into whichever SCR_FactionManager wins (always vanilla now) and validates the Faction state.
//
// API used here is conservative (only GetFactionByKey + standard Faction methods). If a faction
// is missing, this only logs — BaconZombies mod is expected to self-register BaconZ_Faction.
//
// Logs use prefix [BrasilZ][FactionMgr] for grep-ability.

modded class SCR_FactionManager
{
	protected bool m_bBzFactionInitDone;

	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);

		if (m_bBzFactionInitDone)
			return;
		m_bBzFactionInitDone = true;

		// Defer 3s so other mods (BaconZombies etc) finish registering their factions first.
		GetGame().GetCallqueue().CallLater(BZ_ValidateFactions, 3000, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_ValidateFactions()
	{
		Print("[BrasilZ][FactionMgr] Validating factions post-init", LogLevel.NORMAL);

		Faction civFaction = GetFactionByKey("CIV");
		Faction baconFaction = GetFactionByKey("BaconZ");
		if (!baconFaction)
			baconFaction = GetFactionByKey("BACONZ");
		if (!baconFaction)
			baconFaction = GetFactionByKey("Zombies");

		if (civFaction)
			Print(string.Format("[BrasilZ][FactionMgr] CIV registered (entity=%1)", civFaction), LogLevel.NORMAL);
		else
			Print("[BrasilZ][FactionMgr] WARNING: CIV faction NOT registered — players cannot spawn into CIV", LogLevel.WARNING);

		if (baconFaction)
			Print(string.Format("[BrasilZ][FactionMgr] BaconZ-like faction registered (entity=%1)", baconFaction), LogLevel.NORMAL);
		else
			Print("[BrasilZ][FactionMgr] WARNING: BaconZ faction NOT registered — zombie hostility uncertain", LogLevel.WARNING);
	}
}
