class BZ_SafezoneWeaponGuardComponentClass : ScriptComponentClass
{
}

class BZ_SafezoneWeaponGuardUtils
{
	static const vector SAFEZONE_CENTER = "5403.721 299.52 5634.185";
	static const float SAFEZONE_RADIUS_SQ = 260.0 * 260.0;

	static bool IsInside(IEntity entity)
	{
		if (!entity)
			return false;

		return vector.DistanceSq(entity.GetOrigin(), SAFEZONE_CENTER) <= SAFEZONE_RADIUS_SQ;
	}

	static bool IsWeaponItem(IEntity item)
	{
		if (!item)
			return false;

		return item.FindComponent(BaseWeaponComponent) != null;
	}
}

modded class SCR_CharacterInventoryStorageComponent
{
	override bool CanUseItem(notnull IEntity item, ESlotFunction slotFunction = ESlotFunction.TYPE_GENERIC)
	{
		if (BZ_SafezoneWeaponGuardUtils.IsInside(GetOwner()) && BZ_SafezoneWeaponGuardUtils.IsWeaponItem(item))
			return false;

		return super.CanUseItem(item, slotFunction);
	}

	override bool UseItem(notnull IEntity item, ESlotFunction slotFunction = ESlotFunction.TYPE_GENERIC, SCR_EUseContext context = SCR_EUseContext.FROM_QUICKSLOT)
	{
		if (BZ_SafezoneWeaponGuardUtils.IsInside(GetOwner()) && BZ_SafezoneWeaponGuardUtils.IsWeaponItem(item))
			return false;

		return super.UseItem(item, slotFunction, context);
	}
}

modded class SCR_EquipWeaponAction
{
	override bool CanBePerformedScript(IEntity user)
	{
		if (BZ_SafezoneWeaponGuardUtils.IsInside(user))
			return false;

		return super.CanBePerformedScript(user);
	}
}

modded class SCR_EquipWeaponHolsterAction
{
	override bool CanBePerformedScript(IEntity user)
	{
		if (BZ_SafezoneWeaponGuardUtils.IsInside(user))
			return false;

		return super.CanBePerformedScript(user);
	}
}

class BZ_SafezoneWeaponGuardComponent : ScriptComponent
{
	[Attribute("260", UIWidgets.EditBox, "Safezone radius used to disarm players")]
	protected float m_fRadius;

	[Attribute("100", UIWidgets.EditBox, "How often players are checked, in milliseconds")]
	protected int m_iCheckIntervalMs;

	[Attribute("1", UIWidgets.CheckBox, "Enable debug messages")]
	protected bool m_bDebug;

