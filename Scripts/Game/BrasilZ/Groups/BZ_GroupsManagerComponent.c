[ComponentEditorProps(category: "BrasilZ/Groups", description: "Keeps vanilla group menu available without auto-assigning players to spawn groups.")]
class BZ_GroupsManagerComponentClass : SCR_GroupsManagerComponentClass
{
}

class BZ_GroupsManagerComponent : SCR_GroupsManagerComponent
{
	//------------------------------------------------------------------------------------------------
	override protected void OnPlayerRegistered(int playerId)
	{
		Print(string.Format("[BrasilZ][Groups] Player %1 registered without automatic spawn group.", playerId), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	override void OnPlayerAuditSuccess(int playerId)
	{
		// Skip super — no auto-assign to faction default group after auth.
		Print(string.Format("[BrasilZ][Groups] Player %1 audit success, no auto-group.", playerId), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	void RemovePlayerFromAllGroups(int playerId)
	{
		SCR_AIGroup group = GetPlayerGroup(playerId);
		if (group)
		{
			group.RemovePlayer(playerId);
			Print(string.Format("[BrasilZ][Groups] Removed player %1 from group %2 on spawn.", playerId, group.GetGroupID()), LogLevel.NORMAL);
		}
	}
}
