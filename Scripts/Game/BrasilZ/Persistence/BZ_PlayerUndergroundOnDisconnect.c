// BrasilZ cache buster: 2026-05-25-unstuck-only-on-first-connect-not-respawn
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
	// Only lift bodies that were actually buried by the sink offset. Players standing at sea
	// level on the coast can sit at Y ~ -1.4 — that is NOT a buried body. The threshold has
	// to be:
	//   * above the highest possible buried Y. Sink adds -1000 to current Y. Chernarus tops
	//     out around Y=600, so the highest possible buried Y is ~ -400.
	//   * below the deepest non-buried position. Players can swim at Y ~ -30 in deep water;
	//     we do NOT want to lift those.
	// -100 satisfies both: anyone at Y < -100 was buried by us; anyone at Y > -100 (sea
	// level, coast, ocean diver) is legitimately placed.
	//
	// Previous value of -500 missed players sunk from mountaintops (post-sink Y = -450) —
	// CaverinhaTV reproduced this after a server restart: pre-bury Y=550 → boot scan
	// buried to -450 → -450 > -500 so lift was skipped → vanilla restored at -450 underwater.
	protected static const float BZ_BURIED_SENTINEL_Y = -100.0;

	// Estado tracking pra auto-unstuck movement detect
	protected vector m_vBzSpawnStartPos;
	protected bool m_bBzUnstuckFired;

	//------------------------------------------------------------------------------------------------
	override void OnControlledEntityChanged(IEntity from, IEntity to)
	{
		super.OnControlledEntityChanged(from, to);

		// CLIENT-SIDE AUTO-UNSTUCK (2026-05-25): replica logic do mod
		// ReloadandHealUnstuck (!unstuck command). Quando player ganha controle de char
		// novo (from=null, to=char), agenda 2s pós-spawn pra rodar unstuck pattern:
		// 1. SetStanceChange(STAND) — força stand up
		// 2. TryEquipRightHandItem(null, EEquipTypeWeapon) — desequipa arma (break stuck anim)
		// 3. SetOrigin(pos + Y+0.5) — nudge anti-clipping
		// User reportou que !unstuck manual corrige weapon binding stale pós-reconnect.
		// Roda apenas no CLIENT (onde !unstuck mod roda).
		// Auto-unstuck client-side desativado — testes mostraram que não reproduz
		// state do !unstuck manual mesmo com polling. Vanilla input/chat flow tem
		// side effects não reproduzíveis programaticamente.
		// Fallback: notifica player via chat pra digitar !unstuck manualmente.
		// Auto-unstuck: simula chat msg "!unstuck" → dispara OnNewMessage do mod
		// ReloadandHealUnstuck → mesmo code path do unstuck manual.
		// SINGLE SHOT 10s pós-spawn — testes mostraram que 10s é o sweet spot
		// (player ja tem char fully replicated + weapon binding settled).
		// Auto-unstuck flow:
		//   T+10s: Pass1 = unequip 3x + stance + nudge
		//   T+12s: Pass2 = re-equip weapon do inventário (simula player apertar Q manual)
		//   T+15s: Pass3 = !unstuck chat
		//   T+17s: Pass4 = re-equip weapon
		//   T+20s: Pass5 = !unstuck chat
		//   T+22s: Pass6 = re-equip weapon
		// Padrão: !unstuck → 2s depois → re-equip explícito = simula player apertando Q
		// AUTO-UNSTUCK SÓ NA ENTRADA DO SERVIDOR — não em respawn.
		// m_bBzUnstuckFired = idempotency flag. PlayerController instance persiste
		// entre respawns, então flag bloqueia disparo subsequente.
		// Respawn pós morte = vanilla recria char + dispara OnControlledEntityChanged,
		// mas flag jah true = skip.
		if (!Replication.IsServer() && !from && to && !m_bBzUnstuckFired)
		{
			PlayerController pcCheck = GetGame().GetPlayerController();
			if (pcCheck && pcCheck.GetControlledEntity() == to)
			{
				m_bBzUnstuckFired = true;  // dispara só 1x por sessão
				GetGame().GetCallqueue().CallLater(BZ_UnequipAllWeapons, 10000, false);
				GetGame().GetCallqueue().CallLater(BZ_ReequipWeapon, 12000, false);
				GetGame().GetCallqueue().CallLater(BZ_FireUnstuckChat, 15000, false);
				GetGame().GetCallqueue().CallLater(BZ_ReequipWeapon, 17000, false);
				GetGame().GetCallqueue().CallLater(BZ_FireUnstuckChat, 20000, false);
				GetGame().GetCallqueue().CallLater(BZ_ReequipWeapon, 22000, false);
			}
		}

		if (!Replication.IsServer())
			return;

		// DISCONNECT BEHAVIOR REWORK (2026-05-23) — adoption of ReforgedZ pattern.
		// Vanilla SCR_ReconnectComponent + audit timeout handle disconnect cleanup.
		// Death corpses managed by BZ_DecoupleDeadBody (kept 30min loot window).
		Print(string.Format("[BrasilZ][UndergroundHide] OnControlledEntityChanged from=%1 to=%2", from != null, to != null), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	// Re-equip weapon do inventário. Simula player apertando Q após !unstuck.
	// Procura primeira weapon no inventory + TryEquipRightHandItem(weapon, EEquipTypeWeapon).
	// Vanilla equip path = binding fresh, ActionsManager refresh.
	protected void BZ_ReequipWeapon()
	{
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return;

		IEntity controlled = pc.GetControlledEntity();
		if (!controlled)
			return;

		SCR_CharacterControllerComponent charCtrl = SCR_CharacterControllerComponent.Cast(
			controlled.FindComponent(SCR_CharacterControllerComponent));
		if (!charCtrl)
			return;

		SCR_InventoryStorageManagerComponent inv = SCR_InventoryStorageManagerComponent.Cast(
			controlled.FindComponent(SCR_InventoryStorageManagerComponent));
		if (!inv)
		{
			Print("[BrasilZ][AutoUnstuck] ReequipWeapon skip — no inventory.", LogLevel.WARNING);
			return;
		}

		array<IEntity> items = {};
		inv.GetItems(items);

		IEntity weapon = null;
		string foundPrefab = "(none)";
		foreach (IEntity item : items)
		{
			if (!item)
				continue;
			BaseWeaponComponent wcomp = BaseWeaponComponent.Cast(item.FindComponent(BaseWeaponComponent));
			if (!wcomp)
				continue;
			weapon = item;
			if (item.GetPrefabData())
				foundPrefab = item.GetPrefabData().GetPrefabName();
			break;  // primeira weapon
		}

		if (!weapon)
		{
			Print("[BrasilZ][AutoUnstuck] ReequipWeapon: no weapon found em inventário.", LogLevel.NORMAL);
			return;
		}

		bool result = charCtrl.TryEquipRightHandItem(weapon, EEquipItemType.EEquipTypeWeapon, false);
		Print(string.Format("[BrasilZ][AutoUnstuck] ReequipWeapon: TryEquip(%1) = %2", foundPrefab, result), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	// Pass 2/3/4: Dispara fake chat msg "!unstuck" → mod ReloadandHealUnstuck.OnNewMessage
	// roda 3 ações (stance + unequip + nudge). Mesmo code path do manual.
	protected void BZ_FireUnstuckChat()
	{
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return;

		IEntity controlled = pc.GetControlledEntity();
		if (!controlled)
		{
			Print("[BrasilZ][AutoUnstuck] FireChat skip — no controlled entity.", LogLevel.WARNING);
			return;
		}

		SCR_ChatComponent chat = SCR_ChatComponent.Cast(pc.FindComponent(SCR_ChatComponent));
		if (!chat)
		{
			Print("[BrasilZ][AutoUnstuck] FireChat skip — no SCR_ChatComponent.", LogLevel.WARNING);
			return;
		}

		int playerId = pc.GetPlayerId();
		Print(string.Format("[BrasilZ][AutoUnstuck] FireChat: !unstuck via OnNewMessage (playerId=%1)", playerId), LogLevel.NORMAL);
		chat.OnNewMessage("!unstuck", 0, playerId);
	}

	//------------------------------------------------------------------------------------------------
	// Pass 1: Desequipa TODAS armas do player (iterate WeaponManager slots + null cada).
	// Diferente do !unstuck que só limpa right hand — este força clean de todas weapon refs.
	protected void BZ_UnequipAllWeapons()
	{
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return;

		IEntity controlled = pc.GetControlledEntity();
		if (!controlled)
		{
			Print("[BrasilZ][AutoUnstuck] UnequipAll skip — no controlled entity.", LogLevel.WARNING);
			return;
		}

		SCR_CharacterControllerComponent charCtrl = SCR_CharacterControllerComponent.Cast(
			controlled.FindComponent(SCR_CharacterControllerComponent));
		if (!charCtrl)
		{
			Print("[BrasilZ][AutoUnstuck] UnequipAll skip — no charCtrl.", LogLevel.WARNING);
			return;
		}

		// 1. Stance reset
		charCtrl.SetStanceChange(ECharacterStance.STAND);

		// 2. Unequip weapon 3x — múltiplas chamadas força limpar bindings de armas
		// equipped (primary + secondary se houver). API só expõe EEquipTypeWeapon.
		charCtrl.TryEquipRightHandItem(null, EEquipItemType.EEquipTypeWeapon);
		charCtrl.TryEquipRightHandItem(null, EEquipItemType.EEquipTypeWeapon);
		charCtrl.TryEquipRightHandItem(null, EEquipItemType.EEquipTypeWeapon);

		// 3. Nudge
		vector pos = controlled.GetOrigin();
		pos[1] = pos[1] + 0.5;
		controlled.SetOrigin(pos);

		Print(string.Format("[BrasilZ][AutoUnstuck] Pass1: Unequip 3x armas + stance + nudge em %1", pos), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	// Auto-unstuck direto: replica !unstuck 3 ações sem passar pelo chat.
	// 1. SetStanceChange(STAND) — força stand up
	// 2. TryEquipRightHandItem(null, EEquipTypeWeapon) — desequipa arma (break stuck bind)
	// 3. SetOrigin(pos + Y+0.5) — nudge anti-clipping
	protected void BZ_FireUnstuckDirect()
	{
		// Idempotent: só dispara 1x
		if (m_bBzUnstuckFired)
			return;
		m_bBzUnstuckFired = true;

		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return;

		IEntity controlled = pc.GetControlledEntity();
		if (!controlled)
		{
			Print("[BrasilZ][AutoUnstuck] No controlled entity — skip.", LogLevel.WARNING);
			return;
		}

		SCR_CharacterControllerComponent charCtrl = SCR_CharacterControllerComponent.Cast(
			controlled.FindComponent(SCR_CharacterControllerComponent));
		if (!charCtrl)
		{
			Print("[BrasilZ][AutoUnstuck] No SCR_CharacterControllerComponent — abort.", LogLevel.WARNING);
			return;
		}

		// 1. Stand up
		charCtrl.SetStanceChange(ECharacterStance.STAND);

		// 2. Unequip weapon
		charCtrl.TryEquipRightHandItem(null, EEquipItemType.EEquipTypeWeapon);

		// 3. Nudge 0.5m up
		vector pos = controlled.GetOrigin();
		pos[1] = pos[1] + 0.5;
		controlled.SetOrigin(pos);

		Print(string.Format("[BrasilZ][AutoUnstuck] Auto-unstuck executado direto em %1 — stance + unequip + nudge", pos), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	// Polling: aguarda weapon REAL bindar antes de rodar unstuck.
	// Detecta quando GetCurrent() retorna weapon entity (path NÃO contém "Characters/")
	// = arma está realmente na mão = mesmo state de quando player digita !unstuck.
	protected void BZ_PollAutoUnstuck(IEntity character, int attempt)
	{
		const int MAX_ATTEMPTS = 30;  // 30s max

		if (!character || character.IsDeleted())
			return;

		PlayerController pc = GetGame().GetPlayerController();
		if (!pc || pc.GetControlledEntity() != character)
			return;

		if (attempt >= MAX_ATTEMPTS)
		{
			Print(string.Format("[BrasilZ][AutoUnstuck] Polling timeout (%1s) — weapon nunca bindou. Skip.", MAX_ATTEMPTS), LogLevel.WARNING);
			return;
		}

		CharacterControllerComponent cc = CharacterControllerComponent.Cast(character.FindComponent(CharacterControllerComponent));
		if (!cc)
		{
			GetGame().GetCallqueue().CallLater(BZ_PollAutoUnstuck, 1000, false, character, attempt + 1);
			return;
		}

		BaseWeaponManagerComponent wm = cc.GetWeaponManagerComponent();
		if (!wm)
		{
			GetGame().GetCallqueue().CallLater(BZ_PollAutoUnstuck, 1000, false, character, attempt + 1);
			return;
		}

		BaseWeaponComponent current = wm.GetCurrent();
		bool weaponBound = false;
		string currentPrefab = "(none)";
		if (current && current.GetOwner() && current.GetOwner().GetPrefabData())
		{
			currentPrefab = current.GetOwner().GetPrefabData().GetPrefabName();
			// Weapon binding REAL: path contém "Weapon"/"Rifle"/"Knife" etc, NÃO "Characters/"
			if (!currentPrefab.Contains("Characters/") && !currentPrefab.Contains("/Core/"))
				weaponBound = true;
		}

		if (!weaponBound)
		{
			// Weapon ainda não bindada. Retry 1s depois.
			Print(string.Format("[BrasilZ][AutoUnstuck] Poll %1/%2 — weapon ainda não bound (GetCurrent=%3). Retry 1s.", attempt + 1, MAX_ATTEMPTS, currentPrefab), LogLevel.NORMAL);
			GetGame().GetCallqueue().CallLater(BZ_PollAutoUnstuck, 1000, false, character, attempt + 1);
			return;
		}

		// Weapon bindada = mesmo state de !unstuck manual. Roda unstuck.
		Print(string.Format("[BrasilZ][AutoUnstuck] Weapon bindada (%1) após %2s — rodando unstuck.", currentPrefab, attempt + 1), LogLevel.NORMAL);
		BZ_AutoUnstuck(character);
	}

	//------------------------------------------------------------------------------------------------
	// Client-side auto-unstuck. Replica !unstuck command do mod ReloadandHealUnstuck.
	// Roda quando weapon binding tá real (detectado via BZ_PollAutoUnstuck).
	protected void BZ_AutoUnstuck(IEntity character)
	{
		if (!character || character.IsDeleted())
			return;

		PlayerController pc = GetGame().GetPlayerController();
		if (!pc || pc.GetControlledEntity() != character)
			return;

		SCR_CharacterControllerComponent charCtrl = SCR_CharacterControllerComponent.Cast(
			character.FindComponent(SCR_CharacterControllerComponent));
		if (!charCtrl)
		{
			Print("[BrasilZ][AutoUnstuck] No SCR_CharacterControllerComponent — abort", LogLevel.WARNING);
			return;
		}

		// 1. Force stand
		charCtrl.SetStanceChange(ECharacterStance.STAND);

		// 2. Unequip weapon — break stuck animations + weapon binding
		charCtrl.TryEquipRightHandItem(null, EEquipItemType.EEquipTypeWeapon);

		// 3. Physical nudge 0.5m up — break floor clipping
		vector pos = character.GetOrigin();
		pos[1] = pos[1] + 0.5;
		character.SetOrigin(pos);

		Print(string.Format("[BrasilZ][AutoUnstuck] Auto-unstuck executado pra char em %1 — stance reset + weapon unequip + nudge", pos), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	// Delayed check: 2s pós-spawn pra dar tempo da vanilla terminar replication.
	// Se WeaponManager.GetCurrent() null + inventário tem weapon → rebind.
	protected void BZ_CheckAndRebindWeapon(IEntity character)
	{
		if (!character || character.IsDeleted())
			return;

		CharacterControllerComponent cc = CharacterControllerComponent.Cast(character.FindComponent(CharacterControllerComponent));
		if (!cc)
			return;

		BaseWeaponManagerComponent wm = cc.GetWeaponManagerComponent();
		if (!wm)
			return;

		BaseWeaponComponent current = wm.GetCurrent();
		bool hasValidWeapon = false;
		string equipped = "(none)";
		if (current && current.GetOwner())
		{
			if (current.GetOwner().GetPrefabData())
				equipped = current.GetOwner().GetPrefabData().GetPrefabName();
			// Valida: owner deve ser weapon entity REAL, não o próprio char.
			// GetCurrent() às vezes retorna component default do char (não arma bound).
			// Path com "Characters/" = char próprio, não arma.
			if (!equipped.Contains("Characters/") && !equipped.Contains("/Core/"))
				hasValidWeapon = true;
		}

		if (hasValidWeapon)
		{
			Print(string.Format("[BrasilZ][WeaponRebind] Char tem arma REAL bound (%1) — skip rebind.", equipped), LogLevel.NORMAL);
			return;
		}

		// Nenhuma arma equipada (ou GetCurrent retornou char como false positive).
		Print(string.Format("[BrasilZ][WeaponRebind] Char SEM arma bound pós-reconnect (GetCurrent=%1). Procurando weapon no inventário...", equipped), LogLevel.WARNING);
		BZ_TryRebindWeapon(character, cc, wm);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_SinkCharacterOnDisconnect(IEntity character)
	{
		if (!character || character.IsDeleted())
		{
			Print("[BrasilZ][UndergroundHide] Sink skipped — character null/deleted", LogLevel.WARNING);
			return;
		}

		vector preSinkPos = character.GetOrigin();

		// Count children for diagnostic. Weapon/gadget/inventory tracked.
		int childCount = 0;
		string equipped = "(none)";
		CharacterControllerComponent ccCount = CharacterControllerComponent.Cast(character.FindComponent(CharacterControllerComponent));
		if (ccCount)
		{
			BaseWeaponManagerComponent wmCount = ccCount.GetWeaponManagerComponent();
			if (wmCount)
			{
				BaseWeaponComponent w = wmCount.GetCurrent();
				if (w && w.GetOwner() && w.GetOwner().GetPrefabData())
					equipped = w.GetOwner().GetPrefabData().GetPrefabName();
			}
		}
		IEntity sibCh = character.GetChildren();
		while (sibCh && childCount < 30)
		{
			sibCh = sibCh.GetSibling();
			childCount++;
		}

		// Dead/INCAP characters become lootable corpses via BZ_GameMode decouple path —
		// don't bury them, players need them visible.
		if (!BZ_IsCharacterAlive(character))
		{
			Print(string.Format("[BrasilZ][UndergroundHide] Sink skipped — char not alive at %1 (likely dead/INCAP, will be decoupled separately) | equipped=%2 children=%3", preSinkPos, equipped, childCount), LogLevel.NORMAL);
			return;
		}

		Print(string.Format("[BrasilZ][UndergroundHide] Sink BEGIN | pre-pos=%1 | equipped=%2 | children=%3", preSinkPos, equipped, childCount), LogLevel.NORMAL);

		// Disable damage handling BEFORE moving. Engine has void/out-of-bounds death zones
		// well above Y=-1000; without this the body dies underground, save persists a dead
		// character, and reconnect rejects it as a stale dead body — player loses progress.
		// EnableDamageHandling(false) blocks all damage sources (fall, void, environment)
		// without firing the damage-manager invokers that the old SetHealthScaled(1.0)
		// approach used to corrupt weapon attachments.
		SCR_CharacterDamageManagerComponent charDmg = SCR_CharacterDamageManagerComponent.Cast(character.FindComponent(SCR_CharacterDamageManagerComponent));
		if (charDmg)
			charDmg.EnableDamageHandling(false);

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

		// Post-sink weapon check. If sink caused binding loss, log it here so we can
		// correlate with reload/inspect bugs after reconnect.
		string equippedPostSink = "(none)";
		CharacterControllerComponent ccPostSink = CharacterControllerComponent.Cast(character.FindComponent(CharacterControllerComponent));
		if (ccPostSink)
		{
			BaseWeaponManagerComponent wmPostSink = ccPostSink.GetWeaponManagerComponent();
			if (wmPostSink)
			{
				BaseWeaponComponent wPostSink = wmPostSink.GetCurrent();
				if (wPostSink && wPostSink.GetOwner() && wPostSink.GetOwner().GetPrefabData())
					equippedPostSink = wPostSink.GetOwner().GetPrefabData().GetPrefabName();
			}
		}

		Print(string.Format("[BrasilZ][UndergroundHide] Sink END | post-pos=%1 | equipped post-sink=%2 (compare to pre-sink to detect binding loss)", pos, equippedPostSink), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	protected void BZ_LiftCharacterOnReconnect(IEntity character)
	{
		if (!character || character.IsDeleted())
		{
			Print("[BrasilZ][UndergroundHide] Lift skipped — character null/deleted", LogLevel.WARNING);
			return;
		}

		vector pos = character.GetOrigin();

		// Diagnostic: weapon + children state on reconnect, BEFORE any toggle.
		string equippedAtLift = "(none)";
		int childCountLift = 0;
		CharacterControllerComponent ccLift = CharacterControllerComponent.Cast(character.FindComponent(CharacterControllerComponent));
		if (ccLift)
		{
			BaseWeaponManagerComponent wmLift = ccLift.GetWeaponManagerComponent();
			if (wmLift)
			{
				BaseWeaponComponent wLift = wmLift.GetCurrent();
				if (wLift && wLift.GetOwner() && wLift.GetOwner().GetPrefabData())
					equippedAtLift = wLift.GetOwner().GetPrefabData().GetPrefabName();
			}
		}
		IEntity sibLift = character.GetChildren();
		while (sibLift && childCountLift < 30)
		{
			sibLift = sibLift.GetSibling();
			childCountLift++;
		}

		if (pos[1] >= BZ_BURIED_SENTINEL_Y)
		{
			Print(string.Format("[BrasilZ][UndergroundHide] Lift skipped — pos %1 not buried (Y >= %2) | equipped=%3 children=%4 (reconnect at surface, no lift needed)", pos, BZ_BURIED_SENTINEL_Y, equippedAtLift, childCountLift), LogLevel.NORMAL);

			// WEAPON BINDING RECOVERY (orthogonal to lift). Legacy saves corrupted by old
			// BootScan that did DeleteEntityAndChildren may have weaponManager.GetCurrent()=null
			// while weapon entities still exist as children. Reload/inspect actions become
			// unbound because ActionsManager has no currentWeapon to attach to. Force re-bind
			// the first found weapon in inventory.
			if (ccLift && equippedAtLift == "(none)")
			{
				BaseWeaponManagerComponent wmRebind = ccLift.GetWeaponManagerComponent();
				if (wmRebind)
				{
					BZ_TryRebindWeapon(character, ccLift, wmRebind);
				}
			}

			return; // not buried by us (player at sea level / on coast); do not lift
		}

		Print(string.Format("[BrasilZ][UndergroundHide] Lift BEGIN | pre-pos=%1 | equipped=%2 | children=%3", pos, equippedAtLift, childCountLift), LogLevel.NORMAL);

		// Reverse the sink exactly. GetSurfaceY(X,Z) was unreliable - if X,Z happened to be
		// near a mountain, it returned the peak altitude (e.g. 327m) and the player would
		// fall from there to the actual ground after reconnect. Adding the offset back
		// restores the character to the exact pre-disconnect Y.
		vector liftPos = pos;
		liftPos[1] = liftPos[1] + BZ_UNDERGROUND_OFFSET;

		// DEEP-VOID RECOVERY: a previous BootScan sink-fix (now reverted) pushed orphan
		// bodies to Y=-3000 and that depth got persisted by autosave before the player
		// reconnected. Adding the standard 1000m offset (-3000 + 1000 = -2000) leaves
		// them still underground. Detect this case and force-lift to surface via terrain
		// raycast. Threshold -1500 covers any Y deeper than the legitimate -1000 sink.
		const float BZ_DEEP_VOID_RECOVERY_Y = -1500.0;
		if (pos[1] < BZ_DEEP_VOID_RECOVERY_Y)
		{
			BaseWorld bw = GetGame().GetWorld();
			if (bw)
			{
				float terrainY = bw.GetSurfaceY(pos[0], pos[2]);
				liftPos[1] = terrainY + 2.0; // small clearance so capsule doesn't clip
				Print(string.Format("[BrasilZ][UndergroundHide] DEEP-VOID RECOVERY — pre-pos Y=%1 (< %2). Using GetSurfaceY=%3 → lift to %4 (legacy save from old sink-Y=-3000 bug)", pos[1], BZ_DEEP_VOID_RECOVERY_Y, terrainY, liftPos), LogLevel.WARNING);
			}
		}

		BaseGameEntity bgEntity = BaseGameEntity.Cast(character);
		if (!bgEntity)
		{
			character.SetOrigin(liftPos);
			Print(string.Format("[BrasilZ][UndergroundHide] Body lifted (SetOrigin fallback) to %1.", liftPos), LogLevel.WARNING);
			return;
		}

		vector transform[4];
		bgEntity.GetWorldTransform(transform);
		transform[3] = liftPos;

		bgEntity.Teleport(transform);

		// Re-enable damage handling now that the body is back at its original surface position
		// and bound to a PlayerController again. Without this the player would be invincible.
		SCR_CharacterDamageManagerComponent charDmg = SCR_CharacterDamageManagerComponent.Cast(character.FindComponent(SCR_CharacterDamageManagerComponent));
		if (charDmg)
		{
			charDmg.EnableDamageHandling(true);
			Print(string.Format("[BrasilZ][UndergroundHide] EnableDamageHandling(true) on reconnect for char at %1", liftPos), LogLevel.NORMAL);
		}
		else
		{
			Print("[BrasilZ][UndergroundHide] WARNING — no damage manager on lifted character (cannot re-enable damage)", LogLevel.WARNING);
		}

		// Post-lift weapon check — verify weapon binding intact after teleport.
		string equippedAfterLift = "(none)";
		CharacterControllerComponent ccPost = CharacterControllerComponent.Cast(character.FindComponent(CharacterControllerComponent));
		BaseWeaponManagerComponent wmPost = null;
		if (ccPost)
		{
			wmPost = ccPost.GetWeaponManagerComponent();
			if (wmPost)
			{
				BaseWeaponComponent wPost = wmPost.GetCurrent();
				if (wPost && wPost.GetOwner() && wPost.GetOwner().GetPrefabData())
					equippedAfterLift = wPost.GetOwner().GetPrefabData().GetPrefabName();
			}
		}
		Print(string.Format("[BrasilZ][UndergroundHide] Lift END | post-pos=%1 | equipped post-lift=%2 (compare to pre-lift to detect binding loss)", liftPos, equippedAfterLift), LogLevel.NORMAL);

		// WEAPON BINDING RECOVERY on lift path too (legacy save corruption).
		if (ccPost && wmPost && equippedAfterLift == "(none)")
		{
			BZ_TryRebindWeapon(character, ccPost, wmPost);
		}
	}

	//------------------------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------
	// Walks the character inventory looking for a weapon entity to re-bind. The fix is
	// 2-phase per Bohemia vanilla pattern (see RZ_CabinetLockPlayerComponent_GunRack.c
	// + SCR_EquipWeaponAction): server-side SelectWeapon ALONE does not refresh the
	// ActionsManager bindings. Must dispatch an RPC to the OWNING CLIENT which then
	// calls TryEquipRightHandItem — that triggers the proper inventory/animation/action
	// rebuild chain. Reload/inspect actions become available only after this completes.
	protected void BZ_TryRebindWeapon(IEntity character, CharacterControllerComponent controller, BaseWeaponManagerComponent weaponManager)
	{
		SCR_InventoryStorageManagerComponent inv = SCR_InventoryStorageManagerComponent.Cast(character.FindComponent(SCR_InventoryStorageManagerComponent));
		if (!inv)
		{
			Print("[BrasilZ][UndergroundHide] Weapon rebind FAIL — no inventory storage manager", LogLevel.WARNING);
			return;
		}

		array<IEntity> allItems = {};
		inv.GetItems(allItems);

		IEntity foundWeapon = null;
		foreach (IEntity item : allItems)
		{
			if (!item)
				continue;

			BaseWeaponComponent wcomp = BaseWeaponComponent.Cast(item.FindComponent(BaseWeaponComponent));
			if (!wcomp)
				continue;

			foundWeapon = item;
			break; // first weapon wins
		}

		if (!foundWeapon)
		{
			Print(string.Format("[BrasilZ][UndergroundHide] Weapon rebind SKIP — no weapon found in inventory of %1 items", allItems.Count()), LogLevel.NORMAL);
			return;
		}

		string foundPrefab = "(no-prefab)";
		if (foundWeapon.GetPrefabData())
			foundPrefab = foundWeapon.GetPrefabData().GetPrefabName();

		// Get RplId for cross-network reference. Item must have RplComponent to be
		// addressable from the client RPC handler.
		RplComponent itemRpl = RplComponent.Cast(foundWeapon.FindComponent(RplComponent));
		if (!itemRpl)
		{
			Print(string.Format("[BrasilZ][UndergroundHide] Weapon rebind FAIL — weapon %1 has no RplComponent", foundPrefab), LogLevel.WARNING);
			return;
		}

		// Send to OWNING CLIENT. TryEquipRightHandItem must run client-side or the
		// action manager doesn't refresh and reload/inspect stay unbound.
		Print(string.Format("[BrasilZ][UndergroundHide] Weapon REBIND server-side — dispatching RPC to client to equip %1 (RplId=%2)", foundPrefab, itemRpl.Id()), LogLevel.WARNING);
		Rpc(BZ_Rpc_EquipInHands, itemRpl.Id());
	}

	//------------------------------------------------------------------------------------------------
	// Client-side RPC handler: receives the weapon RplId from server and performs the
	// actual equip via TryEquipRightHandItem. Pattern copied verbatim from vanilla
	// RZ_CabinetLockPlayerComponent_GunRack.RPC_DoEquipInHands which solves the same
	// "server SelectWeapon doesn't refresh actions" problem.
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void BZ_Rpc_EquipInHands(RplId itemRplId)
	{
		Print(string.Format("[BrasilZ][UndergroundHide][CLIENT] Received equip RPC for RplId=%1", itemRplId), LogLevel.NORMAL);

		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
		{
			Print("[BrasilZ][UndergroundHide][CLIENT] No PlayerController on client — abort equip", LogLevel.WARNING);
			return;
		}

		IEntity controlled = pc.GetControlledEntity();
		if (!controlled)
		{
			Print("[BrasilZ][UndergroundHide][CLIENT] No controlled entity — abort equip", LogLevel.WARNING);
			return;
		}

		CharacterControllerComponent ctrl = CharacterControllerComponent.Cast(controlled.FindComponent(CharacterControllerComponent));
		if (!ctrl)
		{
			Print("[BrasilZ][UndergroundHide][CLIENT] No CharacterControllerComponent — abort equip", LogLevel.WARNING);
			return;
		}

		RplComponent itemRpl = RplComponent.Cast(Replication.FindItem(itemRplId));
		if (!itemRpl)
		{
			Print(string.Format("[BrasilZ][UndergroundHide][CLIENT] FindItem returned null for RplId=%1", itemRplId), LogLevel.WARNING);
			return;
		}

		IEntity item = itemRpl.GetEntity();
		if (!item)
		{
			Print("[BrasilZ][UndergroundHide][CLIENT] Item entity null from Rpl handle", LogLevel.WARNING);
			return;
		}

		string itemPrefab = "(no-prefab)";
		if (item.GetPrefabData())
			itemPrefab = item.GetPrefabData().GetPrefabName();

		bool result = ctrl.TryEquipRightHandItem(item, EEquipItemType.EEquipTypeWeapon, false);
		Print(string.Format("[BrasilZ][UndergroundHide][CLIENT] TryEquipRightHandItem(%1, EEquipTypeWeapon) = %2 — reload/inspect should now be available", itemPrefab, result), LogLevel.WARNING);
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
