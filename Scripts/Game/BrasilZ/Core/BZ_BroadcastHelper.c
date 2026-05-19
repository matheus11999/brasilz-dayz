// Broadcast helper — sends a chat-style message to every connected player via the vanilla
// SCR_ChatPanelManager so server warnings show up in their chat window.
class BZ_BroadcastHelper
{
	static void SendServerMessage(string message)
	{
		if (!Replication.IsServer())
			return;

		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (!playerManager)
			return;

		array<int> players = {};
		playerManager.GetPlayers(players);

		foreach (int playerId : players)
		{
			SCR_PlayerController controller = SCR_PlayerController.Cast(playerManager.GetPlayerController(playerId));
			if (!controller)
				continue;

			SCR_HintManagerComponent hintMgr = SCR_HintManagerComponent.GetInstance();
			if (hintMgr)
				hintMgr.ShowCustomHint(message, "BrasilZ", 10.0, false);
		}

		// Also push as a global notification so anyone watching the log sees it.
		Print(string.Format("[BrasilZ][Broadcast] %1", message), LogLevel.NORMAL);
	}
}
