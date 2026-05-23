// BrasilZ — DarcMissions fix: dedupe subIdx + cooldown 60min após completion.
//
// Problema vanilla SDRC:
//   - SDRC_MissionHelper.SelectMissionIndex usa confList.GetRandomElement() sem dedup.
//     Random sample com reposição → mesmo subIdx pode spawnar várias vezes seguidas.
//   - Após mission completa, MissionCycleManager respawna em ~5s sem cooldown.
//
// Solução BrasilZ:
//   - Hook SDRC_Mission.MissionStart → adiciona subIdx ao set s_aActiveSubIdx
//   - Hook SDRC_Mission.MissionEnd → remove subIdx do set + registra cooldown
//   - Modded SDRC_MissionHelper.SelectMissionIndex filtra confList:
//     1. Remove subIdx em s_aActiveSubIdx (já active local)
//     2. Remove subIdx com cooldown ativo
//
// Independente de SDRC_Missions_BaseGameMode — tudo trackado local.

class BZ_MissionCooldownTracker
{
	// subIdx → unix timestamp em segundos da última completion
	static ref map<int, int> s_mLastCompletionTime = new map<int, int>();

	// Set de subIdx atualmente active (hook em MissionStart/MissionEnd)
	static ref array<int> s_aActiveSubIdx = new array<int>();

	// Cooldown em segundos. 3600 = 60min.
	static const int BZ_MISSION_COOLDOWN_SEC = 3600;

	//------------------------------------------------------------------------------------------------
	static void RegisterStart(int subIdx, string posName = "")
	{
		if (subIdx < 0)
			return;

		if (!s_aActiveSubIdx.Contains(subIdx))
			s_aActiveSubIdx.Insert(subIdx);

		Print(string.Format("[BrasilZ][MissionDedupe] subIdx %1 START. Active count: %2", subIdx, s_aActiveSubIdx.Count()), LogLevel.NORMAL);

		// Discord webhook: notify mission start.
		if (BZ_DiscordConfig.LOG_MISSION)
		{
			ref array<ref BZ_DiscordField> fields = new array<ref BZ_DiscordField>();
			fields.Insert(new BZ_DiscordField("Mission", posName));
			fields.Insert(new BZ_DiscordField("subIdx", subIdx.ToString()));
			fields.Insert(new BZ_DiscordField("Active", s_aActiveSubIdx.Count().ToString()));

			BZ_DiscordWebhook.Send(
				"🎯 Missão iniciada",
				"Bandidos invadiram **" + posName + "**",
				BZ_DiscordConfig.COLOR_ORANGE,
				fields
			);
		}
	}

	//------------------------------------------------------------------------------------------------
	static void RegisterEnd(int subIdx, string posName = "", bool won = false)
	{
		if (subIdx < 0)
			return;

		int idx = s_aActiveSubIdx.Find(subIdx);
		if (idx >= 0)
			s_aActiveSubIdx.Remove(idx);

		s_mLastCompletionTime.Set(subIdx, System.GetUnixTime());

		Print(string.Format("[BrasilZ][MissionCooldown] subIdx %1 END. Cooldown %2s ativado. Active count: %3", subIdx, BZ_MISSION_COOLDOWN_SEC, s_aActiveSubIdx.Count()), LogLevel.NORMAL);

		// Discord webhook: notify mission end.
		if (BZ_DiscordConfig.LOG_MISSION)
		{
			ref array<ref BZ_DiscordField> fields = new array<ref BZ_DiscordField>();
			fields.Insert(new BZ_DiscordField("Mission", posName));
			fields.Insert(new BZ_DiscordField("subIdx", subIdx.ToString()));
			fields.Insert(new BZ_DiscordField("Cooldown", (BZ_MISSION_COOLDOWN_SEC / 60).ToString() + "min"));

			string title;
			string desc;
			int color;
			if (won)
			{
				title = "✅ Missão concluída";
				desc = "Players retomaram **" + posName + "**";
				color = BZ_DiscordConfig.COLOR_GREEN;
			}
			else
			{
				title = "⏹ Missão encerrada";
				desc = "Missão **" + posName + "** terminou";
				color = BZ_DiscordConfig.COLOR_GRAY;
			}

			BZ_DiscordWebhook.Send(title, desc, color, fields);
		}
	}

