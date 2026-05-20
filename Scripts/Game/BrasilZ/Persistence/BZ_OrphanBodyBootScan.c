// Boot-time orphan body cleanup.
//
// Scenario: server crashes (no graceful disconnect). On restart, engine recreates every
// persisted character entity in world. Players online at crash time who don't reconnect
// leave their body visible on the ground forever — the disconnect-hide path
// (BZ_PlayerUndergroundOnDisconnect) never fired because the disconnect events were
// skipped by the crash.
//
// This component runs once after the persistence system is fully ACTIVE plus a short
// grace window (so legitimate reconnects can re-bind their controller first), then sinks
// every ChimeraCharacter that still has no PlayerController to Y = -1000. When the player
// eventually reconnects, BZ_PlayerUndergroundOnDisconnect.BZ_LiftCharacterOnReconnect
// teleports them back to the surface — the orphan sink is transparent to legitimate users.
[ComponentEditorProps(category: "BrasilZ/Persistence", description: "One-time boot scan that buries orphan player bodies (no controller bound).")]
class BZ_OrphanBodyBootScanClass : ScriptComponentClass
{
}

class BZ_OrphanBodyBootScan : ScriptComponent
{
	[Attribute("30", UIWidgets.EditBox, "Seconds to wait after persistence becomes ACTIVE before scanning (lets reconnects re-bind).")]
	protected int m_iGraceSeconds;

	[Attribute("15000", UIWidgets.EditBox, "World half-extent in meters for the AABB query (Chernarus is ~15km).")]
	protected float m_fScanHalfExtent;

	[Attribute("1000", UIWidgets.EditBox, "Y offset (meters) used to bury orphan bodies.")]
	protected float m_fUndergroundOffset;

	protected bool m_bScanDone;
	protected ref array<IEntity> m_aFoundEntities;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		SetEventMask(owner, EntityEvent.INIT);
	}

	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);

		if (!Replication.IsServer())
			return;

		SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetScriptedInstance();
		if (!persistence)
		{
			Print("[BrasilZ][BootScan] SCR_PersistenceSystem not available — skipping orphan scan.", LogLevel.WARNING);
			return;
		}

		if (persistence.GetState() >= EPersistenceSystemState.ACTIVE)
		{
			// Already active (no save to load) — schedule grace window from now.
			ScheduleScan();
		}
		else
		{
			persistence.GetOnStateChanged().Insert(OnPersistenceStateChanged);
			Print("[BrasilZ][BootScan] Waiting for SCR_PersistenceSystem.ACTIVE before scanning.", LogLevel.NORMAL);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void OnPersistenceStateChanged(EPersistenceSystemState oldState, EPersistenceSystemState newState)
	{
		if (newState != EPersistenceSystemState.ACTIVE)
			return;

		SCR_PersistenceSystem persistence = SCR_PersistenceSystem.GetScriptedInstance();
		if (persistence)
			persistence.GetOnStateChanged().Remove(OnPersistenceStateChanged);

		ScheduleScan();
	}

	//------------------------------------------------------------------------------------------------
	protected void ScheduleScan()
	{
		if (m_bScanDone)
			return;

		int delayMs = m_iGraceSeconds * 1000;
		GetGame().GetCallqueue().CallLater(RunOrphanScan, delayMs, false);
		Print(string.Format("[BrasilZ][BootScan] Orphan scan scheduled in %1s.", m_iGraceSeconds), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	protected void RunOrphanScan()
	{
		if (m_bScanDone)
			return;
		m_bScanDone = true;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
		{
			Print("[BrasilZ][BootScan] World unavailable.", LogLevel.WARNING);
			return;
		}

		m_aFoundEntities = new array<IEntity>();

		vector worldMin = Vector(-m_fScanHalfExtent, -m_fScanHalfExtent, -m_fScanHalfExtent);
		vector worldMax = Vector(m_fScanHalfExtent, m_fScanHalfExtent, m_fScanHalfExtent);
		world.QueryEntitiesByAABB(worldMin, worldMax, QueryCollectEntity, FilterEntity, EQueryEntitiesFlags.DYNAMIC);

		int buried = 0;
		PlayerManager pm = GetGame().GetPlayerManager();

		foreach (IEntity entity : m_aFoundEntities)
		{
			if (!entity || entity.IsDeleted())
				continue;

			ChimeraCharacter character = ChimeraCharacter.Cast(entity);
			if (!character)
				continue;

			// Skip if a player already owns this character (reconnected during grace window).
			if (pm && pm.GetPlayerIdFromControlledEntity(entity) > 0)
				continue;

			// Skip dead/INCAP bodies — those are lootable corpses handled by the corpse cleanup tick.
			if (!IsCharacterAlive(character))
				continue;

			// Skip bodies that are already buried (don't sink twice).
			if (entity.GetOrigin()[1] <= -1.0)
				continue;

			BuryOrphan(character);
			buried++;
		}

		m_aFoundEntities = null;
		Print(string.Format("[BrasilZ][BootScan] Orphan scan complete — buried %1 stale alive bodies.", buried), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	protected bool QueryCollectEntity(IEntity entity)
	{
		if (entity)
			m_aFoundEntities.Insert(entity);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected bool FilterEntity(IEntity entity)
	{
		// Cheap pre-filter so we only collect characters.
		return entity && ChimeraCharacter.Cast(entity) != null;
	}

	//------------------------------------------------------------------------------------------------
	protected void BuryOrphan(IEntity character)
	{
		BaseGameEntity bgEntity = BaseGameEntity.Cast(character);
		if (!bgEntity)
			return;

		vector transform[4];
		bgEntity.GetWorldTransform(transform);

		vector pos = transform[3];
		pos[1] = pos[1] - m_fUndergroundOffset;
		transform[3] = pos;

		bgEntity.Teleport(transform);
		Print(string.Format("[BrasilZ][BootScan] Buried orphan body at %1.", pos), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsCharacterAlive(IEntity character)
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
