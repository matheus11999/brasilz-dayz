class BZ_PlayerDeathRegistry
{
	protected static ref BZ_PlayerDeathRegistry s_Instance;
	protected static const string DEATH_DIR = "$profile:BrasilZ/Deaths";

	protected ref map<string, bool> m_mDeadUIDs = new map<string, bool>();
	protected ref map<int, IEntity> m_mDeadBodies = new map<int, IEntity>();
	protected bool m_bInitialized;

	static BZ_PlayerDeathRegistry GetInstance()
	{
		if (!s_Instance)
			s_Instance = new BZ_PlayerDeathRegistry();
		s_Instance.EnsureInit();
		return s_Instance;
	}

	protected void EnsureInit()
	{
		if (m_bInitialized)
			return;

		FileIO.MakeDirectory("$profile:BrasilZ");
		FileIO.MakeDirectory(DEATH_DIR);
		m_bInitialized = true;
	}

	bool IsDeadByUID(string uid)
	{
		if (uid.IsEmpty())
			return false;

		if (m_mDeadUIDs.Contains(uid))
			return m_mDeadUIDs.Get(uid);

		if (HasFlagFile(uid))
		{
			m_mDeadUIDs.Set(uid, true);
			return true;
		}

		return false;
	}

	void FlagDead(string uid)
	{
		if (uid.IsEmpty())
			return;

		m_mDeadUIDs.Set(uid, true);
		WriteFlagFile(uid);
	}

	void ClearDead(string uid)
	{
		if (uid.IsEmpty())
			return;

		m_mDeadUIDs.Remove(uid);
		DeleteFlagFile(uid);
	}

	void TrackDeadBody(int playerId, IEntity body)
	{
		if (playerId <= 0 || !body)
			return;

		m_mDeadBodies.Set(playerId, body);
	}

	bool HasDeadBody(int playerId)
	{
		return m_mDeadBodies.Contains(playerId);
	}

	IEntity GetDeadBody(int playerId)
	{
		if (!m_mDeadBodies.Contains(playerId))
			return null;
		return m_mDeadBodies.Get(playerId);
	}

	void ClearDeadBody(int playerId)
	{
		m_mDeadBodies.Remove(playerId);
	}

	protected string GetFlagPath(string uid)
	{
		string sanitized = SanitizeUid(uid);
		return string.Format("%1/%2.flag", DEATH_DIR, sanitized);
	}

	protected bool HasFlagFile(string uid)
	{
		FileHandle handle = FileIO.OpenFile(GetFlagPath(uid), FileMode.READ);
		if (!handle)
			return false;
		handle.Close();
		return true;
	}

	protected void WriteFlagFile(string uid)
	{
		FileHandle handle = FileIO.OpenFile(GetFlagPath(uid), FileMode.WRITE);
		if (!handle)
			return;
		handle.WriteLine("dead");
		handle.Close();
	}

	protected void DeleteFlagFile(string uid)
	{
		FileIO.DeleteFile(GetFlagPath(uid));
	}

	protected string SanitizeUid(string uid)
	{
		string result = uid;
		result.Replace("/", "_");
		result.Replace(":", "_");
		result.Replace("*", "_");
		result.Replace("?", "_");
		result.Replace("<", "_");
		result.Replace(">", "_");
		result.Replace("|", "_");
		result.Replace("\\", "_");
		return result;
	}
}
