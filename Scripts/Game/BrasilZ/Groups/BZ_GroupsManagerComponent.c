// BrasilZ Groups Manager - manual groups only.
//
// Vanilla group logic is allowed to initialize the group UI, but any automatic
// group assignment during spawn/faction setup is removed right after it happens.
[ComponentEditorProps(category: "BrasilZ/Groups", description: "Manual groups only. Players spawn without group.")]
class BZ_GroupsManagerComponentClass : SCR_GroupsManagerComponentClass
{
}

class BZ_GroupsManagerComponent : SCR_GroupsManagerComponent
{
	protected static const int BZ_GROUP_AUTOREMOVE_DELAY_MS = 250;
	protected static const int BZ_EMPTY_GROUP_CLEANUP_INTERVAL_MS = 60000;
	protected bool m_bBZCleanupArmed;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		if (!m_bBZCleanupArmed)
		{
			m_bBZCleanupArmed = true;
			GetGame().GetCallqueue().CallLater(BZ_DeleteEmptyPlayableGroups, BZ_EMPTY_GROUP_CLEANUP_INTERVAL_MS, true);
		}
	}

	//------------------------------------------------------------------------------------------------
	override protected void OnPlayerRegistered(int playerId)
	{
		super.OnPlayerRegistered(playerId);
		Print(string.Format("[BrasilZ][Groups] Player %1 registered - manual groups only.", playerId), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	override void OnPlayerAuditSuccess(int playerId)
	{
		super.OnPlayerAuditSuccess(playerId);
		GetGame().GetCallqueue().CallLater(BZ_RemoveAutomaticGroupForPlayer, BZ_GROUP_AUTOREMOVE_DELAY_MS, false, playerId, "audit");
	}

	//------------------------------------------------------------------------------------------------
	override void OnPlayerFactionChanged(notnull FactionAffiliationComponent owner, Faction previousFaction, Faction newFaction)
	{
		// Let vanilla initialize faction group data and menu infrastructure.
		super.OnPlayerFactionChanged(owner, previousFaction, newFaction);

		int playerId = BZ_GetPlayerIdFromFactionOwner(owner);
		if (playerId > 0)
			GetGame().GetCallqueue().CallLater(BZ_RemoveAutomaticGroupForPlayer, BZ_GROUP_AUTOREMOVE_DELAY_MS, false, playerId, "faction");
	}

	//------------------------------------------------------------------------------------------------
	protected int BZ_GetPlayerIdFromFactionOwner(FactionAffiliationComponent owner)
	{
		if (!owner)
			return 0;

		PlayerController playerController = PlayerController.Cast(owner.GetOwner());
		if (playerController)
			return playerController.GetPlayerId();

		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (!playerManager)
			return 0;

		array<int> players = {};
		playerManager.GetPlayers(players);
		foreach (int playerId : players)
		{
			PlayerController controller = playerManager.GetPlayerController(playerId);
			if (!controller)
				continue;

			FactionAffiliationComponent component = FactionAffiliationComponent.Cast(controller.FindComponent(FactionAffiliationComponent));
			if (component == owner)
				return playerId;
		}

		return 0;
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_RemoveAutomaticGroupForPlayer(int playerId, string reason)
	{
		if (playerId <= 0)
			return;

		SCR_AIGroup group = GetPlayerGroup(playerId);
		if (!group)
			return;

		int groupId = group.GetGroupID();
		group.RemovePlayer(playerId);
		Print(string.Format("[BrasilZ][Groups] Removed player %1 from automatic group %2 after %3 setup.", playerId, groupId, reason), LogLevel.NORMAL);

		if (!BZ_GroupHasPlayers(group))
		{
			DeleteGroupDelayed(group);
			Print(string.Format("[BrasilZ][Groups] Empty automatic group %1 queued for deletion.", groupId), LogLevel.NORMAL);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected bool BZ_GroupHasPlayers(SCR_AIGroup group)
	{
		if (!group)
			return false;

		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (!playerManager)
			return true;

		array<int> players = {};
		playerManager.GetPlayers(players);
		foreach (int playerId : players)
		{
			SCR_AIGroup playerGroup = GetPlayerGroup(playerId);
			if (playerGroup == group)
				return true;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_DeleteEmptyPlayableGroups()
	{
		array<SCR_AIGroup> groups = {};
		GetAllPlayableGroups(groups);

		foreach (SCR_AIGroup group : groups)
		{
			if (!group)
				continue;

			if (BZ_GroupHasPlayers(group))
				continue;

			int groupId = group.GetGroupID();
			DeleteGroupDelayed(group);
			Print(string.Format("[BrasilZ][Groups] Empty playable group %1 queued for periodic deletion.", groupId), LogLevel.NORMAL);
		}
	}
}
