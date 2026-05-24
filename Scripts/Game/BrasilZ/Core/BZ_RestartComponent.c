[ComponentEditorProps(category: "BrasilZ/Core", description: "Scheduled server restart with chat warnings, ReforgedZ-style.")]
class BZ_RestartComponentClass : ScriptComponentClass
{
}

class BZ_RestartComponent : ScriptComponent
{
	protected static BZ_RestartComponent s_Instance;
	protected Widget m_wBzRestartMsgListRoot;
	protected VerticalLayoutWidget m_wBzRestartMsgList;

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
	protected bool m_bFourMinWarned;
	protected bool m_bThreeMinWarned;
	protected bool m_bTwoMinWarned;
	protected bool m_bOneMinWarned;
	protected bool m_bRestartTriggered;
	protected bool m_bManualRestartActive;
	protected bool m_bFinalSaveStarted;
	protected bool m_bSaveRoundStarted;
	protected bool m_bKick10sScheduled;

	protected static const int BZ_SHUTDOWN_LEAD_SEC = 180;
	protected static const int BZ_PRE_KICK_WARNING_SEC = 10;
	protected static const int BZ_MANUAL_MIN_RESTART_SEC = 180;

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

		// EOnInit não disparou no servidor (log mostrava AutoSave OK mas zero Restart logs).
		// Componente attachado em vanilla GameMode prefab às vezes não recebe EVENT_INIT.
		// Fallback: agenda Init via CallLater no OnPostInit (sempre dispara).
		GetGame().GetCallqueue().CallLater(BZ_TryInit, 5000, false);
	}

	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);
		BZ_TryInit();
	}

	//------------------------------------------------------------------------------------------------
	// Idempotent. Chamado de OnPostInit (via CallLater) E de EOnInit. Re-entrada vira no-op
	// via m_iTargetRestartUnixTime > 0 guard.
	protected void BZ_TryInit()
	{
		if (IsProxy() || !Replication.IsServer())
			return;
		if (m_iTargetRestartUnixTime > 0)
		{
			Print("[BrasilZ][Restart] BZ_TryInit re-entry — already armed.", LogLevel.NORMAL);
			return;
		}

		Print("[BrasilZ][Restart] BZ_TryInit fired — parsing config + arming.", LogLevel.NORMAL);
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
	// PROD: usa os horários UTC configurados no layer.
	// BZ_UPTIME_MODE=true → uptime-based (X segs pós-boot).
	// BZ_UPTIME_MODE=false → fallback UTC hours (m_sRestartHoursUTC do layer).
	protected static const bool BZ_UPTIME_MODE = false;
	protected static const int BZ_UPTIME_RESTART_SEC = 14400;

	// Static flag pra outros sistemas checarem se shutdown está rolando.
	// BZ_GameMode.OnPlayerConnected lê isso pra rejeitar reconnects durante kick window.
	protected static bool s_bShutdownInProgress = false;

	static bool IsShutdownInProgress()
	{
		return s_bShutdownInProgress;
	}

	protected void Init()
	{
		int timer;
		if (BZ_UPTIME_MODE)
		{
			timer = BZ_UPTIME_RESTART_SEC;
			Print(string.Format("[BrasilZ][Restart] UPTIME MODE: restart em %1s pós-boot (%2h)", timer, timer / 3600), LogLevel.NORMAL);
		}
		else
		{
			timer = ComputeNextRestartTimer();
		}

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
		if (timer <= 240)
			m_bFourMinWarned = true;
		if (timer <= 180)
			m_bThreeMinWarned = true;
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
		int shutdownLeadSec = BZ_GetShutdownLeadSeconds();

		// Arma o shutdown antes dos avisos de minuto. Se o tick cair dentro da janela
		// final, os avisos abaixo ja sabem que o kick foi agendado/feito.
		if (!m_bSaveRoundStarted && timeLeft <= shutdownLeadSec + BZ_PRE_KICK_WARNING_SEC)
		{
			m_bSaveRoundStarted = true;
			s_bShutdownInProgress = true;
			if (m_bManualRestartActive)
				Broadcast(string.Format("DESCONEXAO AUTOMATICA EM %1s. Servidor reinicia em %2s. Reconexoes bloqueadas.", BZ_PRE_KICK_WARNING_SEC, timeLeft));
			else
				Broadcast(string.Format("DESCONEXAO AUTOMATICA EM %1s. Reconexoes bloqueadas. Servidor reinicia em %2.", BZ_PRE_KICK_WARNING_SEC, BZ_FormatDuration(timeLeft)));
			GetGame().GetCallqueue().CallLater(BZ_StartShutdownSequence, BZ_PRE_KICK_WARNING_SEC * 1000, false);
		}

		if (!m_bThirtyMinWarned && timeLeft <= 1800)
		{
			m_bThirtyMinWarned = true;
			BZ_BroadcastRestartWarning("30 MINUTOS", timeLeft, shutdownLeadSec);
		}

		if (!m_bTenMinWarned && timeLeft <= 600)
		{
			m_bTenMinWarned = true;
			BZ_BroadcastRestartWarning("10 MINUTOS", timeLeft, shutdownLeadSec);
		}

		if (!m_bFiveMinWarned && timeLeft <= 300)
		{
			m_bFiveMinWarned = true;
			BZ_BroadcastRestartWarning("5 MINUTOS", timeLeft, shutdownLeadSec);
			Broadcast("Deslogue para seu corpo nao ficar no jogo :) ou volte apos o restart");
		}

		if (!m_bFourMinWarned && timeLeft <= 240)
		{
			m_bFourMinWarned = true;
			BZ_BroadcastRestartWarning("4 MINUTOS", timeLeft, shutdownLeadSec);
		}

		if (!m_bThreeMinWarned && timeLeft <= 180)
		{
			m_bThreeMinWarned = true;
			BZ_BroadcastRestartWarning("3 MINUTOS", timeLeft, shutdownLeadSec);
		}

		if (!m_bTwoMinWarned && timeLeft <= 120)
		{
			m_bTwoMinWarned = true;
			BZ_BroadcastRestartWarning("2 MINUTOS", timeLeft, shutdownLeadSec);
		}

		if (!m_bOneMinWarned && timeLeft <= 60)
		{
			m_bOneMinWarned = true;
			BZ_BroadcastRestartWarning("1 MINUTO", timeLeft, shutdownLeadSec);
		}

		if (!m_bRestartTriggered && timeLeft <= 0)
		{
			m_bRestartTriggered = true;
			GetGame().GetCallqueue().Remove(CheckRestartTime);
			Broadcast("Servidor reiniciando agora. Reconecte em breve.");
			Print("[BrasilZ][Restart] Triggering close after 30s shutdown sequence.", LogLevel.NORMAL);
			GetGame().GetCallqueue().CallLater(DoClose, 2000, false);
		}
	}

	//------------------------------------------------------------------------------------------------
	// Master shutdown sequence — dispara aos 90s pré-close.
	// NOVA abordagem: usa vanilla audit timeout (same path manual disconnect que comprovadamente
	// funciona). Pre-kick: APENAS kick (sem save+delete inline). Vanilla disconnect chain adiciona
	// player ao SCR_ReconnectComponent reconnect list. Audit timeout (~60s) dispara
	// OnPlayerAuditTimeouted → SaveAndRemoveCharacter → body removed enquanto server alive →
	// AutoSave seguinte captura WorldState sem entity → close.
	protected int BZ_GetShutdownLeadSeconds()
	{
		return BZ_SHUTDOWN_LEAD_SEC;
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_BroadcastRestartWarning(string restartLabel, int timeLeft, int shutdownLeadSec)
	{
		if (m_bSaveRoundStarted)
		{
			Broadcast(string.Format("SERVIDOR REINICIA EM %1. Reconexoes bloqueadas.", restartLabel));
			return;
		}

		int kickLeft = timeLeft - shutdownLeadSec;
		if (kickLeft <= 0)
		{
			Broadcast(string.Format("DESCONEXAO AUTOMATICA AGORA. Servidor reinicia em %1.", restartLabel));
			return;
		}

		Broadcast(string.Format("DESCONEXAO AUTOMATICA EM %1. Servidor reinicia em %2.", BZ_FormatDuration(kickLeft), restartLabel));
	}

	//------------------------------------------------------------------------------------------------
	protected string BZ_FormatDuration(int seconds)
	{
		if (seconds <= 0)
			return "0s";

		int minutes = seconds / 60;
		int restSeconds = seconds % 60;

		if (minutes <= 0)
			return string.Format("%1s", restSeconds);

		if (restSeconds <= 0)
			return string.Format("%1 minuto(s)", minutes);

		return string.Format("%1 minuto(s) e %2s", minutes, restSeconds);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_StartShutdownSequence()
	{
		Print("[BrasilZ][Restart] === SHUTDOWN SEQUENCE BEGIN (captured entity cleanup) ===", LogLevel.NORMAL);

		// 1. Kick all — captura entities ANTES + dispara kick. Vanilla disconnect chain handle saves.
		KickAllPlayers();

		// 2. 2s pós-kick: SaveAndRemove cada entity capturada. Vanilla chain já completou
		// saves wallet/metabolism/SaveQueue. Agora removemos do mundo + WorldState.
		GetGame().GetCallqueue().CallLater(BZ_RemoveCapturedEntities, 2000, false);

		// 3. Flushes WorldState pós-delete. Captura state sem entities.
		GetGame().GetCallqueue().CallLater(BZ_ForceWorldStateFlush, 5000, false);
		GetGame().GetCallqueue().CallLater(BZ_ForceWorldStateFlush, 15000, false);
		GetGame().GetCallqueue().CallLater(BZ_ForceWorldStateFlush, 30000, false);
		GetGame().GetCallqueue().CallLater(BZ_ForceWorldStateFlush, 60000, false);
		GetGame().GetCallqueue().CallLater(BZ_ForceWorldStateFlush, 80000, false);
		GetGame().GetCallqueue().CallLater(BZ_ForceWorldStateFlush, 105000, false);

		Print("[BrasilZ][Restart] === SHUTDOWN ARMED: kick + remove entities em 2s + 6 flushes ===", LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	// Pós-vanilla-disconnect-chain: itera lista capturada pre-kick + SaveAndRemove cada.
	// Independente do reconnect list (kicks não populam ele).
	protected void BZ_RemoveCapturedEntities()
	{
		int count = m_aBzCapturedKickedEntities.Count();
		if (count == 0)
		{
			Print("[BrasilZ][Restart] BZ_RemoveCapturedEntities: lista vazia.", LogLevel.NORMAL);
			return;
		}

		Print(string.Format("[BrasilZ][Restart] BZ_RemoveCapturedEntities: processando %1 entities.", count), LogLevel.NORMAL);

		for (int i = 0; i < count; i++)
		{
			IEntity entity = m_aBzCapturedKickedEntities[i];
			int playerId = m_aBzCapturedKickedPlayerIds[i];
			if (!entity || entity.IsDeleted())
			{
				Print(string.Format("[BrasilZ][Restart] Player %1 entity já deletada/null — skip.", playerId), LogLevel.NORMAL);
				continue;
			}
			BZ_SaveAndRemoveEntity(entity, playerId);
		}

		m_aBzCapturedKickedEntities.Clear();
		m_aBzCapturedKickedPlayerIds.Clear();
	}

	//------------------------------------------------------------------------------------------------
	// Replica BZ_ReconnectComponent.SaveAndRemoveCharacter. Save + ReleaseTracking + DeleteRplEntity.
	// Dead char preserva corpo (loot window).
	protected void BZ_SaveAndRemoveEntity(IEntity entity, int playerId)
	{
		if (!entity || entity.IsDeleted())
			return;

		ChimeraCharacter character = ChimeraCharacter.Cast(entity);
		if (character)
		{
			CharacterControllerComponent cc = character.GetCharacterController();
			if (cc && cc.IsDead())
			{
				SCR_PersistenceSystem deadPersist = SCR_PersistenceSystem.GetByEntityWorld(entity);
				if (deadPersist && deadPersist.GetState() == EPersistenceSystemState.ACTIVE)
					deadPersist.Save(entity, ESaveGameType.AUTO);
				Print(string.Format("[BrasilZ][Restart] Player %1 dead — corpse preservado.", playerId), LogLevel.NORMAL);
				return;
			}
		}

		SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByEntityWorld(entity);
		if (!persistence || persistence.GetState() != EPersistenceSystemState.ACTIVE)
		{
			Print(string.Format("[BrasilZ][Restart] Player %1 persistence not ACTIVE — força delete entity.", playerId), LogLevel.WARNING);
			RplComponent.DeleteRplEntity(entity, false);
			return;
		}

		UUID charId = persistence.GetId(entity);
		persistence.Save(entity, ESaveGameType.AUTO);
		persistence.ReleaseTracking(entity);
		RplComponent.DeleteRplEntity(entity, false);
		Print(string.Format("[BrasilZ][Restart] Player %1 saved + deleted (charId=%2).", playerId, charId), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	// Pega SCR_ReconnectComponent e chama BZ_ForceAuditAllNow. Garante que SaveAndRemoveCharacter
	// roda pra todos entities reservadas (alive disconnects) ANTES do server close. Sem isso,
	// audit timeout vanilla (>90s) não dispara, entities persistem em WorldState → body fantasma.
	protected void BZ_ForceAuditCleanup()
	{
		SCR_BaseGameMode gm = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
		if (!gm)
		{
			Print("[BrasilZ][Restart] BZ_ForceAuditCleanup: gamemode null", LogLevel.WARNING);
			return;
		}

		SCR_ReconnectComponent rc = SCR_ReconnectComponent.Cast(gm.FindComponent(SCR_ReconnectComponent));
		if (!rc)
		{
			Print("[BrasilZ][Restart] BZ_ForceAuditCleanup: SCR_ReconnectComponent não encontrado", LogLevel.WARNING);
			return;
		}

		Print("[BrasilZ][Restart] BZ_ForceAuditCleanup: chamando BZ_ForceAuditAllNow.", LogLevel.NORMAL);
		rc.BZ_ForceAuditAllNow();
	}

	//------------------------------------------------------------------------------------------------
	// Stub que tenta parar AutoSave do BZ_GameMode. Acessa singleton SCR_BaseGameMode.
	// Se não conseguir, log warning e segue (AutoSave roda só 1x por minuto, baixo risco).
	protected void BZ_GameMode_StopAutoSave()
	{
		SCR_BaseGameMode gm = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
		if (!gm)
		{
			Print("[BrasilZ][Restart] BZ_GameMode_StopAutoSave: gamemode null", LogLevel.WARNING);
			return;
		}
		// Não tem API pública pra parar autosave; flag interno seria ideal mas requer
		// modded class. Por ora só logamos — fluxo principal independe disso.
		Print("[BrasilZ][Restart] AutoSave stop request (no-op, sem hook público)", LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	// Força flush do WorldState pro disco. Chamado múltiplas vezes na janela shutdown pra
	// garantir DB async commit completar antes do RequestClose.
	protected void BZ_ForceWorldStateFlush()
	{
		SaveGameManager saveManager = GetGame().GetSaveGameManager();
		if (!saveManager)
		{
			Print("[BrasilZ][Restart] BZ_ForceWorldStateFlush: SaveGameManager null", LogLevel.WARNING);
			return;
		}
		if (!saveManager.IsSavingPossible())
		{
			Print("[BrasilZ][Restart] BZ_ForceWorldStateFlush: saving not possible (transaction em curso?)", LogLevel.NORMAL);
			return;
		}
		SCR_BaseGameMode.BZ_OverwriteLatestSave(saveManager);
		Print(string.Format("[BrasilZ][Restart] WorldState flush triggered at unix=%1", System.GetUnixTime()), LogLevel.NORMAL);
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

		if (seconds < BZ_MANUAL_MIN_RESTART_SEC)
			seconds = BZ_MANUAL_MIN_RESTART_SEC;

		m_bManualRestartActive = true;
		m_iTargetRestartUnixTime = System.GetUnixTime() + seconds;

		// Reset warning flags so they re-fire in the manual window if applicable.
		m_bThirtyMinWarned = (seconds <= 1800);
		m_bTenMinWarned = (seconds <= 600);
		m_bFiveMinWarned = (seconds <= 300);
		m_bFourMinWarned = (seconds <= 240);
		m_bThreeMinWarned = (seconds <= 180);
		m_bTwoMinWarned = (seconds <= 120);
		m_bOneMinWarned = (seconds <= 60);

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
		GetGame().GetCallqueue().Remove(DoFinalSaveThenClose);
		GetGame().GetCallqueue().Remove(KickAllPlayers);
		GetGame().GetCallqueue().Remove(BZ_StartShutdownSequence);
		GetGame().GetCallqueue().Remove(BZ_ForceWorldStateFlush);
		GetGame().GetCallqueue().Remove(BZ_RemoveCapturedEntities);
		m_bRestartTriggered = false;
		m_bManualRestartActive = false;
		m_bFinalSaveStarted = false;
		m_bSaveRoundStarted = false;
		m_bKick10sScheduled = false;

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

		BZ_SendVisibleRestartNotification(message);
		Rpc(RpcDo_ShowBigRestartMessage, message);
		RpcDo_ShowBigRestartMessage(message);

		Rpc(RpcDo_ShowMessage, message);
		// Also show locally on server-host (Broadcast RPC skips the sender).
		RpcDo_ShowMessage(message);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_SendVisibleRestartNotification(string message)
	{
		if (message.Contains("10 MINUTOS") || message.Contains("600 segundos"))
		{
			SCR_NotificationsComponent.SendToEveryone(ENotification.SERVER_RESTART_10MIN);
			return;
		}

		if (message.Contains("5 MINUTOS") || message.Contains("300 segundos"))
		{
			SCR_NotificationsComponent.SendToEveryone(ENotification.SERVER_RESTART_5MIN);
			return;
		}

		if (message.Contains("3 MINUTOS") || message.Contains("180 segundos"))
		{
			SCR_NotificationsComponent.SendToEveryone(ENotification.SERVER_RESTART_3MIN);
			return;
		}

		if (message.Contains("1 MINUTO") || message.Contains("60 segundos"))
			SCR_NotificationsComponent.SendToEveryone(ENotification.SERVER_RESTART_1MIN);
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

	// Lista de entities capturadas pré-kick pra SaveAndRemove pós-vanilla-chain.
	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_ShowBigRestartMessage(string messageContent)
	{
		if (!GetGame() || !GetGame().GetWorkspace())
			return;

		if (!m_wBzRestartMsgList)
		{
			m_wBzRestartMsgListRoot = GetGame().GetWorkspace().CreateWidgets("{B7A1D3F4304C91A0}UI/Layouts/Notifications/BZ_RestartMsgList.layout");
			if (m_wBzRestartMsgListRoot)
				m_wBzRestartMsgList = VerticalLayoutWidget.Cast(m_wBzRestartMsgListRoot.FindAnyWidget("BZRestartMsgListVerticalLayout"));
		}

		if (!m_wBzRestartMsgList)
		{
			Print("[BrasilZ][Restart] Big restart notification UI not ready.", LogLevel.WARNING);
			return;
		}

		Widget widget = GetGame().GetWorkspace().CreateWidgets("{B7A1D3F4304C91B0}UI/Layouts/Notifications/BZ_RestartMsgBox.layout", m_wBzRestartMsgList);
		if (!widget)
		{
			Print("[BrasilZ][Restart] Failed to create big restart notification widget.", LogLevel.WARNING);
			return;
		}

		BZ_RestartMsgBox msgBox = BZ_RestartMsgBox.Cast(widget.FindHandler(BZ_RestartMsgBox));
		if (msgBox)
			msgBox.SetText(messageContent);
	}

	protected ref array<IEntity> m_aBzCapturedKickedEntities = new array<IEntity>();
	protected ref array<int> m_aBzCapturedKickedPlayerIds = new array<int>();

	//------------------------------------------------------------------------------------------------
	// Kick todos players. ANTES do kick, captura entity referenes — vanilla SCR_ReconnectComponent
	// NÃO adiciona players ao reconnect list em kicks explícitos (log mostrou "reconnect list vazia").
	// Pós-kick + vanilla disconnect chain (~1s), iteramos lista capturada e SaveAndRemove manualmente
	// cada entity. Garante body removal sem depender de audit timeout vanilla.
	protected void KickAllPlayers()
	{
		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
		{
			Print("[BrasilZ][Restart] KickAllPlayers — PlayerManager null", LogLevel.WARNING);
			return;
		}

		array<int> players = {};
		pm.GetPlayers(players);

		// 1. Capture entities ANTES do kick pra usar pós-chain.
		m_aBzCapturedKickedEntities.Clear();
		m_aBzCapturedKickedPlayerIds.Clear();
		foreach (int playerId : players)
		{
			IEntity character = pm.GetPlayerControlledEntity(playerId);
			if (character)
			{
				m_aBzCapturedKickedEntities.Insert(character);
				m_aBzCapturedKickedPlayerIds.Insert(playerId);
				Print(string.Format("[BrasilZ][Restart] Captured entity for player %1 pre-kick.", playerId), LogLevel.NORMAL);
			}
		}

		// 2. Kick all.
		foreach (int playerId : players)
		{
			// Bohemia docs: KickPlayer(int iPlayerId, PlayerManagerKickReason reason, int timeout=0).
			// Vanilla OnPlayerDisconnected chain dispara save (wallet/metabolism/persistence).
			// SCR_ReconnectComponent adiciona entity ao reconnect list com audit timeout.
			// Audit fires em ~60s → BZ_ReconnectComponent.OnPlayerAuditTimeouted →
			// SaveAndRemoveCharacter (save final + delete entity).
			pm.KickPlayer(playerId, PlayerManagerKickReason.KICK, 0);
		}

		Print(string.Format("[BrasilZ][Restart] Kicked %1 players (vanilla audit timeout cleanup ~60s).", players.Count()), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	// Inline copy de BZ_ReconnectComponent.SaveAndRemoveCharacter — save alive char + delete entity.
	// Dead char: só save (preserva corpo pra loot). Chamado pré-kick no restart.
	protected void BZ_SaveAndRemoveAlive(int playerId, IEntity entity)
	{
		if (!entity)
			return;

		ChimeraCharacter character = ChimeraCharacter.Cast(entity);
		if (character)
		{
			CharacterControllerComponent cc = character.GetCharacterController();
			if (cc && cc.IsDead())
			{
				// Dead body — save only, keep corpse pra loot.
				SCR_PersistenceSystem deadPersist = SCR_PersistenceSystem.GetByEntityWorld(entity);
				if (deadPersist && deadPersist.GetState() == EPersistenceSystemState.ACTIVE)
					deadPersist.Save(entity, ESaveGameType.AUTO);
				Print(string.Format("[BrasilZ][Restart] Player %1 dead — corpse preservado, sem delete", playerId), LogLevel.NORMAL);
				return;
			}
		}

		SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetByEntityWorld(entity);
		if (!persistence || persistence.GetState() != EPersistenceSystemState.ACTIVE)
		{
			Print(string.Format("[BrasilZ][Restart] Player %1 persistence not ACTIVE — skip remove", playerId), LogLevel.WARNING);
			return;
		}

		UUID charId = persistence.GetId(entity);
		if (!charId)
		{
			Print(string.Format("[BrasilZ][Restart] Player %1 charId NULL — force delete entity", playerId), LogLevel.ERROR);
			RplComponent.DeleteRplEntity(entity, false);
			return;
		}

		persistence.Save(entity, ESaveGameType.AUTO);
		persistence.ReleaseTracking(entity);
		RplComponent.DeleteRplEntity(entity, false);
		Print(string.Format("[BrasilZ][Restart] Player %1 alive char saved + deleted (charId=%2)", playerId, charId), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	protected void DoClose()
	{
		if (Replication.IsServer())
			GetGame().RequestClose();
	}

	//------------------------------------------------------------------------------------------------
	protected void DoFinalSaveThenClose()
	{
		if (!Replication.IsServer())
			return;

		if (m_bFinalSaveStarted)
			return;
		m_bFinalSaveStarted = true;

		BZ_SaveOnlinePlayers();
		GetGame().GetCallqueue().CallLater(DoClose, 5000, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_SaveOnlinePlayers()
	{
		PlayerManager playerManager = GetGame().GetPlayerManager();
		SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetScriptedInstance();
		SaveGameManager saveManager = GetGame().GetSaveGameManager();

		if (!playerManager || !persistence || persistence.GetState() != EPersistenceSystemState.ACTIVE)
		{
			Print("[BrasilZ][Restart] Final save skipped: persistence/player manager not ready.", LogLevel.WARNING);
			return;
		}

		array<int> players = {};
		playerManager.GetPlayers(players);

		foreach (int playerId : players)
		{
			PlayerController controller = playerManager.GetPlayerController(playerId);
			if (controller)
				persistence.Save(controller, ESaveGameType.AUTO);

			IEntity character = playerManager.GetPlayerControlledEntity(playerId);
			if (character)
				persistence.Save(character, ESaveGameType.AUTO);
		}

		if (saveManager && saveManager.IsSavingPossible())
			SCR_BaseGameMode.BZ_OverwriteLatestSave(saveManager);

		Print(string.Format("[BrasilZ][Restart] Final save requested for %1 online player(s).", players.Count()), LogLevel.NORMAL);
	}
}
