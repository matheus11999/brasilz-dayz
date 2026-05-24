// Keeps the Respawn button available in the pause/sidebar menu and routes it
// through BrasilZ random respawn instead of relying on the vanilla deploy menu.
modded class PauseMenuUI
{
	protected static const ResourceName BZ_PAUSE_BUTTON_LAYOUT = "{9ECCD201BCF07E95}UI/layouts/Menus/PauseMenu/PauseMenuButton.layout";
	protected static const int BZ_RESPAWN_BUTTON_COOLDOWN_MS = 6000;
	protected static const int BZ_RESPAWN_MENU_LOCK_MS = 10000;
	protected SCR_ButtonTextComponent m_BZRespawnButton;
	protected bool m_bBZRespawnClicked;
	protected bool m_bBZRespawnMenuLocked;
	protected int m_iBZRespawnMenuUnlockTick;

	//------------------------------------------------------------------------------------------------
	override void OnMenuOpen()
	{
		super.OnMenuOpen();

		m_bBZRespawnMenuLocked = true;
		m_iBZRespawnMenuUnlockTick = System.GetTickCount() + BZ_RESPAWN_MENU_LOCK_MS;
		GetGame().GetCallqueue().Remove(BZ_UpdateRespawnMenuLock);

		BZ_EnsureRespawnButton();
		GetGame().GetCallqueue().CallLater(BZ_EnsureRespawnButton, 100, false);
		GetGame().GetCallqueue().CallLater(BZ_EnsureRespawnButton, 500, false);
		GetGame().GetCallqueue().CallLater(BZ_UpdateRespawnMenuLock, 0, false);
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
	protected Widget BZ_CreatePauseMenuButton(string name, string text)
	{
		Widget root = GetRootWidget();
		if (!root)
			return null;

		Widget container = root.FindAnyWidget("ButtonRow");
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
