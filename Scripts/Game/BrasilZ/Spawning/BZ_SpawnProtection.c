// BrasilZ spawn protection.
//
// Grants players a brief damage-immunity window after they materialize in the world.
// Applies to BOTH paths:
//   1. Fresh spawn from deploy menu (new players, post-death respawn)
//   2. Reconnect-with-progress (vanilla possess of persisted character)
//
// Why:
//   * Prevents instant death to ambient zombies that spawned on top of the player
//   * Gives client time to load the world chunk before damage starts ticking
//   * Stops spawn-camping (PvP shoots before player can even render)
//   * Mitigates lingering bleeding effects from previous session
//
// Implementation: disable damage handling on character damage manager for N seconds,
// then re-enable. Same EnableDamageHandling pattern used by BZ_PlayerUndergroundOnDisconnect.
//
// Log markers (grep `[BrasilZ][SpawnProtection]`):
//   ON  → spawn protection activated, lasts N seconds
//   OFF → spawn protection deactivated, damage normal

class BZ_SpawnProtection
{
	// Duration of immunity window in milliseconds. 15s is the DayZ-like standard —
	// long enough for full chunk load + audio cue, short enough not to be exploitable
	// for combat-logging (players cannot use it as PvP shield in normal play).
	static const int BZ_SPAWN_PROTECTION_MS = 15000;

	//------------------------------------------------------------------------------------------------
	// Activate spawn protection on a freshly-spawned character. Safe to call multiple
	// times — subsequent calls just refresh the timer.
	static void Apply(IEntity character, int playerId)
	{
		if (!character || character.IsDeleted())
			return;

		SCR_CharacterDamageManagerComponent dmg = SCR_CharacterDamageManagerComponent.Cast(SCR_DamageManagerComponent.GetDamageManager(character));
		if (!dmg)
		{
			Print(string.Format("[BrasilZ][SpawnProtection] Player %1 — no damage manager, cannot apply protection", playerId), LogLevel.WARNING);
			return;
		}

		dmg.EnableDamageHandling(false);
		GetGame().GetCallqueue().Remove(BZ_DisableSpawnProtection);
		GetGame().GetCallqueue().CallLater(BZ_DisableSpawnProtection, BZ_SPAWN_PROTECTION_MS, false, character, playerId);

		Print(string.Format("[BrasilZ][SpawnProtection] Player %1 ON for %2ms at %3", playerId, BZ_SPAWN_PROTECTION_MS, character.GetOrigin()), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	// Re-enable damage handling. Called by CallLater scheduler after the protection
	// window expires. Idempotent — if character was deleted (died/disconnected within
	// window), simply log and return.
	static void BZ_DisableSpawnProtection(IEntity character, int playerId)
	{
		if (!character || character.IsDeleted())
		{
			Print(string.Format("[BrasilZ][SpawnProtection] Player %1 OFF — character no longer valid (disconnected during protection?)", playerId), LogLevel.NORMAL);
			return;
		}

		SCR_CharacterDamageManagerComponent dmg = SCR_CharacterDamageManagerComponent.Cast(SCR_DamageManagerComponent.GetDamageManager(character));
		if (!dmg)
			return;

		dmg.EnableDamageHandling(true);
		Print(string.Format("[BrasilZ][SpawnProtection] Player %1 OFF — damage restored", playerId), LogLevel.NORMAL);
	}
}
