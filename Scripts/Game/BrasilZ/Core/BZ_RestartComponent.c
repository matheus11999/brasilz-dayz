[ComponentEditorProps(category: "BrasilZ/Core", description: "Scheduled server restart with chat warnings, ReforgedZ-style.")]
class BZ_RestartComponentClass : ScriptComponentClass
{
}

class BZ_RestartComponent : ScriptComponent
{
	protected static BZ_RestartComponent s_Instance;

	[Attribute("0,4,8,12,16,20", UIWidgets.EditBox, "Comma-separated UTC hours when the server should restart.")]
	protected string m_sRestartHoursUTC;

	[Attribute("7200", UIWidgets.EditBox, "Minimum uptime in seconds before allowing a scheduled restart (skip slot if too early).")]
	protected int m_iMinUptimeSec;

	[Attribute("10000", UIWidgets.EditBox, "Polling interval in milliseconds.")]
	protected int m_iPollIntervalMs;

	protected ref array<int> m_aRestartHoursUTC = new array<int>();
	protected int m_iTargetRestartUnixTime;
	protected int m_iStartupUnixTime;

	protected bool m_bThirtyMinWarned;
	protected bool m_bTenMinWarned;
	protected bool m_bFiveMinWarned;
	protected bool m_bTwoMinWarned;
	protected bool m_bOneMinWarned;
	protected bool m_bRestartTriggered;
	protected bool m_bManualRestartActive;

	//------------------------------------------------------------------------------------------------
	static BZ_RestartComponent GetInstance()
	{
		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	bool IsProxy()
	{
		RplComponent rpl = RplComponent.Cast(GetOwner().FindComponent(RplComponent));
		return rpl && rpl.IsProxy();
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		SetEventMask(owner, EntityEvent.INIT);
		s_Instance = this;
	}

	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);

		if (IsProxy() || !Replication.IsServer())
			return;

