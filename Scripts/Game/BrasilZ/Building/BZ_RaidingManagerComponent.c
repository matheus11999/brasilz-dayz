// BrasilZ — block incendiary damage on raidable structures (Foundations, Walls, Boxes).
// Vanilla BLD_RaidingManagerComponent.HijackDamageHandling falls through to
// SCR_FlammableHitZone.HandleIncendiaryDamage which bypasses the raid multiplier,
// letting Molotovs do raw ~500 damage. Intercepting INCENDIARY here and returning
// false skips the flammable handler entirely → 0 damage.
modded class BLD_RaidingManagerComponent
{
	override bool HijackDamageHandling(notnull BaseDamageContext damageContext)
	{
		if (damageContext.damageType == EDamageType.INCENDIARY)
			return false;

		return super.HijackDamageHandling(damageContext);
	}
}
