// BrasilZ cache buster: 2026-05-25-bootscan-destroyed-vehicles
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
	protected static const int BZ_CORPSE_LIFETIME_SEC = 1800; // 30 minutes
	protected static const int BZ_CORPSE_CLEANUP_INTERVAL_MS = 60000;
	protected static const string BZ_CORPSE_DB_DIR = "$profile:BrasilZ";
	protected static const string BZ_CORPSE_DB_PATH = "$profile:BrasilZ/Corpses.db";
	protected static const string BZ_CORPSE_DB_VERSION = "BZ_CORPSES_V1";
	protected static const float BZ_CORPSE_REATTACH_RADIUS = 3.0;
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
	protected ref array<IEntity> m_aBzBikeResults;
	// FIX (zombie WorldState bloat) — ambient zombies were persisting in WorldState.json.
	// 357 entries × ~17KB each = 6.2MB, hit 16MB engine limit → SAVE_FAILED → players lose
	// progress on restart. Boot scan now wipes any leftover zombie chars from disk so save
	// stays small. Live zombies also get SetPersistence(false) via BZ_BaconZombieTuning.
	protected ref array<IEntity> m_aBzZombieResults;
	// Veículos destruídos — limpa no boot pra não acumular no save.
	protected ref array<IEntity> m_aBzDestroyedVehicleResults;
	protected ref array<IEntity> m_aBzTrackedCorpses = new array<IEntity>();
	protected ref array<int> m_aBzCorpseExpireUnix = new array<int>();
	protected ref array<vector> m_aBzPersistedCorpsePositions = new array<vector>();
	protected ref array<int> m_aBzPersistedCorpseExpireUnix = new array<int>();

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

		BZ_LoadCorpseDatabase();
		GetGame().GetCallqueue().CallLater(BZ_TickCorpseCleanup, BZ_CORPSE_CLEANUP_INTERVAL_MS, true);
		GetGame().GetCallqueue().CallLater(BZ_TryStartAutoSave, BZ_AUTOSAVE_START_DELAY_MS, false);
		GetGame().GetCallqueue().CallLater(BZ_TryArmOrphanScan, 1000, false);
		GetGame().GetCallqueue().CallLater(BZ_PortalRewards.EnsureStarted, 5000, false);
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
		m_bBzOrphanScanDone = true;

		// PLAYER ORPHAN SCAN DISABLED (2026-05-23) — adoption of ReforgedZ pattern.
		//
		// Previously: walked the world looking for alive ChimeraCharacter entities matching
		// BrasilZ player prefabs that weren't controlled by an online player, then either
		// deleted (broke weapon binding via DeleteEntityAndChildren) or sunk Y=-3000 (got
		// persisted in save, player respawned underground).
		//
		// Now: SCR_ReconnectComponent.m_ReconnectPlayerList holds disconnected player
		// entities with an audit timeout. BZ_ReconnectComponent.OnPlayerAuditTimeouted
		// calls SaveAndRemoveCharacter → save + delete cleanly. After server restart,
		// the engine repopulates the reconnect list from persistence and the same audit
		// cleanup runs. No orphan scan needed.
		//
		// Mission AI wipe still runs below — DarcMissions entities aren't in the reconnect
		// list and need explicit cleanup.
		// ORPHAN DELETION RE-ENABLED (2026-05-25): vanilla m_ReconnectPlayerList é
		// in-memory + vazia no boot. Orphans carregados do save NUNCA entram nessa lista
		// → audit timeout não dispara → órfão eterno. Boot scan precisa limpar
		// orphans-do-save explicitamente. SessionStorage UUID preserva inventário/pos
		// independente do delete da entity no mundo.
		Print("[BrasilZ][BootScan] Player orphan scan ENABLED — save-loaded orphans não estão em m_ReconnectPlayerList, audit timeout não pega.", LogLevel.NORMAL);

		BaseWorld world = GetGame().GetWorld();
		if (!world)
		{
			Print("[BrasilZ][BootScan] World null — abort scan", LogLevel.WARNING);
			return;
		}

		m_aBzOrphanScanResults = new array<IEntity>();
		m_aBzMissionAiResults = new array<IEntity>();
		m_aBzBikeResults = new array<IEntity>();
		m_aBzZombieResults = new array<IEntity>();
		m_aBzDestroyedVehicleResults = new array<IEntity>();
		world.QueryEntitiesBySphere(vector.Zero, BZ_ORPHAN_SCAN_RADIUS, BZ_QueryCollectOrphanCandidate, null, EQueryEntitiesFlags.DYNAMIC);

		int orphansFound = m_aBzOrphanScanResults.Count();
		Print(string.Format("[BrasilZ][BootScan] %1 player orphan candidates encontrados — iterando pra Save + Delete.", orphansFound), LogLevel.NORMAL);

		PlayerManager pm = GetGame().GetPlayerManager();
		int deleted = 0;
		int deadCorpses = 0;  // skipped pra loot 30min
		int wipedMissionAi = 0;
		int wipedZombies = 0;

		foreach (IEntity entity : m_aBzOrphanScanResults)
		{
			if (!entity || entity.IsDeleted())
			{
				Print("[BrasilZ][BootScan] Skip — entity null/deleted", LogLevel.NORMAL);
				continue;
			}

			ChimeraCharacter character = ChimeraCharacter.Cast(entity);
			if (!character)
			{
				Print(string.Format("[BrasilZ][BootScan] Skip — not ChimeraCharacter at %1", entity.GetOrigin()), LogLevel.NORMAL);
				continue;
			}

			// Owned by a connected player → leave alone.
			if (pm && pm.GetPlayerIdFromControlledEntity(entity) > 0)
			{
				int ownerId = pm.GetPlayerIdFromControlledEntity(entity);
				Print(string.Format("[BrasilZ][BootScan] Skip — entity at %1 owned by online player %2", entity.GetOrigin(), ownerId), LogLevel.NORMAL);
				continue;
			}

			// Skip dead/INCAPACITATED corpses — those are PvP loot and MUST stay visible
			// across restarts so a kill 5 minutes before reboot doesn't get wiped on boot.
			// Corpses accumulate forever; admin can clean manually if needed.
			SCR_DamageManagerComponent dmg = SCR_DamageManagerComponent.GetDamageManager(character);
			if (dmg && dmg.IsDestroyed())
			{
				deadCorpses++;
				BZ_ReattachPersistedCorpse(entity);
				Print(string.Format("[BrasilZ][BootScan] Skip — corpse (destroyed) at %1, preserved for loot", entity.GetOrigin()), LogLevel.NORMAL);
				continue;
			}

			CharacterControllerComponent cc = CharacterControllerComponent.Cast(character.FindComponent(CharacterControllerComponent));
			if (cc && cc.GetLifeState() != ECharacterLifeState.ALIVE)
			{
				deadCorpses++;
				BZ_ReattachPersistedCorpse(entity);
				Print(string.Format("[BrasilZ][BootScan] Skip — lifeState=%1 at %2", typename.EnumToString(ECharacterLifeState, cc.GetLifeState()), entity.GetOrigin()), LogLevel.NORMAL);
				continue;
			}

			// Pre-sink diagnostics: count children + identify weapon/gadget. Critical to
			// understand what entities WOULD have been deleted by the old DeleteEntityAndChildren.
			// Children stay intact via sink approach but log helps verify.
			int childCount = 0;
			string childInfo = "";
			IEntity child = entity.GetChildren();
			while (child && childCount < 20)
			{
				ResourceName childPrefab = "(no-prefab)";
				if (child.GetPrefabData())
					childPrefab = child.GetPrefabData().GetPrefabName();
				childInfo = childInfo + string.Format("\n  child[%1] prefab=%2", childCount, childPrefab);
				child = child.GetSibling();
				childCount++;
			}

			// Identify equipped weapon for diagnostic clarity.
			string equippedWeapon = "(none)";
			if (cc)
			{
				BaseWeaponManagerComponent wm = cc.GetWeaponManagerComponent();
				if (wm)
				{
					BaseWeaponComponent curWeapon = wm.GetCurrent();
					if (curWeapon && curWeapon.GetOwner() && curWeapon.GetOwner().GetPrefabData())
						equippedWeapon = curWeapon.GetOwner().GetPrefabData().GetPrefabName();
				}
			}

			float orphanHP = -1;
			if (dmg)
				orphanHP = dmg.GetHealth();
			Print(string.Format("[BrasilZ][BootScan] FOUND alive orphan at %1 | HP=%2 | children=%3 | equipped=%4%5",
				entity.GetOrigin(),
				orphanHP,
				childCount,
				equippedWeapon,
				childInfo), LogLevel.NORMAL);

			// DELETE alive orphan parent only — DO NOT touch children. The previous
			// DeleteEntityAndChildren recursively nuked the equipped weapon and inventory
			// entities, breaking WeaponManager bindings on reconnect (reload/inspect
			// broken). The previous SINK to Y=-3000 caused the engine save to persist
			// the underground Y over the legit save — player respawned at Y=-1993 in
			// pitch-black void.
			//
			// Correct path: stop persistence tracking BEFORE delete (so engine doesn't
			// save anything about this entity), then RplComponent.DeleteRplEntity with
			// recursive=false. Children become loose entities; engine garbage-collects
			// orphaned weapons/items eventually. SessionStorage holds the player's
			// legit save data independently — reconnect uses that.
			vector orphanPos = entity.GetOrigin();

			// CRITICAL ORDER (matches ReforgedZ_ReconnectComponent.SaveAndRemoveCharacter):
			// Save → ReleaseTracking → OverwriteLatestSave → DeleteRplEntity.
			//
			// IMPORTANTE: StopTracking() faz GARBAGE-COLLECT do inventário save (player perde
			// progresso). Use ReleaseTracking() — só remove tracking, save preservado.
			// Ver: RZ_BaseMission.c:406 "StopTracking on inventory items causes the persistence
			// system to garbage-collect them."
			SCR_PersistenceSystem orphanPersist = SCR_PersistenceSystem.GetByEntityWorld(entity);
			if (orphanPersist && orphanPersist.GetState() == EPersistenceSystemState.ACTIVE)
			{
				orphanPersist.Save(entity, ESaveGameType.AUTO);
				orphanPersist.ReleaseTracking(entity);
				Print(string.Format("[BrasilZ][BootScan] Save + ReleaseTracking on orphan at %1 — SessionStorage UUID preservada.", orphanPos), LogLevel.NORMAL);

				// Flush save to disk ANTES do delete pra garantir DB commit do Save() finalizar.
				SaveGameManager saveManager = GetGame().GetSaveGameManager();
				if (saveManager && saveManager.IsSavingPossible())
				{
					SCR_BaseGameMode.BZ_OverwriteLatestSave(saveManager);
					Print(string.Format("[BrasilZ][BootScan] OverwriteLatestSave flushed for orphan at %1", orphanPos), LogLevel.NORMAL);
				}
			}

			// Single-entity delete via Rpl (recursive=false). Keeps weapon/inventory
			// children floating — engine garbage-collects.
			RplComponent orphanRpl = RplComponent.Cast(entity.FindComponent(RplComponent));
			if (orphanRpl)
			{
				RplComponent.DeleteRplEntity(entity, false);
				Print(string.Format("[BrasilZ][BootScan] DeleteRplEntity(recursive=false) on orphan at %1", orphanPos), LogLevel.NORMAL);
			}
			else
			{
				// Fallback: SCR_EntityHelper has no single-entity option easily; use
				// AndChildren as last resort. Only happens if entity has no RplComponent
				// (rare for player-prefab orphans).
				SCR_EntityHelper.DeleteEntityAndChildren(entity);
				Print(string.Format("[BrasilZ][BootScan] FALLBACK DeleteEntityAndChildren on orphan at %1 (no RplComponent — children also deleted)", orphanPos), LogLevel.WARNING);
			}

			deleted++;
		}

		m_aBzOrphanScanResults = null;
		Print(string.Format("[BrasilZ][BootScan] Player orphan deletion completo — %1 alive orphans deletados (Save + StopTracking + DeleteRplEntity). SessionStorage UUID preservada. Dead corpses left for 30min loot via BZ_TickCorpseCleanup.", deleted), LogLevel.NORMAL);

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
		int bikeCandidates = m_aBzBikeResults.Count();
		int wipedBikes = 0;
		foreach (IEntity bikeEnt : m_aBzBikeResults)
		{
			if (!bikeEnt || bikeEnt.IsDeleted())
				continue;

			SCR_PersistenceSystem bikePersist = SCR_PersistenceSystem.GetByEntityWorld(bikeEnt);
			if (bikePersist && bikePersist.GetState() == EPersistenceSystemState.ACTIVE)
				bikePersist.StopTracking(bikeEnt);

			RplComponent bikeRpl = RplComponent.Cast(bikeEnt.FindComponent(RplComponent));
			if (bikeRpl)
				RplComponent.DeleteRplEntity(bikeEnt, false);
			else
				SCR_EntityHelper.DeleteEntityAndChildren(bikeEnt);

			wipedBikes++;
		}

		m_aBzBikeResults = null;
		Print(string.Format("[BrasilZ][BootScan] Deployable bike wipe complete - candidates=%1 deleted=%2.", bikeCandidates, wipedBikes), LogLevel.NORMAL);

		// Discord webhook — resumo boot (envia sempre, mesmo zero)
		// NOTA: zombieCandidates + wipedMissionAi populados depois nesse mesmo método.
		// Construímos summary AGORA com placeholders, mas atualizamos depois via schedule.
		// Simples: agenda envio Discord 1s depois (após zombie/mission wipe completar).
		int summaryDeleted = deleted;
		int summaryDeadCorpses = deadCorpses;
		int summaryBikes = wipedBikes;
		int summaryMissionAi = wipedMissionAi;
		Print(string.Format("[BrasilZ][BootScan] Mission AI wipe complete - removed %1 leftover mission AI entities (factions: %2).", wipedMissionAi, s_aBzMissionEnemyFactionKeys), LogLevel.NORMAL);

		// FIX (zombie WorldState bloat) — wipe leftover ambient zombies from previous session.
		// Pre-fix saves accumulated 357 zombies × 17KB = 6.2MB in WorldState.json, hitting the
		// 16MB engine limit and breaking saves. SetPersistence(false) in BZ_BaconZombieTuning
		// stops NEW zombies from being persisted, but existing save still has stale entries —
		// boot wipe removes them so the next save shrinks.
		int zombieCandidates = m_aBzZombieResults.Count();
		foreach (IEntity zombieEnt : m_aBzZombieResults)
		{
			if (!zombieEnt || zombieEnt.IsDeleted())
				continue;

			// Defensive: never wipe a player-controlled char (paranoia — query already filters
			// by prefab whitelist but double-check here).
			if (pm && pm.GetPlayerIdFromControlledEntity(zombieEnt) > 0)
				continue;

			// Stop persistence tracking so the engine doesn't write the entity back to disk
			// during the delete window.
			SCR_PersistenceSystem zPersist = SCR_PersistenceSystem.GetByEntityWorld(zombieEnt);
			if (zPersist && zPersist.GetState() == EPersistenceSystemState.ACTIVE)
				zPersist.StopTracking(zombieEnt);

			SCR_EntityHelper.DeleteEntityAndChildren(zombieEnt);
			wipedZombies++;
		}

		m_aBzZombieResults = null;
		Print(string.Format("[BrasilZ][BootScan] Zombie wipe complete - candidates=%1 deleted=%2 (factionless ambient zombies, prevents WorldState bloat).", zombieCandidates, wipedZombies), LogLevel.NORMAL);

		// Destroyed vehicles wipe pass
		int destVehCandidates = m_aBzDestroyedVehicleResults.Count();
		int wipedDestVehicles = 0;
		foreach (IEntity vehEnt : m_aBzDestroyedVehicleResults)
		{
			if (!vehEnt || vehEnt.IsDeleted())
				continue;

			SCR_PersistenceSystem vPersist = SCR_PersistenceSystem.GetByEntityWorld(vehEnt);
			if (vPersist && vPersist.GetState() == EPersistenceSystemState.ACTIVE)
				vPersist.StopTracking(vehEnt);

			RplComponent vehRpl = RplComponent.Cast(vehEnt.FindComponent(RplComponent));
			if (vehRpl)
				RplComponent.DeleteRplEntity(vehEnt, false);
			else
				SCR_EntityHelper.DeleteEntityAndChildren(vehEnt);

			wipedDestVehicles++;
		}
		m_aBzDestroyedVehicleResults = null;
		Print(string.Format("[BrasilZ][BootScan] Destroyed vehicle wipe complete - candidates=%1 deleted=%2.", destVehCandidates, wipedDestVehicles), LogLevel.NORMAL);

		if (wipedBikes > 0)
		{
			GetGame().GetCallqueue().CallLater(BZ_FlushSaveToDisk, 5000, false);
			Print("[BrasilZ][BootScan] Scheduled save flush after deployable bike cleanup.", LogLevel.NORMAL);
		}

		int scanElapsedMs = System.GetTickCount() - scanStartTick;
		Print(string.Format("[BrasilZ][BootScan] Total scan time: %1ms. Gated connects will now proceed.", scanElapsedMs), LogLevel.NORMAL);

		// Discord webhook — resumo boot completo (todos counters prontos agora)
		ref array<ref BZ_DiscordField> summaryFields = {};
		summaryFields.Insert(new BZ_DiscordField("👤 Corpos vivos removidos", string.Format("%1", summaryDeleted)));
		summaryFields.Insert(new BZ_DiscordField("💀 Corpos mortos preservados (loot 30min)", string.Format("%1", summaryDeadCorpses)));
		summaryFields.Insert(new BZ_DiscordField("🚲 Bicicletas removidas", string.Format("%1", summaryBikes)));
		summaryFields.Insert(new BZ_DiscordField("🧟 Zombies removidos", string.Format("%1", wipedZombies)));
		summaryFields.Insert(new BZ_DiscordField("🪖 Mission AI removidos", string.Format("%1", summaryMissionAi)));
		summaryFields.Insert(new BZ_DiscordField("💥 Veículos destruídos removidos", string.Format("%1", wipedDestVehicles)));
		BZ_DiscordWebhook.Send(
			"🔄 Servidor iniciado",
			"Resumo da limpeza no boot:",
			BZ_DiscordConfig.COLOR_BLUE,
			summaryFields
		);
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

		auto prefabData = entity.GetPrefabData();
		if (!prefabData)
			return true;

		ResourceName prefab = prefabData.GetPrefabName();
		if (prefab.IsEmpty())
			return true;

		if (prefab == "{3FDBFE7F1A1EB8F2}Prefabs/Vehicles/Wheeled/WW2_Bike/WW2_Bike_base.et"
			|| prefab == "{7FC3F9F617C308B4}Prefabs/Vehicles/Wheeled/WW2_Bike_Deployable.et")
		{
			m_aBzBikeResults.Insert(entity);
			return true;
		}

		// Veículos destruídos — qualquer Vehicle com damage manager destroyed.
		// Filtro: entity é Vehicle (não ChimeraCharacter, não bike acima).
		if (Vehicle.Cast(entity))
		{
			SCR_DamageManagerComponent vehDmg = SCR_DamageManagerComponent.Cast(entity.FindComponent(SCR_DamageManagerComponent));
			if (vehDmg && vehDmg.IsDestroyed())
			{
				m_aBzDestroyedVehicleResults.Insert(entity);
			}
			return true;
		}

		if (!ChimeraCharacter.Cast(entity))
			return true;

		if (s_aBzPlayerCharacterPrefabs.Contains(prefab))
		{
			m_aBzOrphanScanResults.Insert(entity);
			return true;
		}

		FactionAffiliationComponent facComp = FactionAffiliationComponent.Cast(entity.FindComponent(FactionAffiliationComponent));
		string factionKey = "";
		bool hasFaction = false;
		if (facComp)
		{
			Faction faction = facComp.GetAffiliatedFaction();
			if (faction)
			{
				factionKey = faction.GetFactionKey();
				hasFaction = true;
				if (s_aBzMissionEnemyFactionKeys.Contains(factionKey))
				{
					m_aBzMissionAiResults.Insert(entity);
					return true;
				}
			}
		}

		// Zombie classification: ChimeraCharacter with NO faction (or empty factionKey),
		// not in player prefab whitelist, not in mission AI faction list. Ambient BaconZombies
		// match this — they spawn without affiliation. Wipe at boot to keep WorldState lean.
		if (!hasFaction || factionKey.IsEmpty())
		{
			m_aBzZombieResults.Insert(entity);
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

		// EXPLICIT PLAYER ENTITY SAVE — without this, autosave only persists world state
		// via SaveGameManager. Player characters that are still alive in-world (online)
		// do NOT get their current position/inventory/HP serialized during the autosave
		// flush. Result: after server restart, online players reverted to the last
		// SCR_PersistenceSystem.Save event (typically the last disconnect or kill).
		//
		// Pattern from ReforgedZ_RespawnSystemComponent.OnPlayerKilled_S (lines 348-357):
		// explicit persistence.Save(controller) + persistence.Save(character) BEFORE
		// the world flush. We do the same here for every online player every autosave
		// tick to capture in-flight state (movement, looting, combat damage, metabolism).
		PlayerManager pmAuto = GetGame().GetPlayerManager();
		SCR_PersistenceSystem persistAuto = SCR_PersistenceSystem.GetByEntityWorld(this);
		int savedPlayerCount = 0;
		if (pmAuto && persistAuto && persistAuto.GetState() == EPersistenceSystemState.ACTIVE)
		{
			array<int> playerIds = {};
			pmAuto.GetPlayers(playerIds);

			foreach (int pid : playerIds)
			{
				if (pid <= 0)
					continue;

				SCR_PlayerController pcAuto = SCR_PlayerController.Cast(pmAuto.GetPlayerController(pid));
				if (pcAuto)
				{
					persistAuto.Save(pcAuto);
				}

				IEntity charAuto = pmAuto.GetPlayerControlledEntity(pid);
				if (charAuto)
				{
					persistAuto.Save(charAuto);
					savedPlayerCount++;
				}
			}
		}

		bool ok = BZ_OverwriteLatestSave(saveManager);
		int elapsedMs = System.GetTickCount() - startTick;
		s_bBzSaveInProgress = false;
		Print(string.Format("[BrasilZ][AutoSave] Tick ok=%1 elapsed=%2ms savedPlayers=%3", ok, elapsedMs, savedPlayerCount), LogLevel.NORMAL);

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
		{
			Print(string.Format("[BrasilZ][Decouple] Player %1 — body NULL, skip decouple", playerId), LogLevel.WARNING);
			return;
		}

		vector bodyPos = body.GetOrigin();

		// Identify equipped weapon + children count BEFORE decouple, so log shows
		// what items are about to enter the 30-min loot window.
		int childCount = 0;
		IEntity ch = body.GetChildren();
		while (ch && childCount < 30)
		{
			ch = ch.GetSibling();
			childCount++;
		}

		string equippedPrefab = "(none)";
		CharacterControllerComponent ccDecouple = CharacterControllerComponent.Cast(body.FindComponent(CharacterControllerComponent));
		if (ccDecouple)
		{
			BaseWeaponManagerComponent wmDecouple = ccDecouple.GetWeaponManagerComponent();
			if (wmDecouple)
			{
				BaseWeaponComponent curW = wmDecouple.GetCurrent();
				if (curW && curW.GetOwner() && curW.GetOwner().GetPrefabData())
					equippedPrefab = curW.GetOwner().GetPrefabData().GetPrefabName();
			}
		}

		SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByEntityWorld(body);
		if (!persistence)
		{
			Print(string.Format("[BrasilZ][Decouple] Player %1 — persistence NULL at %2, skip", playerId, bodyPos), LogLevel.WARNING);
			return;
		}

		int persistState = persistence.GetState();
		if (persistState != EPersistenceSystemState.ACTIVE)
		{
			Print(string.Format("[BrasilZ][Decouple] Player %1 — persistence not ACTIVE (state=%2), skip decouple", playerId, persistState), LogLevel.WARNING);
			return;
		}

		Print(string.Format("[BrasilZ][Decouple] Player %1 BEGIN | pos=%2 | children=%3 | equipped=%4", playerId, bodyPos, childCount, equippedPrefab), LogLevel.NORMAL);

		persistence.StopTracking(body);
		persistence.StartTracking(body);
		persistence.Save(body, ESaveGameType.AUTO);

		BZ_TrackCorpseForCleanup(body);
		Print(string.Format("[BrasilZ][Decouple] Player %1 END — corpse decoupled (30min lootable timer started, tracked corpses=%2)", playerId, m_aBzTrackedCorpses.Count()), LogLevel.NORMAL);
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

		int expireUnix = System.GetUnixTime() + BZ_CORPSE_LIFETIME_SEC;
		BZ_TrackCorpseWithExpire(corpse, expireUnix, true);
	}

	//------------------------------------------------------------------------------------------------
	// Public: query if corpse is in the tracked loot window.
	bool BZ_IsTrackedCorpse(IEntity corpse)
	{
		if (!corpse)
			return false;
		return m_aBzTrackedCorpses.Contains(corpse);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_TickCorpseCleanup()
	{
		if (BZ_CORPSE_LIFETIME_SEC <= 0 || m_aBzTrackedCorpses.IsEmpty())
			return;

		int currentUnix = System.GetUnixTime();
		int cleaned = 0;
		int pruned = 0;
		int kept = 0;

		for (int i = m_aBzTrackedCorpses.Count() - 1; i >= 0; i--)
		{
			IEntity corpse = m_aBzTrackedCorpses[i];
			if (!corpse)
			{
				m_aBzTrackedCorpses.Remove(i);
				m_aBzCorpseExpireUnix.Remove(i);
				pruned++;
				continue;
			}

			int expireUnix = m_aBzCorpseExpireUnix[i];
			if (currentUnix >= expireUnix)
			{
				vector cpos = corpse.GetOrigin();

				// Count children + prefab for diagnostic (post-30min loot items getting nuked).
				int corpseChildren = 0;
				IEntity cch = corpse.GetChildren();
				while (cch && corpseChildren < 30)
				{
					cch = cch.GetSibling();
					corpseChildren++;
				}

				Print(string.Format("[BrasilZ][CorpseCleanup] Removing corpse at %1 (expiredUnix=%2, children=%3 will be deleted via DeleteEntityAndChildren)", cpos, expireUnix, corpseChildren), LogLevel.NORMAL);
				m_aBzTrackedCorpses.Remove(i);
				m_aBzCorpseExpireUnix.Remove(i);
				SCR_EntityHelper.DeleteEntityAndChildren(corpse);
				cleaned++;
			}
			else
			{
				kept++;
			}
		}

		if (cleaned > 0 || pruned > 0)
		{
			BZ_WriteCorpseDatabase();
			Print(string.Format("[BrasilZ][CorpseCleanup] Tick complete: cleaned=%1 (age>30min) pruned=%2 (null refs) kept=%3 (still in window)", cleaned, pruned, kept), LogLevel.NORMAL);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_TrackCorpseWithExpire(IEntity corpse, int expireUnix, bool writeDisk)
	{
		if (!corpse || expireUnix <= System.GetUnixTime())
			return;

		if (m_aBzTrackedCorpses.Contains(corpse))
			return;

		m_aBzTrackedCorpses.Insert(corpse);
		m_aBzCorpseExpireUnix.Insert(expireUnix);

		vector pos = corpse.GetOrigin();
		m_aBzPersistedCorpsePositions.Insert(pos);
		m_aBzPersistedCorpseExpireUnix.Insert(expireUnix);

		if (writeDisk)
			BZ_WriteCorpseDatabase();

		Print(string.Format("[BrasilZ][CorpseCleanup] Tracking corpse at %1 until unix=%2 (%3s remaining).", pos, expireUnix, expireUnix - System.GetUnixTime()), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_ReattachPersistedCorpse(IEntity corpse)
	{
		if (!corpse || m_aBzTrackedCorpses.Contains(corpse))
			return;

		int now = System.GetUnixTime();
		vector pos = corpse.GetOrigin();
		int bestIdx = -1;
		float bestDist = BZ_CORPSE_REATTACH_RADIUS;

		for (int i = 0; i < m_aBzPersistedCorpsePositions.Count(); i++)
		{
			int expireUnix = m_aBzPersistedCorpseExpireUnix[i];
			float dist = vector.Distance(pos, m_aBzPersistedCorpsePositions[i]);
			if (dist <= bestDist)
			{
				bestDist = dist;
				bestIdx = i;
			}
		}

		if (bestIdx >= 0)
		{
			int expireUnix = m_aBzPersistedCorpseExpireUnix[bestIdx];
			if (expireUnix <= now)
			{
				Print(string.Format("[BrasilZ][CorpseCleanup] Removing expired persisted corpse at %1 during boot reattach (expiredUnix=%2).", pos, expireUnix), LogLevel.NORMAL);
				SCR_EntityHelper.DeleteEntityAndChildren(corpse);
				return;
			}

			BZ_TrackCorpseWithExpire(corpse, expireUnix, false);
			Print(string.Format("[BrasilZ][CorpseCleanup] Reattached persisted corpse at %1, expires in %2s.", pos, expireUnix - now), LogLevel.NORMAL);
			return;
		}

		// Legacy corpse without disk metadata. Give it one cleanup window instead of leaving it forever.
		BZ_TrackCorpseWithExpire(corpse, now + BZ_CORPSE_LIFETIME_SEC, true);
		Print(string.Format("[BrasilZ][CorpseCleanup] Reattached legacy corpse at %1 with fresh 30min window.", pos), LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_LoadCorpseDatabase()
	{
		m_aBzPersistedCorpsePositions.Clear();
		m_aBzPersistedCorpseExpireUnix.Clear();

		FileHandle handle = FileIO.OpenFile(BZ_CORPSE_DB_PATH, FileMode.READ);
		if (!handle)
			return;

		int now = System.GetUnixTime();
		int loaded = 0;
		int expired = 0;

		string line;
		while (handle.ReadLine(line) > 0)
		{
			if (line.IsEmpty() || line == BZ_CORPSE_DB_VERSION)
				continue;

			array<string> columns = {};
			line.Split("|", columns, false);
			if (columns.Count() < 4)
				continue;

			int expireUnix = columns[0].ToInt();
			if (expireUnix <= now)
				expired++;
			else
				loaded++;

			vector pos = Vector(columns[1].ToFloat(), columns[2].ToFloat(), columns[3].ToFloat());
			m_aBzPersistedCorpsePositions.Insert(pos);
			m_aBzPersistedCorpseExpireUnix.Insert(expireUnix);
		}

		handle.Close();

		Print(string.Format("[BrasilZ][CorpseCleanup] Loaded corpse db: active=%1 expiredDropped=%2.", loaded, expired), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_WriteCorpseDatabase()
	{
		FileIO.MakeDirectory(BZ_CORPSE_DB_DIR);

		FileHandle handle = FileIO.OpenFile(BZ_CORPSE_DB_PATH, FileMode.WRITE);
		if (!handle)
		{
			Print("[BrasilZ][CorpseCleanup] Could not write corpse database.", LogLevel.WARNING);
			return;
		}

		handle.WriteLine(BZ_CORPSE_DB_VERSION);

		int now = System.GetUnixTime();
		for (int i = 0; i < m_aBzTrackedCorpses.Count(); i++)
		{
			IEntity corpse = m_aBzTrackedCorpses[i];
			if (!corpse || corpse.IsDeleted())
				continue;

			int expireUnix = m_aBzCorpseExpireUnix[i];
			if (expireUnix <= now)
				continue;

			vector pos = corpse.GetOrigin();
			handle.WriteLine(string.Format("%1|%2|%3|%4", expireUnix, pos[0], pos[1], pos[2]));
		}

		handle.Close();
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

		// Reject reconnects durante shutdown window — server vai fechar em ~90s, evita
		// player entrar/spawnar/criar entity nova que viraria órfão no save.
		if (BZ_RestartComponent.IsShutdownInProgress() && !BZ_IsProxy())
		{
			Print(string.Format("[BrasilZ][Connect] Player %1 REJECTED — server shutdown em curso. Kick imediato.", playerId), LogLevel.WARNING);
			PlayerManager pm = GetGame().GetPlayerManager();
			if (pm)
				pm.KickPlayer(playerId, PlayerManagerKickReason.KICK, 0);
			return;
		}

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

		// Start bed-respawn flag tick on this player's controller. 3s delay ensures
		// controller is fully initialized post-connect. Server-side only.
		if (!BZ_IsProxy())
			GetGame().GetCallqueue().CallLater(BZ_StartBedFlagTickForPlayer, 3000, false, playerId);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_StartBedFlagTickForPlayer(int playerId)
	{
		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return;
		PlayerController pc = pm.GetPlayerController(playerId);
		if (!pc)
			return;
		SCR_PlayerController scrPc = SCR_PlayerController.Cast(pc);
		if (!scrPc)
			return;
		scrPc.BZ_StartBedFlagTickIfNeeded();
		Print(string.Format("[BrasilZ][BedRespawn] Started bed flag tick for player %1.", playerId), LogLevel.NORMAL);
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