	//------------------------------------------------------------------------------------------------
	static bool IsActive(int subIdx)
	{
		return s_aActiveSubIdx.Contains(subIdx);
	}

	//------------------------------------------------------------------------------------------------
	static bool IsInCooldown(int subIdx)
	{
		int lastCompletion = 0;
		if (!s_mLastCompletionTime.Find(subIdx, lastCompletion))
			return false;

		int now = System.GetUnixTime();
		int elapsed = now - lastCompletion;
		return elapsed < BZ_MISSION_COOLDOWN_SEC;
	}

	//------------------------------------------------------------------------------------------------
	static int GetRemainingCooldown(int subIdx)
	{
		int lastCompletion = 0;
		if (!s_mLastCompletionTime.Find(subIdx, lastCompletion))
			return 0;

		int now = System.GetUnixTime();
		int elapsed = now - lastCompletion;
		return Math.Max(0, BZ_MISSION_COOLDOWN_SEC - elapsed);
	}
}

//------------------------------------------------------------------------------------------------
// Override SelectMissionIndex pra excluir subIdx duplicados + em cooldown.
modded class SDRC_MissionHelper
{
	static override int SelectMissionIndex(array<ref int> confList, int missionSubIdx)
	{
		// Quando missionSubIdx != -1, mantém comportamento vanilla (retorna valor direto).
		if (missionSubIdx != -1)
		{
			if (missionSubIdx > -1 && missionSubIdx < confList.Count())
				return missionSubIdx;
			return -1;
		}

		if (confList.IsEmpty())
		{
			SDRC_Log.Add("[SDRC_MissionHelper:SelectMissionIndex] Mission list is empty.", LogLevel.ERROR);
			return -1;
		}

		// Filtra confList: remove ativos + em cooldown.
		array<int> available = {};
		foreach (int subIdx : confList)
		{
			if (BZ_MissionCooldownTracker.IsActive(subIdx))
				continue;

			if (BZ_MissionCooldownTracker.IsInCooldown(subIdx))
			{
				int remaining = BZ_MissionCooldownTracker.GetRemainingCooldown(subIdx);
				Print(string.Format("[BrasilZ][MissionCooldown] subIdx %1 em cooldown, %2s restantes", subIdx, remaining), LogLevel.NORMAL);
				continue;
			}

			available.Insert(subIdx);
		}

		if (available.IsEmpty())
		{
			Print("[BrasilZ][MissionDedupe] Nenhum subIdx disponivel. Skip spawn.", LogLevel.NORMAL);
			return -1;
		}

		int picked = available.GetRandomElement();
		Print(string.Format("[BrasilZ][MissionDedupe] Picked subIdx %1 (disponiveis: %2/%3)", picked, available.Count(), confList.Count()), LogLevel.NORMAL);
		return picked;
	}
}

//------------------------------------------------------------------------------------------------
// Hooks em SDRC_Mission para track active + cooldown.
modded class SDRC_Mission
{
	override void MissionStart()
	{
		super.MissionStart();

		int subIdx = GetSubIdx();
		string posName = GetPosName();
		if (posName.IsEmpty())
			posName = string.Format("subIdx %1", subIdx);

		BZ_MissionCooldownTracker.RegisterStart(subIdx, posName);
	}

	override void MissionEnd()
	{
		int subIdx = GetSubIdx();
		string posName = GetPosName();
		if (posName.IsEmpty())
			posName = string.Format("subIdx %1", subIdx);

		// IsWin() not exposed on every SDRC version, leave false.
		bool won = false;

		BZ_MissionCooldownTracker.RegisterEnd(subIdx, posName, won);

		super.MissionEnd();
	}
}
