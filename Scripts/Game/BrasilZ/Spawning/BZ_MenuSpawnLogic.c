// Rebuild marker: forces Workbench to detect source change and repackage the .pak.
// Last bump: 2026-05-26 — IncapKill: só HP<=0 + preserva instigator (anti-SUICIDE bug).
class BZ_MenuSpawnLogic : SCR_MenuSpawnLogic
{
	protected static const int MAX_PERSISTENCE_ACTIVE_WAIT_MS = 30000;
	protected static const int PERSISTENCE_ACTIVE_CHECK_INTERVAL_MS = 100;

	// Toggle: se true, pula deploy menu e spawna direto em random BZ_SpawnPoint com faction CIV.
	// Se false, vanilla behavior (deploy menu).
	protected static const bool BZ_AUTO_SPAWN_ENABLED = true;

	// INCAP detection: player vira INCAPACITATED (HP=0 mas char não destroyed → não dispara
	// EntityLost → fica caído sem respawn). Ticker varre players + forçaa morte se INCAP > N ms.
	// Reduzido pra 3s — sem death screen UI, player não tem tempo de espera longo aceitável.
	protected static const int BZ_INCAP_FORCE_KILL_MS = 3000;     // 3s stuck → kill
	protected static const int BZ_INCAP_CHECK_INTERVAL_MS = 1000; // checa a cada 1s
	// BZ FIX v2 (post-PvP respawn stuck): wait 4s after death before auto-spawn so vanilla
	// engine completes death cleanup (unbind PlayerController from destroyed corpse).
	// Mirrors ReforgedZ_RespawnSystemComponent.QueueCharacterMenu(playerId, 4000) pattern.
	// Without this delay, RequestRespawn fires while engine still has stale controlled
	// binding → spawn rejected → 5 verify retries fail → player stuck until manual ESC
	// PauseRespawn (which fires after the same 4s+ engine settle window).
	// Cache buster: 2026-05-26 respawn-delay-4s deploy
	protected static const int BZ_DEATH_AUTOSPAWN_DELAY_MS = 4000;
	protected static const int BZ_DEATH_AUTOSPAWN_RETRY_MS = 250;
	protected static const int BZ_DEATH_AUTOSPAWN_MAX_RETRIES = 2;
	protected static const int BZ_DEATH_AUTOSPAWN_PENDING_CLEAR_MS = 7000;
	protected static const int BZ_DEATH_AUTOSPAWN_VERIFY_MS = 1000;
	// BZ FIX v3: increased from 5 to 90. Engine vanilla cleanup of PlayerController binding
	// to destroyed-tracked corpse takes 30-60s (observed: pause button only succeeds at 50s
	// mark when controlled becomes null). 90×1s = 90s window covers worst case.
	protected static const int BZ_DEATH_AUTOSPAWN_VERIFY_MAX = 90;
	protected static const int BZ_CONNECT_AUTOSPAWN_CHECK_MS = 15000;
	protected ref map<int, int> m_mBzIncapStartTime = new map<int, int>();
	protected static ref map<int, int> s_mBzDeathRespawnRetries = new map<int, int>();
	protected static ref map<int, int> s_mBzDeathRespawnVerifyRetries = new map<int, int>();
	protected static ref set<int> s_aBzDeathRespawnPending = new set<int>();
	protected bool m_bBzIncapTickerArmed = false;

	protected ref map<int, int> m_mPersistenceWaitTime = new map<int, int>();

	//------------------------------------------------------------------------------------------------
	void BZ_MenuSpawnLogic()
	{
		// SAVE both so the engine adds the character to SCR_ReconnectComponent.m_ReconnectPlayerList.
		// On a real disconnect the audit timer ticks down and the override in BZ_ReconnectComponent
		// (BZ_ReconnectComponent.OnPlayerAuditTimeouted / SaveAndRemoveCharacter) cleans the body up.
		// On server restart the engine repopulates the reconnect list from the persistence save, so
		// orphan bodies left by players who didn't reconnect get the same audit-timeout cleanup.
		// SCR_ReconnectComponent MUST be present on the GameMode prefab for this flow to work.
		m_eDisconnectPlayerControllerBehaviour = SCR_ESpawnLogicDisconnectBehaviour.SAVE;
		m_eDisconnectCharacterBehaviour = SCR_ESpawnLogicDisconnectBehaviour.SAVE;
		m_sForcedFaction = "CIV";
		m_bWaitForSpawnPoints = true;
		// 0 = sem delay. Auto-spawn dispara antes do menu vanilla ter chance de abrir client-side.
		m_fDeployMenuOpenDelay = 0.0;

		// Arma incap ticker (server-side). Roda no callqueue global; idempotente via flag.
		if (!m_bBzIncapTickerArmed)
		{
			m_bBzIncapTickerArmed = true;
			GetGame().GetCallqueue().CallLater(BZ_TickIncapKill, BZ_INCAP_CHECK_INTERVAL_MS, true);
		}
	}

