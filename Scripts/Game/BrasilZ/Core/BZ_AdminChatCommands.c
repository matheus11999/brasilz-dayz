// Hooks into SCR_ChatPanelManager to read every chat message server-side. If an admin types
// "!restart [seconds] [reason]" or "!cancelrestart", route to BZ_RestartComponent.
//
// Commands:
//   !restart                  → restart in 60s (default).
//   !restart 300              → restart in 300s.
//   !restart 120 reboot       → restart in 120s with reason "reboot".
//   !cancelrestart            → cancel any pending restart.
//
// Admin check: PlayerManager.HasPlayerRole(playerId, EPlayerRole.ADMINISTRATOR).
modded class SCR_ChatPanelManager
{
	override void OnNewMessage(SCR_ChatMessageEventArgs args)
	{
		// Server-side only intercept so commands can't be triggered from other clients.
		if (Replication.IsServer() && args && args.m_iSenderId > 0 && !args.m_sMessage.IsEmpty())
		{
			if (BZ_AdminChatCommands.TryHandle(args.m_iSenderId, args.m_sMessage))
				return;
		}

		super.OnNewMessage(args);
	}
}

class BZ_AdminChatCommands
{
	//------------------------------------------------------------------------------------------------
	static bool TryHandle(int playerId, string message)
	{
		string trimmed = message;
		trimmed.Trim();
		if (trimmed.IsEmpty())
			return false;

		string lower = trimmed;
		lower.ToLower();

		if (lower == "!restart" || lower.StartsWith("!restart "))
			return HandleRestart(playerId, trimmed);

		if (lower == "!cancelrestart")
			return HandleCancelRestart(playerId);

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool HandleRestart(int playerId, string message)
	{
		if (!IsAdmin(playerId))
		{
			Print(string.Format("[BrasilZ][AdminChat] Player %1 tried !restart but is not admin.", playerId), LogLevel.WARNING);
			return true;
		}

		BZ_RestartComponent restart = BZ_RestartComponent.GetInstance();
		if (!restart)
		{
			Print("[BrasilZ][AdminChat] !restart issued but BZ_RestartComponent not found.", LogLevel.ERROR);
			return true;
		}

		// Parse args: !restart [seconds] [reason...]
		int seconds = 60;
		string reason = "";

		array<string> parts = {};
		message.Split(" ", parts, false);

		if (parts.Count() >= 2)
		{
			string secondsStr = parts[1];
			secondsStr.Trim();
			int parsed = secondsStr.ToInt();
			if (parsed > 0)
				seconds = parsed;
		}

		if (parts.Count() >= 3)
		{
			for (int i = 2; i < parts.Count(); i++)
			{
				if (!reason.IsEmpty())
					reason += " ";
				reason += parts[i];
			}
		}

		restart.ForceRestartIn(seconds, reason);
		Print(string.Format("[BrasilZ][AdminChat] Player %1 triggered !restart %2 (%3).", playerId, seconds, reason), LogLevel.NORMAL);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool HandleCancelRestart(int playerId)
	{
		if (!IsAdmin(playerId))
			return true;

		BZ_RestartComponent restart = BZ_RestartComponent.GetInstance();
		if (!restart)
			return true;

		restart.CancelRestart();
		Print(string.Format("[BrasilZ][AdminChat] Player %1 cancelled pending restart.", playerId), LogLevel.NORMAL);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsAdmin(int playerId)
	{
		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return false;

		return pm.HasPlayerRole(playerId, EPlayerRole.ADMINISTRATOR);
	}
}
