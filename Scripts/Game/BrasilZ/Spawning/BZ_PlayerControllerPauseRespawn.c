// Client-to-server request used by the Pause Menu Respawn button.
modded class SCR_PlayerController
{
	//------------------------------------------------------------------------------------------------
	void BZ_RequestPauseMenuRespawn()
	{
		int playerId = GetPlayerId();
		Print(string.Format("[BrasilZ][PauseRespawn] Client requesting respawn for player %1.", playerId), LogLevel.NORMAL);
		Rpc(BZ_RpcAskPauseMenuRespawn, playerId);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void BZ_RpcAskPauseMenuRespawn(int playerId)
	{
		int ownerPlayerId = GetPlayerId();
		if (ownerPlayerId > 0 && playerId != ownerPlayerId)
		{
			Print(string.Format("[BrasilZ][PauseRespawn] Player id mismatch request=%1 owner=%2, using owner id.", playerId, ownerPlayerId), LogLevel.WARNING);
			playerId = ownerPlayerId;
		}

		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (!playerManager)
			return;

		BZ_MenuSpawnLogic.BZ_ClearDeathRespawnStateStatic(playerId);

		BZ_MenuSpawnLogic spawnLogic = BZ_MenuSpawnLogic.Cast(GetGame().GetGameMode().FindComponent(BZ_MenuSpawnLogic));
		if (spawnLogic)
			spawnLogic.BZ_ClearDeathRespawnState(playerId);

		IEntity controlled = playerManager.GetPlayerControlledEntity(playerId);
		if (controlled)
		{
			SCR_DamageManagerComponent damageManager = SCR_DamageManagerComponent.GetDamageManager(controlled);
			bool alive = damageManager && !damageManager.IsDestroyed() && damageManager.GetHealth() > 0;
			if (alive)
			{
				SCR_CharacterDamageManagerComponent characterDamage = SCR_CharacterDamageManagerComponent.Cast(damageManager);
				if (characterDamage)
				{
					characterDamage.Kill(Instigator.CreateInstigator(null));
					Print(string.Format("[BrasilZ][PauseRespawn] Player %1 killed by pause respawn request.", playerId), LogLevel.WARNING);
					GetGame().GetCallqueue().CallLater(BZ_ForcePauseMenuRespawnAfterKill, 900, false, playerId);
					return;
				}

				Print(string.Format("[BrasilZ][PauseRespawn] Player %1 has alive entity but no character damage manager; respawn request aborted.", playerId), LogLevel.WARNING);
				return;
			}
		}

		if (spawnLogic)
		{
			spawnLogic.BZ_ForceRandomRespawnFromButton(playerId);
			return;
		}

		BZ_RespawnSystemComponent respawnSystem = BZ_RespawnSystemComponent.Cast(GetGame().GetGameMode().FindComponent(BZ_RespawnSystemComponent));
		if (!respawnSystem)
		{
			Print("[BrasilZ][PauseRespawn] BZ_RespawnSystemComponent not found.", LogLevel.ERROR);
			return;
		}

		respawnSystem.RequestRandomSpawn(playerId);
		Print(string.Format("[BrasilZ][PauseRespawn] Player %1 had no alive controlled entity; random spawn requested.", playerId), LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_ForcePauseMenuRespawnAfterKill(int playerId)
	{
		if (!Replication.IsServer())
			return;

		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (!playerManager)
			return;

		PlayerController playerController = playerManager.GetPlayerController(playerId);
		if (!playerController)
			return;

		IEntity controlled = playerManager.GetPlayerControlledEntity(playerId);
		if (controlled)
		{
			SCR_DamageManagerComponent damageManager = SCR_DamageManagerComponent.GetDamageManager(controlled);
			bool alive = damageManager && !damageManager.IsDestroyed() && damageManager.GetHealth() > 0;
			if (alive)
			{
				Print(string.Format("[BrasilZ][PauseRespawn] Player %1 already has alive entity after pause respawn kill.", playerId), LogLevel.NORMAL);
				return;
			}
		}

		BZ_MenuSpawnLogic.BZ_ClearDeathRespawnStateStatic(playerId);

		BZ_MenuSpawnLogic spawnLogic = BZ_MenuSpawnLogic.Cast(GetGame().GetGameMode().FindComponent(BZ_MenuSpawnLogic));
		if (spawnLogic)
		{
			spawnLogic.BZ_ForceRandomRespawnFromButton(playerId);
			return;
		}

		BZ_RespawnSystemComponent respawnSystem = BZ_RespawnSystemComponent.Cast(GetGame().GetGameMode().FindComponent(BZ_RespawnSystemComponent));
		if (!respawnSystem)
		{
			Print("[BrasilZ][PauseRespawn] BZ_RespawnSystemComponent not found during deferred respawn.", LogLevel.ERROR);
			return;
		}

		respawnSystem.RequestRandomSpawn(playerId);
		Print(string.Format("[BrasilZ][PauseRespawn] Player %1 deferred random spawn requested after pause respawn kill.", playerId), LogLevel.WARNING);
	}
}
