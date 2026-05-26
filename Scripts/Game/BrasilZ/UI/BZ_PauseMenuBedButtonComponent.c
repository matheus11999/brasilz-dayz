class BZ_PauseMenuBedButtonComponent : SCR_ButtonTextComponent
{
	protected bool m_bClicked;

	override void HandlerAttached(Widget w)
	{
		super.HandlerAttached(w);

		if (SCR_Global.IsEditMode())
			return;

		Print("[BrasilZ][BedRespawn][UI] Bed layout button attached.", LogLevel.NORMAL);
		GetGame().GetCallqueue().CallLater(BZ_Refresh, 0, false);
		GetGame().GetCallqueue().CallLater(BZ_Refresh, 500, false);
	}

	protected void BZ_Refresh()
	{
		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (pc)
			pc.BZ_RequestBedFlagRefresh();

		if (m_bClicked)
		{
			SetText("Bed Spawning...");
			SetEnabled(false);
		}
		else if (pc && pc.BZ_GetBedCooldownRemaining() > 0)
		{
			int remaining = pc.BZ_GetBedCooldownRemaining();
			SetText(string.Format("Bed (%1m %2s)", remaining / 60, remaining % 60));
			SetEnabled(false);
		}
		else
		{
			SetText("Bed");
			SetEnabled(true);
		}

		SetVisible(true);
		m_OnClicked.Clear();
		m_OnClicked.Insert(BZ_OnClicked);
		GetGame().GetCallqueue().CallLater(BZ_Refresh, 1000, false);
	}

	protected void BZ_OnClicked()
	{
		if (m_bClicked)
			return;

		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (!pc)
			return;

		if (pc.BZ_GetBedCooldownRemaining() > 0)
			return;

		m_bClicked = true;
		SetText("Bed Spawning...");
		SetEnabled(false);

		Print(string.Format("[BrasilZ][BedRespawn][UI] Bed layout button clicked (player=%1, hasBed=%2, cd=%3s)", pc.GetPlayerId(), pc.BZ_HasBoundBed(), pc.BZ_GetBedCooldownRemaining()), LogLevel.NORMAL);
		pc.BZ_RequestBedRespawn();
	}
}
