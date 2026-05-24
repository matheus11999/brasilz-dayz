[ComponentEditorProps(category: "BrasilZ/Spawning", description: "Handles BrasilZ random spawn points.")]
class BZ_SpawnPointSpawnHandlerComponentClass : SCR_SpawnPointSpawnHandlerComponentClass
{
}

class BZ_SpawnPointSpawnHandlerComponent : SCR_SpawnPointSpawnHandlerComponent
{
	//------------------------------------------------------------------------------------------------
	protected override bool ValidateData_S(SCR_SpawnRequestComponent requestComponent, SCR_SpawnData data)
	{
		BZ_SpawnPointSpawnData spawnPointData = BZ_SpawnPointSpawnData.Cast(data);
		if (!spawnPointData)
			return super.ValidateData_S(requestComponent, data);

		if (spawnPointData.HasExplicitTransform())
			return true;

		return spawnPointData.GetSpawnPoint() != null;
	}

	//------------------------------------------------------------------------------------------------
	protected override bool CanRequestSpawn_S(SCR_SpawnRequestComponent requestComponent, SCR_SpawnData data, out SCR_ESpawnResult result)
	{
		BZ_SpawnPointSpawnData spawnPointData = BZ_SpawnPointSpawnData.Cast(data);
		if (!spawnPointData)
			return super.CanRequestSpawn_S(requestComponent, data, result);

		if (spawnPointData.HasExplicitTransform())
		{
			result = SCR_ESpawnResult.OK;
			return true;
		}

		if (!spawnPointData.GetSpawnPoint())
		{
			result = SCR_ESpawnResult.SPAWN_NOT_ALLOWED;
			return false;
		}

		result = SCR_ESpawnResult.OK;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected override bool PrepareEntity_S(SCR_SpawnRequestComponent requestComponent, IEntity entity, SCR_SpawnData data)
	{
		BZ_SpawnPointSpawnData spawnPointData = BZ_SpawnPointSpawnData.Cast(data);
		if (!spawnPointData)
			return super.PrepareEntity_S(requestComponent, entity, data);

		if (spawnPointData.HasExplicitTransform())
		{
			entity.SetOrigin(spawnPointData.GetPosition());
			entity.SetYawPitchRoll(spawnPointData.GetAngles());
			return super.PrepareEntity_S(requestComponent, entity, data);
		}

		BZ_SpawnPoint spawnPoint = spawnPointData.GetSpawnPoint();
		if (!spawnPoint)
			return false;

		vector pos, ypr;
		spawnPoint.GetPosYPR(pos, ypr);
		entity.SetOrigin(pos);
		entity.SetYawPitchRoll(ypr);

		return super.PrepareEntity_S(requestComponent, entity, data);
	}

	//------------------------------------------------------------------------------------------------
	protected override SCR_ESpawnResult SpawnEntity_S(SCR_SpawnRequestComponent requestComponent, notnull SCR_SpawnData data, out IEntity spawnedEntity)
	{
		// Classify spawn source for diagnostic logging.
		string spawnSource = "UNKNOWN";
		if (BZ_SpawnPointSpawnData.Cast(data))
			spawnSource = "BZ_SPAWN_POINT (deploy menu BrasilZ)";
		else if (SCR_PossessSpawnData.Cast(data))
			spawnSource = "RECONNECT (possess persisted character)";
		else if (SCR_SpawnPointSpawnData.Cast(data))
			spawnSource = "VANILLA_SPAWN_POINT (deploy menu vanilla flow)";
		else
			spawnSource = string.Format("OTHER (%1)", data.Type().ToString());

		BZ_SpawnPointSpawnData spawnPointData = BZ_SpawnPointSpawnData.Cast(data);
		if (!spawnPointData)
		{
			Print(string.Format("[BrasilZ][SpawnHandler] Vanilla path: source=%1", spawnSource), LogLevel.NORMAL);
			SCR_ESpawnResult vanillaResult = super.SpawnEntity_S(requestComponent, data, spawnedEntity);

			// CRITICAL: do NOT run PostProcessSpawnedPlayer for non-BZ spawn data.
			// SCR_PossessSpawnData (reconnect) reaches this branch — running starter loadout
			// would call StripMilitaryItems on the persisted character and break weapon
			// state (reload/inspect actions stop working because the equipped weapon is removed).
			// Starter loadout must only apply to fresh menu spawns (BZ_SpawnPointSpawnData below).

			// BUT we DO need to clear the persisted death flag here. The vanilla deploy menu
			// builds SCR_SpawnPointSpawnData (not BZ_*), so without this hook a player who died
			// last session would loop forever: rejoin → death flag rejects char → menu opens →
			// player picks spawn → flag never cleared → next rejoin rejects again.
			//
			// Reconnects use SCR_PossessSpawnData, which is also non-BZ. Do NOT clear the flag
			// for reconnects — possessing a saved character does not mean the dead flag should
			// drop. The original death flag check already rejected those before reaching here.
			if (vanillaResult == SCR_ESpawnResult.OK && spawnedEntity && !SCR_PossessSpawnData.Cast(data))
			{
				if (SCR_SpawnPointSpawnData.Cast(data))
				{
					int vanillaPlayerId = BZ_ResolvePlayerIdFromSpawnedEntity(spawnedEntity);
					PostProcessSpawnedPlayer(spawnedEntity, vanillaPlayerId);
					GetGame().GetCallqueue().CallLater(ClearDeathFlagForFreshSpawn, 500, false, spawnedEntity);
				}
				else
				{
					ClearDeathFlagForFreshSpawn(spawnedEntity);
				}
			}

			// SPAWN PROTECTION DISABLED — bug em BZ_SpawnProtection.Apply (linha 44):
			// `CallQueue.Remove(BZ_DisableSpawnProtection)` matava callbacks de OUTROS
			// players. P1 spawna → damage OFF + disable agendado. P2 spawna depois →
			// Remove mata disable do P1 → P1 invencível pra sempre. Players reportaram
			// "nao consigo matar ninguem". Removido até refactor (map per-playerId).
			// if (vanillaResult == SCR_ESpawnResult.OK && spawnedEntity)
			// {
			//     PlayerManager pmProt = GetGame().GetPlayerManager();
			//     int protPlayerId = 0;
			//     if (pmProt)
			//         protPlayerId = pmProt.GetPlayerIdFromControlledEntity(spawnedEntity);
			//     BZ_SpawnProtection.Apply(spawnedEntity, protPlayerId);
			// }

			// Log post-vanilla spawn state.
			if (vanillaResult == SCR_ESpawnResult.OK && spawnedEntity)
				BZ_LogSpawnedEntityState(spawnedEntity, spawnSource, "vanilla branch");

			return vanillaResult;
		}

		BZ_SpawnPoint spawnPoint = spawnPointData.GetSpawnPoint();
		if (!spawnPoint && !spawnPointData.HasExplicitTransform())
		{
			Print("[BrasilZ] No spawn point in spawn data.", LogLevel.ERROR);
			return SCR_ESpawnResult.SPAWN_NOT_ALLOWED;
		}

		ResourceName prefab = spawnPointData.GetPrefab();
		if (prefab.IsEmpty())
		{
			Print("[BrasilZ] No character prefab in spawn data.", LogLevel.ERROR);
			return SCR_ESpawnResult.SPAWN_NOT_ALLOWED;
		}

		Print(string.Format("[BrasilZ][SpawnHandler] BZ path: source=%1 prefab=%2 explicit=%3 spawnPoint=%4", spawnSource, prefab, spawnPointData.HasExplicitTransform(), spawnPoint != null), LogLevel.NORMAL);

		SCR_ESpawnResult result = super.SpawnEntity_S(requestComponent, data, spawnedEntity);
		if (result != SCR_ESpawnResult.OK || !spawnedEntity)
		{
			Print(string.Format("[BrasilZ][SpawnHandler] FAIL result=%1 entity=%2", result, spawnedEntity != null), LogLevel.WARNING);
			return result;
		}

		PostProcessSpawnedPlayer(spawnedEntity, spawnPointData.GetPlayerId());

		// Detailed spawn state log: faction, group, prefab, pos. Critical for debugging
		// "player spawned but with wrong faction/group" complaints.
		BZ_LogSpawnedEntityState(spawnedEntity, spawnSource, "BZ branch");

		if (spawnPointData.HasExplicitTransform())
			Print(string.Format("[BrasilZ][SpawnHandler] Spawned saved player at %1", spawnPointData.GetPosition()), LogLevel.NORMAL);
		else
			Print(string.Format("[BrasilZ][SpawnHandler] Spawned player at %1", spawnPoint.GetOrigin()), LogLevel.NORMAL);

		return result;
	}

	//------------------------------------------------------------------------------------------------
	// Dump faction, group, prefab, pos, HP, lifeState of just-spawned char. Logs at warning
	// level so it stands out in script log. Call after super.SpawnEntity_S returns OK.
	protected void BZ_LogSpawnedEntityState(IEntity spawnedEntity, string spawnSource, string branch)
	{
		if (!spawnedEntity)
			return;

		vector spawnPos = spawnedEntity.GetOrigin();

		// Prefab
		string prefabPath = "(no-prefab-data)";
		if (spawnedEntity.GetPrefabData())
			prefabPath = spawnedEntity.GetPrefabData().GetPrefabName();

		// Faction
		string factionKey = "(no-faction-comp)";
		FactionAffiliationComponent facComp = FactionAffiliationComponent.Cast(spawnedEntity.FindComponent(FactionAffiliationComponent));
		if (facComp)
		{
			Faction f = facComp.GetAffiliatedFaction();
			if (f)
				factionKey = f.GetFactionKey();
			else
				factionKey = "(null-faction)";
		}

		// Group via PlayerManager → SCR_GroupsManagerComponent
		string groupInfo = "(unable_to_resolve)";
		PlayerManager pm = GetGame().GetPlayerManager();
		int playerId = 0;
		if (pm)
			playerId = pm.GetPlayerIdFromControlledEntity(spawnedEntity);
		if (playerId > 0)
		{
			SCR_GroupsManagerComponent groupsMgr = SCR_GroupsManagerComponent.GetInstance();
			if (groupsMgr)
			{
				SCR_AIGroup group = groupsMgr.GetPlayerGroup(playerId);
				if (group)
					groupInfo = string.Format("group_id=%1 size=%2", group.GetGroupID(), group.GetAgentsCount());
				else
					groupInfo = "NONE (correct — BZ_GroupsManagerComponent skips auto-assign)";
			}
			else
			{
				groupInfo = "no_groups_manager";
			}
		}
		else
		{
			groupInfo = string.Format("playerId_resolution_failed (pm=%1)", pm != null);
		}

		// HP + lifeState
		SCR_DamageManagerComponent dmgMgr = SCR_DamageManagerComponent.GetDamageManager(spawnedEntity);
		float spawnHP = -1;
		if (dmgMgr)
			spawnHP = dmgMgr.GetHealth();
		ECharacterLifeState spawnLifeState = ECharacterLifeState.ALIVE;
		CharacterControllerComponent spawnCC = CharacterControllerComponent.Cast(spawnedEntity.FindComponent(CharacterControllerComponent));
		if (spawnCC)
			spawnLifeState = spawnCC.GetLifeState();

		Print(string.Format("[BrasilZ][SpawnedState] Player %1 spawned | source=%2 | branch=%3 | prefab=%4 | pos=%5 | HP=%6 | lifeState=%7 | faction=%8 | %9",
			playerId, spawnSource, branch, prefabPath, spawnPos, spawnHP, typename.EnumToString(ECharacterLifeState, spawnLifeState), factionKey, groupInfo), LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	protected void PostProcessSpawnedPlayer(IEntity spawnedEntity, int playerId)
	{
		// Fresh spawn from menu — clear any death flag from previous life so future reconnects work.
		if (playerId > 0)
		{
			string uid = BZ_Utils.GetPlayerUID(playerId);
			if (!uid.IsEmpty())
			{
				BZ_PlayerDeathRegistry registry = BZ_PlayerDeathRegistry.GetInstance();
				if (registry)
				{
					registry.ClearDead(uid);
					registry.ClearDeadBody(playerId);
				}
			}
		}

		// SPAWN PROTECTION DISABLED — bug multi-player (vide comment no vanilla branch acima).
		// CallQueue.Remove matava disable callbacks de outros players → invencibilidade
		// permanente. Removido até refactor.
		// BZ_SpawnProtection.Apply(spawnedEntity, playerId);

		GetGame().GetCallqueue().CallLater(BZ_StarterLoadout.Apply, 250, false, spawnedEntity);
		GetGame().GetCallqueue().CallLater(BZ_StarterLoadout.Apply, 1250, false, spawnedEntity);
		GetGame().GetCallqueue().CallLater(BZ_StarterLoadout.Apply, 3000, false, spawnedEntity);

		// Discord webhook: notify spawn event (after loadout applied so balance
		// includes starter wallet). Delay 1.5s to let inventory replicate.
		if (playerId > 0 && Replication.IsServer() && BZ_DiscordConfig.LOG_SPAWN)
			GetGame().GetCallqueue().CallLater(BZ_NotifyDiscordSpawn, 1500, false, playerId, spawnedEntity);

		// No-op for groups: BZ_GroupsManagerComponent.OnPlayerRegistered + OnPlayerAuditSuccess
		// already skip vanilla auto-assign. Player joins no group on spawn, but can opt in via
		// the post-spawn group menu (M key) to create or join one.
	}

	//------------------------------------------------------------------------------------------------
	protected int BZ_ResolvePlayerIdFromSpawnedEntity(IEntity spawnedEntity)
	{
		if (!spawnedEntity)
			return 0;

		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return 0;

		return pm.GetPlayerIdFromControlledEntity(spawnedEntity);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_NotifyDiscordSpawn(int playerId, IEntity playerEntity)
	{
		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return;

		string name = pm.GetPlayerName(playerId);
		if (name.IsEmpty())
			name = string.Format("Player %1", playerId);

		vector pos;
		if (playerEntity)
			pos = playerEntity.GetOrigin();

		ref array<ref BZ_DiscordField> fields = new array<ref BZ_DiscordField>();
		fields.Insert(new BZ_DiscordField("Player", name));
		fields.Insert(new BZ_DiscordField("Posição", string.Format("<%1, %2, %3>", Math.Round(pos[0]), Math.Round(pos[1]), Math.Round(pos[2]))));
		BZ_DiscordWebhook.AddBalanceFields(playerEntity, fields);

		BZ_DiscordWebhook.Send(
			"🏠 Player respawnou",
			"**" + name + "** spawnou no mundo",
			BZ_DiscordConfig.COLOR_BLUE,
			fields
		);
	}

	//------------------------------------------------------------------------------------------------
	// Shared clear used by the vanilla-data branch when the player completes a fresh deploy-menu
	// spawn. SCR_SpawnPointSpawnData has no GetPlayerId(), so we resolve via the spawned entity's
	// controller in the PlayerManager.
	protected void ClearDeathFlagForFreshSpawn(IEntity spawnedEntity)
	{
		if (!spawnedEntity)
			return;

		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return;

		int playerId = pm.GetPlayerIdFromControlledEntity(spawnedEntity);
		if (playerId <= 0)
			return;

		string uid = BZ_Utils.GetPlayerUID(playerId);
		if (uid.IsEmpty())
			return;

		BZ_PlayerDeathRegistry registry = BZ_PlayerDeathRegistry.GetInstance();
		if (!registry)
			return;

		if (registry.IsDeadByUID(uid))
		{
			Print(string.Format("[BrasilZ] Clearing persisted death flag for player %1 (UID %2) after fresh menu spawn", playerId, uid), LogLevel.NORMAL);
			registry.ClearDead(uid);
			registry.ClearDeadBody(playerId);
		}
	}
}
