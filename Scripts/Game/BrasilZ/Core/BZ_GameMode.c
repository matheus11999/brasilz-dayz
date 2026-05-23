// BrasilZ game-mode-level hooks injected into vanilla SCR_BaseGameMode.
//
// Why this is a modded class and not a stand-alone subclass:
// The active GameMode entity in chernarus.layer is created as plain `SCR_BaseGameMode`
// using the GameModeSF.et prefab. A subclass `BZ_GameMode : SCR_BaseGameMode` is never
// instantiated (no layer references it), so its EOnInit / overrides never fire. Using
// `modded class SCR_BaseGameMode` makes every SCR_BaseGameMode instance get this logic.
//
// BZ_WalletContentsPersistence.c ALSO declares `modded class SCR_BaseGameMode`. Enfusion
// merges multiple modded declarations and chains overrides through super, so both work as
// long as we call super.OnPlayerDisconnected(...). Wallet hook fires first via super, then
// the engine processes disconnect, then our anti-ALT+F4 / decouple-corpse logic runs.
modded class SCR_BaseGameMode : BaseGameMode
{
	protected static const float BZ_AUTOSAVE_INTERVAL_SEC = 60.0;
	protected static const int BZ_AUTOSAVE_START_DELAY_MS = 5000;
	protected static const int BZ_AUTOSAVE_START_RETRY_MS = 3000;
	// FIX C: reduced from 30000ms — boot scan race with player connect caused entity lost.
	// 1000ms gives persistence layer time to settle without blocking connects long.
	protected static const int BZ_ORPHAN_GRACE_MS = 1000;
	protected static const float BZ_ORPHAN_SCAN_RADIUS = 20000.0;
	protected static const float BZ_ORPHAN_UNDERGROUND_OFFSET = 1000.0;
	protected static const float BZ_CORPSE_LIFETIME_SEC = 1800.0; // 30 minutes
	protected static const int BZ_CORPSE_CLEANUP_INTERVAL_MS = 60000;
	// FIX C: max time to wait for scan before allowing deferred connects through anyway.
	protected static const int BZ_CONNECT_GATE_MAX_RETRIES = 20; // 20 * 500ms = 10s cap
	// FIX B: save queue — engine SaveGame transaction is singleton. Concurrent saves error
	// with "Another transaction in progress". Queue serializes them.
	protected static const int BZ_SAVE_QUEUE_DELAY_MS = 250;

	// Whitelist of player character prefab resources used by BZ_RunOrphanScan to filter
	// out AI/zombie/mission squad bodies. Only entities whose prefab matches one of these
	// will be considered as player orphans for the boot bury sweep.
	protected static ref array<ResourceName> s_aBzPlayerCharacterPrefabs = {
		"{748B185D87E782EE}Prefabs/Characters/Character_BrasilZ_Survivor.et",
		"{B70400000000A001}Prefabs/Characters/Character_BrasilZ_Survivor_M70.et",
		"{B70400000000A002}Prefabs/Characters/Character_BrasilZ_Survivor_M88.et",
		"{B70400000000A003}Prefabs/Characters/Character_BrasilZ_Survivor_BDU.et",
		"{B70400000000A004}Prefabs/Characters/Character_BrasilZ_Survivor_Worker.et"
	};

	// Faction keys for DarcMissions enemy AI. SDRC_AIHelper.SpawnGroup calls
	// SpawnEntityPrefabPersistence on the SCR_AIGroup but individual AI chars may still
	// survive across server restart. Boot wipes any leftover non-player-controlled chars
	// belonging to these factions so the mission system can respawn cleanly.
	// USSR is included because DarcMissions config sets it as fallback faction.
	protected static ref array<string> s_aBzMissionEnemyFactionKeys = {
		"PLASTICBANDIT",
		"USSR"
	};

	protected ref array<IEntity> m_aBzOrphanScanResults;
	protected ref array<IEntity> m_aBzMissionAiResults;
	protected ref array<IEntity> m_aBzTrackedCorpses = new array<IEntity>();
	protected ref array<int> m_aBzCorpseDeathTimes = new array<int>();

	protected bool m_bBzAutoSaveEnabled;
	protected bool m_bBzAutoSaveScheduled;
	protected int m_iBzFlushRetryCount;
	protected int m_iBzOrphanArmRetries;
	protected bool m_bBzOrphanScanDone;
	protected bool m_bBzHooksInitialized;

	// FIX B: save queue state. Concurrent BZ_SavePlayerAndFlushToDisk calls collided with
	// engine's single-transaction save lock causing "Another transaction in progress" errors
	// and lost progress. Queue serializes save requests.
	protected static bool s_bBzSaveInProgress = false;
	protected static ref array<int> s_aBzPendingSaves = new array<int>();
	protected static int s_iBzSaveTotalQueued = 0;
	protected static int s_iBzSaveTotalCompleted = 0;
	protected static int s_iBzSaveTotalRejected = 0;

	// FIX C: connect-gate retry tracker. Maps playerId → retry count to cap deferrals.
	protected ref map<int, int> m_mBzConnectGateRetries = new map<int, int>();

	//------------------------------------------------------------------------------------------------
	bool BZ_IsProxy()
	{
		RplComponent rpl = RplComponent.Cast(FindComponent(RplComponent));
		return rpl && rpl.IsProxy();
	}

	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);

		if (m_bBzHooksInitialized)
			return;
		m_bBzHooksInitialized = true;

		if (BZ_IsProxy())
			return;

		Print("[BrasilZ][GameMode] EOnInit fired. Arming autosave + corpse cleanup + boot scan.", LogLevel.NORMAL);

		GetGame().GetCallqueue().CallLater(BZ_TickCorpseCleanup, BZ_CORPSE_CLEANUP_INTERVAL_MS, true);
		GetGame().GetCallqueue().CallLater(BZ_TryStartAutoSave, BZ_AUTOSAVE_START_DELAY_MS, false);
		GetGame().GetCallqueue().CallLater(BZ_TryArmOrphanScan, 1000, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_TryArmOrphanScan()
	{
		if (m_bBzOrphanScanDone)
			return;

		SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetScriptedInstance();
		if (!persistence)
		{
			m_iBzOrphanArmRetries++;
			if (m_iBzOrphanArmRetries < 30)
				GetGame().GetCallqueue().CallLater(BZ_TryArmOrphanScan, 1000, false);
			else
				Print("[BrasilZ][BootScan] Persistence system never appeared, giving up.", LogLevel.WARNING);
			return;
		}

		EPersistenceSystemState state = persistence.GetState();
		if (state >= EPersistenceSystemState.ACTIVE)
		{
			Print(string.Format("[BrasilZ][BootScan] Persistence already ACTIVE (state=%1). Scheduling scan.", state), LogLevel.NORMAL);
			BZ_ScheduleOrphanScan();
			return;
		}

		persistence.GetOnStateChanged().Insert(BZ_OnPersistenceStateChanged);
		Print(string.Format("[BrasilZ][BootScan] Persistence not ACTIVE yet (state=%1). Hooked state-changed event.", state), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_OnPersistenceStateChanged(EPersistenceSystemState oldState, EPersistenceSystemState newState)
	{
		Print(string.Format("[BrasilZ][BootScan] Persistence state change %1 -> %2.", oldState, newState), LogLevel.NORMAL);

		if (newState != EPersistenceSystemState.ACTIVE)
			return;

		SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetScriptedInstance();
		if (persistence)
			persistence.GetOnStateChanged().Remove(BZ_OnPersistenceStateChanged);

		BZ_ScheduleOrphanScan();
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_ScheduleOrphanScan()
	{
		if (m_bBzOrphanScanDone)
			return;

		GetGame().GetCallqueue().CallLater(BZ_RunOrphanScan, BZ_ORPHAN_GRACE_MS, false);
		Print("[BrasilZ][BootScan] Orphan scan scheduled in 30s after persistence ACTIVE.", LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_RunOrphanScan()
	{
		if (m_bBzOrphanScanDone)
		{
			Print("[BrasilZ][BootScan] Scan already done — skipping re-entry", LogLevel.NORMAL);
			return;
		}
		int scanStartTick = System.GetTickCount();
		Print("[BrasilZ][BootScan] Starting orphan scan (this gates player connects until done)", LogLevel.NORMAL);
		m_bBzOrphanScanDone = true;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
		{
			Print("[BrasilZ][BootScan] World null — abort scan", LogLevel.WARNING);
			return;
		}

		m_aBzOrphanScanResults = new array<IEntity>();
		m_aBzMissionAiResults = new array<IEntity>();
		world.QueryEntitiesBySphere(vector.Zero, BZ_ORPHAN_SCAN_RADIUS, BZ_QueryCollectOrphanCandidate, null, EQueryEntitiesFlags.DYNAMIC);

		PlayerManager pm = GetGame().GetPlayerManager();
		int deleted = 0;
		int wipedMissionAi = 0;

		foreach (IEntity entity : m_aBzOrphanScanResults)
		{
			if (!entity || entity.IsDeleted())
				continue;

			ChimeraCharacter character = ChimeraCharacter.Cast(entity);
			if (!character)
				continue;

			// Owned by a connected player → leave alone.
			if (pm && pm.GetPlayerIdFromControlledEntity(entity) > 0)
				continue;

			// Skip dead/INCAPACITATED corpses — those are PvP loot and MUST stay visible
			// across restarts so a kill 5 minutes before reboot doesn't get wiped on boot.
			// Corpses accumulate forever; admin can clean manually if needed.
			SCR_DamageManagerComponent dmg = SCR_DamageManagerComponent.GetDamageManager(character);
			if (dmg && dmg.IsDestroyed())
				continue;

			CharacterControllerComponent cc = CharacterControllerComponent.Cast(character.FindComponent(CharacterControllerComponent));
			if (cc && cc.GetLifeState() != ECharacterLifeState.ALIVE)
				continue;

			// DELETE alive orphan bodies instead of burying. SessionStorage already holds the
			// player's last legit save (position, inventory, health). Leaving the body alive
			// underground caused autosave to persist the buried Y-1000 over the legit save,
			// making the player respawn at sea floor on reconnect. Deleting the entity removes
			// the bad source of truth; vanilla restores from SessionStorage cleanly.
			vector orphanPos = entity.GetOrigin();
			SCR_EntityHelper.DeleteEntityAndChildren(entity);

			Print(string.Format("[BrasilZ][BootScan] Deleted alive orphan body at %1 (SessionStorage save preserved).", orphanPos), LogLevel.NORMAL);
			deleted++;
		}

		m_aBzOrphanScanResults = null;
		Print(string.Format("[BrasilZ][BootScan] Orphan scan complete - deleted %1 alive orphan bodies. Dead corpses left for loot.", deleted), LogLevel.NORMAL);

		// Mission AI wipe pass — delete leftover bandit chars from interrupted missions.
		foreach (IEntity missionAi : m_aBzMissionAiResults)
		{
			if (!missionAi || missionAi.IsDeleted())
				continue;

			if (pm && pm.GetPlayerIdFromControlledEntity(missionAi) > 0)
				continue;

			SCR_EntityHelper.DeleteEntityAndChildren(missionAi);
			wipedMissionAi++;
		}

		m_aBzMissionAiResults = null;
		int scanElapsedMs = System.GetTickCount() - scanStartTick;
		Print(string.Format("[BrasilZ][BootScan] Mission AI wipe complete - removed %1 leftover mission AI entities (factions: %2).", wipedMissionAi, s_aBzMissionEnemyFactionKeys), LogLevel.NORMAL);
		Print(string.Format("[BrasilZ][BootScan] Total scan time: %1ms. Gated connects will now proceed.", scanElapsedMs), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	// Filter callback. Only collect entities that:
	//   1. Are ChimeraCharacters (filters out vehicles, props, etc).
	//   2. Have a prefab in s_aBzPlayerCharacterPrefabs (filters out zombies / AI squads).
	//
	// Without the prefab check, the orphan sweep was burying BaconZombies infected and
	// DarcMissions AI groups: log shows clusters of 10+ "orphan bodies" at <9368,-686,5991>
	// (zombie horde) and <12884,-897,14608> (mission AI squad).
	protected bool BZ_QueryCollectOrphanCandidate(IEntity entity)
	{
		if (!entity)
			return true;

		if (!ChimeraCharacter.Cast(entity))
			return true;

		auto prefabData = entity.GetPrefabData();
		if (!prefabData)
			return true;

		ResourceName prefab = prefabData.GetPrefabName();
		if (prefab.IsEmpty())
			return true;

		if (s_aBzPlayerCharacterPrefabs.Contains(prefab))
		{
			m_aBzOrphanScanResults.Insert(entity);
			return true;
		}

		FactionAffiliationComponent facComp = FactionAffiliationComponent.Cast(entity.FindComponent(FactionAffiliationComponent));
		if (facComp)
		{
			Faction faction = facComp.GetAffiliatedFaction();
			if (faction && s_aBzMissionEnemyFactionKeys.Contains(faction.GetFactionKey()))
				m_aBzMissionAiResults.Insert(entity);
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_TryStartAutoSave()
	{
		if (m_bBzAutoSaveScheduled)
			return;

		SaveGameManager saveManager = GetGame().GetSaveGameManager();
		if (!saveManager || !saveManager.IsSavingPossible())
		{
			GetGame().GetCallqueue().CallLater(BZ_TryStartAutoSave, BZ_AUTOSAVE_START_RETRY_MS, false);
			return;
		}

		m_bBzAutoSaveEnabled = true;
		m_bBzAutoSaveScheduled = true;
		int intervalMs = BZ_AUTOSAVE_INTERVAL_SEC * 1000;
		GetGame().GetCallqueue().CallLater(BZ_PerformAutoSave, intervalMs, true);
		Print(string.Format("[BrasilZ] Autosave armed every %1s", BZ_AUTOSAVE_INTERVAL_SEC), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_PerformAutoSave()
	{
		if (!m_bBzAutoSaveEnabled)
			return;

		// FIX B: skip autosave if queue busy. Avoids piling onto an in-flight transaction.
		if (s_bBzSaveInProgress)
		{
			Print("[BrasilZ][AutoSave] Skipped — save queue busy", LogLevel.NORMAL);
			return;
		}

		SaveGameManager saveManager = GetGame().GetSaveGameManager();
		bool hasMgr = (saveManager != null);
		bool canSave = false;
		if (hasMgr)
			canSave = saveManager.IsSavingPossible();
		if (!hasMgr || !canSave)
		{
			Print(string.Format("[BrasilZ][AutoSave] Skipped — saving impossible (mgr=%1, possible=%2)", hasMgr, canSave), LogLevel.NORMAL);
			return;
		}

		s_bBzSaveInProgress = true;
		int startTick = System.GetTickCount();
		bool ok = BZ_OverwriteLatestSave(saveManager);
		int elapsedMs = System.GetTickCount() - startTick;
		s_bBzSaveInProgress = false;
		Print(string.Format("[BrasilZ][AutoSave] Tick ok=%1 elapsed=%2ms", ok, elapsedMs), LogLevel.NORMAL);

		if (!ok)
			Print("[BrasilZ][AutoSave] No save to overwrite", LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	static bool BZ_OverwriteLatestSave(SaveGameManager saveManager)
	{
		if (!saveManager || !saveManager.IsSavingPossible())
			return false;

		SaveGame active = saveManager.GetActiveSave();
		if (active)
			return saveManager.RequestSavePointOverwrite(active, ESaveGameRequestFlags.BLOCKING);

		array<SaveGame> saves = {};
		int count = saveManager.GetSaves(saves);
		if (count > 0)
		{
			SaveGame latest = saves[count - 1];
			return saveManager.RequestSavePointOverwrite(latest, ESaveGameRequestFlags.BLOCKING);
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	// FIX B: deferred flush also respects save queue lock.
	protected void BZ_FlushSaveToDisk()
	{
		SaveGameManager saveManager = GetGame().GetSaveGameManager();
		if (!saveManager)
		{
			Print("[BrasilZ][FlushSave] SaveGameManager null — abort", LogLevel.WARNING);
			return;
		}

		if (!saveManager.IsSavingPossible())
		{
			m_iBzFlushRetryCount++;
			Print(string.Format("[BrasilZ][FlushSave] Saving impossible, retry %1/10", m_iBzFlushRetryCount), LogLevel.NORMAL);
			if (m_iBzFlushRetryCount < 10)
				GetGame().GetCallqueue().CallLater(BZ_FlushSaveToDisk, 1000, false);
			return;
		}

		if (s_bBzSaveInProgress)
		{
			Print("[BrasilZ][FlushSave] Save queue busy, retry in 1s", LogLevel.NORMAL);
			GetGame().GetCallqueue().CallLater(BZ_FlushSaveToDisk, 1000, false);
			return;
		}

		m_iBzFlushRetryCount = 0;
		s_bBzSaveInProgress = true;
		int startTick = System.GetTickCount();
		bool ok = BZ_OverwriteLatestSave(saveManager);
		int elapsedMs = System.GetTickCount() - startTick;
		s_bBzSaveInProgress = false;
		Print(string.Format("[BrasilZ][FlushSave] Flush ok=%1 elapsed=%2ms", ok, elapsedMs), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------
	protected void BZ_DecoupleDeadBody(IEntity body, int playerId)
	{
		if (!body)
			return;

		SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByEntityWorld(body);
		if (!persistence || persistence.GetState() != EPersistenceSystemState.ACTIVE)
			return;

		persistence.StopTracking(body);
		persistence.StartTracking(body);
		persistence.Save(body, ESaveGameType.AUTO);

		BZ_TrackCorpseForCleanup(body);
		Print(string.Format("[BrasilZ] Dead body decoupled for player %1 (30min lootable timer started)", playerId), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_DecoupleUnconsciousBody(IEntity body, int playerId)
	{
		if (!body)
			return;

		SCR_CharacterDamageManagerComponent charDamageMgr = SCR_CharacterDamageManagerComponent.Cast(body.FindComponent(SCR_CharacterDamageManagerComponent));
		if (charDamageMgr)
		{
			Instigator instigator = Instigator.CreateInstigator(null);
			charDamageMgr.Kill(instigator);
		}

		SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByEntityWorld(body);
		if (!persistence || persistence.GetState() != EPersistenceSystemState.ACTIVE)
			return;

		persistence.StopTracking(body);
		persistence.StartTracking(body);
		persistence.Save(body, ESaveGameType.AUTO);

		BZ_TrackCorpseForCleanup(body);
		Print(string.Format("[BrasilZ] Unconscious body decoupled for player %1 (30min lootable timer started)", playerId), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	// Track a freshly-made corpse so the 60s cleanup tick can delete it after 30min lifetime.
	// Note: tracker is in memory — corpses from before a restart won't get cleaned automatically
	// (their entries are lost), they linger until an admin removes them or another mechanism
	// handles them. In-session corpses get the proper 30min lifetime.
	void BZ_TrackCorpseForCleanup(IEntity corpse)
	{
		if (!corpse || BZ_CORPSE_LIFETIME_SEC <= 0)
			return;

		if (m_aBzTrackedCorpses.Contains(corpse))
			return;

		m_aBzTrackedCorpses.Insert(corpse);
		m_aBzCorpseDeathTimes.Insert(System.GetTickCount());
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_TickCorpseCleanup()
	{
		if (BZ_CORPSE_LIFETIME_SEC <= 0 || m_aBzTrackedCorpses.IsEmpty())
			return;

		int currentTime = System.GetTickCount();
		float lifetimeMs = BZ_CORPSE_LIFETIME_SEC * 1000;

		for (int i = m_aBzTrackedCorpses.Count() - 1; i >= 0; i--)
		{
			IEntity corpse = m_aBzTrackedCorpses[i];
			if (!corpse)
			{
				m_aBzTrackedCorpses.Remove(i);
				m_aBzCorpseDeathTimes.Remove(i);
				continue;
			}

			float ageMs = currentTime - m_aBzCorpseDeathTimes[i];
			if (ageMs >= lifetimeMs)
			{
				Print(string.Format("[BrasilZ] Cleaning up corpse at %1 (age: %2min)", corpse.GetOrigin(), ageMs / 60000), LogLevel.NORMAL);
				m_aBzTrackedCorpses.Remove(i);
				m_aBzCorpseDeathTimes.Remove(i);
				SCR_EntityHelper.DeleteEntityAndChildren(corpse);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	// FIX B: public entry point. Enqueues the save instead of running it directly.
	// Engine's SaveGame transaction is singleton — concurrent calls cause
	// "Another transaction in progress" warnings and lost player progress.
	protected void BZ_SavePlayerAndFlushToDisk(int playerId)
	{
		Print(string.Format("[BrasilZ][SaveQueue] Enqueue save for player %1 (queue size=%2, inProgress=%3)", playerId, s_aBzPendingSaves.Count(), s_bBzSaveInProgress), LogLevel.NORMAL);
		s_iBzSaveTotalQueued++;

		if (s_bBzSaveInProgress)
		{
			if (s_aBzPendingSaves.Find(playerId) == -1)
			{
				s_aBzPendingSaves.Insert(playerId);
				Print(string.Format("[BrasilZ][SaveQueue] Player %1 queued (position=%2)", playerId, s_aBzPendingSaves.Count()), LogLevel.NORMAL);
			}
			else
			{
				Print(string.Format("[BrasilZ][SaveQueue] Player %1 already pending — skipped duplicate enqueue", playerId), LogLevel.NORMAL);
				s_iBzSaveTotalRejected++;
			}
			return;
		}

		BZ_DoSaveNow(playerId);
	}

	//------------------------------------------------------------------------------------------------
	// FIX B: actual save execution. Holds the lock for the duration. Schedules next queued
	// player via callqueue with a small delay so the engine transaction fully completes.
	protected void BZ_DoSaveNow(int playerId)
	{
		s_bBzSaveInProgress = true;
		int startTick = System.GetTickCount();
		Print(string.Format("[BrasilZ][SaveQueue] BEGIN save player %1 (queued=%2 done=%3 rejected=%4)", playerId, s_iBzSaveTotalQueued, s_iBzSaveTotalCompleted, s_iBzSaveTotalRejected), LogLevel.NORMAL);

		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
		{
			Print("[BrasilZ][SaveQueue] PlayerManager null — abort save", LogLevel.WARNING);
			BZ_FinishSaveAndProcessQueue();
			return;
		}

		SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByEntityWorld(this);
		int persistState = -1;
		if (persistence)
			persistState = persistence.GetState();
		if (!persistence || persistState != EPersistenceSystemState.ACTIVE)
		{
			Print(string.Format("[BrasilZ][SaveQueue] Persistence not ACTIVE (state=%1) — abort player %2 save", persistState, playerId), LogLevel.WARNING);
			BZ_FinishSaveAndProcessQueue();
			return;
		}

		SCR_PlayerController controller = SCR_PlayerController.Cast(pm.GetPlayerController(playerId));
		if (controller)
		{
			persistence.Save(controller);
			Print(string.Format("[BrasilZ][SaveQueue] Saved controller for player %1", playerId), LogLevel.NORMAL);
		}

		IEntity character = pm.GetPlayerControlledEntity(playerId);
		if (character)
		{
			persistence.Save(character);
			Print(string.Format("[BrasilZ][SaveQueue] Saved character entity for player %1 at %2", playerId, character.GetOrigin()), LogLevel.NORMAL);
		}
		else
		{
			Print(string.Format("[BrasilZ][SaveQueue] Player %1 has no controlled entity (dead/menu)", playerId), LogLevel.NORMAL);
		}

		SaveGameManager saveManager = GetGame().GetSaveGameManager();
		if (!saveManager)
		{
			Print("[BrasilZ][SaveQueue] SaveGameManager null — abort", LogLevel.WARNING);
			BZ_FinishSaveAndProcessQueue();
			return;
		}

		if (!saveManager.IsSavingPossible())
		{
			Print(string.Format("[BrasilZ][SaveQueue] Saving not possible right now — defer flush 1s for player %1", playerId), LogLevel.WARNING);
			GetGame().GetCallqueue().CallLater(BZ_FlushSaveToDisk, 1000, false);
			BZ_FinishSaveAndProcessQueue();
			return;
		}

		bool ok = BZ_OverwriteLatestSave(saveManager);
		int elapsedMs = System.GetTickCount() - startTick;
		s_iBzSaveTotalCompleted++;
		Print(string.Format("[BrasilZ][SaveQueue] END save player %1 — ok=%2 elapsed=%3ms", playerId, ok, elapsedMs), LogLevel.NORMAL);

		BZ_FinishSaveAndProcessQueue();
	}

	//------------------------------------------------------------------------------------------------
	// FIX B: release lock and process next queued save.
	protected void BZ_FinishSaveAndProcessQueue()
	{
		s_bBzSaveInProgress = false;

		if (s_aBzPendingSaves.IsEmpty())
		{
			Print("[BrasilZ][SaveQueue] Queue empty — idle", LogLevel.NORMAL);
			return;
		}

		int nextPlayerId = s_aBzPendingSaves[0];
		s_aBzPendingSaves.RemoveOrdered(0);
		Print(string.Format("[BrasilZ][SaveQueue] Processing next queued player %1 (remaining=%2)", nextPlayerId, s_aBzPendingSaves.Count()), LogLevel.NORMAL);
		GetGame().GetCallqueue().CallLater(BZ_DoSaveNow, BZ_SAVE_QUEUE_DELAY_MS, false, nextPlayerId);
	}

	//------------------------------------------------------------------------------------------------
	// FIX C: gate connect during boot scan. Scan deleting orphan entities at positions where
	// players were about to restore caused entity-lost during spawn (engine destroyed the new
	// char that referenced the just-deleted GUID). Deferring connect until scan finishes
	// closes that race window. Retry map declared in member section above.
	override void OnPlayerConnected(int playerId)
	{
		Print(string.Format("[BrasilZ][Connect] Player %1 connecting (scanDone=%2, proxy=%3)", playerId, m_bBzOrphanScanDone, BZ_IsProxy()), LogLevel.NORMAL);

		if (!m_bBzOrphanScanDone && !BZ_IsProxy())
		{
			int retries = 0;
			if (m_mBzConnectGateRetries.Contains(playerId))
				retries = m_mBzConnectGateRetries.Get(playerId);

			if (retries < BZ_CONNECT_GATE_MAX_RETRIES)
			{
				m_mBzConnectGateRetries.Set(playerId, retries + 1);
				Print(string.Format("[BrasilZ][Connect] Player %1 deferred — orphan scan in progress (retry %2/%3)", playerId, retries + 1, BZ_CONNECT_GATE_MAX_RETRIES), LogLevel.WARNING);
				GetGame().GetCallqueue().CallLater(BZ_RetryDeferredConnect, 500, false, playerId);
				return;
			}

			Print(string.Format("[BrasilZ][Connect] Player %1 deferral cap reached (%2 retries) — letting through anyway", playerId, retries), LogLevel.WARNING);
		}

		m_mBzConnectGateRetries.Remove(playerId);
		Print(string.Format("[BrasilZ][Connect] Player %1 forwarding to vanilla OnPlayerConnected", playerId), LogLevel.NORMAL);
		super.OnPlayerConnected(playerId);
	}

	//------------------------------------------------------------------------------------------------
	// FIX C: re-entry point for deferred connects. Must be a separate method because CallLater
	// arg-binding can't directly invoke an override.
	protected void BZ_RetryDeferredConnect(int playerId)
	{
		OnPlayerConnected(playerId);
	}

	//------------------------------------------------------------------------------------------------
	override void OnPlayerDisconnected(int playerId, KickCauseCode cause = KickCauseCode.NONE, int timeout = -1)
	{
		Print(string.Format("[BrasilZ][Disconnect] Player %1 disconnecting (cause=%2 timeout=%3, proxy=%4)", playerId, cause, timeout, BZ_IsProxy()), LogLevel.NORMAL);

		if (BZ_IsProxy())
		{
			super.OnPlayerDisconnected(playerId, cause, timeout);
			return;
		}

		PlayerManager pm = GetGame().GetPlayerManager();
		IEntity playerEntity = null;
		if (pm)
			playerEntity = pm.GetPlayerControlledEntity(playerId);

		bool preserveBody = false;

		if (playerEntity)
		{
			// Anti-ALT+F4 death flag
			if (BZ_Utils.IsCharacterDying(playerEntity))
			{
				string dyingUid = BZ_Utils.GetPlayerUID(playerId);
				if (!dyingUid.IsEmpty())
				{
					BZ_PlayerDeathRegistry registry = BZ_PlayerDeathRegistry.GetInstance();
					if (registry)
					{
						registry.FlagDead(dyingUid);
						registry.TrackDeadBody(playerId, playerEntity);
						Print(string.Format("[BrasilZ] Anti-ALT+F4: flagged UID %1 as dead on disconnect", dyingUid), LogLevel.WARNING);
					}
				}
			}

			SCR_CharacterControllerComponent charController = SCR_CharacterControllerComponent.Cast(playerEntity.FindComponent(SCR_CharacterControllerComponent));
			if (charController)
			{
				ECharacterLifeState lifeState = charController.GetLifeState();
				if (lifeState == ECharacterLifeState.DEAD)
				{
					BZ_DecoupleDeadBody(playerEntity, playerId);
					preserveBody = true;
				}
				else if (lifeState == ECharacterLifeState.INCAPACITATED)
				{
					BZ_DecoupleUnconsciousBody(playerEntity, playerId);
					preserveBody = true;
				}
			}
		}

		if (preserveBody)
		{
			BZ_SavePlayerAndFlushToDisk(playerId);
			m_OnPlayerDisconnected.Invoke(playerId, cause, timeout);
			foreach (SCR_BaseGameModeComponent comp : m_aAdditionalGamemodeComponents)
				comp.OnPlayerDisconnected(playerId, cause, timeout);
			m_OnPostCompPlayerDisconnected.Invoke(playerId, cause, timeout);
			if (m_pRespawnSystemComponent)
				m_pRespawnSystemComponent.OnPlayerDisconnected_S(playerId, cause, timeout);
			return;
		}

		BZ_SavePlayerAndFlushToDisk(playerId);
		super.OnPlayerDisconnected(playerId, cause, timeout);
	}

	//------------------------------------------------------------------------------------------------
	override void OnPlayerKilled(int playerId, IEntity playerEntity, IEntity killerEntity, notnull Instigator killer)
	{
		vector deathPos = vector.Zero;
		float deathHP = -1;
		string killerName = "unknown";
		string instigatorTypeName = "unknown";
		string killerCategory = "UNKNOWN";  // ENVIRONMENT / PVE / PVP / SUICIDE / OTHER

		if (playerEntity)
		{
			deathPos = playerEntity.GetOrigin();
			SCR_DamageManagerComponent dmgMgr = SCR_DamageManagerComponent.GetDamageManager(playerEntity);
			if (dmgMgr)
				deathHP = dmgMgr.GetHealth();
		}

		// Categorize killer source for quick log filtering.
		if (!killerEntity)
		{
			killerName = "no_killer_entity";
			killerCategory = "ENVIRONMENT_OR_UNKNOWN (fall/drown/starvation/no-instigator)";
		}
		else if (killerEntity == playerEntity)
		{
			killerName = "self";
			killerCategory = "SUICIDE";
		}
		else
		{
			string killerPrefab = "(no-prefab)";
			if (killerEntity.GetPrefabData())
				killerPrefab = killerEntity.GetPrefabData().GetPrefabName();
			killerName = string.Format("entity@%1 prefab=%2", killerEntity.GetOrigin(), killerPrefab);

			// Player-controlled killer? Check via player manager.
			PlayerManager pmCheck = GetGame().GetPlayerManager();
			if (pmCheck && pmCheck.GetPlayerIdFromControlledEntity(killerEntity) > 0)
				killerCategory = "PVP";
			else if (killerPrefab.IndexOf("Zombie") >= 0 || killerPrefab.IndexOf("Infected") >= 0 || killerPrefab.IndexOf("BaconZ") >= 0)
				killerCategory = "PVE_ZOMBIE";
			else if (killerPrefab.IndexOf("PLASTICBANDIT") >= 0 || killerPrefab.IndexOf("Bandit") >= 0)
				killerCategory = "PVE_BANDIT";
			else
				killerCategory = "PVE_OTHER";
		}

		// Instigator class name reveals damage source (gunshot, grenade, fall, drown, etc).
		if (killer)
			instigatorTypeName = killer.Type().ToString();

		Print(string.Format("[BrasilZ][Killed] Player %1 killed | pos=%2 | HP=%3 | category=%4 | killer=%5 | instigator=%6 (proxy=%7)", playerId, deathPos, deathHP, killerCategory, killerName, instigatorTypeName, BZ_IsProxy()), LogLevel.NORMAL);

		super.OnPlayerKilled(playerId, playerEntity, killerEntity, killer);

		if (BZ_IsProxy() || !playerEntity)
		{
			Print(string.Format("[BrasilZ][Killed] Skipping post-kill handling (proxy=%1, hasEntity=%2)", BZ_IsProxy(), playerEntity != null), LogLevel.NORMAL);
			return;
		}

		string deadUid = BZ_Utils.GetPlayerUID(playerId);
		if (!deadUid.IsEmpty())
		{
			BZ_PlayerDeathRegistry registry = BZ_PlayerDeathRegistry.GetInstance();
			if (registry)
			{
				registry.FlagDead(deadUid);
				registry.TrackDeadBody(playerId, playerEntity);
				Print(string.Format("[BrasilZ][Killed] Flagged UID %1 dead in registry", deadUid), LogLevel.NORMAL);
			}
		}

		BZ_DecoupleDeadBody(playerEntity, playerId);

		PlayerManager pm = GetGame().GetPlayerManager();
		if (pm)
		{
			SCR_PersistenceSystem deathPersistence = SCR_PersistenceSystem.GetByEntityWorld(playerEntity);
			if (deathPersistence && deathPersistence.GetState() == EPersistenceSystemState.ACTIVE)
			{
				SCR_PlayerController controller = SCR_PlayerController.Cast(pm.GetPlayerController(playerId));
				if (controller)
				{
					deathPersistence.Save(controller);
					Print(string.Format("[BrasilZ][Killed] Persisted death-state controller for player %1", playerId), LogLevel.NORMAL);
				}
			}
		}

		// FIX B: route through save queue instead of direct save. Two reasons:
		// 1. Other disconnect/autosave may be in flight — direct call races.
		// 2. Need the dead-flag in registry to persist before SaveGameManager flush.
		BZ_SavePlayerAndFlushToDisk(playerId);
	}
}
