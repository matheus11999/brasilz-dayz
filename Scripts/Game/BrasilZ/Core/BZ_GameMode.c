[EntityEditorProps(category: "BrasilZ/Core", description: "BrasilZ survival game mode with reconnect persistence")]
class BZ_GameModeClass : SCR_BaseGameModeClass
{
}

class BZ_GameMode : SCR_BaseGameMode
{
	protected static BZ_GameMode s_Instance;

	[Attribute("1200", desc: "Corpse cleanup time in seconds (0 to disable). Bodies removed after this delay to limit entity count.")]
	protected float m_fCorpseLifetimeSec;

	[Attribute("60", desc: "Auto-save interval in seconds (0 to disable). Overwrites the latest save point so disconnected player data is preserved.")]
	protected float m_fAutoSaveInterval;

	protected static const int CORPSE_CLEANUP_INTERVAL_MS = 60000;
	protected static const int AUTOSAVE_START_DELAY_MS = 5000;
	protected static const int AUTOSAVE_START_RETRY_MS = 3000;
	protected static const int ORPHAN_GRACE_MS = 30000;
	protected static const float ORPHAN_SCAN_RADIUS = 20000.0;
	protected static const float ORPHAN_UNDERGROUND_OFFSET = 1000.0;

	protected bool m_bOrphanScanDone;

	protected ref array<IEntity> m_aTrackedCorpses = new array<IEntity>();
	protected ref array<int> m_aCorpseDeathTimes = new array<int>();

	protected int m_iFlushRetryCount;
	protected bool m_bAutoSaveEnabled;
	protected bool m_bAutoSaveScheduled;

	//------------------------------------------------------------------------------------------------
	static BZ_GameMode GetInstance()
	{
		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	bool IsProxy()
	{
		RplComponent rpl = RplComponent.Cast(FindComponent(RplComponent));
		return rpl && rpl.IsProxy();
	}

	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);
		s_Instance = this;

		if (IsProxy())
			return;

		if (m_fCorpseLifetimeSec > 0)
			GetGame().GetCallqueue().CallLater(TickCorpseCleanup, CORPSE_CLEANUP_INTERVAL_MS, true);

		if (m_fAutoSaveInterval > 0)
			GetGame().GetCallqueue().CallLater(TryStartAutoSave, AUTOSAVE_START_DELAY_MS, false);