		ParseRestartHours();
		m_iStartupUnixTime = System.GetUnixTime();
		Init();
	}

	//------------------------------------------------------------------------------------------------
	protected void ParseRestartHours()
	{
		m_aRestartHoursUTC.Clear();

		if (m_sRestartHoursUTC.IsEmpty())
			return;

		array<string> parts = {};
		m_sRestartHoursUTC.Split(",", parts, false);

		foreach (string p : parts)
		{
			string trimmed = p;
			trimmed.Trim();
			if (trimmed.IsEmpty())
				continue;

			int hour = trimmed.ToInt();
			if (hour < 0 || hour > 23)
				continue;

			m_aRestartHoursUTC.Insert(hour);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void Init()
	{
		int timer = ComputeNextRestartTimer();
		if (timer <= 0)
		{
			Print("[BrasilZ][Restart] No valid restart slot configured. Scheduler idle.", LogLevel.WARNING);
			return;
		}

		m_iTargetRestartUnixTime = System.GetUnixTime() + timer;

		if (timer <= 1800)
			m_bThirtyMinWarned = true;
		if (timer <= 600)
			m_bTenMinWarned = true;
		if (timer <= 300)
			m_bFiveMinWarned = true;
		if (timer <= 120)
			m_bTwoMinWarned = true;
		if (timer <= 60)
			m_bOneMinWarned = true;

		GetGame().GetCallqueue().CallLater(CheckRestartTime, m_iPollIntervalMs, true);

		Print(string.Format("[BrasilZ][Restart] Scheduled restart in %1 seconds (unix %2).", timer, m_iTargetRestartUnixTime), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	// Compute seconds until next configured restart hour, skipping slots that would happen sooner
	// than m_iMinUptimeSec (prevents back-to-back restarts after a slow startup).
	protected int ComputeNextRestartTimer()
	{
		if (m_aRestartHoursUTC.IsEmpty())
			return 0;

		int currentUnixTime = System.GetUnixTime();
		int secondsInDay = currentUnixTime % 86400;
		int currentHour = secondsInDay / 3600;

		int candidate = -1;
		foreach (int h : m_aRestartHoursUTC)
		{
			int secsUntil = (h * 3600) - secondsInDay;
			if (secsUntil <= 0)
				continue;

			if (secsUntil < m_iMinUptimeSec)
				continue;

			candidate = secsUntil;
			break;
		}

		if (candidate <= 0)
		{
			// Wrap to tomorrow's first slot
			candidate = ((m_aRestartHoursUTC[0] + 24) * 3600) - secondsInDay;
			if (candidate < m_iMinUptimeSec && m_aRestartHoursUTC.Count() > 1)
			{
				for (int i = 1; i < m_aRestartHoursUTC.Count(); i++)
				{
					int alt = ((m_aRestartHoursUTC[i] + 24) * 3600) - secondsInDay;
					if (alt >= m_iMinUptimeSec)
					{
						candidate = alt;
						break;
					}
				}
			}
		}

		return candidate;
	}

	//------------------------------------------------------------------------------------------------
	protected void CheckRestartTime()
	{
		int timeLeft = m_iTargetRestartUnixTime - System.GetUnixTime();

		if (!m_bThirtyMinWarned && timeLeft <= 1800)
		{
			m_bThirtyMinWarned = true;
			Broadcast("SERVIDOR REINICIA EM 30 MINUTOS!");
		}

		if (!m_bTenMinWarned && timeLeft <= 600)
		{
			m_bTenMinWarned = true;
			Broadcast("SERVIDOR REINICIA EM 10 MINUTOS!");
		}

		if (!m_bFiveMinWarned && timeLeft <= 300)
		{
			m_bFiveMinWarned = true;
			Broadcast("SERVIDOR REINICIA EM 5 MINUTOS!");
		}

		if (!m_bTwoMinWarned && timeLeft <= 120)
		{
			m_bTwoMinWarned = true;
			Broadcast("SERVIDOR REINICIA EM 2 MINUTOS!");
		}

		if (!m_bOneMinWarned && timeLeft <= 60)
		{
			m_bOneMinWarned = true;
			Broadcast("SERVIDOR REINICIA EM 1 MINUTO!");
		}

		if (!m_bRestartTriggered && timeLeft <= 0)
		{
			m_bRestartTriggered = true;
			GetGame().GetCallqueue().Remove(CheckRestartTime);
			Broadcast("Servidor reiniciando agora. Reconecte em breve.");
			Print("[BrasilZ][Restart] Closing server now.", LogLevel.NORMAL);
			GetGame().GetCallqueue().CallLater(DoClose, 3000, false);
		}
	}

	//------------------------------------------------------------------------------------------------
	// Force a restart in N seconds. Admin-triggered (via chat command).
	void ForceRestartIn(int seconds, string reason = "")
	{
		if (m_bRestartTriggered || m_bManualRestartActive)
		{
			Print("[BrasilZ][Restart] Manual restart ignored: already counting down.", LogLevel.WARNING);
			return;
		}

		if (seconds < 5)
			seconds = 5;

		m_bManualRestartActive = true;
		m_iTargetRestartUnixTime = System.GetUnixTime() + seconds;

		// Reset warning flags so they re-fire in the manual window if applicable.
		m_bThirtyMinWarned = (seconds < 1800);
		m_bTenMinWarned = (seconds < 600);
		m_bFiveMinWarned = (seconds < 300);
		m_bTwoMinWarned = (seconds < 120);
		m_bOneMinWarned = (seconds < 60);

		GetGame().GetCallqueue().Remove(CheckRestartTime);
		GetGame().GetCallqueue().CallLater(CheckRestartTime, m_iPollIntervalMs, true);

		string banner = "ADMIN: servidor reinicia em " + seconds.ToString() + " segundos.";
		if (!reason.IsEmpty())
			banner += " Motivo: " + reason;

		Broadcast(banner);
		Print(string.Format("[BrasilZ][Restart] Manual restart in %1s. Reason='%2'", seconds, reason), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	// Cancel pending restart (manual or scheduled). Useful for admin abort.
	void CancelRestart()
	{
		GetGame().GetCallqueue().Remove(CheckRestartTime);
		GetGame().GetCallqueue().Remove(DoClose);
		m_bRestartTriggered = false;
		m_bManualRestartActive = false;

		Broadcast("ADMIN: reinicio cancelado.");
		Print("[BrasilZ][Restart] Pending restart cancelled.", LogLevel.NORMAL);

		// Re-arm the next scheduled slot.
		Init();
	}

	//------------------------------------------------------------------------------------------------
	int GetTimeUntilRestart()
	{
		if (m_iTargetRestartUnixTime <= 0)
			return -1;

		return Math.Max(0, m_iTargetRestartUnixTime - System.GetUnixTime());
	}

	//------------------------------------------------------------------------------------------------
	protected void Broadcast(string message)
	{
		Print(string.Format("[BrasilZ][Restart] %1", message), LogLevel.NORMAL);

		if (!Replication.IsServer())
			return;

		Rpc(RpcDo_ShowMessage, message);
		// Also show locally on server-host (Broadcast RPC skips the sender).
		RpcDo_ShowMessage(message);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_ShowMessage(string messageContent)
	{
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return;

		SCR_ChatComponent chatComponent = SCR_ChatComponent.Cast(pc.FindComponent(SCR_ChatComponent));
		if (!chatComponent)
			return;

		chatComponent.ShowMessage(string.Format("[SERVER] %1", messageContent));
	}

	//------------------------------------------------------------------------------------------------
	protected void DoClose()
	{
		if (Replication.IsServer())
			GetGame().RequestClose();
	}
}