	//------------------------------------------------------------------------------------------------
	// Periodic scan: força morte de players INCAPACITATED há mais de BZ_INCAP_FORCE_KILL_MS.
	// Vanilla não dispara EntityLost em INCAP (só DEAD/destroyed) → player fica caído sem
	// respawn. Mata via damage manager pra disparar OnPlayerKilled → OnPlayerEntityLost_S →
	// auto-spawn ciclo normal.
	protected void BZ_TickIncapKill()
	{
		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return;

		array<int> ids = {};
		pm.GetPlayers(ids);
		int nowMs = System.GetTickCount();

		foreach (int pid : ids)
		{
			if (pid <= 0)
				continue;

			IEntity ent = pm.GetPlayerControlledEntity(pid);
			if (!ent)
			{
				m_mBzIncapStartTime.Remove(pid);
				continue;
			}

			CharacterControllerComponent cc = CharacterControllerComponent.Cast(ent.FindComponent(CharacterControllerComponent));
			if (!cc)
			{
				m_mBzIncapStartTime.Remove(pid);
				continue;
			}

			ECharacterLifeState state = cc.GetLifeState();

			// DIAG: log estado quando player tem HP baixo OU não-ALIVE, ajuda rastrear bug
			// "fica caído sem respawn". Health também útil.
			SCR_DamageManagerComponent dmg = SCR_DamageManagerComponent.GetDamageManager(ent);
			float hp = -1;
			bool destroyed = false;
			if (dmg)
			{
				hp = dmg.GetHealth();
				destroyed = dmg.IsDestroyed();
			}

			// SÓ INCAPACITATED é stuck. DEAD = ok (auto-spawn vai tratar via EntityLost).
			// DEAD-with-entity-not-deleted é estado transiente entre death → spawn novo —
			// matar nesse momento mata o spawn novo. Evitar.
			if (state != ECharacterLifeState.INCAPACITATED)
			{
				m_mBzIncapStartTime.Remove(pid);
				continue;
			}

			// SÓ MATAR INCAP COM HP <= 0 — player com HP > 0 pode se recuperar
			// (bleed-out natural, revive amigo, etc). Skip force-kill se ainda tem vida.
			if (hp > 0)
			{
				m_mBzIncapStartTime.Remove(pid);
				continue;
			}

			// DIAG: só quando INCAP confirmado + HP zerado.
			Print(string.Format("[BrasilZ][IncapKill] DIAG Player %1 INCAP HP=%2 destroyed=%3", pid, hp, destroyed), LogLevel.NORMAL);

			// Player INCAP — registra start ou checa timeout.
			int startTime;
			if (!m_mBzIncapStartTime.Find(pid, startTime))
			{
				m_mBzIncapStartTime.Set(pid, nowMs);
				Print(string.Format("[BrasilZ][IncapKill] Player %1 INCAP detectado, timer iniciado (force kill em %2ms).", pid, BZ_INCAP_FORCE_KILL_MS), LogLevel.NORMAL);
				continue;
			}

			int elapsedMs = nowMs - startTime;
			if (elapsedMs < BZ_INCAP_FORCE_KILL_MS)
				continue;

			// Timeout — força morte via SCR_CharacterDamageManagerComponent.Kill.
			SCR_CharacterDamageManagerComponent charDmg = SCR_CharacterDamageManagerComponent.Cast(dmg);
			if (charDmg)
			{
				// PRESERVA INSTIGATOR original — busca último damage source pra evitar SUICIDE.
				// Sem isso, Kill(null) marca categoria como SUICIDE no OnPlayerKilled.
				Instigator lastInstigator = charDmg.GetInstigator();
				if (!lastInstigator)
				{
					// Fallback: cria instigator vazio (mesmo comportamento antigo)
					lastInstigator = Instigator.CreateInstigator(null);
					Print(string.Format("[BrasilZ][IncapKill] Player %1 — sem instigator armazenado, fallback Kill(null) → marca SUICIDE", pid), LogLevel.WARNING);
				}
				else
				{
					Print(string.Format("[BrasilZ][IncapKill] Player %1 — usando último instigator pra preservar killer real.", pid), LogLevel.NORMAL);
				}

				charDmg.Kill(lastInstigator);
				Print(string.Format("[BrasilZ][IncapKill] Player %1 stuck %2ms — Kill() via SCR_CharacterDamageManager.", pid, BZ_INCAP_FORCE_KILL_MS), LogLevel.WARNING);

				// Backstop: se Kill() não disparar EntityLost em 5s, deleta entity direto via Rpl.
				GetGame().GetCallqueue().CallLater(BZ_ForceDeleteEntityIfStuck, 5000, false, pid, ent);
			}
			else
			{
				Print(string.Format("[BrasilZ][IncapKill] Player %1 — SCR_CharacterDamageManagerComponent não encontrado, force delete direto.", pid), LogLevel.WARNING);
				BZ_ForceDeleteEntityIfStuck(pid, ent);
			}

			m_mBzIncapStartTime.Remove(pid);
		}
	}

	//------------------------------------------------------------------------------------------------
	// Backstop: se SCR_CharacterDamageManagerComponent.Kill não disparar EntityLost por algum
	// motivo (engine bug, replication issue), força delete da entity via RplComponent.
	protected void BZ_ForceDeleteEntityIfStuck(int pid, IEntity stuckEnt)
	{
		if (!stuckEnt || stuckEnt.IsDeleted())
			return;

		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return;

		// Se player já tem char novo (auto-spawn deu certo), nada a fazer.
		IEntity current = pm.GetPlayerControlledEntity(pid);
		if (current && current != stuckEnt)
		{
			Print(string.Format("[BrasilZ][IncapKill] Player %1 — auto-spawn rodou, entity nova ativa (no force delete).", pid), LogLevel.NORMAL);
			return;
		}

		// Entity ainda controlled OU mesma entity — força delete via Rpl.
		RplComponent rpl = RplComponent.Cast(stuckEnt.FindComponent(RplComponent));
		if (rpl)
		{
			RplComponent.DeleteRplEntity(stuckEnt, false);
			Print(string.Format("[BrasilZ][IncapKill] Player %1 — Kill() falhou, DELETE RplEntity force.", pid), LogLevel.WARNING);
		}
		else
		{
			SCR_EntityHelper.DeleteEntityAndChildren(stuckEnt);
			Print(string.Format("[BrasilZ][IncapKill] Player %1 — Kill() falhou, DeleteEntityAndChildren fallback.", pid), LogLevel.WARNING);
		}
	}

	//------------------------------------------------------------------------------------------------
	// Set faction CIV CEDO no register (padrão ReforgedZ_MenuSpawnLogic.OnPlayerRegistered_S
	// linhas 47-57). Garante que faction está setada antes de qualquer DoSpawn_S ou check de menu.
	override void OnPlayerRegistered_S(int playerId)
	{
		m_mPersistenceWaitTime.Remove(playerId);

		// Aplica CIV faction imediato (m_sForcedFaction="CIV" no ctor).
		Faction forcedFaction;
		if (GetForcedFaction(forcedFaction))
		{
			PlayerController pc = GetGame().GetPlayerManager().GetPlayerController(playerId);
			if (pc)
			{
				SCR_PlayerFactionAffiliationComponent pfa = SCR_PlayerFactionAffiliationComponent.Cast(pc.FindComponent(SCR_PlayerFactionAffiliationComponent));
				if (pfa && pfa.GetAffiliatedFaction() != forcedFaction)
				{
					pfa.RequestFaction(forcedFaction);
					Print(string.Format("[BrasilZ][AutoSpawn] Player %1 — faction setada para CIV em OnPlayerRegistered_S (early).", playerId), LogLevel.NORMAL);
				}
			}
		}

		super.OnPlayerRegistered_S(playerId);

		if (BZ_AUTO_SPAWN_ENABLED)
			GetGame().GetCallqueue().CallLater(BZ_AutoSpawnIfStillNoEntityOnConnect, BZ_CONNECT_AUTOSPAWN_CHECK_MS, false, playerId);
	}

	//------------------------------------------------------------------------------------------------
	// Wait for SCR_PersistenceSystem to become ACTIVE before querying player data on join.
	// Reconnecting players skip the wait — their entity is already in the reconnect list and
	// blocking would let the audit timeout fire and discard the entity.
	override protected void RequestPlayerData_S(int playerId)
	{
		PlayerController pc = GetGame().GetPlayerManager().GetPlayerController(playerId);
		if (!pc)
			return;

		SCR_ReconnectComponent reconnect = SCR_ReconnectComponent.GetInstance();
		if (reconnect && reconnect.GetReconnectState(playerId) == SCR_EReconnectState.ENTITY_AVAILABLE)
		{
			Print(string.Format("[BrasilZ] Player %1 has reconnect entity — skipping persistence wait", playerId), LogLevel.NORMAL);
			super.RequestPlayerData_S(playerId);
			return;
		}

		if (!m_Persistence)
		{
			super.RequestPlayerData_S(playerId);
			return;
		}

		EPersistenceSystemState state = m_Persistence.GetState();
		if (state == EPersistenceSystemState.ACTIVE || state == EPersistenceSystemState.FAILURE)
		{
			super.RequestPlayerData_S(playerId);
			return;
		}

		int waitTime = m_mPersistenceWaitTime.Get(playerId);
		if (waitTime >= MAX_PERSISTENCE_ACTIVE_WAIT_MS)
		{
			Print(string.Format("[BrasilZ] Persistence wait timeout for player %1 — falling back", playerId), LogLevel.WARNING);
			super.RequestPlayerData_S(playerId);
			return;
		}

		m_mPersistenceWaitTime.Set(playerId, waitTime + PERSISTENCE_ACTIVE_CHECK_INTERVAL_MS);
		GetGame().GetCallqueue().CallLater(RequestPlayerData_S, PERSISTENCE_ACTIVE_CHECK_INTERVAL_MS, false, playerId);
	}

