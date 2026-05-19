class BZ_MenuSpawnLogic : SCR_MenuSpawnLogic
{
	protected static const int MAX_PERSISTENCE_ACTIVE_WAIT_MS = 30000;
	protected static const int PERSISTENCE_ACTIVE_CHECK_INTERVAL_MS = 100;

	protected ref map<int, int> m_mPersistenceWaitTime = new map<int, int>();

	//------------------------------------------------------------------------------------------------
	void BZ_MenuSpawnLogic()
	{
		// Controller SAVE keeps the player slot so persistence reloads them on reconnect.
		// Character DELETE removes the live body from the world on alive disconnect so it
		// doesn't sit visible until the SCR_ReconnectComponent audit timeout (the prefab
		// doesn't ship that component, so without DELETE the body would linger forever).
		// Dead/INCAPACITATED disconnect is intercepted in BZ_GameMode.OnPlayerDisconnected
		// BEFORE super: decouple gives the corpse an independent persistence ID and skips
		// super entirely, so the lootable body stays in world regardless of this setting.
		m_eDisconnectPlayerControllerBehaviour = SCR_ESpawnLogicDisconnectBehaviour.SAVE;
		m_eDisconnectCharacterBehaviour = SCR_ESpawnLogicDisconnectBehaviour.DELETE;
		m_sForcedFaction = "CIV";
		m_bWaitForSpawnPoints = true;
		m_fDeployMenuOpenDelay = 4.0;
	}

	//------------------------------------------------------------------------------------------------
	override void OnPlayerRegistered_S(int playerId)
	{
		m_mPersistenceWaitTime.Remove(playerId);

		// If this player still has a lingering disconnect body waiting to expire,
		// remove it now so they don't reconnect alongside a duplicate corpse.
		BZ_GameMode gm = BZ_GameMode.GetInstance();
		if (gm)
			gm.CancelPendingBodyDelete(playerId);

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
	override protected void OnPlayerCharacterLoaded_S(EPersistenceStatusCode statusCode, Managed result, bool isLast, Managed context)
	{
		Tuple1<int> playerDataContext = Tuple1<int>.Cast(context);
		int playerId = playerDataContext.param1;

		BaseGameEntity player = BaseGameEntity.Cast(result);
		BaseGameEntity loadedEntity = player;
		bool rejectedAsDead = false;

		// Death flag persisted in $profile:BrasilZ/Deaths — survives server restart
		if (player)
		{
			string uid = BZ_Utils.GetPlayerUID(playerId);
			if (!uid.IsEmpty())
			{
				BZ_PlayerDeathRegistry registry = BZ_PlayerDeathRegistry.GetInstance();
				if (registry && registry.IsDeadByUID(uid))
				{
					Print(string.Format("[BrasilZ] Player %1 (UID %2) has persisted death flag — rejecting character", playerId, uid), LogLevel.NORMAL);
					player = null;
					rejectedAsDead = true;
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

			bool lifeStateDead = (lifeState == ECharacterLifeState.DEAD || lifeState == ECharacterLifeState.INCAPACITATED);
			bool healthDead = destroyed || (dmgMgr && health <= 0);

			if (healthDead)
			{
				Print(string.Format("[BrasilZ] Player %1 character has health<=0 (lifeState=%2) — rejecting", playerId, typename.EnumToString(ECharacterLifeState, lifeState)), LogLevel.NORMAL);
				player = null;
				rejectedAsDead = true;
			}
			else if (lifeStateDead)
			{
				// Health is healthy but lifeState says dead — likely uninitialized state, ignore and accept.
				Print(string.Format("[BrasilZ] Player %1 lifeState=%2 with health=%3 — treating as transient init state, accepting", playerId, typename.EnumToString(ECharacterLifeState, lifeState), health), LogLevel.WARNING);
			}
		}

		// Near-origin position fix (0,0,0 bug after persistence corruption)
		if (player)
		{
			vector pos = player.GetOrigin();
			if (pos[0] < 10 && pos[0] > -10 && pos[2] < 10 && pos[2] > -10)
			{
				Print(string.Format("[BrasilZ] Player %1 loaded at invalid position %2 — relocating to spawn", playerId, pos), LogLevel.WARNING);
				BZ_SpawnPoint spawnPoint = BZ_SpawnPoint.GetRandomSpawnPoint();
				if (spawnPoint)
				{
					vector spawnPos, spawnYpr;
					spawnPoint.GetPosYPR(spawnPos, spawnYpr);
					if (spawnPos[0] > 10 || spawnPos[0] < -10 || spawnPos[2] > 10 || spawnPos[2] < -10)
					{
						vector transform[4];
						player.GetWorldTransform(transform);
						transform[3] = spawnPos;
						player.Teleport(transform);
					}
				}
			}
		}

		if (!player)
		{
			// Delete stale persisted entity so it doesn't linger in the world
			if (rejectedAsDead && loadedEntity)
			{
				Print(string.Format("[BrasilZ] Deleting stale persisted entity for dead player %1", playerId), LogLevel.WARNING);
				RplComponent.DeleteRplEntity(loadedEntity, false);
			}

			DoInitialSpawn_S(playerId);
			return;
		}

		SCR_PossessSpawnData data = SCR_PossessSpawnData.FromEntity(player);
		data.SetSkipPreload(false);
		GetPlayerRespawnComponent_S(playerId).RequestSpawn(data);
	}
}
