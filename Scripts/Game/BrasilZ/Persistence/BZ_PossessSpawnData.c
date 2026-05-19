// Override SCR_PossessSpawnData.GetPosition so possess spawns (reconnect) resolve the entity's
// actual world origin from the RplId instead of returning a stale/zero position.
//
// Without this, the engine preload step queries GetPosition() to stream world chunks and bind
// character components. If the position is invalid, weapons end up in the inventory but their
// ActionsManagerComponent never re-binds to the new controller, so reload/inspect/drop break
// after reconnect even though the loot itself is intact.
modded class SCR_PossessSpawnData
{
	override vector GetPosition()
	{
		if (!m_RplId.IsValid())
			return vector.Zero;

		RplComponent rplComponent = RplComponent.Cast(Replication.FindItem(m_RplId));
		if (!rplComponent)
			return vector.Zero;

		IEntity entity = rplComponent.GetEntity();
		if (!entity)
			return vector.Zero;

		return entity.GetOrigin();
	}
}
