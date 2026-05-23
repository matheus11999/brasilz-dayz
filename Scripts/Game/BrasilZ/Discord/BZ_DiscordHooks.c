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

		if (result && !BZ_DiscordConfig.LOG_PURCHASE)
			return result;
		if (!result && !BZ_DiscordConfig.LOG_PURCHASE_FAIL)
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

		if (!result || !BZ_DiscordConfig.LOG_SALE)
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

	BZ_DiscordWebhook.Send(title, desc, color, fields);
}

// ============================================================================
// Player lifecycle hooks (connect / disconnect / kill)
// Attached to BZ_GameMode so we only fire on the BrasilZ game mode, not in
// editor or other modes.
// ============================================================================
modded class SCR_BaseGameMode
{
	//------------------------------------------------------------------------------------------------
	override void OnPlayerConnected(int playerId)
	{
		super.OnPlayerConnected(playerId);

		if (!Replication.IsServer() || !BZ_DiscordConfig.LOG_CONNECT)
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

			BZ_DiscordWebhook.Send(
				"🆕 Player NOVO no servidor",
				"**" + name + "** entrou pela primeira vez!",
				BZ_DiscordConfig.COLOR_GREEN,
				newFields
			);
		}
		else
		{
			// Delay 3s so vanilla character-restore finishes and the wallet
			// entities are present in the inventory before we read the balance.
			// Without the delay GetPlayerControlledEntity returns null and the
			// balance shows $0 even when the player has cash.
			GetGame().GetCallqueue().CallLater(BZ_DiscordHooks_SendConnectEvent, 3000, false, playerId, name, onlineCount);
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
		if (Replication.IsServer() && BZ_DiscordConfig.LOG_DISCONNECT)
		{
			PlayerManager pm = GetGame().GetPlayerManager();
			if (pm)
			{
				name = pm.GetPlayerName(playerId);
				if (name.IsEmpty())
					name = string.Format("Player %1", playerId);

				IEntity playerEntity = pm.GetPlayerControlledEntity(playerId);
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

		if (!name.IsEmpty())
		{
			ref array<ref BZ_DiscordField> fields = new array<ref BZ_DiscordField>();
			fields.Insert(new BZ_DiscordField("Valor Na Carteira", "$" + walletTotal.ToString()));
			fields.Insert(new BZ_DiscordField("Valor Fora da Carteira", "$" + looseTotal.ToString()));
			fields.Insert(new BZ_DiscordField("Total", "$" + grandTotal.ToString()));

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
		// CAPTURE BALANCE FIRST — before super.OnPlayerKilled runs BZ_GameMode.OnPlayerKilled
		// which calls BZ_DecoupleDeadBody. Decouple changes persistence ownership and vanilla
		// death also drops slotted items (backpack with wallet inside). After super, the
		// wallet is no longer reachable via the character's inventory hierarchy, so we'd
		// log $0/$0/$0. Snapshot the balance synchronously at the start of the kill event.
		int preVictimWallet = 0;
		int preVictimLoose = 0;
		int preVictimTotal = 0;
		if (playerEntity)
			BZ_DiscordWebhook.GetPlayerBalanceDetailed(playerEntity, preVictimWallet, preVictimLoose, preVictimTotal);

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
		// show what the killer was carrying BEFORE looting. Resolve killer entity first.
		int preKillerWallet = 0;
		int preKillerLoose = 0;
		int preKillerTotal = 0;
		IEntity preKillerEnt = null;
		if (killer)
			preKillerEnt = killer.GetInstigatorEntity();
		if (preKillerEnt && preKillerEnt != playerEntity)
			BZ_DiscordWebhook.GetPlayerBalanceDetailed(preKillerEnt, preKillerWallet, preKillerLoose, preKillerTotal);

		super.OnPlayerKilled(playerId, playerEntity, killerEntity, killer);

		if (!Replication.IsServer() || !BZ_DiscordConfig.LOG_KILL)
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

		BZ_DiscordWebhook.Send(title, desc, color, fields);
	}
}
