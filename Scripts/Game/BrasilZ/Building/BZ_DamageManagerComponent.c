// BrasilZ — block incendiary damage on misc building parts using BLD_DamageManagerComponent.
// Same pattern as RaidingManager + DoorRaidManager. Intercept INCENDIARY before vanilla
// flammable handler runs.
modded class BLD_DamageManagerComponent
{
	override bool HijackDamageHandling(notnull BaseDamageContext damageContext)
	{
		if (damageContext.damageType == EDamageType.INCENDIARY)
			return false;

		return super.HijackDamageHandling(damageContext);
	}
}
