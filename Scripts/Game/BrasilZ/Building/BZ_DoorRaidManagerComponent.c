// BrasilZ — block incendiary damage on doors.
// Vanilla BLD_DoorRaidManagerComponent.HijackDamageHandling also falls through to
// SCR_FlammableHitZone.HandleIncendiaryDamage on INCENDIARY, bypassing the door
// raid multiplier. Block here.
modded class BLD_DoorRaidManagerComponent
{
	override bool HijackDamageHandling(notnull BaseDamageContext damageContext)
	{
		if (damageContext.damageType == EDamageType.INCENDIARY)
			return false;

		return super.HijackDamageHandling(damageContext);
	}
}
