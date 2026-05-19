[ComponentEditorProps(category: "BrasilZ/Spawning", description: "BrasilZ respawn system using random map spawn points.")]
class BZ_RespawnSystemComponentClass : SCR_RespawnSystemComponentClass
{
}

class BZ_RespawnSystemComponent : SCR_RespawnSystemComponent
{
	[Attribute(category: "BrasilZ")]
	protected ref array<ResourceName> m_aDefaultCharacterPrefabs;

	//------------------------------------------------------------------------------------------------
	override void OnInit(IEntity owner)
	{
		if (!m_SpawnLogic)
			m_SpawnLogic = new BZ_MenuSpawnLogic();

		super.OnInit(owner);
	}

	//------------------------------------------------------------------------------------------------
	ResourceName GetDefaultCharacterPrefab()
	{
		if (m_aDefaultCharacterPrefabs && !m_aDefaultCharacterPrefabs.IsEmpty())
			return m_aDefaultCharacterPrefabs.GetRandomElement();

		return "{748B185D87E782EE}Prefabs/Characters/Character_BrasilZ_Survivor.et";
	}

	//------------------------------------------------------------------------------------------------
	void RequestRandomSpawn(int playerId)
	{
		RequestRandomSpawnWithPrefab(playerId, GetDefaultCharacterPrefab());
	}

	//------------------------------------------------------------------------------------------------
	void RequestRandomSpawnWithPrefab(int playerId, ResourceName prefab)
	{
		if (!Replication.IsServer())
			return;

		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (!playerManager)
			return;

		PlayerController playerController = playerManager.GetPlayerController(playerId);
		if (!playerController)
			return;

		SCR_SpawnRequestComponent requestComponent = SCR_SpawnRequestComponent.Cast(playerController.FindComponent(SCR_SpawnRequestComponent));
		if (!requestComponent)
		{
			Print(string.Format("[BrasilZ] Player %1 has no SCR_SpawnRequestComponent.", playerId), LogLevel.ERROR);
			return;
		}

		BZ_SpawnPoint spawnPoint = BZ_SpawnPoint.GetRandomSpawnPoint();
		if (!spawnPoint)
		{
			Print("[BrasilZ] No BZ_SpawnPoint found in world.", LogLevel.ERROR);
			return;
		}

		if (prefab.IsEmpty())
		{
			Print("[BrasilZ] No character prefab configured.", LogLevel.ERROR);
			return;
		}

		BZ_SpawnPointSpawnData spawnData = new BZ_SpawnPointSpawnData();
		spawnData.SetSpawnPoint(spawnPoint);
		spawnData.SetPlayerId(playerId);
		spawnData.SetPrefab(prefab);
		requestComponent.RequestRespawn(spawnData);
	}

	//------------------------------------------------------------------------------------------------
	void RequestSavedPositionSpawnWithPrefab(int playerId, ResourceName prefab, vector position, vector angles)
	{
		if (!Replication.IsServer())
			return;

		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (!playerManager)
			return;

		PlayerController playerController = playerManager.GetPlayerController(playerId);
		if (!playerController)
			return;

		SCR_SpawnRequestComponent requestComponent = SCR_SpawnRequestComponent.Cast(playerController.FindComponent(SCR_SpawnRequestComponent));
		if (!requestComponent)
		{
			Print(string.Format("[BrasilZ] Player %1 has no SCR_SpawnRequestComponent.", playerId), LogLevel.ERROR);
			return;
		}

		if (prefab.IsEmpty())
		{
			Print("[BrasilZ] No character prefab configured.", LogLevel.ERROR);
			return;
		}

		BZ_SpawnPointSpawnData spawnData = new BZ_SpawnPointSpawnData();
		spawnData.SetPlayerId(playerId);
		spawnData.SetPrefab(prefab);
		spawnData.SetExplicitTransform(position, angles);
		requestComponent.RequestRespawn(spawnData);
	}
}
