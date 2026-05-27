// ============================================================================
// Shop purchase / sale hooks
// ============================================================================
modded class ADM_ShopBaseComponent
{
	//------------------------------------------------------------------------------------------------
	override bool AskPurchase(IEntity player, ADM_PlayerShopManagerComponent playerManager, ADM_ShopMerchandise merchandise, int quantity)
	{
		bool result = super.AskPurchase(player, playerManager, merchandise, quantity);

		if (!Replication.IsServer() || !player || !merchandise)
			return result;

		if (result && !BZ_DiscordConfig.LOG_PURCHASE && !BZ_PortalConfig.LOG_PURCHASE)
			return result;
		if (!result && !BZ_DiscordConfig.LOG_PURCHASE_FAIL && !BZ_PortalConfig.LOG_PURCHASE_FAIL)
			return result;

		BZ_DiscordHooks_LogShopTransaction(player, merchandise, quantity, result, true);
		return result;
	}

	//------------------------------------------------------------------------------------------------
	override bool AskSell(IEntity player, ADM_PlayerShopManagerComponent playerManager, ADM_ShopMerchandise merchandise, int quantity)
	{
		bool result = super.AskSell(player, playerManager, merchandise, quantity);

		if (!Replication.IsServer() || !player || !merchandise)
			return result;

		if (!result)
			return result;

		if (!BZ_DiscordConfig.LOG_SALE && !BZ_PortalConfig.LOG_SALE)
			return result;

		BZ_DiscordHooks_LogShopTransaction(player, merchandise, quantity, result, false);
		return result;
	}
}

// Sends the delayed connect event after character restore so the wallet
// balance is available. Called via CallLater from OnPlayerConnected.
void BZ_DiscordHooks_SendConnectEvent(int playerId, string name, int onlineCount)
{
	PlayerManager pm = GetGame().GetPlayerManager();
	if (!pm)
		return;

	IEntity playerEntity = pm.GetPlayerControlledEntity(playerId);

	ref array<ref BZ_DiscordField> fields = new array<ref BZ_DiscordField>();
	fields.Insert(new BZ_DiscordField("Players online", onlineCount.ToString()));
	BZ_DiscordWebhook.AddBalanceFields(playerEntity, fields);

	// Metabolism + bleeding state on reconnect — useful to detect:
	//   (a) anti-cheese: did stats drop offline? (shouldn't, per FMMetabolism2 design)
	//   (b) at-risk reconnect: player came back almost empty, will die in minutes
	//   (c) cross-restart persistence integrity
	//   (d) bleeding alert: player came back actively bleeding, needs bandage NOW
	if (playerEntity)
	{
		SCR_CharacterControllerComponent ctrl = SCR_CharacterControllerComponent.Cast(playerEntity.FindComponent(SCR_CharacterControllerComponent));
		float rcHyd = -1, rcEng = -1;
		if (ctrl)
		{
			rcHyd = ctrl.GetHydration();
			rcEng = ctrl.GetEnergy();
		}

		bool rcBleeding = false;
		SCR_CharacterDamageManagerComponent rcDmg = SCR_CharacterDamageManagerComponent.Cast(SCR_DamageManagerComponent.GetDamageManager(playerEntity));
		if (rcDmg)
			rcBleeding = rcDmg.IsBleeding();

		string status = "OK";
		if (rcHyd <= 0.02 || rcEng <= 0.02)
			status = "💀 AT_RISK (vai morrer em minutos)";
		else if (rcHyd < 0.25 || rcEng < 0.25)
			status = "⚠️ LOW";

		if (ctrl)
		{
			fields.Insert(new BZ_DiscordField("💧 Hidratação", string.Format("%1%% (%2)", Math.Round(rcHyd * 100), status)));
			fields.Insert(new BZ_DiscordField("🍖 Energia", string.Format("%1%%", Math.Round(rcEng * 100))));
		}

		if (rcBleeding)
			fields.Insert(new BZ_DiscordField("🩸 Sangrando", "SIM — precisa bandagem agora"));

		Print(string.Format("[BrasilZ][Metabolism] Player %1 reconnect snapshot: hydration=%2 energy=%3 bleeding=%4 status=%5", playerId, rcHyd, rcEng, rcBleeding, status), LogLevel.NORMAL);
	}

	BZ_DiscordWebhook.Send(
		"👋 Player conectou",
		"**" + name + "** entrou no servidor",
		BZ_DiscordConfig.COLOR_BLUE,
		fields
	);
}