	protected IEntity m_Owner;
	protected ref array<int> m_aDisarmedPlayers = {};
	protected ref array<int> m_aProtectedPlayers = {};
	protected ref map<int, float> m_mProtectedHealth = new map<int, float>();

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		m_Owner = owner;
		SetEventMask(owner, EntityEvent.INIT);
	}

	override void EOnInit(IEntity owner)
	{
		if (m_iCheckIntervalMs < 100)
			m_iCheckIntervalMs = 100;

		Print(string.Format("[BrasilZ][Safezone] WeaponGuard component EOnInit on entity at %1", owner.GetOrigin()), LogLevel.NORMAL);

		// Server-only: tick + mutation are authoritative on server.
		if (!Replication.IsServer())
		{
			Print("[BrasilZ][Safezone] WeaponGuard OnPostInit on client — skipping (server-only).", LogLevel.NORMAL);
			return;
		}

		Print(string.Format("[BrasilZ][Safezone] WeaponGuard starting on SERVER, radius=%1, interval=%2ms", m_fRadius, m_iCheckIntervalMs), LogLevel.NORMAL);
		GetGame().GetCallqueue().CallLater(CheckPlayers, m_iCheckIntervalMs, true);
	}

	void ~BZ_SafezoneWeaponGuardComponent()
	{
		GetGame().GetCallqueue().Remove(CheckPlayers);
	}

	protected void CheckPlayers()
	{
		if (!m_Owner)
			return;

		if (!Replication.IsServer())
			return;

		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (!playerManager)
			return;

		float radiusSq = m_fRadius * m_fRadius;
		vector center = m_Owner.GetOrigin();

		array<int> players = {};
		playerManager.GetPlayers(players);

		for (int i = m_aDisarmedPlayers.Count() - 1; i >= 0; i--)
		{
			if (players.Find(m_aDisarmedPlayers[i]) == -1)
				m_aDisarmedPlayers.Remove(i);
		}

		for (int protectedIndex = m_aProtectedPlayers.Count() - 1; protectedIndex >= 0; protectedIndex--)
		{
			int trackedPlayerId = m_aProtectedPlayers[protectedIndex];
			if (players.Find(trackedPlayerId) == -1)
				UnprotectPlayer(trackedPlayerId);
		}

		foreach (int playerId : players)
		{
			IEntity character = playerManager.GetPlayerControlledEntity(playerId);
			if (!character)
			{
				UntrackPlayer(playerId);
				UnprotectPlayer(playerId);
				continue;
			}

			bool inside = vector.DistanceSq(character.GetOrigin(), center) <= radiusSq;
			if (!inside)
			{
				UntrackPlayer(playerId);
				UnprotectPlayer(playerId);
				continue;
			}

			ProtectPlayerHealth(playerId, character);
			DisarmPlayer(playerId, character);
		}
	}

	protected void ProtectPlayerHealth(int playerId, IEntity character)
	{
		SCR_DamageManagerComponent damageManager = SCR_DamageManagerComponent.GetDamageManager(character);
		if (!damageManager)
			return;

		damageManager.EnableDamageHandling(false);

		float maxHealth = damageManager.GetMaxHealth();
		if (maxHealth <= 0)
			return;

		float currentHealth = damageManager.GetHealth();
		if (currentHealth <= 0)
			return;

		if (!m_mProtectedHealth.Contains(playerId))
		{
			m_mProtectedHealth.Set(playerId, currentHealth);
			if (m_aProtectedPlayers.Find(playerId) == -1)
				m_aProtectedPlayers.Insert(playerId);
			return;
		}

		float protectedHealth = m_mProtectedHealth.Get(playerId);
		if (currentHealth >= protectedHealth)
		{
			m_mProtectedHealth.Set(playerId, currentHealth);
			return;
		}

		damageManager.SetHealthScaled(protectedHealth / maxHealth);
	}

	protected void UnprotectPlayer(int playerId)
	{
		m_mProtectedHealth.Remove(playerId);

		int index = m_aProtectedPlayers.Find(playerId);
		if (index != -1)
			m_aProtectedPlayers.Remove(index);
	}

	protected void DisarmPlayer(int playerId, IEntity character)
	{
		CharacterControllerComponent controller = CharacterControllerComponent.Cast(character.FindComponent(CharacterControllerComponent));
		if (!controller)
			return;

		// Server-side disarm. SetWeaponRaised(false) + SelectWeapon(null) cover
		// most cases; client-side modded SCR_CharacterControllerComponent below
		// enforces the same on owner authority to keep visuals in sync.
		controller.SetWeaponRaised(false);

		auto weaponManager = controller.GetWeaponManagerComponent();
		if (weaponManager)
		{
			BaseWeaponComponent currentWeapon = weaponManager.GetCurrent();
			if (currentWeapon)
				controller.SelectWeapon(null);
		}

		if (!IsTracked(playerId))
		{
			m_aDisarmedPlayers.Insert(playerId);
			if (m_bDebug)
				Print(string.Format("[BrasilZ][Safezone] Player %1 weapon lowered inside safezone.", playerId), LogLevel.NORMAL);
		}
	}

	protected bool IsTracked(int playerId)
	{
		return m_aDisarmedPlayers.Find(playerId) != -1;
	}

	protected void UntrackPlayer(int playerId)
	{
		int index = m_aDisarmedPlayers.Find(playerId);
		if (index == -1)
			return;

		m_aDisarmedPlayers.Remove(index);
	}
}

// Client-side enforcement: modded character controller checks safezone every
// 500ms on the owner client and forces weapon-lowered state. The server-side
// component above is the source of truth, but visual input authority lives on
// the owner client, so we need this tick there too to actually keep the player
// from raising the weapon back up between server ticks.
modded class SCR_CharacterControllerComponent
{
	override void OnControlledByPlayer(IEntity owner, bool controlled)
	{
		super.OnControlledByPlayer(owner, controlled);
		if (!controlled || !owner)
			return;

		GetGame().GetCallqueue().CallLater(BZ_SafezoneTick, 100, true, owner);
	}

	protected void BZ_SafezoneTick(IEntity owner)
	{
		if (!owner)
			return;

		if (!BZ_SafezoneWeaponGuardUtils.IsInside(owner))
			return;

		BaseWeaponManagerComponent weaponManager = GetWeaponManagerComponent();
		if (weaponManager)
		{
			BaseWeaponComponent currentWeapon = weaponManager.GetCurrent();
			if (currentWeapon)
				SelectWeapon(null);
		}

		// Inside safezone — force weapon lowered if currently raised.
		if (IsWeaponRaised())
		{
			SetWeaponRaised(false);
			Print("[BrasilZ][Safezone][CLIENT] Forced weapon lowered (was raised)", LogLevel.NORMAL);
		}
	}
}