	//------------------------------------------------------------------------------------------------
	// Call super FIRST so base game ResolveReconnection runs before any menu queue.
	// If reconnect succeeds, super returns and the player possesses their reserved entity.
	override protected void OnPlayerDataLoaded_S(EPersistenceStatusCode statusCode, Managed result, bool isLast, Managed context)
	{
		PlayerController pc = PlayerController.Cast(Tuple1<PlayerController>.Cast(context).param1);
		if (pc)
			m_mPersistenceWaitTime.Remove(pc.GetPlayerId());

		super.OnPlayerDataLoaded_S(statusCode, result, isLast, context);
	}

	//------------------------------------------------------------------------------------------------
	// Validate the persisted character before possession. Catches post-restart reconnect where
	// SCR_ReconnectComponent has empty list but the save still holds a dead/dying body.
	//
	// Important: in normal/alive cases we MUST call super.OnPlayerCharacterLoaded_S with the
	// original arguments. Vanilla SCR_MenuSpawnLogic does exactly the right thing:
	//   * non-null result → PossessSpawnData → player restored at the saved position
	//   * null result     → DoInitialSpawn_S → deploy menu opens after m_fDeployMenuOpenDelay
	//
	// The old override called PossessSpawnData/DoInitialSpawn_S manually, which bypassed
	// vanilla's menu trigger and caused the respawn button to spawn the player at a default
	// position (often underwater) instead of opening the menu. Now we only intercept to
	// reject dead/death-flagged characters, then forward to super.
	override protected void OnPlayerCharacterLoaded_S(EPersistenceStatusCode statusCode, Managed result, bool isLast, Managed context)
	{
		Tuple1<int> playerDataContext = Tuple1<int>.Cast(context);
		int playerId = playerDataContext.param1;

		BaseGameEntity player = BaseGameEntity.Cast(result);
		BaseGameEntity loadedEntity = player;
		bool rejectedAsDead = false;
		string rejectReason = "";  // tracks WHY player ended up in deploy menu, for diagnostic logging

		// Initial state dump for diagnostics. Logs EXACTLY what persistence returned for this
		// player so we can distinguish "no save exists" (player == null) from "save exists but
		// rejected by validation" (player != null but later set to null).
		string entityDesc = "null";
		if (player)
		{
			vector loadedPos = player.GetOrigin();
			SCR_DamageManagerComponent loadedDmg = SCR_DamageManagerComponent.GetDamageManager(player);
			float loadedHP = -1;
			bool loadedDestroyed = false;
			if (loadedDmg)
			{
				loadedHP = loadedDmg.GetHealth();
				loadedDestroyed = loadedDmg.IsDestroyed();
			}
			ECharacterLifeState loadedLifeState = ECharacterLifeState.ALIVE;
			CharacterControllerComponent loadedCC = CharacterControllerComponent.Cast(player.FindComponent(CharacterControllerComponent));
			if (loadedCC)
				loadedLifeState = loadedCC.GetLifeState();
			entityDesc = string.Format("pos=%1, HP=%2, destroyed=%3, lifeState=%4", loadedPos, loadedHP, loadedDestroyed, typename.EnumToString(ECharacterLifeState, loadedLifeState));
		}
		Print(string.Format("[BrasilZ][SpawnLoad] Player %1 OnPlayerCharacterLoaded_S: statusCode=%2, entity=%3", playerId, statusCode, entityDesc), LogLevel.NORMAL);

		// Death flag persisted in $profile:BrasilZ/Deaths — survives server restart.
		//
		// IMPORTANT: stale-flag self-heal. Previous builds (before a7f0a56) treated lifeState
		// DEAD/INCAPACITATED as proof of death on disconnect, which mis-flagged living players.
		// To unblock those legacy stale flags without manual disk cleanup, we check the loaded
		// character's actual health first. If the persisted save shows the character is alive
		// (health > 0 and not destroyed), the flag is stale — clear it and accept the character.
		// Only honor the flag when the persisted character is genuinely dead.
		if (player)
		{
			string uid = BZ_Utils.GetPlayerUID(playerId);
			if (!uid.IsEmpty())
			{
				BZ_PlayerDeathRegistry registry = BZ_PlayerDeathRegistry.GetInstance();
				if (registry && registry.IsDeadByUID(uid))
				{
					SCR_DamageManagerComponent flagDmg = SCR_DamageManagerComponent.GetDamageManager(player);
					bool charAlive = flagDmg && !flagDmg.IsDestroyed() && flagDmg.GetHealth() > 0;

					if (charAlive)
					{
						Print(string.Format("[BrasilZ][SpawnLoad] Player %1 (UID %2) STALE death flag — char alive HP=%3. Auto-clearing flag, accepting char.", playerId, uid, flagDmg.GetHealth()), LogLevel.WARNING);
						registry.ClearDead(uid);
						registry.ClearDeadBody(playerId);
					}
					else
					{
						float rejHP = -1;
						bool rejDestroyed = false;
						if (flagDmg)
						{
							rejHP = flagDmg.GetHealth();
							rejDestroyed = flagDmg.IsDestroyed();
						}
						rejectReason = string.Format("DEATH_FLAG_PERSISTED+CHAR_CONFIRMED_DEAD (UID=%1, HP=%2, destroyed=%3)", uid, rejHP, rejDestroyed);
						Print(string.Format("[BrasilZ][SpawnLoad] Player %1 REJECT reason=%2", playerId, rejectReason), LogLevel.NORMAL);
						player = null;
						rejectedAsDead = true;
					}
				}
			}
		}

		// Combined lifeState + health check.
		// lifeState alone is unreliable: CharacterControllerComponent.GetLifeState() returns DEAD
		// by default before replication/init completes on the freshly loaded entity. Trust only
		// when health also confirms (<=0 or destroyed). Death flag above is the authoritative anti-ALT+F4.
		if (player)
		{
			SCR_DamageManagerComponent dmgMgr = SCR_DamageManagerComponent.GetDamageManager(player);
			float health = -1;
			bool destroyed = false;
			if (dmgMgr)
			{
				health = dmgMgr.GetHealth();
				destroyed = dmgMgr.IsDestroyed();
			}

			ECharacterLifeState lifeState = ECharacterLifeState.ALIVE;
			CharacterControllerComponent charController = CharacterControllerComponent.Cast(player.FindComponent(CharacterControllerComponent));
			if (charController)
				lifeState = charController.GetLifeState();

			bool healthDead = destroyed || (dmgMgr && health <= 0);

			if (healthDead)
			{
				rejectReason = string.Format("CHAR_HEALTH_ZERO (HP=%1, destroyed=%2, lifeState=%3)", health, destroyed, typename.EnumToString(ECharacterLifeState, lifeState));
				Print(string.Format("[BrasilZ][SpawnLoad] Player %1 REJECT reason=%2", playerId, rejectReason), LogLevel.NORMAL);
				player = null;
				rejectedAsDead = true;
			}
		}

		// Rejected: delete the stale persisted entity and forward to vanilla with a null result.
		// Vanilla SCR_MenuSpawnLogic.OnPlayerCharacterLoaded_S(...null) → DoInitialSpawn_S → menu.
		if (!player)
		{
			if (rejectedAsDead && loadedEntity)
			{
				Print(string.Format("[BrasilZ][SpawnLoad] Deleting stale persisted entity for dead player %1", playerId), LogLevel.WARNING);
				RplComponent.DeleteRplEntity(loadedEntity, false);
			}

			// SUMMARY log: explicit reason why player ends up in deploy menu. Critical for
			// debugging "player had progress but went to spawn menu" complaints.
			string finalReason = rejectReason;
			if (finalReason.IsEmpty())
			{
				if (loadedEntity)
					finalReason = "PERSISTENCE_RETURNED_ENTITY_BUT_REJECTED_UNKNOWN";
				else
					finalReason = "NO_SAVE_DATA (first connection OR save missing)";
			}
			Print(string.Format("[BrasilZ][DeployMenuReason] Player %1 → menu | reason=%2 | delay=%3s", playerId, finalReason, m_fDeployMenuOpenDelay), LogLevel.WARNING);
			BZ_RequestRandomSpawnAndVerify(playerId, finalReason);
			return;
		}

		// Buried-position recovery: a player who disconnected alive was teleported to Y-1000
		// by BZ_SinkCharacterOnDisconnect (or by the boot orphan scan after a server restart).
		//
		// Previous attempt did Teleport BEFORE super.OnPlayerCharacterLoaded_S. That looked
		// correct in logs but the entity origin update was applied async — vanilla read the
		// stale buried Y in the same frame, possessed the character underwater, and the engine
		// killed the char on void damage before the lifted transform replicated. Player ended
		// up dead-decoupled within ~1 frame of joining.
		//
		// Fix: disable damage handling FIRST (so the body survives even if vanilla possess
		// briefly sees the buried pos), forward to super to let vanilla possess, then lift +
		// re-enable damage on the next callqueue tick once the entity is fully attached.
		if (player)
		{
			vector pos = player.GetOrigin();
			// Sentinel must catch every sunk character regardless of pre-sink altitude.
			// Highest map peak ~600m → post-sink max Y ≈ -400. Use -100: anyone below it
			// was buried by us; ocean diver at Y~-30 is legitimately placed and stays.
			const float BURIED_SENTINEL_Y = -100.0;
			// Surface-rescue threshold: a save where the character is below sea level (but not
			// buried) means the player disconnected while submerged. On reconnect they would
			// respawn underwater and drown within seconds. Lift to SURFACE_RESCUE_Y to pop
			// them above the waterline. Observed in production: CaverinhaTV restored at
			// Y=-1.3986 → entity lost / dead body decoupled within the same frame.
			const float SURFACE_RESCUE_Y = 1.0;

			if (pos[1] < BURIED_SENTINEL_Y)
			{
				SCR_CharacterDamageManagerComponent charDmg = SCR_CharacterDamageManagerComponent.Cast(player.FindComponent(SCR_CharacterDamageManagerComponent));
				if (charDmg)
					charDmg.EnableDamageHandling(false);

				GetGame().GetCallqueue().CallLater(BZ_LiftAfterPossess, 250, false, player, pos);

				Print(string.Format("[BrasilZ] Player %1 buried at %2 → damage disabled, lift deferred 250ms past super possess", playerId, pos), LogLevel.NORMAL);
			}
			else if (pos[1] < SURFACE_RESCUE_Y)
			{
				// Submerged but not buried — disable damage briefly and defer a small lift
				// to surface so the player pops up out of the water on reconnect.
				SCR_CharacterDamageManagerComponent charDmg = SCR_CharacterDamageManagerComponent.Cast(player.FindComponent(SCR_CharacterDamageManagerComponent));
				if (charDmg)
					charDmg.EnableDamageHandling(false);

				vector surfacePos = pos;
				surfacePos[1] = SURFACE_RESCUE_Y;
				GetGame().GetCallqueue().CallLater(BZ_LiftToSurfaceAfterPossess, 250, false, player, surfacePos);

				Print(string.Format("[BrasilZ] Player %1 submerged at %2 → damage disabled, surface lift to %3 deferred 250ms", playerId, pos, surfacePos), LogLevel.NORMAL);
			}
			else
			{
				// HORIZONTAL RESCUE: saved pos may be inside geometry (rock, wall, vehicle
				// spawned after disconnect, base-built structure). Vanilla SetOrigin would
				// place char inside the obstacle → engine destroys entity → "entity lost".
				//
				// Pattern from ReforgedZ_SpawnPoint.GetPosYPR + RZ_BaseMission.FindSafeSpawnPosition:
				// query nearest empty terrain in 5m radius; if found > 0.5m from saved pos,
				// shift before super possess. Y stays the same (or recalc from surface).
				vector safePos = pos;
				if (SCR_WorldTools.FindEmptyTerrainPosition(safePos, pos, 5.0))
				{
					float horizontalShift = vector.Distance(safePos, pos);
					if (horizontalShift > 0.5)
					{
						// Recompute Y from terrain surface so player doesn't fall through floor
						// or float in air after horizontal shift.
						BaseWorld bw = GetGame().GetWorld();
						if (bw)
							safePos[1] = bw.GetSurfaceY(safePos[0], safePos[2]);

						player.SetOrigin(safePos);
						Print(string.Format("[BrasilZ][SpawnRescue] Player %1 saved pos %2 blocked, moved %3m to %4", playerId, pos, horizontalShift, safePos), LogLevel.WARNING);
					}
				}
			}
		}

		// Has progress → forward original args to vanilla, which possesses the saved character
		// at its last position. Do NOT call RequestSpawn manually — vanilla does it correctly.
		Print(string.Format("[BrasilZ] Player %1 has progress at %2 → vanilla restores last position", playerId, player.GetOrigin()), LogLevel.NORMAL);
		super.OnPlayerCharacterLoaded_S(statusCode, result, isLast, context);

		// SPAWN PROTECTION DISABLED on reconnect-with-progress.
		//
		// Original intent: 15s damage immunity covering weapon-load races, spawn-camping, etc.
		// Problem: EnableDamageHandling(false) on a char that has a persisted weapon corrupts
		// the WeaponManager + ActionsManager bindings. Players reported reload/inspect actions
		// permanently broken after reconnect. The toggle false→true triggers vanilla's internal
		// action-availability recheck out of sync with the weapon attachment replication, leaving
		// a stale "no action available" state on the equipped weapon.
		//
		// Fresh deploy-menu spawns (PostProcessSpawnedPlayer in BZ_SpawnPointSpawnHandlerComponent)
		// still get SpawnProtection with prefab inventory already initialized.
		//
		// To re-enable selectively: only apply if char has no equipped weapon at restore time.
	}

