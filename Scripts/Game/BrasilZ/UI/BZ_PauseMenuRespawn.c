// Keeps the Respawn button available in the pause/sidebar menu and routes it
// through BrasilZ random respawn instead of relying on the vanilla deploy menu.
//
// Also injects a "Bed" button below Respawn. The server validates bed ownership on click;
// client-side bed state is used only for cooldown text and faster UI refresh.
modded class PauseMenuUI
{
	protected static const ResourceName BZ_PAUSE_BUTTON_LAYOUT = "{9ECCD201BCF07E95}UI/layouts/Menus/PauseMenu/PauseMenuButton.layout";
	protected static const int BZ_RESPAWN_BUTTON_COOLDOWN_MS = 6000;
	protected static const int BZ_RESPAWN_MENU_LOCK_MS = 10000;
	protected SCR_ButtonTextComponent m_BZRespawnButton;
	protected bool m_bBZRespawnClicked;
	protected bool m_bBZRespawnMenuLocked;
	protected int m_iBZRespawnMenuUnlockTick;

	// Bed respawn button state
	protected SCR_ButtonTextComponent m_BZBedButton;
	protected Widget m_wBZBedButton;
	protected bool m_bBZBedClicked;

	//------------------------------------------------------------------------------------------------
	override void OnMenuOpen()
	{
		super.OnMenuOpen();

		m_bBZRespawnMenuLocked = true;
		m_iBZRespawnMenuUnlockTick = System.GetTickCount() + BZ_RESPAWN_MENU_LOCK_MS;
		GetGame().GetCallqueue().Remove(BZ_UpdateRespawnMenuLock);

		SCR_PlayerController scrPc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (scrPc)
			scrPc.BZ_RequestBedFlagRefresh();

		BZ_EnsureRespawnButton();
		BZ_EnsureBedButton();
		GetGame().GetCallqueue().CallLater(BZ_EnsureRespawnButton, 100, false);
		GetGame().GetCallqueue().CallLater(BZ_EnsureBedButton, 100, false);
		GetGame().GetCallqueue().CallLater(BZ_EnsureRespawnButton, 500, false);
		GetGame().GetCallqueue().CallLater(BZ_EnsureBedButton, 500, false);
		GetGame().GetCallqueue().CallLater(BZ_EnsureBedButton, 1500, false);
		GetGame().GetCallqueue().CallLater(BZ_UpdateRespawnMenuLock, 0, false);
		GetGame().GetCallqueue().CallLater(BZ_UpdateBedButton, 1000, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_EnsureRespawnButton()
	{
		Widget respawnWidget = GetRootWidget().FindAnyWidget("Respawn");
		if (!respawnWidget)
			respawnWidget = BZ_CreatePauseMenuButton("Respawn", "Respawn");

		if (!respawnWidget)
		{
			Print("[BrasilZ][PauseRespawn] Respawn button unavailable - no widget/container found.", LogLevel.WARNING);
			return;
		}

		respawnWidget.SetVisible(true);
		respawnWidget.SetEnabled(true);

		Widget parent = respawnWidget.GetParent();
		if (parent)
		{
			parent.SetVisible(true);
			parent.SetEnabled(true);
		}

		m_BZRespawnButton = SCR_ButtonTextComponent.Cast(respawnWidget.FindHandler(SCR_ButtonTextComponent));
		if (!m_BZRespawnButton)
			m_BZRespawnButton = SCR_ButtonTextComponent.GetButtonText("Respawn", m_wRoot);

		if (!m_BZRespawnButton)
		{
			Print("[BrasilZ][PauseRespawn] Respawn widget has no SCR_ButtonTextComponent.", LogLevel.WARNING);
			return;
		}

		if (m_bBZRespawnClicked)
		{
			m_BZRespawnButton.SetText("Respawning...");
			m_BZRespawnButton.SetEnabled(false);
		}
		else if (m_bBZRespawnMenuLocked)
		{
			m_BZRespawnButton.SetText(string.Format("Respawn (%1s)", BZ_GetRespawnMenuLockSeconds()));
			m_BZRespawnButton.SetEnabled(false);
		}
		else
		{
			m_BZRespawnButton.SetText("Respawn");
			m_BZRespawnButton.SetEnabled(true);
		}

		m_BZRespawnButton.GetRootWidget().SetVisible(true);
		m_BZRespawnButton.GetRootWidget().SetEnabled(true);
		m_BZRespawnButton.m_OnClicked.Clear();
		m_BZRespawnButton.m_OnClicked.Insert(BZ_OnRespawnClicked);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_OnRespawnClicked()
	{
		if (m_bBZRespawnMenuLocked)
			return;

		if (m_bBZRespawnClicked)
			return;

		m_bBZRespawnClicked = true;
		if (m_BZRespawnButton)
		{
			m_BZRespawnButton.SetText("Respawning...");
			m_BZRespawnButton.SetEnabled(false);
		}

		GetGame().GetCallqueue().CallLater(BZ_ResetRespawnButtonCooldown, BZ_RESPAWN_BUTTON_COOLDOWN_MS, false);

		SCR_PlayerController playerController = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (!playerController)
		{
			Print("[BrasilZ][PauseRespawn] Cannot request respawn - no local player controller.", LogLevel.WARNING);
			return;
		}

		playerController.BZ_RequestPauseMenuRespawn();
		Close();
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_ResetRespawnButtonCooldown()
	{
		m_bBZRespawnClicked = false;
		BZ_EnsureRespawnButton();
	}

	//------------------------------------------------------------------------------------------------
	protected int BZ_GetRespawnMenuLockSeconds()
	{
		int remainingMs = m_iBZRespawnMenuUnlockTick - System.GetTickCount();
		if (remainingMs <= 0)
			return 0;

		return (remainingMs + 999) / 1000;
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_UpdateRespawnMenuLock()
	{
		if (!m_bBZRespawnMenuLocked)
			return;

		if (System.GetTickCount() >= m_iBZRespawnMenuUnlockTick)
		{
			m_bBZRespawnMenuLocked = false;
			BZ_EnsureRespawnButton();
			return;
		}

		BZ_EnsureRespawnButton();
		GetGame().GetCallqueue().CallLater(BZ_UpdateRespawnMenuLock, 1000, false);
	}

	//------------------------------------------------------------------------------------------------
	// Creates/updates the Bed respawn button. Server validates bed ownership on click.
	// Disabled with countdown if cooldown active.
	protected void BZ_EnsureBedButton()
	{
		SCR_PlayerController scrPc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		bool hasBed = scrPc && scrPc.BZ_HasBoundBed();
		int cd = 0;
		if (scrPc)
			cd = scrPc.BZ_GetBedCooldownRemaining();

		Print(string.Format("[BrasilZ][BedRespawn][UI] EnsureBedButton: hasBed=%1 cooldown=%2s widget=%3", hasBed, cd, m_wBZBedButton != null), LogLevel.NORMAL);

		if (!m_wBZBedButton)
			m_wBZBedButton = GetRootWidget().FindAnyWidget("BedRespawn");

		if (!m_wBZBedButton)
			m_wBZBedButton = BZ_CreatePauseMenuButton("BedRespawn", "Bed");

		if (!m_wBZBedButton)
		{
			Print("[BrasilZ][BedRespawn] Bed button widget could not be created.", LogLevel.WARNING);
			return;
		}

		m_wBZBedButton.SetVisible(true);
		m_wBZBedButton.SetEnabled(true);

		Widget parent = m_wBZBedButton.GetParent();
		if (parent)
		{
			parent.SetVisible(true);
			parent.SetEnabled(true);
		}

		m_BZBedButton = SCR_ButtonTextComponent.Cast(m_wBZBedButton.FindHandler(SCR_ButtonTextComponent));
		if (!m_BZBedButton)
		{
			Print("[BrasilZ][BedRespawn] Bed widget has no SCR_ButtonTextComponent.", LogLevel.WARNING);
			return;
		}

		int cooldownRemaining = 0;
		if (scrPc)
			cooldownRemaining = scrPc.BZ_GetBedCooldownRemaining();

		if (m_bBZBedClicked)
		{
			m_BZBedButton.SetText("Bed Spawning...");
			m_BZBedButton.SetEnabled(false);
		}
		else if (m_bBZRespawnMenuLocked)
		{
			m_BZBedButton.SetText(string.Format("Bed (%1s)", BZ_GetRespawnMenuLockSeconds()));
			m_BZBedButton.SetEnabled(false);
		}
		else if (cooldownRemaining > 0)
		{
			int mins = cooldownRemaining / 60;
			int secs = cooldownRemaining % 60;
			m_BZBedButton.SetText(string.Format("Bed (%1m %2s)", mins, secs));
			m_BZBedButton.SetEnabled(false);
		}
		else if (!hasBed)
		{
			m_BZBedButton.SetText("Bed");
			m_BZBedButton.SetEnabled(true);
		}
		else
		{
			m_BZBedButton.SetText("Bed");
			m_BZBedButton.SetEnabled(true);
		}

		m_BZBedButton.m_OnClicked.Clear();
		m_BZBedButton.m_OnClicked.Insert(BZ_OnBedClicked);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_OnBedClicked()
	{
		if (m_bBZRespawnMenuLocked)
			return;

		if (m_bBZBedClicked)
			return;

		SCR_PlayerController scrPc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (!scrPc)
		{
			Print("[BrasilZ][BedRespawn] No local player controller.", LogLevel.WARNING);
			return;
		}

		if (scrPc.BZ_GetBedCooldownRemaining() > 0)
			return;

		m_bBZBedClicked = true;
		if (m_BZBedButton)
		{
			m_BZBedButton.SetText("Bed Spawning...");
			m_BZBedButton.SetEnabled(false);
		}

		Print(string.Format("[BrasilZ][BedRespawn][UI] Bed button clicked (player=%1, hasBed=%2, cd=%3s)", scrPc.GetPlayerId(), scrPc.BZ_HasBoundBed(), scrPc.BZ_GetBedCooldownRemaining()), LogLevel.NORMAL);

		GetGame().GetCallqueue().CallLater(BZ_ResetBedButtonCooldown, BZ_RESPAWN_BUTTON_COOLDOWN_MS, false);
		scrPc.BZ_RequestBedRespawn();
		Close();
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_ResetBedButtonCooldown()
	{
		m_bBZBedClicked = false;
		BZ_EnsureBedButton();
	}

	//------------------------------------------------------------------------------------------------
	// Periodic refresh of Bed button while menu open. Updates countdown text + visibility
	// in case bed binding state changes (e.g. server marks hasBed mid-menu).
	protected void BZ_UpdateBedButton()
	{
		BZ_EnsureBedButton();
		// Keep ticking while menu open. Workbench auto-stops CallLater when class destroyed.
		GetGame().GetCallqueue().CallLater(BZ_UpdateBedButton, 1000, false);
	}

	//------------------------------------------------------------------------------------------------
	protected Widget BZ_CreatePauseMenuButton(string name, string text)
	{
		Widget root = GetRootWidget();
		if (!root)
			return null;

		Widget container;
		Widget anchorButton;

		anchorButton = root.FindAnyWidget("AdminTools");

		if (name != "Respawn")
		{
			Widget respawnButton = root.FindAnyWidget("Respawn");
			if (respawnButton && !anchorButton)
				anchorButton = respawnButton;
		}

		if (anchorButton)
			container = anchorButton.GetParent();

		if (!container)
			container = root.FindAnyWidget("ButtonRow");

		if (!container)
		{
			Widget continueButton = root.FindAnyWidget("Continue");
			if (continueButton)
				container = continueButton.GetParent();
		}

		if (!container)
		{
			Widget exitButton = root.FindAnyWidget("Exit");
			if (exitButton)
				container = exitButton.GetParent();
		}

		if (!container)
			return null;

		Widget button = GetGame().GetWorkspace().CreateWidgets(BZ_PAUSE_BUTTON_LAYOUT, container);
		if (!button)
			return null;

		button.SetName(name);
		LayoutSlot.SetHorizontalAlign(button, LayoutHorizontalAlign.Stretch);
		LayoutSlot.SetVerticalAlign(button, LayoutVerticalAlign.Center);
		LayoutSlot.SetPadding(button, 0, 0, 0, 0);
		LayoutSlot.SetFillWeight(button, 1);
		container.Update();

		SCR_ButtonTextComponent buttonComponent = SCR_ButtonTextComponent.Cast(button.FindHandler(SCR_ButtonTextComponent));
		if (buttonComponent)
			buttonComponent.SetText(text);

		return button;
	}
}
