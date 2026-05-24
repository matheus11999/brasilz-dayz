class BZ_RestartMsgBox : SCR_ScriptedWidgetComponent
{
	protected TextWidget m_wMsgText;
	protected ImageWidget m_wFill;
	protected ImageWidget m_wEmpty;

	protected float m_fFadeInTime = 0.35;
	protected float m_fFadeOutTime = 0.75;
	protected float m_fTimeLeft = 6.0;
	protected float m_fWholeTime = 6.0;
	protected float m_fLastUpdateTime;

	//------------------------------------------------------------------------------------------------
	override void HandlerAttached(Widget w)
	{
		if (!GetGame().InPlayMode())
			return;

		super.HandlerAttached(w);
		m_wMsgText = TextWidget.Cast(w.FindAnyWidget("MsgText"));
		m_wFill = ImageWidget.Cast(w.FindAnyWidget("Fill"));
		m_wEmpty = ImageWidget.Cast(w.FindAnyWidget("Empty"));
		m_wRoot.SetOpacity(0);
		m_fLastUpdateTime = GetGame().GetWorld().GetWorldTime();
	}

	//------------------------------------------------------------------------------------------------
	void SetText(string text)
	{
		if (!m_wMsgText && m_wRoot)
			m_wMsgText = TextWidget.Cast(m_wRoot.FindAnyWidget("MsgText"));

		if (!m_wMsgText)
			return;

		m_wMsgText.SetText(text);
		m_fWholeTime = Math.Max(5.0, text.Length() * 0.035 + 3.0);
		m_fTimeLeft = m_fWholeTime;
		m_fLastUpdateTime = GetGame().GetWorld().GetWorldTime();
		AudioSystem.PlaySound("{06C02AFB2CA882EB}Sounds/UI/Samples/Menu/UI_Task_Created.wav");
		GetGame().GetCallqueue().CallLater(BZ_FadeTick, 0, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_FadeTick()
	{
		if (!m_wRoot)
			return;

		float now = GetGame().GetWorld().GetWorldTime();
		float deltaTime = (now - m_fLastUpdateTime) / 1000.0;
		m_fLastUpdateTime = now;
		m_fTimeLeft -= deltaTime;

		if (m_fTimeLeft <= 0)
		{
			m_wRoot.RemoveFromHierarchy();
			return;
		}

		float elapsed = m_fWholeTime - m_fTimeLeft;
		float opacity = 1.0;
		if (elapsed < m_fFadeInTime)
			opacity = elapsed / m_fFadeInTime;
		else if (m_fTimeLeft < m_fFadeOutTime)
			opacity = m_fTimeLeft / m_fFadeOutTime;

		m_wRoot.SetOpacity(opacity);

		float progress = m_fTimeLeft / m_fWholeTime;
		if (progress < 0)
			progress = 0;

		if (m_wEmpty)
			HorizontalLayoutSlot.SetFillWeight(m_wEmpty, progress);
		if (m_wFill)
			HorizontalLayoutSlot.SetFillWeight(m_wFill, 1.0 - progress);

		GetGame().GetCallqueue().CallLater(BZ_FadeTick, 0, false);
	}
}
