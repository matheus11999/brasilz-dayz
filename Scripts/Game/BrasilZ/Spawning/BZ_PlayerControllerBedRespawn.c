// Bed respawn request (Pause Menu "Bed" button).
// v2 — chernarus.layer agora tem BrasilZ_BedManager entity (singleton instance).
//
// Server authoritatively checks:
//   1. Cooldown via BZ_BedCooldownStore (persisted in $profile:BrasilZ/BedCooldowns).
//   2. Player has bound bed via BLD_BedManagerComponent.GetBed(uid).
//   3. Bed entity exists in world.
//
// On success: kill current char (if alive) → 900ms later spawn via
// BZ_RespawnSystemComponent.RequestSavedPositionSpawnWithPrefab at bed origin.
// Sets cooldown for next 10min.
//
// Client visibility: m_bBzHasBed + m_iBzBedCooldownUntil are RplProp, replicated
// to owner so pause menu can show/hide + display countdown.

modded class SCR_PlayerController
{
	protected static const int BZ_BED_COOLDOWN_SEC = 600; // 10 min
	protected static const int BZ_BED_HASBED_CHECK_INTERVAL_MS = 5000;
	protected static const int BZ_BED_KILL_TO_SPAWN_DELAY_MS = 900;

	// Bed state — replicado via RPC explícito (RplProp em modded class é unreliable).
	bool m_bBzHasBed;
	int m_iBzBedCooldownUntil;

	protected bool m_bBzBedTickArmed;

	//------------------------------------------------------------------------------------------------
	// Client → server RPC trigger. Called by BZ_PauseMenuRespawn when "Bed" clicked.
	void BZ_RequestBedRespawn()
	{
		int playerId = GetPlayerId();
		Print(string.Format("[BrasilZ][BedRespawn] Client requesting bed respawn for player %1.", playerId), LogLevel.NORMAL);
		Rpc(BZ_RpcAskBedRespawn, playerId);
	}

	//------------------------------------------------------------------------------------------------
	// Client -> server refresh used when the pause menu opens. This avoids waiting up to 5s for
	// the periodic tick before the Bed button can appear.
	void BZ_RequestBedFlagRefresh()
	{
		Rpc(BZ_RpcAskBedFlagRefresh);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void BZ_RpcAskBedFlagRefresh()
	{
		BZ_UpdateAndPushBedFlags(true);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void BZ_RpcAskBedRespawn(int playerId)
	{
		int ownerPlayerId = GetPlayerId();
		if (ownerPlayerId > 0 && playerId != ownerPlayerId)
		{
			Print(string.Format("[BrasilZ][BedRespawn] Player id mismatch req=%1 owner=%2, using owner.", playerId, ownerPlayerId), LogLevel.WARNING);
			playerId = ownerPlayerId;
		}

		string uid = BZ_Utils.GetPlayerUID(playerId);
		if (uid.IsEmpty())
		{
			Print(string.Format("[BrasilZ][BedRespawn] Player %1 has empty UID, abort.", playerId), LogLevel.WARNING);
			return;
		}

		// 1. Cooldown check
		BZ_BedCooldownStore store = BZ_BedCooldownStore.GetInstance();
		if (store.IsOnCooldown(uid))
		{
			int remaining = store.GetCooldownUntil(uid) - System.GetUnixTime();
			Print(string.Format("[BrasilZ][BedRespawn] Player %1 cooldown active, %2s remaining.", playerId, remaining), LogLevel.WARNING);
			return;
		}

		// 2. Player has bound bed
		if (!BLD_BedManagerComponent.instance)
		{
			Print("[BrasilZ][BedRespawn] BLD_BedManagerComponent.instance null — building mod not loaded?", LogLevel.WARNING);
			return;
		}
		string bedId = BLD_BedManagerComponent.instance.GetBed(uid);
		if (bedId.Length() < 5)
		{
			Print(string.Format("[BrasilZ][BedRespawn] Player %1 has no bed bound.", playerId), LogLevel.WARNING);
			return;
		}

		// 3. Resolve bed entity + extract position
		IEntity bedEntity = IEntity.Cast(PersistenceSystem.GetInstance().FindById(bedId));
		if (!bedEntity)
		{
			Print(string.Format("[BrasilZ][BedRespawn] Bed entity for player %1 not found in world (bedId=%2).", playerId, bedId), LogLevel.WARNING);
			return;
		}

		// 4. Bed must be standalone (not inside someone's inventory)
		InventoryItemComponent bedIic = InventoryItemComponent.Cast(bedEntity.FindComponent(InventoryItemComponent));
		if (bedIic && bedIic.GetParentSlot() != null)
		{
			Print(string.Format("[BrasilZ][BedRespawn] Bed for player %1 is inside inventory, cannot spawn.", playerId), LogLevel.WARNING);
			return;
		}

		vector bedPos = bedEntity.GetOrigin();
		vector bedAngles = "0 0 0";

		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return;

		// 5. Kill current char if alive (forces death state). Spawn happens 900ms after.
		IEntity controlled = pm.GetPlayerControlledEntity(playerId);
		bool needsKill = false;
		if (controlled)
		{
			SCR_DamageManagerComponent dmgMgr = SCR_DamageManagerComponent.GetDamageManager(controlled);
			if (dmgMgr && !dmgMgr.IsDestroyed() && dmgMgr.GetHealth() > 0)
			{
				SCR_CharacterDamageManagerComponent charDmg = SCR_CharacterDamageManagerComponent.Cast(dmgMgr);
				if (charDmg)
				{
					charDmg.Kill(Instigator.CreateInstigator(null));
					needsKill = true;
					Print(string.Format("[BrasilZ][BedRespawn] Player %1 killed pre-bed-spawn.", playerId), LogLevel.NORMAL);
				}
			}
		}

		// 6. Set cooldown imediato (mesmo se spawn falhar — evita farming)
		int newCooldown = System.GetUnixTime() + BZ_BED_COOLDOWN_SEC;
		store.SetCooldown(uid, newCooldown);
		m_iBzBedCooldownUntil = newCooldown;
		Replication.BumpMe();

		Print(string.Format("[BrasilZ][BedRespawn] Player %1 cooldown set until unix=%2 (10min).", playerId, newCooldown), LogLevel.NORMAL);

		// 7. Schedule actual spawn after kill processing
		int delay = 0;
		if (needsKill)
			delay = BZ_BED_KILL_TO_SPAWN_DELAY_MS;

		GetGame().GetCallqueue().CallLater(BZ_ExecuteBedSpawn, delay, false, playerId, bedPos, bedAngles);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_ExecuteBedSpawn(int playerId, vector bedPos, vector bedAngles)
	{
		if (!Replication.IsServer())
			return;

		// Clear death respawn state so vanilla doesn't fire random spawn alongside ours.
		BZ_MenuSpawnLogic.BZ_ClearDeathRespawnStateStatic(playerId);

		BZ_RespawnSystemComponent respawnSystem = BZ_RespawnSystemComponent.Cast(GetGame().GetGameMode().FindComponent(BZ_RespawnSystemComponent));
		if (!respawnSystem)
		{
			Print(string.Format("[BrasilZ][BedRespawn] No BZ_RespawnSystemComponent for player %1.", playerId), LogLevel.ERROR);
			return;
		}

		ResourceName prefab = respawnSystem.GetDefaultCharacterPrefab();
		respawnSystem.RequestSavedPositionSpawnWithPrefab(playerId, prefab, bedPos, bedAngles);
		Print(string.Format("[BrasilZ][BedRespawn] Player %1 spawn requested at bed pos %2.", playerId, bedPos), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	// Tick (server) — updates m_bBzHasBed flag + m_iBzBedCooldownUntil from disk store.
	// Called every 5s. Replicates to owner client for UI visibility/timer.
	protected void BZ_BedFlagTick()
	{
		if (!Replication.IsServer())
			return;

		BZ_UpdateAndPushBedFlags(false);
		GetGame().GetCallqueue().CallLater(BZ_BedFlagTick, BZ_BED_HASBED_CHECK_INTERVAL_MS, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_UpdateAndPushBedFlags(bool forceLog)
	{
		int playerId = GetPlayerId();
		if (playerId <= 0)
		{
			return;
		}

		string uid = BZ_Utils.GetPlayerUID(playerId);
		bool hasBedNew = false;
		int cooldownNew = 0;

		if (!uid.IsEmpty())
		{
			if (BLD_BedManagerComponent.instance)
			{
				string bedId = BLD_BedManagerComponent.instance.GetBed(uid);
				if (bedId.Length() >= 5)
					hasBedNew = true;
			}

			BZ_BedCooldownStore store = BZ_BedCooldownStore.GetInstance();
			cooldownNew = store.GetCooldownUntil(uid);
		}

		bool changed = false;
		if (m_bBzHasBed != hasBedNew)
		{
			Print(string.Format("[BrasilZ][BedRespawn][Tick] Player %1 hasBed flag: %2 → %3 (uid=%4)", playerId, m_bBzHasBed, hasBedNew, uid), LogLevel.NORMAL);
			m_bBzHasBed = hasBedNew;
			changed = true;
		}
		if (m_iBzBedCooldownUntil != cooldownNew)
		{
			Print(string.Format("[BrasilZ][BedRespawn][Tick] Player %1 cooldownUntil: %2 → %3 (now=%4)", playerId, m_iBzBedCooldownUntil, cooldownNew, System.GetUnixTime()), LogLevel.NORMAL);
			m_iBzBedCooldownUntil = cooldownNew;
			changed = true;
		}

		// Push state pro owner via RPC explícito (RplProp em modded class unreliable).
		// Server: sempre envia atual (changed OR não — primeira tick sempre push pra
		// client saber o estado inicial).
		Rpc(BZ_RpcUpdateBedFlags, m_bBzHasBed, m_iBzBedCooldownUntil);

		if (changed)
			Print(string.Format("[BrasilZ][BedRespawn][Tick] Pushed RPC to owner: hasBed=%1 cooldown=%2", m_bBzHasBed, m_iBzBedCooldownUntil), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	// RPC server → owner client. Atualiza state local do PauseMenuUI poder ler.
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void BZ_RpcUpdateBedFlags(bool hasBed, int cooldownUntil)
	{
		bool changed = (m_bBzHasBed != hasBed) || (m_iBzBedCooldownUntil != cooldownUntil);
		m_bBzHasBed = hasBed;
		m_iBzBedCooldownUntil = cooldownUntil;
		if (changed)
			Print(string.Format("[BrasilZ][BedRespawn][Owner] Bed state updated: hasBed=%1 cooldown=%2", hasBed, cooldownUntil), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	// Hook tick start — called after player controller initialized server-side.
	void BZ_StartBedFlagTickIfNeeded()
	{
		if (m_bBzBedTickArmed)
		{
			Print(string.Format("[BrasilZ][BedRespawn][TickArm] Already armed for player %1, skip.", GetPlayerId()), LogLevel.NORMAL);
			return;
		}
		if (!Replication.IsServer())
		{
			Print("[BrasilZ][BedRespawn][TickArm] Not server, skip.", LogLevel.NORMAL);
			return;
		}
		m_bBzBedTickArmed = true;
		Print(string.Format("[BrasilZ][BedRespawn][TickArm] Armed for player %1, first tick now, interval=%2ms.", GetPlayerId(), BZ_BED_HASBED_CHECK_INTERVAL_MS), LogLevel.NORMAL);
		BZ_UpdateAndPushBedFlags(true);
		GetGame().GetCallqueue().CallLater(BZ_BedFlagTick, BZ_BED_HASBED_CHECK_INTERVAL_MS, false);
	}

	//------------------------------------------------------------------------------------------------
	// Returns true if bed button should be shown in pause menu.
	bool BZ_HasBoundBed()
	{
		return m_bBzHasBed;
	}

	//------------------------------------------------------------------------------------------------
	// Returns seconds remaining on cooldown, 0 if none.
	int BZ_GetBedCooldownRemaining()
	{
		if (m_iBzBedCooldownUntil <= 0)
			return 0;
		int rem = m_iBzBedCooldownUntil - System.GetUnixTime();
		if (rem < 0)
			return 0;
		return rem;
	}
}
