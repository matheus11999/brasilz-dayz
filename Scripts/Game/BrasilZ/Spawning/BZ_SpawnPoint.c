[EntityEditorProps(category: "BrasilZ/Spawning", description: "BrasilZ random character spawn point", color: "0 200 0 255")]
class BZ_SpawnPointClass : SCR_SpawnPointClass
{
}

class BZ_SpawnPoint : SCR_SpawnPoint
{
	protected static ref array<BZ_SpawnPoint> s_aSpawnPoints = {};

	//------------------------------------------------------------------------------------------------
	override string GetSpawnPointName()
	{
		string name = GetName();
		if (name.Length() > 3 && name.Substring(0, 3) == "BZ_")
			name = name.Substring(3, name.Length() - 3);

		if (!name.IsEmpty())
			return name;

		return "Spawn Point";
	}

	//------------------------------------------------------------------------------------------------
	override bool IsSpawnPointVisibleForPlayer(int pid)
	{
		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool IsSpawnPointActive()
	{
		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool CanReserveFor_S(int playerId, out SCR_ESpawnResult result = SCR_ESpawnResult.SPAWN_NOT_ALLOWED)
	{
		result = SCR_ESpawnResult.OK;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	static BZ_SpawnPoint GetRandomSpawnPoint()
	{
		if (!s_aSpawnPoints || s_aSpawnPoints.IsEmpty())
			return null;

		return s_aSpawnPoints.GetRandomElement();
	}

	//------------------------------------------------------------------------------------------------
	void GetPosYPR(out vector position, out vector ypr)
	{
		position = GetOrigin();
		ypr = GetYawPitchRoll();
		SCR_WorldTools.FindEmptyTerrainPosition(position, position, m_fSpawnRadius);
	}

	//------------------------------------------------------------------------------------------------
	void BZ_SpawnPoint(IEntitySource src, IEntity parent)
	{
		SetFlags(EntityFlags.STATIC, true);
		SetFactionKey("CIV");

		if (GetGame().InPlayMode())
			s_aSpawnPoints.Insert(this);
	}

	//------------------------------------------------------------------------------------------------
	void ~BZ_SpawnPoint()
	{
		if (s_aSpawnPoints)
			s_aSpawnPoints.RemoveItem(this);
	}
}
