modded class SCR_ReconnectComponent
{
	//------------------------------------------------------------------------------------------------
	// Block reconnect if the persisted entity is dead/incapacitated/health<=0 or if the UID has
	// a death flag set (anti-ALT+F4 exploit). Mirrors ReforgedZ behaviour.
	override SCR_EReconnectState GetReconnectState(int playerId)
	{
		SCR_EReconnectState baseState = super.GetReconnectState(playerId);
		if (baseState != SCR_EReconnectState.ENTITY_AVAILABLE)
			return baseState;

		string uid = BZ_Utils.GetPlayerUID(playerId);
		if (!uid.IsEmpty())
		{
			BZ_PlayerDeathRegistry registry = BZ_PlayerDeathRegistry.GetInstance();
			if (registry && registry.IsDeadByUID(uid))
			{
				Print(string.Format("[BrasilZ] Reconnect blocked: UID %1 has death flag — discarding entity", uid), LogLevel.WARNING);
				return SCR_EReconnectState.ENTITY_DISCARDED;
			}
		}

		foreach (SCR_ReconnectData data : m_ReconnectPlayerList)
		{
			if (data.m_iPlayerId != playerId)
				continue;

			ChimeraCharacter character = ChimeraCharacter.Cast(data.m_ReservedEntity);
			if (character)
			{
				CharacterControllerComponent charCtrl = character.GetCharacterController();
				if (charCtrl && charCtrl.GetLifeState() == ECharacterLifeState.INCAPACITATED)
				{
					Print(string.Format("[BrasilZ] Reconnect blocked: player %1 INCAPACITATED — discarding entity", playerId), LogLevel.WARNING);
					return SCR_EReconnectState.ENTITY_DISCARDED;
				}

				SCR_DamageManagerComponent dmgMgr = SCR_DamageManagerComponent.GetDamageManager(character);
				if (dmgMgr && (dmgMgr.IsDestroyed() || dmgMgr.GetHealth() <= 0))
				{
					Print(string.Format("[BrasilZ] Reconnect blocked: player %1 health<=0 — discarding entity", playerId), LogLevel.WARNING);
					return SCR_EReconnectState.ENTITY_DISCARDED;
				}
			}

			break;
		}

		return SCR_EReconnectState.ENTITY_AVAILABLE;
	}

	//------------------------------------------------------------------------------------------------
	// On audit timeout, flush the reserved entity to disk and remove it so it doesn't linger.
	override protected void OnPlayerAuditTimeouted(int playerId)
	{
		if (m_ReconnectPlayerList.IsEmpty())
			return;

		int count = m_ReconnectPlayerList.Count();
		for (int i = 0; i < count; i++)
		{
			if (m_ReconnectPlayerList[i].m_iPlayerId != playerId)
				continue;

			IEntity entity = m_ReconnectPlayerList[i].m_ReservedEntity;
			m_ReconnectPlayerList.Remove(i);

			if (entity)
				SaveAndRemoveCharacter(entity, playerId, 0);

			return;
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void SaveAndRemoveCharacter(IEntity entity, int playerId, int attempt)
	{
		if (!entity)
			return;

		ChimeraCharacter character = ChimeraCharacter.Cast(entity);
		if (character)
		{
			CharacterControllerComponent charController = character.GetCharacterController();
			if (charController && charController.IsDead())
			{
				SCR_PersistenceSystem deadPersist = SCR_PersistenceSystem.GetByEntityWorld(entity);
				if (deadPersist && deadPersist.GetState() == EPersistenceSystemState.ACTIVE)
					deadPersist.Save(entity, ESaveGameType.AUTO);
				return;
			}
		}

		SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByEntityWorld(entity);
		if (!persistence || persistence.GetState() != EPersistenceSystemState.ACTIVE)
		{
			if (attempt >= 10)
			{
				RplComponent.DeleteRplEntity(entity, false);
				return;
			}

			GetGame().GetCallqueue().CallLater(SaveAndRemoveCharacter, 500, false, entity, playerId, attempt + 1);
			return;
		}

		UUID charId = persistence.GetId(entity);
		if (!charId)
		{
			Print(string.Format("[BrasilZ] SaveAndRemoveCharacter: charId NULL for player %1, deleting", playerId), LogLevel.ERROR);
			RplComponent.DeleteRplEntity(entity, false);
			return;
		}

		persistence.Save(entity, ESaveGameType.AUTO);
		persistence.ReleaseTracking(entity);

		SaveGameManager saveManager = GetGame().GetSaveGameManager();
		if (saveManager && saveManager.IsSavingPossible())
			BZ_GameMode.OverwriteLatestSave(saveManager);

		RplComponent.DeleteRplEntity(entity, false);
		Print(string.Format("[BrasilZ] SaveAndRemoveCharacter: deleted entity for player %1 charId %2", playerId, charId), LogLevel.NORMAL);
	}
}
