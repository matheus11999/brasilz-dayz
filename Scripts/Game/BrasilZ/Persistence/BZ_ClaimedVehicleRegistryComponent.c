[ComponentEditorProps(category: "BrasilZ/Persistence", description: "Persists CarKey2 claimed vehicles owned by players.")]
class BZ_ClaimedVehicleRegistryComponentClass : SCR_BaseGameModeComponentClass
{
}

class BZ_ClaimedVehicleRecord
{
	string m_sVehicleId;
	string m_sOwnerUid;
	int m_iOwnerPlayerId;
	ResourceName m_sPrefab;
	string m_sKeyCode;
	string m_sCarDescription;
	vector m_vPosition;
	vector m_vAngles;
	bool m_bActive = true;
	int m_iLastSaveUnix;
}

class BZ_ClaimedVehicleRegistryComponent : SCR_BaseGameModeComponent
{
	protected static BZ_ClaimedVehicleRegistryComponent s_Instance;

	protected const string DB_DIR = "$profile:BrasilZ";
	protected const string DB_PATH = "$profile:BrasilZ/ClaimedVehicles.db";
	protected const string DB_VERSION = "BZ_CLAIMED_VEHICLES_V1";

	[Attribute("60", UIWidgets.EditBox, "Interval in seconds for saving claimed vehicle positions.")]
	protected int m_iSaveIntervalSeconds;

	[Attribute("1", UIWidgets.CheckBox, "Restore saved claimed vehicles when the server starts.")]
	protected bool m_bRestoreOnServerStart;

	[Attribute("5000", UIWidgets.EditBox, "Delay in milliseconds before restoring vehicles after game mode init.")]
	protected int m_iRestoreDelayMs;

	protected ref array<ref BZ_ClaimedVehicleRecord> m_aVehicles = {};
	protected ref map<string, IEntity> m_mLiveVehicles = new map<string, IEntity>();

