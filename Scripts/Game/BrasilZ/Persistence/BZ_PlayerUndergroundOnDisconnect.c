// BrasilZ: hide player body on disconnect by teleporting it 1000m underground.
// Pattern adapted from FoggysSurvival. Old version used SetOrigin + SetHealthScaled and
// corrupted weapon state on reconnect. This rewrite uses the engine Teleport() API (which
// updates child transforms and replication correctly), removes the heal call, and waits a
// frame after disconnect so the engine finishes serializing the entity before we move it.
//
// Why underground instead of deleting:
//   * m_eDisconnectCharacterBehaviour=SAVE keeps body in world for reconnect.
//   * Deleting it would lose reconnect-to-same-entity ability.
//   * Sinking it makes the body invisible (no one sees it, no offline-PvP looting).
//   * Engine save captures Y=-1000 position; on server restart, body loads underground —
//     still invisible. If player never reconnects, body stays buried forever.
//   * On reconnect we lift back to the surface using Teleport, preserving weapon attachments.
modded class SCR_PlayerController
{
	protected static const float BZ_UNDERGROUND_OFFSET = 1000.0;
	protected static const float BZ_SURFACE_LIFT = 0.5;
	protected static const int BZ_SINK_DELAY_MS = 500;

	//------------------------------------------------------------------------------------------------
	override void OnControlledEntityChanged(IEntity from, IEntity to)
	{
		super.OnControlledEntityChanged(from, to);

		if (!Replication.IsServer())
			return;

		// Disconnect: player lost control of the character. Schedule sink.
		if (from && !to)
		{
			GetGame().GetCallqueue().CallLater(BZ_SinkCharacterOnDisconnect, BZ_SINK_DELAY_MS, false, from);
			return;
		}

		// Reconnect: player took control of (possibly underground) character. Lift immediately.
		if (!from && to)
		{
			BZ_LiftCharacterOnReconnect(to);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_SinkCharacterOnDisconnect(IEntity character)
	{
		if (!character || character.IsDeleted())
			return;

		// Dead/INCAP characters become lootable corpses via BZ_GameMode decouple path —
		// don't bury them, players need them visible.
		if (!BZ_IsCharacterAlive(character))
		{
			Print("[BrasilZ][UndergroundHide] Character not alive on disconnect — skipping sink.", LogLevel.NORMAL);
			return;
		}

		// Teleport is defined on BaseGameEntity, not IEntity, so cast first.
		// Teleport (vs SetOrigin) is replicated AND updates child transforms, which is
		// what prevented the weapon corruption seen with the old SetOrigin approach.
		BaseGameEntity bgEntity = BaseGameEntity.Cast(character);
		if (!bgEntity)
		{
			Print("[BrasilZ][UndergroundHide] Character is not a BaseGameEntity, falling back to SetOrigin.", LogLevel.WARNING);
			vector originPos = character.GetOrigin();
			originPos[1] = originPos[1] - BZ_UNDERGROUND_OFFSET;
			character.SetOrigin(originPos);
			return;
		}

		vector transform[4];
		bgEntity.GetWorldTransform(transform);

		vector pos = transform[3];
		pos[1] = pos[1] - BZ_UNDERGROUND_OFFSET;
		transform[3] = pos;

		bgEntity.Teleport(transform);

		Print(string.Format("[BrasilZ][UndergroundHide] Body sunk to %1 on disconnect (teleport).", pos), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_LiftCharacterOnReconnect(IEntity character)
	{
		if (!character || character.IsDeleted())
			return;

		vector pos = character.GetOrigin();
		if (pos[1] >= -1.0)
			return; // already at/above ground, nothing to do

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;

		float surfaceY = world.GetSurfaceY(pos[0], pos[2]);

		BaseGameEntity bgEntity = BaseGameEntity.Cast(character);
		if (!bgEntity)
		{
			vector liftPos = Vector(pos[0], surfaceY + BZ_SURFACE_LIFT, pos[2]);
			character.SetOrigin(liftPos);
			Print(string.Format("[BrasilZ][UndergroundHide] Body lifted (SetOrigin fallback) to %1.", liftPos), LogLevel.WARNING);
			return;
		}

		vector transform[4];
		bgEntity.GetWorldTransform(transform);
		transform[3] = Vector(pos[0], surfaceY + BZ_SURFACE_LIFT, pos[2]);

		bgEntity.Teleport(transform);

		Print(string.Format("[BrasilZ][UndergroundHide] Body lifted to surface at %1 on reconnect (teleport).", transform[3]), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	protected bool BZ_IsCharacterAlive(IEntity character)
	{
		SCR_DamageManagerComponent dmg = SCR_DamageManagerComponent.GetDamageManager(character);
		if (dmg && dmg.IsDestroyed())
			return false;

		CharacterControllerComponent controller = CharacterControllerComponent.Cast(character.FindComponent(CharacterControllerComponent));
		if (controller && controller.GetLifeState() != ECharacterLifeState.ALIVE)
			return false;

		return true;
	}
}
