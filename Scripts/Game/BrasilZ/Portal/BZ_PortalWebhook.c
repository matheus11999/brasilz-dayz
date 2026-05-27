class BZ_PortalCallback : RestCallback
{
	override void OnSuccess(string data, int dataSize)
	{
		// Silent on success. The receiver should return 2xx for accepted events.
	}

	override void OnError(int errorCode)
	{
		Print(string.Format("[BrasilZ][Portal] Event POST failed: errorCode=%1", errorCode), LogLevel.WARNING);
	}

	override void OnTimeout()
	{
		Print("[BrasilZ][Portal] Event POST timeout", LogLevel.WARNING);
	}
}

class BZ_PortalSessionTracker
{
	protected static ref map<string, int> s_mLifeStartByUID = new map<string, int>();

	static void MarkLifeStart(int playerId)
	{
		string uid = BZ_Utils.GetPlayerUID(playerId);
		if (uid.IsEmpty())
			return;

		s_mLifeStartByUID.Set(uid, System.GetUnixTime());
	}

	static void EnsureLifeStart(int playerId)
	{
		string uid = BZ_Utils.GetPlayerUID(playerId);
		if (uid.IsEmpty())
			return;

		if (!s_mLifeStartByUID.Contains(uid))
			s_mLifeStartByUID.Set(uid, System.GetUnixTime());
	}

	static int GetAliveSeconds(int playerId)
	{
		string uid = BZ_Utils.GetPlayerUID(playerId);
		if (uid.IsEmpty())
			return 0;

		int start = 0;
		if (!s_mLifeStartByUID.Find(uid, start) || start <= 0)
			return 0;

		return Math.Max(0, System.GetUnixTime() - start);
	}

	static void MarkDeath(int playerId)
	{
		string uid = BZ_Utils.GetPlayerUID(playerId);
		if (uid.IsEmpty())
			return;

		s_mLifeStartByUID.Remove(uid);
	}
}

class BZ_PortalWebhook
{
	protected static ref BZ_PortalCallback s_Callback;

	static bool IsConfigured()
	{
		BZ_PortalConfig.EnsureLoaded();
		return !BZ_PortalConfig.ENDPOINT_URL.IsEmpty() && !BZ_PortalConfig.API_KEY.IsEmpty();
	}

	static void SendEvent(string eventType, string dataJson)
	{
		if (!Replication.IsServer())
			return;

		if (!IsConfigured())
			return;

		if (!BZ_PortalConfig.IsEventEnabled(eventType))
			return;

		string body = "{";
		body += "\"api_key\":" + JsonString(BZ_PortalConfig.API_KEY) + ",";
		body += "\"schema_version\":" + JsonString(BZ_PortalConfig.SCHEMA_VERSION) + ",";
		body += "\"server_id\":" + JsonString(BZ_PortalConfig.SERVER_ID) + ",";
		body += "\"event_type\":" + JsonString(eventType) + ",";
		body += "\"timestamp_unix\":" + System.GetUnixTime().ToString() + ",";
		body += "\"data\":" + dataJson;
		body += "}";

		SendRaw(body);
	}

	static string PlayerJson(int playerId, string name = "", IEntity entity = null)
	{
		PlayerManager pm = GetGame().GetPlayerManager();
		if (name.IsEmpty() && pm)
			name = pm.GetPlayerName(playerId);
		if (name.IsEmpty())
			name = string.Format("Player %1", playerId);

		string uid = BZ_Utils.GetPlayerUID(playerId);
		string json = "{";
		json += "\"player_id\":" + playerId.ToString() + ",";
		json += "\"name\":" + JsonString(name) + ",";
		json += "\"uid\":" + JsonString(uid);
		if (entity)
		{
			json += ",\"position\":" + VectorJson(entity.GetOrigin());
			string prefab = "";
			if (entity.GetPrefabData())
				prefab = entity.GetPrefabData().GetPrefabName();
			json += ",\"prefab\":" + JsonString(prefab);
		}
		json += "}";
		return json;
	}

	static string BalanceJson(int wallet, int loose, int total)
	{
		return string.Format("{\"wallet\":%1,\"loose\":%2,\"total\":%3}", wallet, loose, total);
	}

	static string VectorJson(vector pos)
	{
		return string.Format("{\"x\":%1,\"y\":%2,\"z\":%3}", pos[0], pos[1], pos[2]);
	}

	static string JsonString(string value)
	{
		return "\"" + EscapeJson(value) + "\"";
	}

	static string JsonBool(bool value)
	{
		if (value)
			return "true";
		return "false";
	}

