// Server-to-portal webhook config. Keep this server-only: never call portal
// hooks from client-side UI code, otherwise the API key can leak to clients.
class BZ_PortalConfig
{
	protected static const string CONFIG_DIR = "$profile:BrasilZ/Portal";
	protected static const string CONFIG_PATH = "$profile:BrasilZ/Portal/Config.json";

	static string ENDPOINT_URL = "";
	static string API_KEY = "";
	static string REWARDS_URL = "";
	static string SERVER_ID = "brasilz-main";
	static string SCHEMA_VERSION = "1.0";
	static int REWARDS_POLL_INTERVAL_MS = 600000;

	static bool LOG_CONNECT       = true;
	static bool LOG_DISCONNECT    = true;
	static bool LOG_KILL          = true;
	static bool LOG_SPAWN         = true;
	static bool LOG_PURCHASE      = true;
	static bool LOG_PURCHASE_FAIL = true;
	static bool LOG_SALE          = true;
	static bool LOG_MISSION       = true;

	protected static bool s_bLoaded;

	static void EnsureLoaded()
	{
		if (s_bLoaded)
			return;

		FileIO.MakeDirectory("$profile:BrasilZ");
		FileIO.MakeDirectory(CONFIG_DIR);

		if (!FileIO.FileExists(CONFIG_PATH))
			WriteDefaultConfig();

		LoadConfig();
		s_bLoaded = true;
	}

	static bool IsEventEnabled(string eventType)
	{
		EnsureLoaded();

		if (eventType == "player_connected") return LOG_CONNECT;
		if (eventType == "player_disconnected") return LOG_DISCONNECT;
		if (eventType == "player_killed") return LOG_KILL;
		if (eventType == "player_spawned") return LOG_SPAWN;
		if (eventType == "shop_purchase") return LOG_PURCHASE;
		if (eventType == "shop_purchase_failed") return LOG_PURCHASE_FAIL;
		if (eventType == "shop_sale") return LOG_SALE;
		if (eventType == "mission_started") return LOG_MISSION;
		if (eventType == "mission_ended") return LOG_MISSION;

		return true;
	}

	protected static void LoadConfig()
	{
		FileHandle handle = FileIO.OpenFile(CONFIG_PATH, FileMode.READ);
		if (!handle)
		{
			Print("[BrasilZ][PortalConfig] Could not open Config.json, using built-in defaults.", LogLevel.WARNING);
			return;
		}

		string json;
		string line;
		while (handle.ReadLine(line) > 0)
			json += line;
		handle.Close();

		string value;
		if (ReadJsonString(json, "endpoint_url", value)) ENDPOINT_URL = value;
		if (ReadJsonString(json, "api_key", value)) API_KEY = value;
		if (ReadJsonString(json, "rewards_url", value)) REWARDS_URL = value;
		if (ReadJsonString(json, "server_id", value)) SERVER_ID = value;
		if (ReadJsonString(json, "schema_version", value)) SCHEMA_VERSION = value;
		REWARDS_POLL_INTERVAL_MS = ReadJsonInt(json, "rewards_poll_interval_ms", REWARDS_POLL_INTERVAL_MS);
		if (REWARDS_POLL_INTERVAL_MS < 30000)
			REWARDS_POLL_INTERVAL_MS = 30000;

		LOG_CONNECT = ReadJsonBool(json, "log_connect", LOG_CONNECT);
		LOG_DISCONNECT = ReadJsonBool(json, "log_disconnect", LOG_DISCONNECT);
		LOG_KILL = ReadJsonBool(json, "log_kill", LOG_KILL);
		LOG_SPAWN = ReadJsonBool(json, "log_spawn", LOG_SPAWN);
		LOG_PURCHASE = ReadJsonBool(json, "log_purchase", LOG_PURCHASE);
		LOG_PURCHASE_FAIL = ReadJsonBool(json, "log_purchase_fail", LOG_PURCHASE_FAIL);
		LOG_SALE = ReadJsonBool(json, "log_sale", LOG_SALE);
		LOG_MISSION = ReadJsonBool(json, "log_mission", LOG_MISSION);

		if (REWARDS_URL.IsEmpty())
			REWARDS_URL = BuildRewardsUrl();

		Print(string.Format("[BrasilZ][PortalConfig] Loaded %1 (endpoint=%2, rewards=%3, server_id=%4).", CONFIG_PATH, ENDPOINT_URL, REWARDS_URL, SERVER_ID), LogLevel.NORMAL);
	}

	static string BuildRewardsUrl()
	{
		string url = ENDPOINT_URL;
		if (url.IsEmpty())
			return string.Empty;
		url.Replace("/v1/arma/events", "/v1/arma/rewards");
		return url;
	}