// Static helper for both shop hooks — keeps the modded class bodies short and
// avoids duplicating identification logic.
void BZ_PortalHooks_SendConnectEvent(int playerId, string name, int onlineCount, bool firstTime)
{
	if (!BZ_PortalConfig.LOG_CONNECT)
		return;

	PlayerManager pm = GetGame().GetPlayerManager();
	if (!pm)
		return;

	IEntity playerEntity = pm.GetPlayerControlledEntity(playerId);
	int walletTotal, looseTotal, grandTotal;
	BZ_DiscordWebhook.GetPlayerBalanceDetailed(playerEntity, walletTotal, looseTotal, grandTotal);

	string data = "{";
	data += "\"player\":" + BZ_PortalWebhook.PlayerJson(playerId, name, playerEntity) + ",";
	data += "\"first_time\":" + BZ_PortalWebhook.JsonBool(firstTime) + ",";
	data += "\"online_count\":" + onlineCount.ToString() + ",";
	data += "\"balance\":" + BZ_PortalWebhook.BalanceJson(walletTotal, looseTotal, grandTotal);
	data += "}";
	BZ_PortalWebhook.SendEvent("player_connected", data);
}

void BZ_DiscordHooks_LogShopTransaction(IEntity player, ADM_ShopMerchandise merchandise, int quantity, bool success, bool isPurchase)
{
	PlayerManager pm = GetGame().GetPlayerManager();
	if (!pm)
		return;

	int playerId = pm.GetPlayerIdFromControlledEntity(player);
	string playerName = pm.GetPlayerName(playerId);
	if (playerName.IsEmpty())
		playerName = string.Format("Player %1", playerId);

	// Extract prefab from whichever merchandise type was used (Item vs Vehicle).
	ResourceName prefab = "";
	ADM_MerchandiseItem mItem = ADM_MerchandiseItem.Cast(merchandise.GetType());
	if (mItem)
		prefab = mItem.GetPrefab();

	ADM_MerchandiseVehicle mVeh = ADM_MerchandiseVehicle.Cast(merchandise.GetType());
	if (mVeh)
		prefab = mVeh.GetPrefab();

	string itemName = BZ_DiscordWebhook.PrefabToName(prefab);
	int price = BZ_DiscordWebhook.SumCurrencyPrice(merchandise.GetBuyPayment(), quantity);
	if (!isPurchase)
		price = BZ_DiscordWebhook.SumCurrencyPrice(merchandise.GetSellPayment(), quantity);

	ref array<ref BZ_DiscordField> fields = new array<ref BZ_DiscordField>();
	fields.Insert(new BZ_DiscordField("Player", playerName));
	fields.Insert(new BZ_DiscordField("Item", itemName));
	fields.Insert(new BZ_DiscordField("Quantidade", quantity.ToString()));
	fields.Insert(new BZ_DiscordField("Preço", "$" + price.ToString()));
	BZ_DiscordWebhook.AddBalanceFields(player, fields);

	string title;
	string desc;
	int color;

	if (isPurchase)
	{
		if (success)
		{
			title = "🛒 Compra realizada";
			desc = "**" + playerName + "** comprou *" + itemName + "*";
			color = BZ_DiscordConfig.COLOR_GREEN;
		}
		else
		{
			title = "❌ Compra falhou";
			desc = "**" + playerName + "** tentou comprar *" + itemName + "*";
			color = BZ_DiscordConfig.COLOR_RED;
		}
	}
	else
	{
		title = "💰 Venda realizada";
		desc = "**" + playerName + "** vendeu *" + itemName + "*";
		color = BZ_DiscordConfig.COLOR_YELLOW;
	}

	bool shouldSendDiscord = false;
	if (isPurchase && success && BZ_DiscordConfig.LOG_PURCHASE)
		shouldSendDiscord = true;
	else if (isPurchase && !success && BZ_DiscordConfig.LOG_PURCHASE_FAIL)
		shouldSendDiscord = true;
	else if (!isPurchase && BZ_DiscordConfig.LOG_SALE)
		shouldSendDiscord = true;

	if (shouldSendDiscord)
		BZ_DiscordWebhook.Send(title, desc, color, fields);

	bool shouldSendPortal = false;
	string portalEventType = "";
	if (isPurchase && success && BZ_PortalConfig.LOG_PURCHASE)
	{
		shouldSendPortal = true;
		portalEventType = "shop_purchase";
	}
	else if (isPurchase && !success && BZ_PortalConfig.LOG_PURCHASE_FAIL)
	{
		shouldSendPortal = true;
		portalEventType = "shop_purchase_failed";
	}
	else if (!isPurchase && BZ_PortalConfig.LOG_SALE)
	{
		shouldSendPortal = true;
		portalEventType = "shop_sale";
	}

	if (shouldSendPortal)
	{
		int portalWallet, portalLoose, portalTotal;
		BZ_DiscordWebhook.GetPlayerBalanceDetailed(player, portalWallet, portalLoose, portalTotal);

		string data = "{";
		data += "\"player\":" + BZ_PortalWebhook.PlayerJson(playerId, playerName, player) + ",";
		data += "\"item\":{\"name\":" + BZ_PortalWebhook.JsonString(itemName) + ",\"prefab\":" + BZ_PortalWebhook.JsonString(prefab) + "},";
		data += "\"quantity\":" + quantity.ToString() + ",";
		data += "\"success\":" + BZ_PortalWebhook.JsonBool(success) + ",";
		data += "\"is_purchase\":" + BZ_PortalWebhook.JsonBool(isPurchase) + ",";
		data += "\"price\":" + price.ToString() + ",";
		data += "\"balance\":" + BZ_PortalWebhook.BalanceJson(portalWallet, portalLoose, portalTotal);
		data += "}";
		BZ_PortalWebhook.SendEvent(portalEventType, data);
	}
}

