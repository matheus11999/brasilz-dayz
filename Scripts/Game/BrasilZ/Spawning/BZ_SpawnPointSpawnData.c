class BZ_SpawnPointSpawnData : SCR_SpawnData
{
	protected BZ_SpawnPoint m_SpawnPoint;
	protected ResourceName m_sPrefab;
	protected vector m_vPosition;
	protected vector m_vAngles;
	protected int m_iPlayerId;
	protected bool m_bExplicitTransform;

	//------------------------------------------------------------------------------------------------
	void SetSpawnPoint(BZ_SpawnPoint spawnPoint)
	{
		m_SpawnPoint = spawnPoint;

		if (spawnPoint)
			spawnPoint.GetPosYPR(m_vPosition, m_vAngles);
	}

	//------------------------------------------------------------------------------------------------
	BZ_SpawnPoint GetSpawnPoint()
	{
		return m_SpawnPoint;
	}

	//------------------------------------------------------------------------------------------------
	void SetExplicitTransform(vector position, vector angles)
	{
		m_vPosition = position;
		m_vAngles = angles;
		m_bExplicitTransform = true;
	}

	//------------------------------------------------------------------------------------------------
	bool HasExplicitTransform()
	{
		return m_bExplicitTransform;
	}

	//------------------------------------------------------------------------------------------------
	void SetPlayerId(int playerId)
	{
		m_iPlayerId = playerId;
	}

	//------------------------------------------------------------------------------------------------
	int GetPlayerId()
	{
		return m_iPlayerId;
	}

	//------------------------------------------------------------------------------------------------
	void SetPrefab(ResourceName prefab)
	{
		m_sPrefab = prefab;
	}

	//------------------------------------------------------------------------------------------------
	override ResourceName GetPrefab()
	{
		return m_sPrefab;
	}

	//------------------------------------------------------------------------------------------------
	override vector GetPosition()
	{
		return m_vPosition;
	}

	//------------------------------------------------------------------------------------------------
	override vector GetAngles()
	{
		return m_vAngles;
	}
}