	protected static void WriteDefaultConfig()
	{
		FileHandle handle = FileIO.OpenFile(CONFIG_PATH, FileMode.WRITE);
		if (!handle)
		{
			Print("[BrasilZ][PortalConfig] Could not create Config.json.", LogLevel.WARNING);
			return;
		}

		handle.WriteLine("{");
		handle.WriteLine("  " + BZ_PortalWebhook.JsonString("endpoint_url") + ": " + BZ_PortalWebhook.JsonString("") + ",");
		handle.WriteLine("  " + BZ_PortalWebhook.JsonString("rewards_url") + ": " + BZ_PortalWebhook.JsonString("") + ",");
		handle.WriteLine("  " + BZ_PortalWebhook.JsonString("api_key") + ": " + BZ_PortalWebhook.JsonString("") + ",");
		handle.WriteLine("  " + BZ_PortalWebhook.JsonString("server_id") + ": " + BZ_PortalWebhook.JsonString("brasilz-main") + ",");
		handle.WriteLine("  " + BZ_PortalWebhook.JsonString("schema_version") + ": " + BZ_PortalWebhook.JsonString("1.0") + ",");
		handle.WriteLine("  " + BZ_PortalWebhook.JsonString("rewards_poll_interval_ms") + ": 600000,");
		handle.WriteLine("  " + BZ_PortalWebhook.JsonString("log_connect") + ": true,");
		handle.WriteLine("  " + BZ_PortalWebhook.JsonString("log_disconnect") + ": true,");
		handle.WriteLine("  " + BZ_PortalWebhook.JsonString("log_kill") + ": true,");
		handle.WriteLine("  " + BZ_PortalWebhook.JsonString("log_spawn") + ": true,");
		handle.WriteLine("  " + BZ_PortalWebhook.JsonString("log_purchase") + ": true,");
		handle.WriteLine("  " + BZ_PortalWebhook.JsonString("log_purchase_fail") + ": true,");
		handle.WriteLine("  " + BZ_PortalWebhook.JsonString("log_sale") + ": true,");
		handle.WriteLine("  " + BZ_PortalWebhook.JsonString("log_mission") + ": true");
		handle.WriteLine("}");
		handle.Close();

		Print(string.Format("[BrasilZ][PortalConfig] Created default config at %1.", CONFIG_PATH), LogLevel.NORMAL);
	}

	protected static bool ReadJsonString(string json, string key, out string value)
	{
		value = "";
		string token = "\"" + key + "\"";
		int keyPos = json.IndexOf(token);
		if (keyPos < 0)
			return false;

		int colon = FindFrom(json, ":", keyPos + token.Length());
		if (colon < 0)
			return false;

		int start = FindFrom(json, "\"", colon + 1);
		if (start < 0)
			return false;

		int end = FindFrom(json, "\"", start + 1);
		if (end < 0)
			return false;

		value = json.Substring(start + 1, end - start - 1);
		return true;
	}

	protected static bool ReadJsonBool(string json, string key, bool fallback)
	{
		string token = "\"" + key + "\"";
		int keyPos = json.IndexOf(token);
		if (keyPos < 0)
			return fallback;

		int colon = FindFrom(json, ":", keyPos + token.Length());
		if (colon < 0)
			return fallback;

		string tail = json.Substring(colon + 1, Math.Min(8, json.Length() - colon - 1));
		tail.Trim();
		if (tail.Length() >= 4 && tail.Substring(0, 4) == "true")
			return true;
		if (tail.Length() >= 5 && tail.Substring(0, 5) == "false")
			return false;

		return fallback;
	}

	protected static int ReadJsonInt(string json, string key, int fallback)
	{
		string token = "\"" + key + "\"";
		int keyPos = json.IndexOf(token);
		if (keyPos < 0)
			return fallback;

		int colon = FindFrom(json, ":", keyPos + token.Length());
		if (colon < 0)
			return fallback;

		int end = colon + 1;
		while (end < json.Length())
		{
			string ch = json.Substring(end, 1);
			if (ch == "," || ch == "}" || ch == "\n" || ch == "\r")
				break;
			end++;
		}

		string value = json.Substring(colon + 1, end - colon - 1);
		value.Trim();
		if (value.IsEmpty())
			return fallback;
		return value.ToInt();
	}

	protected static int FindFrom(string source, string needle, int start)
	{
		if (start < 0 || start >= source.Length())
			return -1;

		string tail = source.Substring(start, source.Length() - start);
		int local = tail.IndexOf(needle);
		if (local < 0)
			return -1;

		return start + local;
	}
}
