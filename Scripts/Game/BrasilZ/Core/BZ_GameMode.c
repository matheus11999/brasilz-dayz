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
	protected static const int BZ_ORPHAN_GRACE_MS = 30000;
	protected static const float BZ_ORPHAN_SCAN_RADIUS = 20000.0;
	protected static const float BZ_ORPHAN_UNDERGROUND_OFFSET = 1000.0;
	protected static const float BZ_CORPSE_LIFETIME_SEC = 1800.0; // 30 minutes
	protected static const int BZ_CORPSE_CLEANUP_INTERVAL_MS = 60000;

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

	protected ref array<IEntity> m_aBzOrphanScanResults;
	protected ref array<IEntity> m_aBzTrackedCorpses = new array<IEntity>();
	protected ref array<int> m_aBzCorpseDeathTimes = new array<int>();

	protected bool m_bBzAutoSaveEnabled;
	protected bool m_bBzAutoSaveScheduled;
	protected int m_iBzFlushRetryCount;
	protected int m_iBzOrphanArmRetries;
	protected bool m_bBzOrphanScanDone;
	protected bool m_bBzHooksInitialized;

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
			return;
		m_bBzOrphanScanDone = true;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;

		m_aBzOrphanScanResults = new array<IEntity>();
		world.QueryEntitiesBySphere(vector.Zero, BZ_ORPHAN_SCAN_RADIUS, BZ_QueryCollectOrphanCandidate, null, EQueryEntitiesFlags.DYNAMIC);

		PlayerManager pm = GetGame().GetPlayerManager();
		int buried = 0;

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

			// Already buried (previous scan or hide-on-disconnect).
			if (entity.GetOrigin()[1] <= -1.0)
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

			BaseGameEntity bgEntity = BaseGameEntity.Cast(entity);
			if (!bgEntity)
				continue;

			// Disable damage so the body doesn't die underground (engine has void/OOB death
			// zones above Y=-1000). Without this the body dies, save persists dead, reconnect
			// rejects, player loses progress.
			SCR_CharacterDamageManagerComponent buriedDmg = SCR_CharacterDamageManagerComponent.Cast(character.FindComponent(SCR_CharacterDamageManagerComponent));
			if (buriedDmg)
				buriedDmg.EnableDamageHandling(false);

			vector transform[4];
			bgEntity.GetWorldTransform(transform);
			vector pos = transform[3];
			pos[1] = pos[1] - BZ_ORPHAN_UNDERGROUND_OFFSET;
			transform[3] = pos;
			bgEntity.Teleport(transform);

			Print(string.Format("[BrasilZ][BootScan] Buried orphan body at %1 (damage disabled).", pos), LogLevel.NORMAL);
			buried++;
		}

		m_aBzOrphanScanResults = null;
		Print(string.Format("[BrasilZ][BootScan] Orphan scan complete - buried %1 alive orphan bodies. Dead corpses left for loot.", buried), LogLevel.NORMAL);
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

		if (!s_aBzPlayerCharacterPrefabs.Contains(prefab))
			return true;

		m_aBzOrphanScanResults.Insert(entity);
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

		SaveGameManager saveManager = GetGame().GetSaveGameManager();
		if (!saveManager || !saveManager.IsSavingPossible())
			return;

		if (!BZ_OverwriteLatestSave(saveManager))
			Print("[BrasilZ] PerformAutoSave: no save to overwrite", LogLevel.WARNING);
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
	protected void BZ_FlushSaveToDisk()
	{
		SaveGameManager saveManager = GetGame().GetSaveGameManager();
		if (!saveManager)
			return;

		if (!saveManager.IsSavingPossible())
		{
			m_iBzFlushRetryCount++;
			if (m_iBzFlushRetryCount < 10)
				GetGame().GetCallqueue().CallLater(BZ_FlushSaveToDisk, 1000, false);
			return;
		}

		m_iBzFlushRetryCount = 0;
		BZ_OverwriteLatestSave(saveManager);
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
	protected void BZ_SavePlayerAndFlushToDisk(int playerId)
	{
		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return;

		SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByEntityWorld(this);
		if (!persistence || persistence.GetState() != EPersistenceSystemState.ACTIVE)
			return;

		SCR_PlayerController controller = SCR_PlayerController.Cast(pm.GetPlayerController(playerId));
		if (controller)
			persistence.Save(controller);

		IEntity character = pm.GetPlayerControlledEntity(playerId);
		if (character)
			persistence.Save(character);

		SaveGameManager saveManager = GetGame().GetSaveGameManager();
		if (!saveManager)
			return;

		if (!saveManager.IsSavingPossible())
		{
			GetGame().GetCallqueue().CallLater(BZ_FlushSaveToDisk, 1000, false);
			return;
		}

		BZ_OverwriteLatestSave(saveManager);
	}

	//------------------------------------------------------------------------------------------------
	override void OnPlayerDisconnected(int playerId, KickCauseCode cause = KickCauseCode.NONE, int timeout = -1)
	{
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
		super.OnPlayerKilled(playerId, playerEntity, killerEntity, killer);

		if (BZ_IsProxy() || !playerEntity)
			return;

		string deadUid = BZ_Utils.GetPlayerUID(playerId);
		if (!deadUid.IsEmpty())
		{
			BZ_PlayerDeathRegistry registry = BZ_PlayerDeathRegistry.GetInstance();
			if (registry)
			{
				registry.FlagDead(deadUid);
				registry.TrackDeadBody(playerId, playerEntity);
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
					deathPersistence.Save(controller);
			}
		}

		SaveGameManager saveManager = GetGame().GetSaveGameManager();
		if (saveManager && saveManager.IsSavingPossible())
			BZ_OverwriteLatestSave(saveManager);
	}
}
