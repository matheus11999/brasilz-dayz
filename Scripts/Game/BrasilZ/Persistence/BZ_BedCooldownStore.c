// Persistent cooldown storage for bed-respawn feature.
//
// Stores unix timestamp per UID in $profile:BrasilZ/BedCooldowns/<uid>.cd
// Survives server restarts + disconnects — file system based, simple key/value.
//
// Cache in memory (m_mCooldownByUid) avoids file I/O on each query. Writes always
// go to disk; reads check cache first, fall through to file on miss.
class BZ_BedCooldownStore
{
	protected static ref BZ_BedCooldownStore s_Instance;
	protected static const string COOLDOWN_DIR = "$profile:BrasilZ/BedCooldowns";

	protected ref map<string, int> m_mCooldownByUid = new map<string, int>();
	protected bool m_bInitialized;

	//------------------------------------------------------------------------------------------------
	static BZ_BedCooldownStore GetInstance()
	{
		if (!s_Instance)
			s_Instance = new BZ_BedCooldownStore();
		s_Instance.EnsureInit();
		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	protected void EnsureInit()
	{
		if (m_bInitialized)
			return;

		FileIO.MakeDirectory("$profile:BrasilZ");
		FileIO.MakeDirectory(COOLDOWN_DIR);
		m_bInitialized = true;
	}

	//------------------------------------------------------------------------------------------------
	// Returns unix timestamp when player can next use bed spawn. 0 = no cooldown.
	int GetCooldownUntil(string uid)
	{
		if (uid.IsEmpty())
			return 0;

		if (m_mCooldownByUid.Contains(uid))
			return m_mCooldownByUid.Get(uid);

		int diskValue = ReadFromFile(uid);
		if (diskValue > 0)
		{
			m_mCooldownByUid.Set(uid, diskValue);
			return diskValue;
		}

		return 0;
	}

	//------------------------------------------------------------------------------------------------
	// Returns true if cooldown is still active (unix now < cooldownUntil).
	bool IsOnCooldown(string uid)
	{
		int until = GetCooldownUntil(uid);
		if (until <= 0)
			return false;
		return System.GetUnixTime() < until;
	}

	//------------------------------------------------------------------------------------------------
	// Sets cooldown to unix timestamp + writes to disk.
	void SetCooldown(string uid, int unixUntil)
	{
		if (uid.IsEmpty())
			return;

		m_mCooldownByUid.Set(uid, unixUntil);
		WriteToFile(uid, unixUntil);
	}

	//------------------------------------------------------------------------------------------------
	void ClearCooldown(string uid)
	{
		if (uid.IsEmpty())
			return;

		m_mCooldownByUid.Remove(uid);
		FileIO.DeleteFile(GetFilePath(uid));
	}

	//------------------------------------------------------------------------------------------------
	protected string GetFilePath(string uid)
	{
		string sanitized = SanitizeUid(uid);
		return string.Format("%1/%2.cd", COOLDOWN_DIR, sanitized);
	}

	//------------------------------------------------------------------------------------------------
	protected int ReadFromFile(string uid)
	{
		FileHandle handle = FileIO.OpenFile(GetFilePath(uid), FileMode.READ);
		if (!handle)
			return 0;

		string line;
		handle.ReadLine(line);
		handle.Close();

		if (line.IsEmpty())
			return 0;

		return line.ToInt();
	}

	//------------------------------------------------------------------------------------------------
	protected void WriteToFile(string uid, int unixUntil)
	{
		FileHandle handle = FileIO.OpenFile(GetFilePath(uid), FileMode.WRITE);
		if (!handle)
			return;
		handle.WriteLine(unixUntil.ToString());
		handle.Close();
	}

	//------------------------------------------------------------------------------------------------
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
