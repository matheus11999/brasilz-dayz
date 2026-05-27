// BrasilZ — force base building parts to spawn at full MaxHealth.
//
// Vanilla BLD_PlacerComponent.c:789 hardcodes ownershipComponent.SetPartHP(3000)
// regardless of the prefab's MaxHealth attribute. Editing MaxHealth in a .et override
// has no effect on placed parts (they still spawn with 3000 HP).
//
// This modded class intercepts the FIRST SetPartHP call (entity init, partHP still 0)
// and forces partHP = MaxHealth when the placer is trying to set a value lower than
// the configured max. Subsequent SetPartHP calls (damage, repair) pass through normally.
modded class BLD_OwnershipComponent
{
	override void SetPartHP(float newPartHP)
	{
		// Init path: entity just placed, partHP still default (0). Force full MaxHealth
		// if placer's hardcoded value is below configured max.
		if (partHP <= 0 && MaxHealth > 0 && newPartHP < MaxHealth)
		{
			Print(string.Format("[BrasilZ][BuildingHP] Forcing partHP=%1 (MaxHealth) instead of placer-hardcoded %2 on %3", MaxHealth, newPartHP, GetOwner()), LogLevel.NORMAL);
			newPartHP = MaxHealth;
		}

		super.SetPartHP(newPartHP);
	}
}