	static string WeaponJson(IEntity character)
	{
		string weaponName = "(desconhecida)";
		string weaponPrefab = "";
		IEntity weaponEntity = null;

		if (character)
		{
			CharacterControllerComponent controller = CharacterControllerComponent.Cast(character.FindComponent(CharacterControllerComponent));
			if (controller)
			{
				BaseWeaponManagerComponent weaponManager = controller.GetWeaponManagerComponent();
				if (weaponManager)
				{
					BaseWeaponComponent currentWeapon = weaponManager.GetCurrent();
					if (currentWeapon && currentWeapon.GetOwner())
					{
						IEntity currentOwner = currentWeapon.GetOwner();
						if (BZ_IsWeaponEntity(currentOwner))
							weaponEntity = currentOwner;
					}
					else
					{
						weaponName = "(maos/vazio)";
					}
				}
			}

			if (!weaponEntity)
				weaponEntity = BZ_FindWeaponInInventory(character);

			if (!weaponEntity)
				weaponEntity = BZ_FindWeaponInChildren(character);
		}

		if (weaponEntity)
		{
			if (weaponEntity.GetPrefabData())
			{
				weaponPrefab = weaponEntity.GetPrefabData().GetPrefabName();
				weaponName = BZ_DiscordWebhook.PrefabToName(weaponPrefab);
			}
			else
			{
				weaponName = "(arma sem prefab)";
			}
		}

		string json = "{";
		json += "\"name\":" + JsonString(weaponName) + ",";
		json += "\"prefab\":" + JsonString(weaponPrefab);
		json += "}";
		return json;
	}

	protected static bool BZ_IsCharacterPrefab(string prefab)
	{
		if (prefab.IsEmpty())
			return false;

		return prefab.IndexOf("Prefabs/Characters/") >= 0 || prefab.IndexOf("Assets/Characters/") >= 0;
	}

	protected static bool BZ_IsWeaponEntity(IEntity entity)
	{
		if (!entity)
			return false;

		BaseWeaponComponent weapon = BaseWeaponComponent.Cast(entity.FindComponent(BaseWeaponComponent));
		if (!weapon)
			return false;

		if (!entity.GetPrefabData())
			return false;

		string prefab = entity.GetPrefabData().GetPrefabName();
		if (BZ_IsCharacterPrefab(prefab))
			return false;

		return true;
	}

	protected static IEntity BZ_FindWeaponInInventory(IEntity character)
	{
		SCR_InventoryStorageManagerComponent inv = SCR_InventoryStorageManagerComponent.Cast(character.FindComponent(SCR_InventoryStorageManagerComponent));
		if (!inv)
			return null;

		array<IEntity> allItems = {};
		inv.GetItems(allItems);

		foreach (IEntity item : allItems)
		{
			if (BZ_IsWeaponEntity(item))
				return item;
		}

		return null;
	}

	protected static IEntity BZ_FindWeaponInChildren(IEntity root)
	{
		IEntity child = root.GetChildren();
		int checked = 0;
		while (child && checked < 64)
		{
			if (BZ_IsWeaponEntity(child))
				return child;

			IEntity nested = BZ_FindWeaponInChildren(child);
			if (nested)
				return nested;

			child = child.GetSibling();
			checked++;
		}

		return null;
	}

	protected static void SendRaw(string body)
	{
		RestApi api = GetGame().GetRestApi();
		if (!api)
		{
			Print("[BrasilZ][Portal] RestApi unavailable, cannot send", LogLevel.WARNING);
			return;
		}

		RestContext ctx = api.GetContext(BZ_PortalConfig.ENDPOINT_URL);
		if (!ctx)
		{
			Print("[BrasilZ][Portal] RestContext null, cannot send", LogLevel.WARNING);
			return;
		}

		// Keep this to a single header. Reforger RestContext is picky here, and
		// the API key is already present in the JSON envelope as api_key.
		ctx.SetHeaders("Content-Type,application/json");

		if (!s_Callback)
			s_Callback = new BZ_PortalCallback();

		ctx.POST(s_Callback, "", body);
	}

	protected static string EscapeJson(string s)
	{
		string bs = "\\";
		string quote = "\"";
		string newline = "\n";
		string carriage = "\r";

		string escBs = bs + bs;
		string escQuote = bs + quote;
		string escNewline = bs + "n";
		string escCarriage = bs + "r";

		s.Replace(bs, escBs);
		s.Replace(quote, escQuote);
		s.Replace(newline, escNewline);
		s.Replace(carriage, escCarriage);
		return s;
	}
}
