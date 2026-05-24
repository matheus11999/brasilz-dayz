/*
  Author: Lagschnitte
*/

[EntityEditorProps()]
class LAG_SCR_ServerRestartTriggerEntityClass extends ScriptedGameTriggerEntityClass
{

}

class LAG_SCR_ServerRestartTriggerEntity extends ScriptedGameTriggerEntity
{
	protected LAG_ServerRestarterConfig_Entry m_ConfigEntry;
	
	//------------------------------------------------------------------------------------------------
	override void OnInit(IEntity owner)
	{
		if (!owner.FindComponent(RplComponent))
		{
			Debug.Error("LAG::SCR_ServerRestartTriggerEntity::OnInit: Missing RPLComponent");		
		}
		
		GetGame().GetCallqueue().CallLater(InitRestarter,5000, false);
	}
	
	//------------------------------------------------------------------------------------------------
	void InitRestarter()
	{
		GetGame().GetCallqueue().Remove(InitRestarter);
		LAG_ServerRestarterConfig_Handler.LoadConfig();
		m_ConfigEntry =  LAG_ServerRestarterConfig_Handler.GetConfig().GetConfigEntry();
		
		GetGame().GetCallqueue().CallLater(Send_Notification, CalcNotificationTime(10), false, ENotification.SERVER_RESTART_10MIN );
		GetGame().GetCallqueue().CallLater(Send_Notification, CalcNotificationTime(5), false, ENotification.SERVER_RESTART_5MIN );
		GetGame().GetCallqueue().CallLater(Send_Notification, CalcNotificationTime(3), false, ENotification.SERVER_RESTART_3MIN );
		GetGame().GetCallqueue().CallLater(Send_Notification, CalcNotificationTime(1), false, ENotification.SERVER_RESTART_1MIN );
		
		GetGame().GetCallqueue().CallLater(Shutdown, m_ConfigEntry.m_iServerRuntime_minutes * 60000, false);
	}
	
	//------------------------------------------------------------------------------------------------
	void Send_Notification(ENotification notificationId)
	{
		int minutesLeft = 10;
		switch(notificationId)
		{
			case ENotification.SERVER_RESTART_10MIN:
				minutesLeft = 10;
			break;
			case ENotification.SERVER_RESTART_5MIN:
				minutesLeft = 5;
			break;
			case ENotification.SERVER_RESTART_3MIN:
				minutesLeft = 3;
			break;
			case ENotification.SERVER_RESTART_1MIN:
				minutesLeft = 1;
			break;
		}
		
		Print(string.Format("LAG_SCR_ServerRestartTriggerEntity::Send_Notification:Server restart in: %1 minute(s)", minutesLeft));
		SCR_NotificationsComponent.SendToEveryone(notificationId);
	}
	
	//------------------------------------------------------------------------------------------------
	int CalcNotificationTime(int minutesBeforeShutdown)
	{
		int minutesLeft = m_ConfigEntry.m_iServerRuntime_minutes - minutesBeforeShutdown;
		return minutesLeft * 60000; //to milliseconds
	}
	
	//------------------------------------------------------------------------------------------------
	void Shutdown()
	{
		Print(string.Format("LAG_SCR_ServerRestartTriggerEntity::Shutdown:Server shut down..."));
		GetGame().RequestClose();
	}
	
}