	//------------------------------------------------------------------------------------------------
	// Player lost their character (died, admin-deleted, etc.). Pass straight through to vanilla
	// SCR_MenuSpawnLogic — its native flow opens the deploy menu after m_fDeployMenuOpenDelay
	// (this already worked correctly before recent changes). Log only.
	override void OnPlayerEntityLost_S(int playerId)
	{
		// Capture state BEFORE entity is fully released. Critical for diagnostics: was player
		// alive when entity was lost? Killed by zombie? Drowned? Crashed vehicle?
		PlayerManager pm = GetGame().GetPlayerManager();
		string lostContext = "no_player_manager";
		if (pm)
		{
			IEntity lastEntity = pm.GetPlayerControlledEntity(playerId);
			if (lastEntity)
			{
				vector lastPos = lastEntity.GetOrigin();
				SCR_DamageManagerComponent lostDmg = SCR_DamageManagerComponent.GetDamageManager(lastEntity);
				float lostHP = -1;
				bool lostDestroyed = false;
				if (lostDmg)
				{
					lostHP = lostDmg.GetHealth();
					lostDestroyed = lostDmg.IsDestroyed();
				}
				ECharacterLifeState lostLifeState = ECharacterLifeState.ALIVE;
				CharacterControllerComponent lostCC = CharacterControllerComponent.Cast(lastEntity.FindComponent(CharacterControllerComponent));
				if (lostCC)
					lostLifeState = lostCC.GetLifeState();
				lostContext = string.Format("pos=%1, HP=%2, destroyed=%3, lifeState=%4", lastPos, lostHP, lostDestroyed, typename.EnumToString(ECharacterLifeState, lostLifeState));
			}
			else
			{
				lostContext = "controlled_entity_already_null (deleted between kill and entity-lost event)";
			}
		}
		Print(string.Format("[BrasilZ][EntityLost] Player %1 lost entity | %2 | auto-spawn flow active", playerId, lostContext), LogLevel.WARNING);
		Print(string.Format("[BrasilZ][AutoSpawn] Player %1 entity lost, no deploy menu | %2", playerId, lostContext), LogLevel.WARNING);

		if (BZ_AUTO_SPAWN_ENABLED)
		{
			if (s_aBzDeathRespawnPending.Contains(playerId))
			{
				Print(string.Format("[BrasilZ][AutoSpawn] Player %1 entity lost found stale random respawn pending - resetting and scheduling fresh respawn.", playerId), LogLevel.WARNING);
				BZ_ClearDeathRespawnState(playerId);
			}

			// Let vanilla move the controller into respawn-ready state. The deploy menu UI
			// stays blocked by BZ_DeployMenuBlocker, but RequestSpawn needs this setup.
			super.OnPlayerEntityLost_S(playerId);

			s_aBzDeathRespawnPending.Insert(playerId);
			// 1000ms delay — 100ms era curto demais, engine ainda processando death cleanup
			// (decouple, replication). Delay maior dá tempo de PlayerController despossuir
			// o corpo morto antes do RequestSpawn tentar possuir o char novo.
			GetGame().GetCallqueue().CallLater(BZ_AutoSpawnAfterDeath, BZ_DEATH_AUTOSPAWN_DELAY_MS, false, playerId);
			Print(string.Format("[BrasilZ][AutoSpawn] Player %1 entity lost - random respawn scheduled in %2ms.", playerId, BZ_DEATH_AUTOSPAWN_DELAY_MS), LogLevel.NORMAL);
			return;
		}

		super.OnPlayerEntityLost_S(playerId);
	}