	//------------------------------------------------------------------------------------------------
	static BZ_ClaimedVehicleRegistryComponent GetInstance()
	{
		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		s_Instance = this;

		if (!Replication.IsServer())
			return;

		if (m_iSaveIntervalSeconds < 15)
			m_iSaveIntervalSeconds = 15;

		FileIO.MakeDirectory(DB_DIR);
		LoadDatabase();

		if (m_bRestoreOnServerStart)
			GetGame().GetCallqueue().CallLater(RestoreSavedVehicles, m_iRestoreDelayMs, false);

		GetGame().GetCallqueue().CallLater(SaveAll, m_iSaveIntervalSeconds * 1000, true);
		Print(string.Format("[BrasilZ][Vehicles] Claimed vehicle persistence enabled. saveInterval=%1s records=%2", m_iSaveIntervalSeconds, m_aVehicles.Count()), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	void RegisterClaim(IEntity userEntity, IEntity vehicleEntity)
	{
		if (!Replication.IsServer() || !userEntity || !vehicleEntity)
			return;

		IEntity vehicle = vehicleEntity.GetRootParent();
		if (!vehicle)
			vehicle = vehicleEntity;

		ResourceName prefab = GetEntityPrefab(vehicle);
		if (prefab.IsEmpty())
		{
			Print("[BrasilZ][Vehicles] Cannot register claimed vehicle without prefab data.", LogLevel.WARNING);
			return;
		}

		string keyCode;
		string carDescription;
		ReadCarKey2Data(vehicle, keyCode, carDescription);

		int playerId = ResolvePlayerId(userEntity);
		string ownerUid = ResolvePlayerUid(playerId);
		string vehicleId = BuildVehicleId(vehicle, prefab, keyCode, ownerUid);

		BZ_ClaimedVehicleRecord record = FindRecord(vehicleId);
		if (!record)
		{
			record = new BZ_ClaimedVehicleRecord();
			record.m_sVehicleId = vehicleId;
			m_aVehicles.Insert(record);
		}

		record.m_sOwnerUid = ownerUid;
		record.m_iOwnerPlayerId = playerId;
		record.m_sPrefab = prefab;
		record.m_sKeyCode = keyCode;
		record.m_sCarDescription = carDescription;
		record.m_bActive = true;

		UpdateRecordTransform(record, vehicle);
		m_mLiveVehicles.Set(vehicleId, vehicle);

		SaveAll();
		Print(string.Format("[BrasilZ][Vehicles] Claimed vehicle saved. owner=%1 vehicle=%2 prefab=%3 pos=%4", ownerUid, vehicleId, prefab, record.m_vPosition), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	void TouchVehicle(IEntity vehicleEntity)
	{
		if (!Replication.IsServer() || !vehicleEntity)
			return;

		IEntity vehicle = vehicleEntity.GetRootParent();
		if (!vehicle)
			vehicle = vehicleEntity;

		string keyCode;
		string carDescription;
		ReadCarKey2Data(vehicle, keyCode, carDescription);

		BZ_ClaimedVehicleRecord record = FindRecordByKeyCode(keyCode);
		if (!record)
			return;

		record.m_sCarDescription = carDescription;
		UpdateRecordTransform(record, vehicle);
		m_mLiveVehicles.Set(record.m_sVehicleId, vehicle);
	}

	//------------------------------------------------------------------------------------------------
	void SaveAll()
	{
		if (!Replication.IsServer())
			return;

		UpdateLiveVehicleRecords();
		WriteDatabase();
	}

	//------------------------------------------------------------------------------------------------
	protected void RestoreSavedVehicles()
	{
		if (!Replication.IsServer())
			return;

		int restored;
		foreach (BZ_ClaimedVehicleRecord record : m_aVehicles)
		{
			if (!record || !record.m_bActive || record.m_sPrefab.IsEmpty())
				continue;

			if (m_mLiveVehicles.Contains(record.m_sVehicleId) && m_mLiveVehicles.Get(record.m_sVehicleId))
				continue;

			Resource resource = Resource.Load(record.m_sPrefab);
			if (!resource || !resource.IsValid())
			{
				Print(string.Format("[BrasilZ][Vehicles] Could not restore vehicle %1; invalid prefab %2", record.m_sVehicleId, record.m_sPrefab), LogLevel.WARNING);
				continue;
			}

			vector transform[4];
			Math3D.AnglesToMatrix(record.m_vAngles, transform);
			transform[3] = record.m_vPosition;

			EntitySpawnParams spawnParams();
			spawnParams.TransformMode = ETransformMode.WORLD;
			for (int i = 0; i < 4; i++)
				spawnParams.Transform[i] = transform[i];

			IEntity vehicle = GetGame().SpawnEntityPrefab(resource, GetOwner().GetWorld(), spawnParams);
			if (!vehicle)
			{
				Print(string.Format("[BrasilZ][Vehicles] Failed to spawn restored claimed vehicle %1", record.m_sVehicleId), LogLevel.WARNING);
				continue;
			}

			ApplyCarKey2Data(vehicle, record);
			m_mLiveVehicles.Set(record.m_sVehicleId, vehicle);
			restored++;
		}

		Print(string.Format("[BrasilZ][Vehicles] Restored %1 claimed vehicles from %2 records.", restored, m_aVehicles.Count()), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	protected void LoadDatabase()
	{
		m_aVehicles.Clear();

		FileHandle handle = FileIO.OpenFile(DB_PATH, FileMode.READ);
		if (!handle)
			return;

		string line;
		while (handle.ReadLine(line) > 0)
		{
			if (line.IsEmpty() || line == DB_VERSION)
				continue;

			BZ_ClaimedVehicleRecord record = DeserializeRecord(line);
			if (record)
				m_aVehicles.Insert(record);
		}

		handle.Close();
	}

	//------------------------------------------------------------------------------------------------
	protected void WriteDatabase()
	{
		FileIO.MakeDirectory(DB_DIR);

		FileHandle handle = FileIO.OpenFile(DB_PATH, FileMode.WRITE);
		if (!handle)
		{
			Print("[BrasilZ][Vehicles] Could not open claimed vehicle database for writing.", LogLevel.WARNING);
			return;
		}

		handle.WriteLine(DB_VERSION);

		foreach (BZ_ClaimedVehicleRecord record : m_aVehicles)
		{
			if (!record)
				continue;

			handle.WriteLine(SerializeRecord(record));
		}

		handle.Close();
	}

	//------------------------------------------------------------------------------------------------
	protected void UpdateLiveVehicleRecords()
	{
		for (int i = m_aVehicles.Count() - 1; i >= 0; i--)
		{
			BZ_ClaimedVehicleRecord record = m_aVehicles[i];
			if (!record || !record.m_bActive)
				continue;

			IEntity vehicle = m_mLiveVehicles.Get(record.m_sVehicleId);
			if (!vehicle || vehicle.IsDeleted())
			{
				RemoveVehicleOwnership(record, "removed");
				continue;
			}

			if (IsVehicleDestroyed(vehicle))
			{
				RemoveVehicleOwnership(record, "destroyed");
				continue;
			}

			UpdateRecordTransform(record, vehicle);

			string keyCode;
			string carDescription;
			ReadCarKey2Data(vehicle, keyCode, carDescription);
			if (!keyCode.IsEmpty())
				record.m_sKeyCode = keyCode;
			if (!carDescription.IsEmpty())
				record.m_sCarDescription = carDescription;
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void UpdateRecordTransform(notnull BZ_ClaimedVehicleRecord record, IEntity vehicle)
	{
		if (!vehicle || vehicle.IsDeleted())
			return;

		record.m_vPosition = vehicle.GetOrigin();
		record.m_vAngles = vehicle.GetYawPitchRoll();
		record.m_iLastSaveUnix = BuildTimestamp();
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsVehicleDestroyed(IEntity vehicle)
	{
		if (!vehicle || vehicle.IsDeleted())
			return true;

		SCR_DamageManagerComponent damageManager = SCR_DamageManagerComponent.Cast(vehicle.FindComponent(SCR_DamageManagerComponent));
		if (damageManager && damageManager.IsDestroyed())
			return true;

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected void RemoveVehicleOwnership(BZ_ClaimedVehicleRecord record, string reason)
	{
		if (!record)
			return;

		string vehicleId = record.m_sVehicleId;
		string ownerUid = record.m_sOwnerUid;

		m_mLiveVehicles.Remove(vehicleId);
		m_aVehicles.RemoveItem(record);

		Print(string.Format("[BrasilZ][Vehicles] Claimed vehicle ownership removed. reason=%1 vehicle=%2 owner=%3", reason, vehicleId, ownerUid), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	protected BZ_ClaimedVehicleRecord FindRecord(string vehicleId)
	{
		foreach (BZ_ClaimedVehicleRecord record : m_aVehicles)
		{
			if (record && record.m_sVehicleId == vehicleId)
				return record;
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	protected BZ_ClaimedVehicleRecord FindRecordByKeyCode(string keyCode)
	{
		if (keyCode.IsEmpty())
			return null;

		foreach (BZ_ClaimedVehicleRecord record : m_aVehicles)
		{
			if (record && record.m_sKeyCode == keyCode)
				return record;
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	protected ResourceName GetEntityPrefab(IEntity entity)
	{
		if (!entity)
			return string.Empty;

		auto prefabData = entity.GetPrefabData();
		if (!prefabData)
			return string.Empty;

		return prefabData.GetPrefabName();
	}

	//------------------------------------------------------------------------------------------------
	protected int ResolvePlayerId(IEntity userEntity)
	{
		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (!playerManager || !userEntity)
			return 0;

		IEntity userRoot = userEntity.GetRootParent();
		if (!userRoot)
			userRoot = userEntity;

		array<int> players = {};
		playerManager.GetPlayers(players);

		foreach (int playerId : players)
		{
			IEntity controlled = playerManager.GetPlayerControlledEntity(playerId);
			if (!controlled)
				continue;

			if (controlled == userEntity || controlled == userRoot || controlled.GetRootParent() == userRoot)
				return playerId;
		}

		return 0;
	}

	//------------------------------------------------------------------------------------------------
	protected string ResolvePlayerUid(int playerId)
	{
		if (playerId <= 0)
			return "unknown";

		return string.Format("playerId:%1", playerId);
	}

	//------------------------------------------------------------------------------------------------
	protected string BuildVehicleId(IEntity vehicle, ResourceName prefab, string keyCode, string ownerUid)
	{
		if (!keyCode.IsEmpty())
			return string.Format("key:%1", keyCode);

		vector pos = vehicle.GetOrigin();
		string id = "fallback:";
		id = AppendDbColumn(id, ownerUid, ":");
		id = AppendDbColumn(id, prefab, ":");
		id = AppendDbColumn(id, pos[0].ToString(), ":");
		id = AppendDbColumn(id, pos[1].ToString(), ":");
		id += pos[2].ToString();
		return id;
	}

	//------------------------------------------------------------------------------------------------
	protected void ReadCarKey2Data(IEntity vehicle, out string keyCode, out string carDescription)
	{
		keyCode = string.Empty;
		carDescription = string.Empty;

		if (!vehicle)
			return;

		Key_LockComponent keyComponent = Key_LockComponent.Cast(vehicle.FindComponent(Key_LockComponent));
		if (!keyComponent)
			return;

		keyCode = keyComponent.myCode;
		carDescription = keyComponent.carDesc;
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyCarKey2Data(IEntity vehicle, BZ_ClaimedVehicleRecord record)
	{
		if (!vehicle || !record)
			return;

		Key_LockComponent keyComponent = Key_LockComponent.Cast(vehicle.FindComponent(Key_LockComponent));
		if (!keyComponent)
			return;

		if (!record.m_sKeyCode.IsEmpty())
			keyComponent.myCode = record.m_sKeyCode;

		if (!record.m_sCarDescription.IsEmpty())
			keyComponent.carDesc = record.m_sCarDescription;
	}

	//------------------------------------------------------------------------------------------------
	protected string SerializeRecord(notnull BZ_ClaimedVehicleRecord record)
	{
		int active = 0;
		if (record.m_bActive)
			active = 1;

		string row = record.m_sVehicleId;
		row = AppendDbColumn(row, record.m_sOwnerUid);
		row = AppendDbColumn(row, record.m_iOwnerPlayerId.ToString());
		row = AppendDbColumn(row, record.m_sPrefab);
		row = AppendDbColumn(row, record.m_sKeyCode);
		row = AppendDbColumn(row, record.m_sCarDescription);
		row = AppendDbColumn(row, record.m_vPosition[0].ToString());
		row = AppendDbColumn(row, record.m_vPosition[1].ToString());
		row = AppendDbColumn(row, record.m_vPosition[2].ToString());
		row = AppendDbColumn(row, record.m_vAngles[0].ToString());
		row = AppendDbColumn(row, record.m_vAngles[1].ToString());
		row = AppendDbColumn(row, record.m_vAngles[2].ToString());
		row = AppendDbColumn(row, active.ToString());
		row = AppendDbColumn(row, record.m_iLastSaveUnix.ToString());
		row = AppendDbColumn(row, "0");
		row = AppendDbColumn(row, "0");

		return row;
	}

	//------------------------------------------------------------------------------------------------
	protected string AppendDbColumn(string row, string value, string separator = "|")
	{
		string result = row;
		result += separator;
		result += value;
		return result;
	}

	//------------------------------------------------------------------------------------------------
	protected BZ_ClaimedVehicleRecord DeserializeRecord(string line)
	{
		array<string> columns = {};
		line.Split("|", columns, false);

		if (columns.Count() < 14)
			return null;

		BZ_ClaimedVehicleRecord record = new BZ_ClaimedVehicleRecord();
		record.m_sVehicleId = columns[0];
		record.m_sOwnerUid = columns[1];
		record.m_iOwnerPlayerId = columns[2].ToInt();
		record.m_sPrefab = columns[3];
		record.m_sKeyCode = columns[4];
		record.m_sCarDescription = columns[5];
		record.m_vPosition = Vector(columns[6].ToFloat(), columns[7].ToFloat(), columns[8].ToFloat());
		record.m_vAngles = Vector(columns[9].ToFloat(), columns[10].ToFloat(), columns[11].ToFloat());
		record.m_bActive = columns[12].ToInt() != 0;
		record.m_iLastSaveUnix = columns[13].ToInt();

		return record;
	}

	//------------------------------------------------------------------------------------------------
	protected int BuildTimestamp()
	{
		int year, month, day, hour, minute, second;
		System.GetYearMonthDayUTC(year, month, day);
		System.GetHourMinuteSecondUTC(hour, minute, second);

		return year * 100000000 + month * 1000000 + day * 10000 + hour * 100 + minute;
	}
}