		// Hook persistence state so we can run the orphan-body bury scan once it's ACTIVE.
		SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetScriptedInstance();
		if (persistence)
		{
			if (persistence.GetState() >= EPersistenceSystemState.ACTIVE)
				GetGame().GetCallqueue().CallLater(ScheduleOrphanScan, 0, false);
			else
				persistence.GetOnStateChanged().Insert(OnPersistenceStateChangedForOrphanScan);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void OnPersistenceStateChangedForOrphanScan(EPersistenceSystemState oldState, EPersistenceSystemState newState)
	{
		if (newState != EPersistenceSystemState.ACTIVE)
			return;

		SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetScriptedInstance();
		if (persistence)
			persistence.GetOnStateChanged().Remove(OnPersistenceStateChangedForOrphanScan);

		ScheduleOrphanScan();
	}

	//------------------------------------------------------------------------------------------------
	protected void ScheduleOrphanScan()
	{
		if (m_bOrphanScanDone)
			return;

		// 30s grace so legitimate reconnects bind their controller before we bury anything.
		GetGame().GetCallqueue().CallLater(RunOrphanScan, ORPHAN_GRACE_MS, false);
		Print("[BrasilZ][BootScan] Orphan scan scheduled in 30s after persistence ACTIVE.", LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	protected ref array<IEntity> m_aOrphanScanResults;

	protected void RunOrphanScan()
	{
		if (m_bOrphanScanDone)
			return;
		m_bOrphanScanDone = true;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;

		m_aOrphanScanResults = new array<IEntity>();
		world.QueryEntitiesBySphere(vector.Zero, ORPHAN_SCAN_RADIUS, QueryCollectOrphanCandidate, null, EQueryEntitiesFlags.DYNAMIC);

		PlayerManager pm = GetGame().GetPlayerManager();
		int buried = 0;

		foreach (IEntity entity : m_aOrphanScanResults)
		{
			if (!entity || entity.IsDeleted())
				continue;

			ChimeraCharacter character = ChimeraCharacter.Cast(entity);
			if (!character)
				continue;

			// Already owned by a connected player.
			if (pm && pm.GetPlayerIdFromControlledEntity(entity) > 0)
				continue;

			// Skip dead/INCAP (corpse handled elsewhere).
			SCR_DamageManagerComponent dmg = SCR_DamageManagerComponent.GetDamageManager(character);
			if (dmg && dmg.IsDestroyed())
				continue;

			CharacterControllerComponent cc = CharacterControllerComponent.Cast(character.FindComponent(CharacterControllerComponent));
			if (cc && cc.GetLifeState() != ECharacterLifeState.ALIVE)
				continue;

			// Already buried.
			if (entity.GetOrigin()[1] <= -1.0)
				continue;

			BaseGameEntity bgEntity = BaseGameEntity.Cast(entity);
			if (!bgEntity)
				continue;

			vector transform[4];
			bgEntity.GetWorldTransform(transform);
			vector pos = transform[3];
			pos[1] = pos[1] - ORPHAN_UNDERGROUND_OFFSET;
			transform[3] = pos;
			bgEntity.Teleport(transform);

			Print(string.Format("[BrasilZ][BootScan] Buried orphan body at %1.", pos), LogLevel.NORMAL);
			buried++;
		}

		m_aOrphanScanResults = null;
		Print(string.Format("[BrasilZ][BootScan] Orphan scan complete - buried %1 stale alive bodies.", buried), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	protected bool QueryCollectOrphanCandidate(IEntity entity)
	{
		if (entity && ChimeraCharacter.Cast(entity))
			m_aOrphanScanResults.Insert(entity);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	// Wait for the save system to be ready before arming the periodic autosave.
	protected void TryStartAutoSave()
	{
		if (m_bAutoSaveScheduled || m_fAutoSaveInterval <= 0)
			return;

		SaveGameManager saveManager = GetGame().GetSaveGameManager();
		if (!saveManager || !saveManager.IsSavingPossible())
		{
			GetGame().GetCallqueue().CallLater(TryStartAutoSave, AUTOSAVE_START_RETRY_MS, false);
			return;
		}

		StartAutoSave();
	}

	//------------------------------------------------------------------------------------------------
	protected void StartAutoSave()
	{
		if (m_fAutoSaveInterval <= 0)
			return;

		m_bAutoSaveEnabled = true;
		m_bAutoSaveScheduled = true;
		int intervalMs = m_fAutoSaveInterval * 1000;
		GetGame().GetCallqueue().CallLater(PerformAutoSave, intervalMs, true);
		Print(string.Format("[BrasilZ] Autosave armed every %1s", m_fAutoSaveInterval), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	protected void PerformAutoSave()
	{
		if (!m_bAutoSaveEnabled)
			return;

		SaveGameManager saveManager = GetGame().GetSaveGameManager();
		if (!saveManager || !saveManager.IsSavingPossible())
			return;

		if (!OverwriteLatestSave(saveManager))
			Print("[BrasilZ] PerformAutoSave: no save to overwrite", LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	void StopAutoSave()
	{
		m_bAutoSaveEnabled = false;
		m_bAutoSaveScheduled = false;
		GetGame().GetCallqueue().Remove(PerformAutoSave);
	}

	//------------------------------------------------------------------------------------------------
	// Force a single save to disk with SHUTDOWN flag — call from admin/restart hooks before server close.
	void ForceSaveNow()
	{
		SaveGameManager saveManager = GetGame().GetSaveGameManager();
		if (!saveManager)
			return;

		if (!saveManager.IsSavingPossible())
		{
			GetGame().GetCallqueue().CallLater(ForceSaveNow, 3000, false);
			return;
		}

		SaveGame saveToOverwrite = saveManager.GetActiveSave();
		if (!saveToOverwrite)
		{
			array<SaveGame> saves = {};
			int count = saveManager.GetSaves(saves);
			if (count > 0)
				saveToOverwrite = saves[count - 1];
		}

		if (!saveToOverwrite)
		{
			Print("[BrasilZ] ForceSaveNow: no save found to overwrite", LogLevel.WARNING);
			return;
		}

		bool queued = saveManager.RequestSavePointOverwrite(saveToOverwrite, ESaveGameRequestFlags.SHUTDOWN);
		Print(string.Format("[BrasilZ] ForceSaveNow: overwriting save with SHUTDOWN flag, queued=%1", queued), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	// Overwrite the latest save point with BLOCKING flag so disconnect/death state is flushed to disk.
	// Creating a new save would wipe disconnected player data.
	static bool OverwriteLatestSave(SaveGameManager saveManager)
	{
		if (!saveManager || !saveManager.IsSavingPossible())
		{
			Print("[BrasilZ] OverwriteLatestSave: saving not possible", LogLevel.WARNING);
			return false;
		}

		SaveGame active = saveManager.GetActiveSave();
		if (active)
		{
			bool queued = saveManager.RequestSavePointOverwrite(active, ESaveGameRequestFlags.BLOCKING);
			return queued;
		}

		array<SaveGame> saves = {};
		int count = saveManager.GetSaves(saves);
		if (count > 0)
		{
			SaveGame latest = saves[count - 1];
			bool queued = saveManager.RequestSavePointOverwrite(latest, ESaveGameRequestFlags.BLOCKING);
			return queued;
		}

		Print("[BrasilZ] OverwriteLatestSave: no saves found", LogLevel.WARNING);
		return false;
	}

	//------------------------------------------------------------------------------------------------
	override void OnPlayerDisconnected(int playerId, KickCauseCode cause = KickCauseCode.NONE, int timeout = -1)
	{
		if (IsProxy())
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
			// Anti-ALT+F4: if disconnecting while dying/dead, persist a death flag keyed by UID
			// so reconnect can reject stale alive saves.
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
					DecoupleDeadBody(playerEntity, playerId);
					preserveBody = true;
				}
				else if (lifeState == ECharacterLifeState.INCAPACITATED)
				{
					DecoupleUnconsciousBody(playerEntity, playerId);
					preserveBody = true;
				}
			}
		}

		if (preserveBody)
		{
			SavePlayerAndFlushToDisk(playerId);

			// Replicate SCR_BaseGameMode.OnPlayerDisconnected behaviour without deleting the body
			m_OnPlayerDisconnected.Invoke(playerId, cause, timeout);
			foreach (SCR_BaseGameModeComponent comp : m_aAdditionalGamemodeComponents)
				comp.OnPlayerDisconnected(playerId, cause, timeout);
			m_OnPostCompPlayerDisconnected.Invoke(playerId, cause, timeout);
			if (m_pRespawnSystemComponent)
				m_pRespawnSystemComponent.OnPlayerDisconnected_S(playerId, cause, timeout);
			return;
		}

		// Alive disconnect: let the engine SAVE the character into SCR_ReconnectComponent's
		// reconnect list. The audit timer (vanilla default + BZ_ReconnectComponent override) cleans
		// the body when it expires, and the same path also catches orphan bodies recreated from
		// the persistence save after a server restart.
		SavePlayerAndFlushToDisk(playerId);
		super.OnPlayerDisconnected(playerId, cause, timeout);
	}

	//------------------------------------------------------------------------------------------------
	// Kill the unconscious body and rebind persistence so it stays in world as an independent corpse.
	protected void DecoupleUnconsciousBody(IEntity body, int playerId)
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

		TrackCorpseForCleanup(body);
		Print(string.Format("[BrasilZ] Unconscious body decoupled for player %1", playerId), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	// Rebind dead body persistence so it stays in world independent of the disconnecting player.
	protected void DecoupleDeadBody(IEntity body, int playerId)
	{
		if (!body)
			return;

		SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByEntityWorld(body);
		if (!persistence || persistence.GetState() != EPersistenceSystemState.ACTIVE)
			return;

		persistence.StopTracking(body);
		persistence.StartTracking(body);
		persistence.Save(body, ESaveGameType.AUTO);

		TrackCorpseForCleanup(body);
		Print(string.Format("[BrasilZ] Dead body decoupled for player %1", playerId), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	// Track corpse for timed cleanup so dead entities don't accumulate forever.
	void TrackCorpseForCleanup(IEntity corpse)
	{
		if (!corpse || m_fCorpseLifetimeSec <= 0)
			return;

		if (m_aTrackedCorpses.Contains(corpse))
			return;

		m_aTrackedCorpses.Insert(corpse);
		m_aCorpseDeathTimes.Insert(System.GetTickCount());
	}

	//------------------------------------------------------------------------------------------------
	protected void TickCorpseCleanup()
	{
		if (m_fCorpseLifetimeSec <= 0 || m_aTrackedCorpses.IsEmpty())
			return;

		int currentTime = System.GetTickCount();
		float lifetimeMs = m_fCorpseLifetimeSec * 1000;

		for (int i = m_aTrackedCorpses.Count() - 1; i >= 0; i--)
		{
			IEntity corpse = m_aTrackedCorpses[i];
			if (!corpse)
			{
				m_aTrackedCorpses.Remove(i);
				m_aCorpseDeathTimes.Remove(i);
				continue;
			}

			float ageMs = currentTime - m_aCorpseDeathTimes[i];
			if (ageMs >= lifetimeMs)
			{
				Print(string.Format("[BrasilZ] Cleaning up corpse at %1 (age: %2s)", corpse.GetOrigin(), ageMs / 1000), LogLevel.NORMAL);
				m_aTrackedCorpses.Remove(i);
				m_aCorpseDeathTimes.Remove(i);
				SCR_EntityHelper.DeleteEntityAndChildren(corpse);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	// Save controller + character to persistence and flush to disk so disconnect state survives crash/restart.
	protected void SavePlayerAndFlushToDisk(int playerId)
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
			GetGame().GetCallqueue().CallLater(FlushSaveToDisk, 1000, false);
			return;
		}

		if (!OverwriteLatestSave(saveManager))
			Print("[BrasilZ] SavePlayerAndFlushToDisk: no save to overwrite", LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	protected void FlushSaveToDisk()
	{
		SaveGameManager saveManager = GetGame().GetSaveGameManager();
		if (!saveManager)
			return;

		if (!saveManager.IsSavingPossible())
		{
			m_iFlushRetryCount++;
			if (m_iFlushRetryCount < 10)
				GetGame().GetCallqueue().CallLater(FlushSaveToDisk, 1000, false);
			return;
		}

		m_iFlushRetryCount = 0;
		if (!OverwriteLatestSave(saveManager))
			Print("[BrasilZ] FlushSaveToDisk: no save to overwrite", LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	// Persist death flag and decouple corpse when player dies on the server (so disconnect-during-death
	// doesn't roll the player back to an alive save).
	override void OnPlayerKilled(int playerId, IEntity playerEntity, IEntity killerEntity, notnull Instigator killer)
	{
		super.OnPlayerKilled(playerId, playerEntity, killerEntity, killer);

		if (IsProxy() || !playerEntity)
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

		DecoupleDeadBody(playerEntity, playerId);

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
			OverwriteLatestSave(saveManager);
	}
}
