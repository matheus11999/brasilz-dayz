modded class SCR_DeployMenuMain
{
	//------------------------------------------------------------------------------------------------
	override void OnMenuOpen()
	{
		super.OnMenuOpen();
		BZ_HideGroupControls();
		BZ_QueueHideGroupControls();
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuOpened()
	{
		super.OnMenuOpened();
		BZ_HideGroupControls();
		BZ_QueueHideGroupControls();
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_HideGroupControls()
	{
		if (m_GroupOpenButton)
			m_GroupOpenButton.SetVisible(false);

		BZ_HideGroupWidget("GroupManager");
		BZ_HideGroupWidget("GroupMenu");
		BZ_HideGroupWidget("Groups");
		BZ_HideGroupWidget("GroupList");
		BZ_HideGroupWidget("GroupSelection");
		BZ_HideGroupWidget("GroupSelector");
		BZ_HideGroupWidget("GroupRoot");
		BZ_HideGroupWidget("GroupPanel");
		BZ_HideGroupWidget("GroupFrame");
		BZ_HideGroupWidget("GroupTab");
		BZ_HideGroupWidget("DeployGroup");
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_QueueHideGroupControls()
	{
		GetGame().GetCallqueue().CallLater(BZ_HideGroupControls, 100, false);
		GetGame().GetCallqueue().CallLater(BZ_HideGroupControls, 500, false);
		GetGame().GetCallqueue().CallLater(BZ_HideGroupControls, 1000, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_HideGroupWidget(string widgetName)
	{
		Widget widget = GetRootWidget().FindAnyWidget(widgetName);
		if (widget)
			widget.SetVisible(false);
	}
}

modded class SCR_RoleSelectionMenu
{
	//------------------------------------------------------------------------------------------------
	override void OnMenuOpen()
	{
		super.OnMenuOpen();
		BZ_HideGroupControls();
		BZ_QueueHideGroupControls();
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuOpened()
	{
		super.OnMenuOpened();
		BZ_HideGroupControls();
		BZ_QueueHideGroupControls();
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_HideGroupControls()
	{
		if (m_GroupOpenButton)
			m_GroupOpenButton.SetVisible(false);

		BZ_HideGroupWidget("GroupManager");
		BZ_HideGroupWidget("GroupMenu");
		BZ_HideGroupWidget("Groups");
		BZ_HideGroupWidget("GroupList");
		BZ_HideGroupWidget("GroupSelection");
		BZ_HideGroupWidget("GroupSelector");
		BZ_HideGroupWidget("GroupRoot");
		BZ_HideGroupWidget("GroupPanel");
		BZ_HideGroupWidget("GroupFrame");
		BZ_HideGroupWidget("GroupTab");
		BZ_HideGroupWidget("DeployGroup");
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_QueueHideGroupControls()
	{
		GetGame().GetCallqueue().CallLater(BZ_HideGroupControls, 100, false);
		GetGame().GetCallqueue().CallLater(BZ_HideGroupControls, 500, false);
		GetGame().GetCallqueue().CallLater(BZ_HideGroupControls, 1000, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_HideGroupWidget(string widgetName)
	{
		Widget widget = GetRootWidget().FindAnyWidget(widgetName);
		if (widget)
			widget.SetVisible(false);
	}
}
