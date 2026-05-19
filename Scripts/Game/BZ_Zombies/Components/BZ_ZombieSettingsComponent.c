[ComponentEditorProps(category: "BrasilZ Zombies/GameMode", description: "Global zombie settings - population cap")]
class BZ_ZombieSettingsComponentClass : SCR_BaseGameModeComponentClass
{
}

class BZ_ZombieSettingsComponent : SCR_BaseGameModeComponent
{
	[Attribute("100", UIWidgets.Slider, "Maximum number of zombies alive at once", "10 500 10", category: "Zombie Population")]
	protected int m_iMaxZombies;

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		if (!Replication.IsServer())
			return;

		BZ_ZombieHordeManager.SetMaxZombies(m_iMaxZombies);
	}
}