	//------------------------------------------------------------------------------------------------
	// Callback do auto-respawn pós-morte. Chama DoSpawn_S diretamente — override já cuida do
	// resto (faction CIV, BZ_SpawnPoint random, loadout, RequestSpawn).
	protected void BZ_AutoSpawnAfterDeath(int playerId)
	{
		// Verifica se player ainda existe (não disconnected entre death e callback).
		PlayerController pc = GetGame().GetPlayerManager().GetPlayerController(playerId);
		if (!pc)
		{
			s_mBzDeathRespawnRetries.Remove(playerId);
			s_mBzDeathRespawnVerifyRetries.Remove(playerId);
			s_aBzDeathRespawnPending.Remove(playerId);
			Print(string.Format("[BrasilZ][AutoSpawn] Player %1 disconnected before delayed random respawn, abort.", playerId), LogLevel.NORMAL);
			return;
		}

		// Verifica se player tem char VIVO (não corpo decoupled). Após morte, vanilla mantém
		// PlayerControlledEntity mapping pro corpo destroyed até cleanup completar — não
		// devemos skip nesse caso, é exatamente o cenário que queremos auto-respawn.
		IEntity controlled = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
		if (controlled)
		{
			SCR_DamageManagerComponent dmg = SCR_DamageManagerComponent.GetDamageManager(controlled);
			bool alive = dmg && !dmg.IsDestroyed() && dmg.GetHealth() > 0;
			if (alive)
			{
				s_mBzDeathRespawnRetries.Remove(playerId);
				s_mBzDeathRespawnVerifyRetries.Remove(playerId);
				s_aBzDeathRespawnPending.Remove(playerId);
				Print(string.Format("[BrasilZ][AutoSpawn] Player %1 already has alive character, skip delayed random respawn.", playerId), LogLevel.NORMAL);
				return;
			}

			int retryCount = s_mBzDeathRespawnRetries.Get(playerId) + 1;
			s_mBzDeathRespawnRetries.Set(playerId, retryCount);
			if (retryCount >= BZ_DEATH_AUTOSPAWN_MAX_RETRIES)
			{
				// BZ FIX v3: mirror pause button flow (BZ_ForceRandomRespawnFromButton).
				// Pause button works because it CLEARS state fully + INSERTS fresh pending
				// flag before DoSpawn_S. Auto-spawn skip-delete path was missing the full
				// clear, leaving stale flags that engine reads as "spawn already pending"
				// → silently drops the new RequestRespawn. Replicate exact pattern here.
				SCR_BaseGameMode bzgmRetry = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
				if (bzgmRetry && bzgmRetry.BZ_IsTrackedCorpse(controlled))
				{
					Print(string.Format("[BrasilZ][RespawnFix] === BEGIN === Player %1 corpse tracked (30min loot). Applying pause-button-style spawn sequence.", playerId), LogLevel.NORMAL);

					// Step 1: clear ALL stale state (retries + verify counters + pending flag)
					BZ_ClearDeathRespawnState(playerId);
					Print(string.Format("[BrasilZ][RespawnFix] Player %1 step 1/3: state cleared (retries+verify+pending all reset).", playerId), LogLevel.NORMAL);

					// Step 2: insert FRESH pending flag (same as pause button L722)
					s_aBzDeathRespawnPending.Insert(playerId);
					s_mBzDeathRespawnVerifyRetries.Set(playerId, 0);
					Print(string.Format("[BrasilZ][RespawnFix] Player %1 step 2/3: fresh pending flag inserted, verify counter=0.", playerId), LogLevel.NORMAL);

					// Step 3: keep the tracked corpse. The spawn handler will explicitly
					// hand over the newly spawned character using SetInitialMainEntity,
					// matching the ReforgedZ/EPF pattern.
					Print(string.Format("[BrasilZ][RespawnFix] Player %1 step 3/3: tracked corpse preserved, calling DoSpawn_S.", playerId), LogLevel.NORMAL);
					DoSpawn_S(playerId);

					GetGame().GetCallqueue().CallLater(BZ_VerifyDeathRespawnCompleted, BZ_DEATH_AUTOSPAWN_VERIFY_MS, false, playerId);
					GetGame().GetCallqueue().CallLater(BZ_ClearDeathRespawnPending, BZ_DEATH_AUTOSPAWN_PENDING_CLEAR_MS + BZ_DEATH_AUTOSPAWN_VERIFY_MS, false, playerId);
					Print(string.Format("[BrasilZ][RespawnFix] Player %1 === END === verify scheduled in %2ms. Watch for [RespawnFix] SUCCESS or FAIL next.", playerId, BZ_DEATH_AUTOSPAWN_VERIFY_MS), LogLevel.NORMAL);
					return;
				}

				Print(string.Format("[BrasilZ][AutoSpawn] Player %1 still controls dead character after %2 retries - corpse NOT tracked, deleting stale controlled corpse before respawn.", playerId, retryCount), LogLevel.WARNING);
				BZ_DeleteStaleControlledCorpse(controlled, playerId);
				GetGame().GetCallqueue().CallLater(BZ_AutoSpawnAfterDeath, 100, false, playerId);
				return;
			}

			GetGame().GetCallqueue().CallLater(BZ_AutoSpawnAfterDeath, BZ_DEATH_AUTOSPAWN_RETRY_MS, false, playerId);
			Print(string.Format("[BrasilZ][AutoSpawn] Player %1 still controls dead character - retry %2/%3 in %4ms.", playerId, retryCount, BZ_DEATH_AUTOSPAWN_MAX_RETRIES, BZ_DEATH_AUTOSPAWN_RETRY_MS), LogLevel.NORMAL);
			return;
		}

		s_mBzDeathRespawnRetries.Remove(playerId);
		s_mBzDeathRespawnVerifyRetries.Set(playerId, 0);
		Print(string.Format("[BrasilZ][AutoSpawn] Player %1 - executing delayed random respawn.", playerId), LogLevel.NORMAL);
		DoSpawn_S(playerId);
		GetGame().GetCallqueue().CallLater(BZ_VerifyDeathRespawnCompleted, BZ_DEATH_AUTOSPAWN_VERIFY_MS, false, playerId);
		GetGame().GetCallqueue().CallLater(BZ_ClearDeathRespawnPending, BZ_DEATH_AUTOSPAWN_PENDING_CLEAR_MS + BZ_DEATH_AUTOSPAWN_VERIFY_MS, false, playerId);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_ClearDeathRespawnPending(int playerId)
	{
		s_aBzDeathRespawnPending.Remove(playerId);
		s_mBzDeathRespawnVerifyRetries.Remove(playerId);
	}

	//------------------------------------------------------------------------------------------------
	void BZ_ClearDeathRespawnState(int playerId)
	{
		BZ_ClearDeathRespawnStateStatic(playerId);
	}

	//------------------------------------------------------------------------------------------------
	static void BZ_ClearDeathRespawnStateStatic(int playerId)
	{
		s_aBzDeathRespawnPending.Remove(playerId);
		s_mBzDeathRespawnRetries.Remove(playerId);
		s_mBzDeathRespawnVerifyRetries.Remove(playerId);
	}

	//------------------------------------------------------------------------------------------------
	void BZ_ForceRandomRespawnFromButton(int playerId)
	{
		if (!Replication.IsServer())
			return;

		BZ_ClearDeathRespawnState(playerId);

		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return;

		PlayerController pc = pm.GetPlayerController(playerId);
		if (!pc)
			return;

		IEntity controlled = pm.GetPlayerControlledEntity(playerId);
		if (controlled)
		{
			SCR_DamageManagerComponent dmg = SCR_DamageManagerComponent.GetDamageManager(controlled);
			bool alive = dmg && !dmg.IsDestroyed() && dmg.GetHealth() > 0;
			if (alive)
			{
				Print(string.Format("[BrasilZ][AutoSpawn] Player %1 force respawn ignored - alive character still controlled.", playerId), LogLevel.NORMAL);
				return;
			}

			SCR_BaseGameMode bzgm = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
			if (bzgm && bzgm.BZ_IsTrackedCorpse(controlled))
			{
				Print(string.Format("[BrasilZ][PauseRespawn] Player %1 force respawn keeps tracked corpse lootable; spawn handler will hand over new character.", playerId), LogLevel.WARNING);
			}
			else
			{
				Print(string.Format("[BrasilZ][PauseRespawn] Player %1 force respawn deleting stale untracked corpse before spawn.", playerId), LogLevel.WARNING);
				BZ_DeleteStaleControlledCorpse(controlled, playerId);
			}
		}

		s_aBzDeathRespawnPending.Insert(playerId);
		s_mBzDeathRespawnVerifyRetries.Set(playerId, 0);
		Print(string.Format("[BrasilZ][AutoSpawn] Player %1 - force random respawn requested by pause button.", playerId), LogLevel.WARNING);
		DoSpawn_S(playerId);
		GetGame().GetCallqueue().CallLater(BZ_VerifyDeathRespawnCompleted, BZ_DEATH_AUTOSPAWN_VERIFY_MS, false, playerId);
		GetGame().GetCallqueue().CallLater(BZ_ClearDeathRespawnPending, BZ_DEATH_AUTOSPAWN_PENDING_CLEAR_MS + BZ_DEATH_AUTOSPAWN_VERIFY_MS, false, playerId);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_RequestRandomSpawnAndVerify(int playerId, string reason)
	{
		if (s_aBzDeathRespawnPending.Contains(playerId))
		{
			Print(string.Format("[BrasilZ][AutoSpawn] Player %1 random spawn request ignored - spawn already pending (%2).", playerId, reason), LogLevel.NORMAL);
			return;
		}

		s_aBzDeathRespawnPending.Insert(playerId);
		s_mBzDeathRespawnRetries.Remove(playerId);
		s_mBzDeathRespawnVerifyRetries.Set(playerId, 0);
		Print(string.Format("[BrasilZ][AutoSpawn] Player %1 - random spawn requested by recovery path (%2).", playerId, reason), LogLevel.WARNING);
		DoSpawn_S(playerId);
		GetGame().GetCallqueue().CallLater(BZ_VerifyDeathRespawnCompleted, BZ_DEATH_AUTOSPAWN_VERIFY_MS, false, playerId);
		GetGame().GetCallqueue().CallLater(BZ_ClearDeathRespawnPending, BZ_DEATH_AUTOSPAWN_PENDING_CLEAR_MS + BZ_DEATH_AUTOSPAWN_VERIFY_MS, false, playerId);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_AutoSpawnIfStillNoEntityOnConnect(int playerId)
	{
		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return;

		PlayerController pc = pm.GetPlayerController(playerId);
		if (!pc)
			return;

		IEntity controlled = pm.GetPlayerControlledEntity(playerId);
		if (controlled)
		{
			SCR_DamageManagerComponent dmg = SCR_DamageManagerComponent.GetDamageManager(controlled);
			bool alive = dmg && !dmg.IsDestroyed() && dmg.GetHealth() > 0;
			if (alive)
			{
				Print(string.Format("[BrasilZ][AutoSpawn] Player %1 connect rescue skipped - alive controlled entity exists.", playerId), LogLevel.NORMAL);
				return;
			}

			Print(string.Format("[BrasilZ][AutoSpawn] Player %1 connect rescue found dead controlled entity - deleting before spawn.", playerId), LogLevel.WARNING);
			BZ_DeleteStaleControlledCorpse(controlled, playerId);
		}

		BZ_RequestRandomSpawnAndVerify(playerId, "CONNECT_NO_ALIVE_CONTROLLED_ENTITY");
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_VerifyDeathRespawnCompleted(int playerId)
	{
		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return;

		PlayerController pc = pm.GetPlayerController(playerId);
		if (!pc)
		{
			BZ_ClearDeathRespawnPending(playerId);
			return;
		}

		IEntity controlled = pm.GetPlayerControlledEntity(playerId);

		// SUCCESS: player has alive controlled entity → respawn complete
		if (controlled)
		{
			SCR_DamageManagerComponent dmg = SCR_DamageManagerComponent.GetDamageManager(controlled);
			bool alive = dmg && !dmg.IsDestroyed() && dmg.GetHealth() > 0;
			if (alive)
			{
				s_mBzDeathRespawnRetries.Remove(playerId);
				BZ_ClearDeathRespawnPending(playerId);
				vector spawnPos = controlled.GetOrigin();
				int verifyTotal = s_mBzDeathRespawnVerifyRetries.Get(playerId);
				Print(string.Format("[BrasilZ][RespawnFix] === SUCCESS === Player %1 respawn verified with alive controlled entity at %2 (HP=%3, took %4 verify attempts).", playerId, spawnPos, dmg.GetHealth(), verifyTotal), LogLevel.NORMAL);
				return;
			}
		}

		int verifyCount = s_mBzDeathRespawnVerifyRetries.Get(playerId) + 1;
		s_mBzDeathRespawnVerifyRetries.Set(playerId, verifyCount);

		if (verifyCount > BZ_DEATH_AUTOSPAWN_VERIFY_MAX)
		{
			Print(string.Format("[BrasilZ][RespawnFix] === FAIL === Player %1 respawn verify failed after %2 retries (~%3s). Engine never released possession of dead corpse. Clearing pending — player must press ESC > Respawn manually.", playerId, BZ_DEATH_AUTOSPAWN_VERIFY_MAX, BZ_DEATH_AUTOSPAWN_VERIFY_MAX), LogLevel.ERROR);
			BZ_ClearDeathRespawnPending(playerId);
			return;
		}

		// Controlled state branches:
		// A) controlled == null → engine RELEASED possession. Fire DoSpawn_S NOW.
		// B) controlled is dead+tracked -> keep corpse and retry request; spawn handler performs explicit handover.
		// C) controlled is dead+NOT tracked -> safe to delete + retry.
		if (!controlled)
		{
			Print(string.Format("[BrasilZ][RespawnFix] Player %1 verify %2/%3 — controlled is NULL (engine released possession). Firing DoSpawn_S.", playerId, verifyCount, BZ_DEATH_AUTOSPAWN_VERIFY_MAX), LogLevel.NORMAL);
			DoSpawn_S(playerId);
		}
		else
		{
			SCR_BaseGameMode bzgmVerify = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
			bool isTracked = bzgmVerify && bzgmVerify.BZ_IsTrackedCorpse(controlled);
			if (isTracked)
			{
				Print(string.Format("[BrasilZ][RespawnFix] Player %1 verify %2/%3 - still controls tracked dead corpse, retrying DoSpawn_S; corpse stays lootable.", playerId, verifyCount, BZ_DEATH_AUTOSPAWN_VERIFY_MAX), LogLevel.WARNING);
				DoSpawn_S(playerId);
				GetGame().GetCallqueue().CallLater(BZ_VerifyDeathRespawnCompleted, BZ_DEATH_AUTOSPAWN_VERIFY_MS, false, playerId);
				return;
			}
			else
			{
				Print(string.Format("[BrasilZ][AutoSpawn] Player %1 verify %2/%3 — dead controlled NOT tracked, deleting + firing DoSpawn_S.", playerId, verifyCount, BZ_DEATH_AUTOSPAWN_VERIFY_MAX), LogLevel.WARNING);
				BZ_DeleteStaleControlledCorpse(controlled, playerId);
				DoSpawn_S(playerId);
			}
		}

		GetGame().GetCallqueue().CallLater(BZ_VerifyDeathRespawnCompleted, BZ_DEATH_AUTOSPAWN_VERIFY_MS, false, playerId);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_DeleteStaleControlledCorpse(IEntity corpse, int playerId)
	{
		if (!corpse || corpse.IsDeleted())
			return;

		SCR_BaseGameMode bzgm = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
		bool trackedCorpse = bzgm && bzgm.BZ_IsTrackedCorpse(corpse);
		if (trackedCorpse)
		{
			Print(string.Format("[BrasilZ][RespawnFix] Player %1 stale controlled entity is tracked corpse; keeping it for loot window.", playerId), LogLevel.WARNING);
			return;
		}

		RplComponent rpl = RplComponent.Cast(corpse.FindComponent(RplComponent));
		if (rpl)
		{
			RplComponent.DeleteRplEntity(corpse, false);
			Print(string.Format("[BrasilZ][AutoSpawn] Player %1 stale controlled corpse deleted via RplComponent.", playerId), LogLevel.WARNING);
			return;
		}

		SCR_EntityHelper.DeleteEntityAndChildren(corpse);
		Print(string.Format("[BrasilZ][AutoSpawn] Player %1 stale controlled corpse deleted via EntityHelper fallback.", playerId), LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	// AUTO-SPAWN OVERRIDE (skip deploy menu).
	// Vanilla DoSpawn_S calls NotifyReadyForSpawn_S which opens deploy menu on client.
	// Override: auto-spawn directamente em random BZ_SpawnPoint com faction CIV + loadout.
	// Cada chamada (init spawn + respawn pós-morte) é interceptada → sem menu.
	//
	// Fallback: se BZ_SpawnPoint não encontrar ponto, ou loadout faltar, cai pra vanilla.
	override protected void DoSpawn_S(int playerId)
	{
		if (!BZ_AUTO_SPAWN_ENABLED)
		{
			super.DoSpawn_S(playerId);
			return;
		}

		Faction forcedFaction;
		if (GetForcedFaction(forcedFaction))
		{
			SCR_PlayerFactionAffiliationComponent pfa = GetPlayerFactionComponent_S(playerId);
			if (pfa)
				pfa.RequestFaction(forcedFaction);
		}

		SCR_PlayerFactionAffiliationComponent factionComp = GetPlayerFactionComponent_S(playerId);
		Faction faction;
		if (factionComp)
			faction = factionComp.GetAffiliatedFaction();
		if (!faction && forcedFaction)
			faction = forcedFaction;

		if (!faction)
		{
			Print(string.Format("[BrasilZ][AutoSpawn] Player %1 - sem faction setada, abortando auto-spawn para evitar deploy menu.", playerId), LogLevel.WARNING);
			return;
		}

		BZ_RespawnSystemComponent bzRespawn = BZ_RespawnSystemComponent.Cast(m_RespawnSystem);
		if (!bzRespawn)
		{
			Print(string.Format("[BrasilZ][AutoSpawn] Player %1 - BZ_RespawnSystemComponent ausente, abortando auto-spawn.", playerId), LogLevel.WARNING);
			return;
		}

		ResourceName prefab = bzRespawn.GetDefaultCharacterPrefab();
		if (prefab.IsEmpty())
		{
			Print(string.Format("[BrasilZ][AutoSpawn] Player %1 - sem prefab de personagem configurado, abortando auto-spawn.", playerId), LogLevel.WARNING);
			return;
		}

		BZ_SpawnPoint spawnPoint = BZ_SpawnPoint.GetRandomSpawnPoint();
		if (!spawnPoint)
		{
			Print(string.Format("[BrasilZ][AutoSpawn] Player %1 - sem BZ_SpawnPoint disponivel, abortando auto-spawn.", playerId), LogLevel.WARNING);
			return;
		}

		if (BZ_DirectSpawnAtPoint(playerId, prefab, spawnPoint))
		{
			Print(string.Format("[BrasilZ][AutoSpawn] Player %1 -> direct random spawn at %2 (faction=%3, prefab=%4).", playerId, spawnPoint.GetSpawnPointName(), faction.GetFactionKey(), prefab), LogLevel.NORMAL);
			return;
		}

		bzRespawn.RequestSpawnAtPointWithPrefab(playerId, prefab, spawnPoint);
		Print(string.Format("[BrasilZ][AutoSpawn] Player %1 -> fallback RequestRespawn at %2 (faction=%3, prefab=%4).", playerId, spawnPoint.GetSpawnPointName(), faction.GetFactionKey(), prefab), LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	protected bool BZ_DirectSpawnAtPoint(int playerId, ResourceName prefab, BZ_SpawnPoint spawnPoint)
	{
		if (playerId <= 0 || prefab.IsEmpty() || !spawnPoint)
			return false;

		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return false;

		SCR_PlayerController playerController = SCR_PlayerController.Cast(pm.GetPlayerController(playerId));
		if (!playerController)
			return false;

		Resource resource = Resource.Load(prefab);
		if (!resource || !resource.IsValid())
		{
			Print(string.Format("[BrasilZ][AutoSpawn] Player %1 direct spawn failed: invalid prefab %2", playerId, prefab), LogLevel.ERROR);
			return false;
		}

		vector pos, ypr;
		spawnPoint.GetPosYPR(pos, ypr);

		vector transform[4];
		Math3D.AnglesToMatrix(ypr, transform);
		transform[3] = pos;

		EntitySpawnParams spawnParams();
		spawnParams.TransformMode = ETransformMode.WORLD;
		for (int i = 0; i < 4; i++)
			spawnParams.Transform[i] = transform[i];

		IEntity spawnedEntity = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), spawnParams);
		if (!spawnedEntity)
		{
			Print(string.Format("[BrasilZ][AutoSpawn] Player %1 direct spawn failed: SpawnEntityPrefab returned null.", playerId), LogLevel.ERROR);
			return false;
		}

		IEntity previous = pm.GetPlayerControlledEntity(playerId);
		playerController.SetInitialMainEntity(spawnedEntity);

		SCR_BaseGameMode gameMode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
		if (gameMode)
			gameMode.OnPlayerEntityChanged_S(playerId, previous, spawnedEntity);

		SCR_RespawnComponent respawn = SCR_RespawnComponent.Cast(playerController.GetRespawnComponent());
		if (respawn)
			respawn.NotifySpawn(spawnedEntity);

		BZ_PortalSessionTracker.MarkLifeStart(playerId);

		if (BZ_PortalConfig.LOG_SPAWN)
		{
			string name = pm.GetPlayerName(playerId);
			if (name.IsEmpty())
				name = string.Format("Player %1", playerId);

			int walletTotal, looseTotal, grandTotal;
			BZ_DiscordWebhook.GetPlayerBalanceDetailed(spawnedEntity, walletTotal, looseTotal, grandTotal);

			string data = "{";
			data += "\"player\":" + BZ_PortalWebhook.PlayerJson(playerId, name, spawnedEntity) + ",";
			data += "\"prefab\":" + BZ_PortalWebhook.JsonString(prefab) + ",";
			data += "\"spawn_point\":" + BZ_PortalWebhook.JsonString(spawnPoint.GetSpawnPointName()) + ",";
			data += "\"position\":" + BZ_PortalWebhook.VectorJson(pos) + ",";
			data += "\"balance\":" + BZ_PortalWebhook.BalanceJson(walletTotal, looseTotal, grandTotal);
			data += "}";
			BZ_PortalWebhook.SendEvent("player_spawned", data);
		}

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

		Print(string.Format("[BrasilZ][RespawnFix] Player %1 direct spawn handover complete at %2 (previous=%3, new=%4).", playerId, pos, previous, spawnedEntity), LogLevel.WARNING);
		return true;
	}
	//------------------------------------------------------------------------------------------------
	// Deferred surface lift for players who disconnected while submerged. Moves the entity
	// to the explicit surface position (X/Z preserved, Y forced above the waterline) and
	// re-enables damage handling.
	protected void BZ_LiftToSurfaceAfterPossess(IEntity entity, vector surfacePos)
	{
		if (!entity || entity.IsDeleted())
			return;

		BaseGameEntity bgEntity = BaseGameEntity.Cast(entity);
		if (bgEntity)
		{
			vector transform[4];
			bgEntity.GetWorldTransform(transform);
			transform[3] = surfacePos;
			bgEntity.Teleport(transform);
		}
		else
		{
			entity.SetOrigin(surfacePos);
		}

		SCR_CharacterDamageManagerComponent charDmg = SCR_CharacterDamageManagerComponent.Cast(entity.FindComponent(SCR_CharacterDamageManagerComponent));
		if (charDmg)
			charDmg.EnableDamageHandling(true);

		Print(string.Format("[BrasilZ] Surface rescue: %1 (damage re-enabled)", surfacePos), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	// Deferred lift fired by callqueue after vanilla possess has completed. The entity is
	// already attached to the controlling player at the buried Y; we move it back up by the
	// underground offset and re-enable damage so the player picks up at their pre-bury Y
	// with normal damage handling.
	protected void BZ_LiftAfterPossess(IEntity entity, vector buriedPos)
	{
		if (!entity || entity.IsDeleted())
			return;

		const float UNDERGROUND_OFFSET = 1000.0;
		vector liftPos = buriedPos;
		liftPos[1] = liftPos[1] + UNDERGROUND_OFFSET;

		// DEEP-VOID RECOVERY for legacy saves corrupted by the old sink-to-Y=-3000 BootScan.
		// Player saves got persisted at Y=-3000 before that bug was patched. Standard
		// +1000 lift leaves Y=-2000 still underground. Force-lift to terrain surface.
		const float BZ_DEEP_VOID_RECOVERY_Y = -1500.0;
		if (buriedPos[1] < BZ_DEEP_VOID_RECOVERY_Y)
		{
			BaseWorld bw = GetGame().GetWorld();
			if (bw)
			{
				float terrainY = bw.GetSurfaceY(buriedPos[0], buriedPos[2]);
				liftPos[1] = terrainY + 2.0;
				Print(string.Format("[BrasilZ][SpawnLoad] DEEP-VOID RECOVERY — buried Y=%1 (< %2), forcing lift to surface Y=%3 + 2.0 clearance", buriedPos[1], BZ_DEEP_VOID_RECOVERY_Y, terrainY), LogLevel.WARNING);
			}
		}

		BaseGameEntity bgEntity = BaseGameEntity.Cast(entity);
		if (bgEntity)
		{
			vector transform[4];
			bgEntity.GetWorldTransform(transform);
			transform[3] = liftPos;
			bgEntity.Teleport(transform);
		}
		else
		{
			entity.SetOrigin(liftPos);
		}

		SCR_CharacterDamageManagerComponent charDmg = SCR_CharacterDamageManagerComponent.Cast(entity.FindComponent(SCR_CharacterDamageManagerComponent));
		if (charDmg)
			charDmg.EnableDamageHandling(true);

		Print(string.Format("[BrasilZ] Deferred lift: %1 → %2 (damage re-enabled)", buriedPos, liftPos), LogLevel.NORMAL);
	}
}
