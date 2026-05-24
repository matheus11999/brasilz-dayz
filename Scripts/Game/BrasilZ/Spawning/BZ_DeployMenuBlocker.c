// BrasilZ - auto-spawn without showing the vanilla deploy/death menu.
//
// Keep vanilla possession cleanup intact. We only block the client UI from
// opening while BZ_MenuSpawnLogic handles random respawn server-side.
modded class SCR_PlayerDeployMenuHandlerComponent
{
	override protected bool CanOpenMenu()
	{
		return false;
	}
}
