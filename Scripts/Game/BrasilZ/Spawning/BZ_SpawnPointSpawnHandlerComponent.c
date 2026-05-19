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
			if (vanillaResult == SCR_ESpawnResult.OK && spawnedEntity)
				PostProcessSpawnedPlayer(spawnedEntity, 0);

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
}
