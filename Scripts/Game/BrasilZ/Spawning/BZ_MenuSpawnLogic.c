// Rebuild marker: forces Workbench to detect source change and repackage the .pak.
// Bump the comment whenever the published addon needs a refresh on disk.
// Last bump: 2026-05-20 — restore vanilla spawn/menu flow (commit 4406fc6).
class BZ_MenuSpawnLogic : SCR_MenuSpawnLogic
{
	protected static const int MAX_PERSISTENCE_ACTIVE_WAIT_MS = 30000;
	protected static const int PERSISTENCE_ACTIVE_CHECK_INTERVAL_MS = 100;

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
		m_fDeployMenuOpenDelay = 4.0;
	}

	//------------------------------------------------------------------------------------------------
	override void OnPlayerRegistered_S(int playerId)
	{
		m_mPersistenceWaitTime.Remove(playerId);
		super.OnPlayerRegistered_S(playerId);
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
						Print(string.Format("[BrasilZ] Player %1 (UID %2) has stale death flag — character is alive (health=%3). Clearing flag and restoring.", playerId, uid, flagDmg.GetHealth()), LogLevel.WARNING);
						registry.ClearDead(uid);
						registry.ClearDeadBody(playerId);
					}
					else
					{
						Print(string.Format("[BrasilZ] Player %1 (UID %2) has persisted death flag and character is dead — rejecting character", playerId, uid), LogLevel.NORMAL);
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
				Print(string.Format("[BrasilZ] Player %1 character has health<=0 (lifeState=%2) — rejecting", playerId, typename.EnumToString(ECharacterLifeState, lifeState)), LogLevel.NORMAL);
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
				Print(string.Format("[BrasilZ] Deleting stale persisted entity for dead player %1", playerId), LogLevel.WARNING);
				RplComponent.DeleteRplEntity(loadedEntity, false);
			}

			Print(string.Format("[BrasilZ] Player %1 has no progress → vanilla opens deploy menu (delay=%2s)", playerId, m_fDeployMenuOpenDelay), LogLevel.NORMAL);
			// Forward with null result; vanilla branches on (result == null) to open the menu.
			// Keep original statusCode — EPersistenceStatusCode enum has only OK in this SDK.
			super.OnPlayerCharacterLoaded_S(statusCode, null, isLast, context);
			return;
		}

		// Has progress → forward original args to vanilla, which possesses the saved character
		// at its last position. Do NOT call RequestSpawn manually — vanilla does it correctly.
		Print(string.Format("[BrasilZ] Player %1 has progress at %2 → vanilla restores last position", playerId, player.GetOrigin()), LogLevel.NORMAL);
		super.OnPlayerCharacterLoaded_S(statusCode, result, isLast, context);
	}

	//------------------------------------------------------------------------------------------------
	// Player lost their character (died, admin-deleted, etc.). Pass straight through to vanilla
	// SCR_MenuSpawnLogic — its native flow opens the deploy menu after m_fDeployMenuOpenDelay
	// (this already worked correctly before recent changes). Log only.
	override void OnPlayerEntityLost_S(int playerId)
	{
		Print(string.Format("[BrasilZ] Player %1 entity lost → vanilla deploy menu opens in %2s", playerId, m_fDeployMenuOpenDelay), LogLevel.NORMAL);
		super.OnPlayerEntityLost_S(playerId);
	}
}
