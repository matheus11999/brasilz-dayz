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
		BZ_SpawnPointSpawnData spawnPointData = BZ_SpawnPointSpawnData.Cast(data);
		if (!spawnPointData)
		{
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
				ClearDeathFlagForFreshSpawn(data);

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

		SCR_ESpawnResult result = super.SpawnEntity_S(requestComponent, data, spawnedEntity);
		if (result != SCR_ESpawnResult.OK || !spawnedEntity)
			return result;

		PostProcessSpawnedPlayer(spawnedEntity, spawnPointData.GetPlayerId());

		if (spawnPointData.HasExplicitTransform())
			Print(string.Format("[BrasilZ] Spawned saved player at %1", spawnPointData.GetPosition()), LogLevel.NORMAL);
		else
			Print(string.Format("[BrasilZ] Spawned player at %1", spawnPoint.GetOrigin()), LogLevel.NORMAL);

		return result;
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

		GetGame().GetCallqueue().CallLater(BZ_StarterLoadout.Apply, 250, false, spawnedEntity);
		GetGame().GetCallqueue().CallLater(BZ_StarterLoadout.Apply, 1250, false, spawnedEntity);
		GetGame().GetCallqueue().CallLater(BZ_StarterLoadout.Apply, 3000, false, spawnedEntity);

		// Force-leave any group on spawn so player must opt-in via group menu.
		if (playerId > 0)
			GetGame().GetCallqueue().CallLater(RemovePlayerFromGroupsOnSpawn, 500, false, playerId);
	}

	//------------------------------------------------------------------------------------------------
	protected void RemovePlayerFromGroupsOnSpawn(int playerId)
	{
		BZ_GroupsManagerComponent mgr = BZ_GroupsManagerComponent.Cast(SCR_GroupsManagerComponent.GetInstance());
		if (mgr)
			mgr.RemovePlayerFromAllGroups(playerId);
	}

	//------------------------------------------------------------------------------------------------
	// Shared clear used by both the BZ and the vanilla-data branches when the player completes
	// a fresh spawn (deploy menu / starter loadout). Resolves the UID via the spawn data's
	// player id if available, otherwise asks the spawn request component for the controller.
	protected void ClearDeathFlagForFreshSpawn(SCR_SpawnData data)
	{
		int playerId = -1;
		// Prefer the spawn-data player id when present
		SCR_SpawnPointSpawnData vanillaSpawnPoint = SCR_SpawnPointSpawnData.Cast(data);
		if (vanillaSpawnPoint)
			playerId = vanillaSpawnPoint.GetPlayerId();
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
