class BZ_PortalRewardsPendingCallback : RestCallback
{
	override void OnSuccess(string data, int dataSize)
	{
		BZ_PortalRewards.HandlePendingResponse(data);
	}

	override void OnError(int errorCode)
	{
		Print(string.Format("[BrasilZ][PortalRewards] Pending request failed: errorCode=%1", errorCode), LogLevel.WARNING);
	}

	override void OnTimeout()
	{
		Print("[BrasilZ][PortalRewards] Pending request timeout", LogLevel.WARNING);
	}
}

class BZ_PortalRewardsClaimCallback : RestCallback
{
	override void OnSuccess(string data, int dataSize)
	{
		Print("[BrasilZ][PortalRewards] Reward claim confirmed by portal.", LogLevel.NORMAL);
	}

	override void OnError(int errorCode)
	{
		Print(string.Format("[BrasilZ][PortalRewards] Claim request failed after payout: errorCode=%1", errorCode), LogLevel.WARNING);
	}

	override void OnTimeout()
	{
		Print("[BrasilZ][PortalRewards] Claim request timeout after payout.", LogLevel.WARNING);
	}
}

class BZ_PortalRewardRow
{
	int m_iId;
	string m_sHunterUid;
	string m_sHunterName;
	string m_sTargetName;
	int m_iValue;
}

class BZ_PortalRewards
{
	protected static bool s_bStarted;
	protected static ref BZ_PortalRewardsPendingCallback s_PendingCallback;
	protected static ref BZ_PortalRewardsClaimCallback s_ClaimCallback;
	protected static ref map<int, bool> s_mPaidOrInFlight = new map<int, bool>();

	static void EnsureStarted()
	{
		if (s_bStarted || !Replication.IsServer())
			return;

		BZ_PortalConfig.EnsureLoaded();
		if (BZ_PortalConfig.REWARDS_URL.IsEmpty() || BZ_PortalConfig.API_KEY.IsEmpty())
		{
			Print("[BrasilZ][PortalRewards] Rewards disabled: portal URL/api_key not configured.", LogLevel.NORMAL);
			return;
		}

		s_bStarted = true;
		if (!s_PendingCallback)
			s_PendingCallback = new BZ_PortalRewardsPendingCallback();
		if (!s_ClaimCallback)
			s_ClaimCallback = new BZ_PortalRewardsClaimCallback();

		GetGame().GetCallqueue().CallLater(PollPendingRewards, 10000, false);
		GetGame().GetCallqueue().CallLater(PollPendingRewards, BZ_PortalConfig.REWARDS_POLL_INTERVAL_MS, true);
		Print(string.Format("[BrasilZ][PortalRewards] Reward poller armed every %1ms.", BZ_PortalConfig.REWARDS_POLL_INTERVAL_MS), LogLevel.NORMAL);
	}

	protected static void PollPendingRewards()
	{
		if (!Replication.IsServer())
			return;

		BZ_PortalConfig.EnsureLoaded();
		if (BZ_PortalConfig.REWARDS_URL.IsEmpty() || BZ_PortalConfig.API_KEY.IsEmpty())
			return;

		RestApi api = GetGame().GetRestApi();
		if (!api)
			return;

		string url = BZ_PortalConfig.REWARDS_URL + "/pending";
		RestContext ctx = api.GetContext(url);
		if (!ctx)
			return;

		ctx.SetHeaders("Content-Type,application/json");

		string body = "{";
		body += "\"api_key\":" + BZ_PortalWebhook.JsonString(BZ_PortalConfig.API_KEY) + ",";
		body += "\"server_id\":" + BZ_PortalWebhook.JsonString(BZ_PortalConfig.SERVER_ID) + ",";
		body += "\"limit\":10";
		body += "}";
		ctx.POST(s_PendingCallback, "", body);
	}

	static void HandlePendingResponse(string data)
	{
		array<ref BZ_PortalRewardRow> rewards = {};
		ParseRewards(data, rewards);
		foreach (BZ_PortalRewardRow reward : rewards)
		{
			if (!reward || reward.m_iId <= 0 || reward.m_iValue <= 0 || reward.m_sHunterUid.IsEmpty())
				continue;
			if (s_mPaidOrInFlight.Contains(reward.m_iId))
				continue;

			int playerId = FindOnlinePlayerByUid(reward.m_sHunterUid);
			if (playerId <= 0)
				continue;

			PlayerManager pm = GetGame().GetPlayerManager();
			if (!pm)
				continue;
			IEntity player = pm.GetPlayerControlledEntity(playerId);
			if (!player)
				continue;

			s_mPaidOrInFlight.Set(reward.m_iId, true);
			if (BZ_WalletContentsPersistence.AddMoneyToPlayer(player, reward.m_iValue))
			{
				Print(string.Format("[BrasilZ][PortalRewards] Paid bounty #%1: $%2 to %3 for killing %4.", reward.m_iId, reward.m_iValue, reward.m_sHunterName, reward.m_sTargetName), LogLevel.NORMAL);
				ClaimReward(reward);
			}
			else
			{
				s_mPaidOrInFlight.Remove(reward.m_iId);
				Print(string.Format("[BrasilZ][PortalRewards] Could not pay bounty #%1 to uid=%2.", reward.m_iId, reward.m_sHunterUid), LogLevel.WARNING);
			}
		}
	}

	protected static void ClaimReward(BZ_PortalRewardRow reward)
	{
		RestApi api = GetGame().GetRestApi();
		if (!api)
			return;

		RestContext ctx = api.GetContext(BZ_PortalConfig.REWARDS_URL + "/claim");
		if (!ctx)
			return;

		ctx.SetHeaders("Content-Type,application/json");

		string body = "{";
		body += "\"api_key\":" + BZ_PortalWebhook.JsonString(BZ_PortalConfig.API_KEY) + ",";
		body += "\"server_id\":" + BZ_PortalWebhook.JsonString(BZ_PortalConfig.SERVER_ID) + ",";
		body += "\"reward_id\":" + reward.m_iId.ToString() + ",";
		body += "\"hunter_uid\":" + BZ_PortalWebhook.JsonString(reward.m_sHunterUid) + ",";
		body += "\"paid_amount\":" + reward.m_iValue.ToString();
		body += "}";
		ctx.POST(s_ClaimCallback, "", body);
	}

	protected static int FindOnlinePlayerByUid(string uid)
	{
		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return 0;

		array<int> players = {};
		pm.GetPlayers(players);
		foreach (int playerId : players)
		{
			if (BZ_Utils.GetPlayerUID(playerId) == uid)
				return playerId;
		}
		return 0;
	}

	protected static void ParseRewards(string json, notnull array<ref BZ_PortalRewardRow> rewards)
	{
		rewards.Clear();
		int pos = 0;
		while (pos >= 0 && pos < json.Length())
		{
			int idPos = FindFrom(json, "\"id\"", pos);
			if (idPos < 0)
				break;

			int objEnd = FindFrom(json, "}", idPos);
			if (objEnd < 0)
				break;

			string obj = json.Substring(idPos, objEnd - idPos + 1);
			BZ_PortalRewardRow row = new BZ_PortalRewardRow();
			row.m_iId = ReadJsonInt(obj, "id", 0);
			row.m_iValue = ReadJsonInt(obj, "bounty_value", 0);
			ReadJsonString(obj, "hunter_uid", row.m_sHunterUid);
			ReadJsonString(obj, "hunter_name", row.m_sHunterName);
			ReadJsonString(obj, "target_name", row.m_sTargetName);
			rewards.Insert(row);
			pos = objEnd + 1;
		}
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
