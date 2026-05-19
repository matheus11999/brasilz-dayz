// BrasilZ tuning for Ambient Zombies (UNDS): slower, farther, less wave pressure.
modded class UNDS_ZombieSpawnerSystem
{
	protected static const vector BZ_SAFEZONE_CENTER = "5403.675 299.438 5634.716";
	protected static const float BZ_SAFEZONE_ZOMBIE_EXCLUSION_RADIUS = 260.0;

	void UNDS_ZombieSpawnerSystem()
	{
		m_iCheckIntervalMs = 4000;
		m_iCleanupIntervalMs = 5000;
		m_iSpawnProtectionMs = 5000;
		m_iMinSpawnCooldownMs = 10000;
		m_iInitialSeedDelayMs = 3000;

		m_fHiveGainPerKill = 0.004;
		m_fHiveDecayRate = 0.025;
		m_iHiveDecayDelayMs = 20000;
		m_fHiveDropOnDeath = 1.0;
		m_fHiveGainRamp = 1.2;
		m_fHiveDecayAcceleration = 3.0;
		m_iHiveTempoWindowMs = 20000;
		m_fHiveMinTempoMult = 0.1;
		m_fHiveSoftCapStart = 0.25;
		m_fHiveSoftCapMult = 0.05;
		m_fHiveMaxGainPerCleanup = 0.015;

		m_fAmbientMinDist = 190;
		m_fAmbientMaxDist = 330;
		m_fPresenceDistReduction = 0;
		m_fPresenceScanRadius = 35;
		m_fTravelAmbientMinSpeed = 4.5;
		m_iSeedClusterSize = 2;
		m_iAmbientMinSize = 2;
		m_iAmbientMaxSize = 3;
		m_fAmbientChance = 1.0;

		m_fSurgeHiveThreshold = 0.95;
		m_fSurgeChance = 0.05;
		m_fSurgeMinDist = 240;
		m_fSurgeMaxDist = 380;
		m_iSurgeMinSize = 3;
		m_iSurgeMaxSize = 4;

		m_iDesiredClustersPerPlayer = 3;
		m_iMaxClustersPerPlayer = 4;
		m_iMaxClusters = 16;
		m_fCleanupDistance = 520;

		m_fLingerRadius = 45;
		m_iWaveIntervalMs = 1800000;
		m_iMaxWavesPerEvent = 0;
		m_iWaveBaseSize = 1;
		m_iWaveSizeGrowth = 0;
		m_fWaveSpawnDist = 260;
		m_iWaveSequenceCooldownMs = 1800000;
		m_iEncountersClearedForWave = 999999;

		m_fNearbyThreatRadius = 100;
		m_iNearbyAliveMax = 7;
		m_fClusterSpread = 28;
		m_fFOVSpawnBlockAngle = 90.0;

		m_fMediumHiveThreshold = 0.65;
		m_fMediumChanceMax = 0.12;
		m_fExploderHiveThreshold = 0.98;
		m_fExploderChanceMax = 0.01;
		m_fLingerEstablishedMs = 45000;
		m_fLingerSpawnSuppressRadius = 180;
		m_bVerboseLogging = true;

		Print("[BrasilZ][UNDS] Ambient zombie tuning loaded: ambient=190-330m chance=1.0 clusters=3/4 seed=2 size=2-3 spread=28 nearbyMax=7 waveOff=true safezoneExclusion=260m");
	}

	override protected void BuildPrefabList()
	{
		m_aZombiePrefabsCommon = {};
		m_aZombieMedium = {};
		m_aZombieArmed = {};
		m_aZombieExploder = {};

		array<ResourceName> brasilZSoft = {
			"{B701000000000001}Prefabs/Characters/Zombies/BrasilZ_Zombie_Civilian_01.et",
			"{B701000000000002}Prefabs/Characters/Zombies/BrasilZ_Zombie_Civilian_02.et",
			"{B701000000000003}Prefabs/Characters/Zombies/BrasilZ_Zombie_Civilian_03.et",
			"{B701000000000004}Prefabs/Characters/Zombies/BrasilZ_Zombie_Civilian_04.et",
			"{B701000000000005}Prefabs/Characters/Zombies/BrasilZ_Zombie_Civilian_05.et",
			"{B701000000000006}Prefabs/Characters/Zombies/BrasilZ_Zombie_Civilian_06.et"
		};

		foreach (ResourceName prefab : brasilZSoft)
		{
			Resource resource = Resource.Load(prefab);
			if (resource)
			{
				m_aZombiePrefabsCommon.Insert(prefab);
				continue;
			}

			Print(string.Format("[BrasilZ][UNDS] Could not load zombie prefab: %1", prefab), LogLevel.ERROR);
		}

		Print(string.Format("[BrasilZ][UNDS] Prefabs: %1 BrasilZ civilian clothing-only zombies bacon-ai", m_aZombiePrefabsCommon.Count()), LogLevel.NORMAL);
	}

	override protected void CleanupCheck()
	{
		if (RplSession.Mode() == RplMode.Client)
			return;

		PlayerManager playerMgr = GetGame().GetPlayerManager();
		if (!playerMgr)
			return;

		float worldTime = GetGame().GetWorld().GetWorldTime();
		float combatRangeSq = m_fCleanupDistance * m_fCleanupDistance * 0.25;
		float cleanupDistSq = m_fCleanupDistance * m_fCleanupDistance;

		array<int> playerIds = {};
		playerMgr.GetPlayers(playerIds);
		array<vector> playerPositions = {};
		foreach (int pid : playerIds)
		{
			IEntity pEntity = playerMgr.GetPlayerControlledEntity(pid);
			if (IsAlivePlayer(pEntity))
				playerPositions.Insert(pEntity.GetOrigin());
		}

		for (int i = m_aClusters.Count() - 1; i >= 0; i--)
		{
			UNDS_ClusterInfo info = m_aClusters[i];

			if (IsInsideBrasilZSafezoneExclusion(info.m_vCenterPos))
			{
				DeleteClusterEntities(info);
				m_aClusters.Remove(i);
				Print("[UNDS] Safezone exclusion removed zombie cluster near trader hub", LogLevel.NORMAL);
				continue;
			}

			for (int safezoneIndex = info.m_aEntities.Count() - 1; safezoneIndex >= 0; safezoneIndex--)
			{
				IEntity safezoneZombie = info.m_aEntities[safezoneIndex];
				if (!safezoneZombie)
					continue;

				if (IsInsideBrasilZSafezoneExclusion(safezoneZombie.GetOrigin()))
				{
					DeleteZombieEntity(safezoneZombie);
					info.m_aEntities.Remove(safezoneIndex);
				}
			}

			int prevCount = info.m_aEntities.Count();
			for (int j = info.m_aEntities.Count() - 1; j >= 0; j--)
			{
				IEntity zombie = info.m_aEntities[j];
				if (!zombie)
				{
					info.m_aEntities.Remove(j);
					continue;
				}

				SCR_DamageManagerComponent dmg = SCR_DamageManagerComponent.Cast(zombie.FindComponent(SCR_DamageManagerComponent));
				if (dmg && dmg.IsDestroyed())
				{
					info.m_aEntities.Remove(j);
					continue;
				}
			}

			int kills = prevCount - info.m_aEntities.Count();
			if (kills > 0)
			{
				foreach (int combatPid : playerIds)
				{
					IEntity combatEnt = playerMgr.GetPlayerControlledEntity(combatPid);
					if (!combatEnt)
						continue;

					if (vector.DistanceSq(info.m_vCenterPos, combatEnt.GetOrigin()) < combatRangeSq)
					{
						UNDS_PlayerState pState;
						if (m_mPlayerStates.Find(combatPid, pState))
						{
							pState.m_fLastCombatTime = worldTime;
							RegisterKillsForPlayer(pState, kills);
							Print(string.Format("[UNDS] Player %1 hive after %2 kills: %3", combatPid, kills, pState.m_fHiveLevel), LogLevel.NORMAL);
						}
					}
				}
			}

			if (info.m_aEntities.IsEmpty())
			{
				float clearRadiusSq = m_fNearbyThreatRadius * m_fNearbyThreatRadius;
				foreach (int clearPid : playerIds)
				{
					IEntity clearEnt = playerMgr.GetPlayerControlledEntity(clearPid);
					if (!clearEnt)
						continue;

					if (vector.DistanceSq(info.m_vCenterPos, clearEnt.GetOrigin()) < clearRadiusSq)
					{
						UNDS_PlayerState clearState;
						if (m_mPlayerStates.Find(clearPid, clearState))
							clearState.m_iEncountersCleared = clearState.m_iEncountersCleared + 1;
					}
				}

				m_aClusters.Remove(i);
				continue;
			}

			if (!info.m_aEntities.IsEmpty())
			{
				IEntity lead = info.m_aEntities[0];
				if (lead)
					info.m_vCenterPos = lead.GetOrigin();
				else
					Print("[UNDS] Warning: Lead entity in cluster is null.", LogLevel.WARNING);
			}

			bool withinRange = false;
			foreach (vector pPos : playerPositions)
			{
				if (vector.DistanceSq(info.m_vCenterPos, pPos) < cleanupDistSq)
				{
					withinRange = true;
					break;
				}
			}

			if (!withinRange)
			{
				DeleteClusterEntities(info);
				m_aClusters.Remove(i);
			}
		}
	}

	protected bool IsInsideBrasilZSafezoneExclusion(vector position)
	{
		float radiusSq = BZ_SAFEZONE_ZOMBIE_EXCLUSION_RADIUS * BZ_SAFEZONE_ZOMBIE_EXCLUSION_RADIUS;
		return vector.DistanceSq(position, BZ_SAFEZONE_CENTER) <= radiusSq;
	}

	protected void DeleteClusterEntities(UNDS_ClusterInfo info)
	{
		foreach (IEntity member : info.m_aEntities)
		{
			if (!member)
				continue;

			DeleteZombieEntity(member);
		}
	}

	protected void DeleteZombieEntity(IEntity zombie)
	{
		RplComponent rpl = RplComponent.Cast(zombie.FindComponent(RplComponent));
		if (rpl)
			RplComponent.DeleteRplEntity(zombie, false);
		else
			SCR_EntityHelper.DeleteEntityAndChildren(zombie);
	}
}