//------------------------------------------------------------------------------------------------
string BZ_DiscordHooks_GetEquippedWeaponName(IEntity character)
{
	if (!character)
		return "(desconhecida)";

	CharacterControllerComponent controller = CharacterControllerComponent.Cast(character.FindComponent(CharacterControllerComponent));
	if (!controller)
		return "(sem weapon manager)";

	BaseWeaponManagerComponent weaponManager = controller.GetWeaponManagerComponent();
	if (!weaponManager)
		return "(sem weapon manager)";

	BaseWeaponComponent currentWeapon = weaponManager.GetCurrent();
	if (!currentWeapon || !currentWeapon.GetOwner())
		return "(maos/vazio)";

	IEntity weaponEntity = currentWeapon.GetOwner();
	if (!weaponEntity || !weaponEntity.GetPrefabData())
		return "(arma sem prefab)";

	return BZ_DiscordWebhook.PrefabToName(weaponEntity.GetPrefabData().GetPrefabName());
}

// ============================================================================
// Balance snapshot cache
// ----------------------------------------------------------------------------
// PROBLEM: OnPlayerKilled fires AFTER engine drop-on-death dispatches inventory
// items (backpack containing the wallet drops to the ground). At kill time the
// inventory query returns 0 wallets — Discord embed showed "$0 / $0 / $0" for
// the victim even when they had cash.
//
// SOLUTION: cache balance per playerId at a regular interval (15s) while the
// player is alive. OnPlayerKilled uses the cached value (last known live
// balance) as the snapshot. Live re-query still runs as best-effort but cached
// value is preferred when > 0.
// ============================================================================
class BZ_BalanceCacheEntry
{
	int m_iWallet;
	int m_iLoose;
	int m_iTotal;
	int m_iSampledAtMs;
}

class BZ_DiscordBalanceCache
{
	protected static ref map<int, ref BZ_BalanceCacheEntry> s_mCache = new map<int, ref BZ_BalanceCacheEntry>();
	protected static const int BZ_BALANCE_SAMPLE_INTERVAL_MS = 15000; // 15s
	protected static bool s_bTickerArmed = false;

