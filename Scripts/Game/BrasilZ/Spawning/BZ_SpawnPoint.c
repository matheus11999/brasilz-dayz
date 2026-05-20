[EntityEditorProps(category: "BrasilZ/Spawning", description: "BrasilZ random character spawn point", color: "0 200 0 255")]
class BZ_SpawnPointClass : SCR_SpawnPointClass
{
}

class BZ_SpawnPoint : SCR_SpawnPoint
{
	// Minimum Y to be considered safe (above sea level). Chernarus sea level ~0,
	// terrain near coast can snap to -1.4..-1.96, so require at least 1m above.
	protected static const float BZ_MIN_SAFE_Y = 1.0;
	// Max attempts to find a non-underwater random spawn before giving up
	protected static const int BZ_MAX_RANDOM_TRIES = 16;

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
	// Return true if this spawn point's resolved terrain position is above sea level.
	bool IsAboveWater()
	{
		vector pos = GetOrigin();
		SCR_WorldTools.FindEmptyTerrainPosition(pos, pos, m_fSpawnRadius);
		return pos[1] >= BZ_MIN_SAFE_Y;
	}

	//------------------------------------------------------------------------------------------------
	// Returns random spawn point that is not underwater. Falls back to any point if all underwater.
	static BZ_SpawnPoint GetRandomSpawnPoint()
	{
		if (!s_aSpawnPoints || s_aSpawnPoints.IsEmpty())
			return null;

		for (int i = 0; i < BZ_MAX_RANDOM_TRIES; i++)
		{
			BZ_SpawnPoint candidate = s_aSpawnPoints.GetRandomElement();
			if (candidate && candidate.IsAboveWater())
				return candidate;
		}

		Print("[BrasilZ] All random spawn point tries returned underwater positions. Falling back to first element.", LogLevel.WARNING);
		return s_aSpawnPoints[0];
	}

	//------------------------------------------------------------------------------------------------
	void GetPosYPR(out vector position, out vector ypr)
	{
		position = GetOrigin();
		ypr = GetYawPitchRoll();
		SCR_WorldTools.FindEmptyTerrainPosition(position, position, m_fSpawnRadius);

		// Safety: never return an underwater position. If terrain snap put us below sea level,
		// lift to safe Y so the player doesn't spawn drowning.
		if (position[1] < BZ_MIN_SAFE_Y)
		{
			Print(string.Format("[BrasilZ] Spawn point '%1' resolved underwater at %2 - lifting to Y=%3", GetSpawnPointName(), position, BZ_MIN_SAFE_Y), LogLevel.WARNING);
			position[1] = BZ_MIN_SAFE_Y;
		}
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
