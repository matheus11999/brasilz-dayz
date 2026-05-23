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
				killerName = "NPC / Zombie";
			}
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
		BZ_DiscordWebhook.AddBalanceFields(playerEntity, fields);

		BZ_DiscordWebhook.Send(title, desc, color, fields);
	}
}