	//------------------------------------------------------------------------------------------------
	static void ArmTicker()
	{
		if (s_bTickerArmed)
			return;
		s_bTickerArmed = true;
		GetGame().GetCallqueue().CallLater(Tick, BZ_BALANCE_SAMPLE_INTERVAL_MS, true);
		Print("[BrasilZ][DiscordCache] Balance sampler armed every 15s.", LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	static void Tick()
	{
		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return;

		array<int> ids = {};
		pm.GetPlayers(ids);
		foreach (int pid : ids)
		{
			if (pid <= 0)
				continue;
			IEntity ent = pm.GetPlayerControlledEntity(pid);
			if (!ent)
				continue;

			int wallet, loose, total;
			BZ_DiscordWebhook.GetPlayerBalanceDetailed(ent, wallet, loose, total);

			BZ_BalanceCacheEntry entry = s_mCache.Get(pid);
			if (!entry)
			{
				entry = new BZ_BalanceCacheEntry();
				s_mCache.Set(pid, entry);
			}
			entry.m_iWallet = wallet;
			entry.m_iLoose = loose;
			entry.m_iTotal = total;
			entry.m_iSampledAtMs = System.GetTickCount();
		}
	}

	//------------------------------------------------------------------------------------------------
	// Returns last cached balance for the player. Out params zero if no entry.
	// ageMs returns time since last sample (-1 if no entry).
	static void GetCached(int playerId, out int wallet, out int loose, out int total, out int ageMs)
	{
		wallet = 0;
		loose = 0;
		total = 0;
		ageMs = -1;
		BZ_BalanceCacheEntry entry = s_mCache.Get(playerId);
		if (!entry)
			return;
		wallet = entry.m_iWallet;
		loose = entry.m_iLoose;
		total = entry.m_iTotal;
		ageMs = System.GetTickCount() - entry.m_iSampledAtMs;
	}

	//------------------------------------------------------------------------------------------------
	// Remove cache entry on disconnect to avoid stale data on player rejoin
	// with same playerId.
	static void Clear(int playerId)
	{
		if (s_mCache.Contains(playerId))
			s_mCache.Remove(playerId);
	}
}

// ============================================================================
// Player lifecycle hooks (connect / disconnect / kill)
// Attached to BZ_GameMode so we only fire on the BrasilZ game mode, not in
// editor or other modes.
// ============================================================================
modded class SCR_BaseGameMode
{
	//------------------------------------------------------------------------------------------------
	// NOTE: EOnInit override is in BZ_GameMode.c. Cannot declare a second one here (compile
	// fails on duplicate override of same merged modded class). Ticker arms lazily from
	// OnPlayerConnected — first connect triggers ArmTicker (idempotent via s_bTickerArmed).
	override void OnPlayerConnected(int playerId)
	{
		super.OnPlayerConnected(playerId);

		// Lazy-arm balance cache ticker on first server-side connect. Idempotent.
		if (Replication.IsServer())
		{
			BZ_DiscordBalanceCache.ArmTicker();
			BZ_PortalRewards.EnsureStarted();
			BZ_PortalSessionTracker.EnsureLifeStart(playerId);
		}

		if (!Replication.IsServer() || (!BZ_DiscordConfig.LOG_CONNECT && !BZ_PortalConfig.LOG_CONNECT))
			return;

		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return;

		string name = pm.GetPlayerName(playerId);
		if (name.IsEmpty())
			name = string.Format("Player %1", playerId);

		int onlineCount = pm.GetPlayerCount();

		// First-time detection: probe $profile:BrasilZ/seen/<uid>.flag.
		// If file is absent the player has never connected before, then we
		// create the marker so subsequent reconnects fall through the normal
		// path. UID via BZ_Utils.GetPlayerUID (same source as ReforgedZ).
		string uid = BZ_Utils.GetPlayerUID(playerId);
		bool firstTime = false;
		if (!uid.IsEmpty())
		{
			string flagPath = "$profile:BrasilZ/Seen/" + uid + ".flag";
			if (!FileIO.FileExists(flagPath))
			{
				firstTime = true;
				FileHandle fh = FileIO.OpenFile(flagPath, FileMode.WRITE);
				if (fh)
				{
					fh.WriteLine("first_seen=" + System.GetUnixTime().ToString());
					fh.Close();
				}
			}
		}

		if (firstTime)
		{
			ref array<ref BZ_DiscordField> newFields = new array<ref BZ_DiscordField>();
			newFields.Insert(new BZ_DiscordField("Player", name));
			newFields.Insert(new BZ_DiscordField("UID", uid));
			newFields.Insert(new BZ_DiscordField("Players online", onlineCount.ToString()));

			if (BZ_DiscordConfig.LOG_CONNECT)
				BZ_DiscordWebhook.Send(
				"🆕 Player NOVO no servidor",
				"**" + name + "** entrou pela primeira vez!",
				BZ_DiscordConfig.COLOR_GREEN,
				newFields
			);

			GetGame().GetCallqueue().CallLater(BZ_PortalHooks_SendConnectEvent, 3000, false, playerId, name, onlineCount, true);
		}
		else
		{
			// Delay 3s so vanilla character-restore finishes and the wallet
			// entities are present in the inventory before we read the balance.
			// Without the delay GetPlayerControlledEntity returns null and the
			// balance shows $0 even when the player has cash.
			if (BZ_DiscordConfig.LOG_CONNECT)
				GetGame().GetCallqueue().CallLater(BZ_DiscordHooks_SendConnectEvent, 3000, false, playerId, name, onlineCount);
			if (BZ_PortalConfig.LOG_CONNECT)
				GetGame().GetCallqueue().CallLater(BZ_PortalHooks_SendConnectEvent, 3000, false, playerId, name, onlineCount, false);
		}
	}

	//------------------------------------------------------------------------------------------------
	override void OnPlayerDisconnected(int playerId, KickCauseCode cause = KickCauseCode.NONE, int timeout = -1)
	{
		// Capture name AND balance BEFORE super deletes the player record.
		string name = "";
		int walletTotal = 0;
		int looseTotal = 0;
		int grandTotal = 0;
		IEntity disconnectEntity = null;
		if (Replication.IsServer() && (BZ_DiscordConfig.LOG_DISCONNECT || BZ_PortalConfig.LOG_DISCONNECT))
		{
			PlayerManager pm = GetGame().GetPlayerManager();
			if (pm)
			{
				name = pm.GetPlayerName(playerId);
				if (name.IsEmpty())
					name = string.Format("Player %1", playerId);

				IEntity playerEntity = pm.GetPlayerControlledEntity(playerId);
				disconnectEntity = playerEntity;
				BZ_DiscordWebhook.GetPlayerBalanceDetailed(playerEntity, walletTotal, looseTotal, grandTotal);

				// Snapshot metabolism + bleeding state on disconnect. FMMetabolism2 freezes
				// decay on disconnect (callqueue Remove), bleeding effect persists but damage
				// handling is disabled by BZ_SinkCharacterOnDisconnect. Log so reconnect can
				// be cross-referenced.
				if (playerEntity)
				{
					SCR_CharacterControllerComponent ctrl = SCR_CharacterControllerComponent.Cast(playerEntity.FindComponent(SCR_CharacterControllerComponent));
					float dcHyd = -1, dcEng = -1;
					if (ctrl)
					{
						dcHyd = ctrl.GetHydration();
						dcEng = ctrl.GetEnergy();
					}

					bool dcBleeding = false;
					SCR_CharacterDamageManagerComponent dcDmg = SCR_CharacterDamageManagerComponent.Cast(SCR_DamageManagerComponent.GetDamageManager(playerEntity));
					if (dcDmg)
						dcBleeding = dcDmg.IsBleeding();

					string warn = "";
					if (dcHyd >= 0 && dcHyd < 0.25) warn = warn + " THIRSTY";
					if (dcEng >= 0 && dcEng < 0.25) warn = warn + " HUNGRY";
					if (dcBleeding) warn = warn + " BLEEDING(damage_off_while_offline)";
					Print(string.Format("[BrasilZ][Metabolism] Player %1 disconnect snapshot: hydration=%2 energy=%3 bleeding=%4%5", playerId, dcHyd, dcEng, dcBleeding, warn), LogLevel.NORMAL);
				}
			}
		}

		super.OnPlayerDisconnected(playerId, cause, timeout);

		// Drop cached balance — playerId may be reused for a different player on next
		// connect, stale cache could leak into another player's death event.
		BZ_DiscordBalanceCache.Clear(playerId);

		if (!name.IsEmpty())
		{
			ref array<ref BZ_DiscordField> fields = new array<ref BZ_DiscordField>();
			fields.Insert(new BZ_DiscordField("Valor Na Carteira", "$" + walletTotal.ToString()));
			fields.Insert(new BZ_DiscordField("Valor Fora da Carteira", "$" + looseTotal.ToString()));
			fields.Insert(new BZ_DiscordField("Total", "$" + grandTotal.ToString()));

			if (BZ_PortalConfig.LOG_DISCONNECT)
			{
				string data = "{";
				data += "\"player\":" + BZ_PortalWebhook.PlayerJson(playerId, name, disconnectEntity) + ",";
				data += "\"cause_code\":" + cause.ToString() + ",";
				data += "\"timeout\":" + timeout.ToString() + ",";
				data += "\"balance\":" + BZ_PortalWebhook.BalanceJson(walletTotal, looseTotal, grandTotal);
				data += "}";
				BZ_PortalWebhook.SendEvent("player_disconnected", data);
			}

			if (BZ_DiscordConfig.LOG_DISCONNECT)
				BZ_DiscordWebhook.Send(
				"🚪 Player saiu",
				"**" + name + "** desconectou",
				BZ_DiscordConfig.COLOR_GRAY,
				fields
			);
		}
	}

	//------------------------------------------------------------------------------------------------
	override void OnPlayerKilled(int playerId, IEntity playerEntity, IEntity killerEntity, notnull Instigator killer)
	{
		// CAPTURE BALANCE FIRST — engine drops the dead body's backpack (which holds the
		// wallet) BEFORE OnPlayerKilled fires. By the time we get here the live inventory
		// query returns 0 wallets and Discord showed $0 for the victim even with cash.
		//
		// Strategy: prefer the cached balance (sampled every 15s by BZ_DiscordBalanceCache).
		// Cache holds the last KNOWN-ALIVE balance, which is the right value to report at
		// kill time. Fall back to live snapshot only if cache is empty (player died < 15s
		// after connect) AND live > 0 (rare — drop usually already happened).
		int preVictimWallet = 0;
		int preVictimLoose = 0;
		int preVictimTotal = 0;
		int cachedWallet = 0, cachedLoose = 0, cachedTotal = 0, cacheAgeMs = -1;
		BZ_DiscordBalanceCache.GetCached(playerId, cachedWallet, cachedLoose, cachedTotal, cacheAgeMs);

		int liveWallet = 0, liveLoose = 0, liveTotal = 0;
		if (playerEntity)
			BZ_DiscordWebhook.GetPlayerBalanceDetailed(playerEntity, liveWallet, liveLoose, liveTotal);

		// Use cache if present, else live. If both present and cache total > live total,
		// trust cache (engine likely already dropped items).
		if (cacheAgeMs >= 0 && cachedTotal >= liveTotal)
		{
			preVictimWallet = cachedWallet;
			preVictimLoose = cachedLoose;
			preVictimTotal = cachedTotal;
		}
		else
		{
			preVictimWallet = liveWallet;
			preVictimLoose = liveLoose;
			preVictimTotal = liveTotal;
		}

		Print(string.Format("[BrasilZ][Killed] Victim balance snapshot: cache=$%1 (age=%2ms) live=$%3 → using=$%4",
			cachedTotal, cacheAgeMs, liveTotal, preVictimTotal), LogLevel.NORMAL);

		// CAPTURE METABOLISM state pre-death. FMMetabolism2 applies fatal damage via
		// SetHealthScaled(0) without Instigator — vanilla OnPlayerKilled then fires with
		// killerEntity=null, so we'd just log "Ambiente". Read hydration/energy at the
		// moment of death so we can differentiate "death by thirst" vs "death by starvation"
		// vs other environmental causes (fall, drown).
		float preHydration = -1;
		float preEnergy = -1;
		bool preBleeding = false;
		if (playerEntity)
		{
			SCR_CharacterControllerComponent ctrl = SCR_CharacterControllerComponent.Cast(playerEntity.FindComponent(SCR_CharacterControllerComponent));
			if (ctrl)
			{
				preHydration = ctrl.GetHydration();
				preEnergy = ctrl.GetEnergy();
			}

			// Bleeding state via vanilla damage manager. Vanilla bleeding deals damage via
			// persistent SCR_BleedingDamageEffect — if HP drops to 0 from bleeding the kill
			// fires with killerEntity = original shooter (PvP/PvE) OR null if shooter
			// despawned. Captures so we know death cause when killer was de-referenced.
			SCR_CharacterDamageManagerComponent dmgMgr = SCR_CharacterDamageManagerComponent.Cast(SCR_DamageManagerComponent.GetDamageManager(playerEntity));
			if (dmgMgr)
				preBleeding = dmgMgr.IsBleeding();

			Print(string.Format("[BrasilZ][Killed] Player %1 health snapshot: hydration=%2 energy=%3 bleeding=%4", playerId, preHydration, preEnergy, preBleeding), LogLevel.NORMAL);
		}

		// Also snapshot KILLER balance pre-loot. If PvP, killer may walk over and pick up
		// victim's wallet/notes within seconds — but at the EXACT moment of kill we want to
		// show what the killer was carrying BEFORE looting. Killer is alive at this point so
		// live query usually works, but use cache if available (more reliable across edge
		// cases like killer also taking lethal damage in the same frame).
		int preKillerWallet = 0;
		int preKillerLoose = 0;
		int preKillerTotal = 0;
		IEntity preKillerEnt = null;
		if (killer)
			preKillerEnt = killer.GetInstigatorEntity();

		float killDistance = -1;
		string killWeapon = "(desconhecida)";
		if (playerEntity && preKillerEnt && preKillerEnt != playerEntity)
		{
			killDistance = vector.Distance(playerEntity.GetOrigin(), preKillerEnt.GetOrigin());
			killWeapon = BZ_DiscordHooks_GetEquippedWeaponName(preKillerEnt);
		}

		int killerLiveW = 0, killerLiveL = 0, killerLiveT = 0;
		if (preKillerEnt && preKillerEnt != playerEntity)
			BZ_DiscordWebhook.GetPlayerBalanceDetailed(preKillerEnt, killerLiveW, killerLiveL, killerLiveT);

		PlayerManager pmKiller = GetGame().GetPlayerManager();
		int killerPidEarly = 0;
		if (pmKiller && preKillerEnt)
			killerPidEarly = pmKiller.GetPlayerIdFromControlledEntity(preKillerEnt);

		int killerCacheW = 0, killerCacheL = 0, killerCacheT = 0, killerCacheAge = -1;
		if (killerPidEarly > 0)
			BZ_DiscordBalanceCache.GetCached(killerPidEarly, killerCacheW, killerCacheL, killerCacheT, killerCacheAge);

		if (killerCacheAge >= 0 && killerCacheT >= killerLiveT)
		{
			preKillerWallet = killerCacheW;
			preKillerLoose = killerCacheL;
			preKillerTotal = killerCacheT;
		}
		else
		{
			preKillerWallet = killerLiveW;
			preKillerLoose = killerLiveL;
			preKillerTotal = killerLiveT;
		}

		super.OnPlayerKilled(playerId, playerEntity, killerEntity, killer);

		bool shouldSendDiscordKill = BZ_DiscordConfig.LOG_KILL;
		bool shouldSendPortalKill = BZ_PortalConfig.LOG_KILL;
		if (!Replication.IsServer() || (!shouldSendDiscordKill && !shouldSendPortalKill))
			return;

		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return;

		string victimName = pm.GetPlayerName(playerId);
		if (victimName.IsEmpty())
			victimName = string.Format("Player %1", playerId);

		// Identify killer if any (could be NPC, self, environment, or another player).
		int killerPlayerId = 0;
		string killerName = "Ambiente";
		IEntity killerEnt = null;
		if (killer)
			killerEnt = killer.GetInstigatorEntity();

		if (killerEnt)
		{
			killerPlayerId = pm.GetPlayerIdFromControlledEntity(killerEnt);
			if (killerPlayerId > 0)
			{
				killerName = pm.GetPlayerName(killerPlayerId);
				if (killerName.IsEmpty())
					killerName = string.Format("Player %1", killerPlayerId);
			}
			else
			{
				// AI killer — classify by prefab path. Better than generic "NPC / Zombie"
				// because admin can grep Discord logs to track which AI type kills players most.
				string killerPrefab = "(no-prefab)";
				if (killerEnt.GetPrefabData())
					killerPrefab = killerEnt.GetPrefabData().GetPrefabName();

				if (killerPrefab.IndexOf("Zombie") >= 0 || killerPrefab.IndexOf("Infected") >= 0 || killerPrefab.IndexOf("BaconZ") >= 0)
					killerName = "🧟 Zombie";
				else if (killerPrefab.IndexOf("PLASTICBANDIT") >= 0 || killerPrefab.IndexOf("Bandit") >= 0)
					killerName = "🔫 Bandido (NPC)";
				else if (killerPrefab.IndexOf("Demon") >= 0 || killerPrefab.IndexOf("Boss") >= 0)
					killerName = "👹 Demônio/Boss";
				else if (killerPrefab.IndexOf("Animal") >= 0 || killerPrefab.IndexOf("Wolf") >= 0 || killerPrefab.IndexOf("Bear") >= 0)
					killerName = "🐺 Animal Selvagem";
				else
					killerName = string.Format("NPC (%1)", killerPrefab);
			}
		}
		else
		{
			// No killer entity — could be FMMetabolism2 (SetHealthScaled bypasses Instigator),
			// fall damage, drowning, bleeding (if shooter despawned), or other environmental.
			// Use snapshot to differentiate. FMMetabolism applies fatal damage when stat
			// <= 0.02 (EmptyThreshold). Bleeding adds 🩸 marker.
			const float METAB_EMPTY = 0.02;
			bool starved = (preEnergy >= 0 && preEnergy <= METAB_EMPTY);
			bool dehydrated = (preHydration >= 0 && preHydration <= METAB_EMPTY);

			if (starved && dehydrated)
				killerName = "💀 Fome + Sede";
			else if (starved)
				killerName = "🍖 Fome (Starvation)";
			else if (dehydrated)
				killerName = "💧 Sede (Desidratação)";
			else if (preBleeding)
				killerName = "🩸 Sangramento (sem socorro)";
			else
				killerName = "Ambiente (queda/afogamento/desconhecido)";
		}

		bool isSuicide = (killerPlayerId > 0 && killerPlayerId == playerId);
		bool isPvP     = (killerPlayerId > 0 && killerPlayerId != playerId);

		string title;
		string desc;
		int color;

		if (isSuicide)
		{
			title = "💀 Suicídio";
			desc  = "**" + victimName + "** se matou";
			color = BZ_DiscordConfig.COLOR_GRAY;
		}
		else if (isPvP)
		{
			title = "⚔ PvP Kill";
			desc  = "**" + killerName + "** matou **" + victimName + "**";
			color = BZ_DiscordConfig.COLOR_PURPLE;
		}
		else
		{
			title = "💀 Player morreu";
			desc  = "**" + victimName + "** foi morto por *" + killerName + "*";
			color = BZ_DiscordConfig.COLOR_RED;
		}

		ref array<ref BZ_DiscordField> fields = new array<ref BZ_DiscordField>();
		fields.Insert(new BZ_DiscordField("Vítima", victimName));
		fields.Insert(new BZ_DiscordField("Killer", killerName));
		if (killDistance >= 0)
			fields.Insert(new BZ_DiscordField("Distancia", string.Format("%1 m", Math.Round(killDistance))));
		if (killDistance >= 0)
			fields.Insert(new BZ_DiscordField("Arma", killWeapon));

		// Victim balance (PRE-death snapshot — captured before super decoupled body)
		if (isPvP)
			fields.Insert(new BZ_DiscordField("💰 Saldo Vítima — Carteira", "$" + preVictimWallet.ToString()));
		else
			fields.Insert(new BZ_DiscordField("Valor Na Carteira", "$" + preVictimWallet.ToString()));

		if (isPvP)
			fields.Insert(new BZ_DiscordField("💰 Saldo Vítima — Solto", "$" + preVictimLoose.ToString()));
		else
			fields.Insert(new BZ_DiscordField("Valor Fora da Carteira", "$" + preVictimLoose.ToString()));

		if (isPvP)
			fields.Insert(new BZ_DiscordField("💰 Saldo Vítima — Total", "$" + preVictimTotal.ToString()));
		else
			fields.Insert(new BZ_DiscordField("Total", "$" + preVictimTotal.ToString()));

		// Killer balance (PRE-loot snapshot — what killer carried at moment of kill,
		// before walking over to loot victim). Only show on PvP — no point on suicide
		// (same player) or NPC kills (no wallet on zombies).
		if (isPvP)
		{
			fields.Insert(new BZ_DiscordField("🔫 Saldo Killer — Carteira", "$" + preKillerWallet.ToString()));
			fields.Insert(new BZ_DiscordField("🔫 Saldo Killer — Solto", "$" + preKillerLoose.ToString()));
			fields.Insert(new BZ_DiscordField("🔫 Saldo Killer — Total", "$" + preKillerTotal.ToString()));
		}

		if (shouldSendPortalKill)
		{
			string killerType = "environment";
			string killerPrefab = "";
			if (isSuicide)
				killerType = "suicide";
			else if (isPvP)
				killerType = "player";
			else if (killerEnt)
			{
				if (killerEnt.GetPrefabData())
					killerPrefab = killerEnt.GetPrefabData().GetPrefabName();

				if (killerPrefab.IndexOf("Zombie") >= 0 || killerPrefab.IndexOf("Infected") >= 0 || killerPrefab.IndexOf("BaconZ") >= 0)
					killerType = "zombie";
				else if (killerPrefab.IndexOf("PLASTICBANDIT") >= 0 || killerPrefab.IndexOf("Bandit") >= 0)
					killerType = "bandit";
				else
					killerType = "npc";
			}

			string data = "{";
			data += "\"victim\":" + BZ_PortalWebhook.PlayerJson(playerId, victimName, playerEntity) + ",";
			data += "\"victim_balance\":" + BZ_PortalWebhook.BalanceJson(preVictimWallet, preVictimLoose, preVictimTotal) + ",";
			data += "\"victim_stats\":{\"hydration\":" + preHydration.ToString() + ",\"energy\":" + preEnergy.ToString() + ",\"bleeding\":" + BZ_PortalWebhook.JsonBool(preBleeding) + "},";
			data += "\"alive_seconds\":" + BZ_PortalSessionTracker.GetAliveSeconds(playerId).ToString() + ",";
			data += "\"killer\":{\"type\":" + BZ_PortalWebhook.JsonString(killerType) + ",\"name\":" + BZ_PortalWebhook.JsonString(killerName) + ",\"prefab\":" + BZ_PortalWebhook.JsonString(killerPrefab);
			if (killerPlayerId > 0)
				data += ",\"player\":" + BZ_PortalWebhook.PlayerJson(killerPlayerId, killerName, killerEnt);
			data += "},";
			data += "\"killer_balance\":" + BZ_PortalWebhook.BalanceJson(preKillerWallet, preKillerLoose, preKillerTotal) + ",";
			data += "\"weapon\":" + BZ_PortalWebhook.WeaponJson(killerEnt) + ",";
			data += "\"distance_m\":" + killDistance.ToString() + ",";
			data += "\"is_pvp\":" + BZ_PortalWebhook.JsonBool(isPvP) + ",";
			data += "\"is_suicide\":" + BZ_PortalWebhook.JsonBool(isSuicide) + ",";
			data += "\"title\":" + BZ_PortalWebhook.JsonString(title);
			data += "}";
			BZ_PortalWebhook.SendEvent("player_killed", data);
			BZ_PortalSessionTracker.MarkDeath(playerId);
		}

		if (shouldSendDiscordKill)
			BZ_DiscordWebhook.Send(title, desc, color, fields);
	}
}
