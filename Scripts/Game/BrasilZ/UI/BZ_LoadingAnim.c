modded class ArmaReforgerLoadingAnim
{
    override void Load()
    {
        m_wRoot = m_WorkspaceWidget.CreateWidgets(
            "{695B7335262CB916}UI/layouts/Menus/LoadingScreen/ScenarioLoadingScreen.layout",
            m_WorkspaceWidget
        );

        m_LayoutComponent = new SCR_LoadingScreenComponent();
        m_wRoot.AddHandler(m_LayoutComponent);

        SplashScreen.s_iSplashShown++;
    }
}
