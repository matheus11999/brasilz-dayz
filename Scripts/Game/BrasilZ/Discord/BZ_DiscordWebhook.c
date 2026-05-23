// Lightweight field struct used to build embed "fields" arrays.
class BZ_DiscordField
{
	string m_sName;
	string m_sValue;
	bool m_bInline;

	void BZ_DiscordField(string n, string v, bool inlineFlag = true)
	{
		m_sName = n;
		m_sValue = v;
		m_bInline = inlineFlag;
	}
}

// RestCallback handler. Discord webhooks respond with 204 No Content on success,
// so OnSuccess gets called with empty body. We only care about OnError to surface
// connectivity / token problems in the server log.
class BZ_DiscordCallback : RestCallback
{
	override void OnSuccess(string data, int dataSize)
	{
		// silent — 204 No Content is expected on success
	}

	override void OnError(int errorCode)
	{
		Print(string.Format("[BZ][Discord] Webhook POST failed: errorCode=%1", errorCode), LogLevel.WARNING);
	}

	override void OnTimeout()
	{
		Print("[BZ][Discord] Webhook POST timeout", LogLevel.WARNING);
	}
}

// Main webhook API. Call Send(title, description, color, fields) from anywhere
// on the server. The class handles JSON escaping, embed assembly, and the
// underlying RestContext POST. Server-only by design — never invoke on clients
// (they would leak the webhook URL into their packet log).
class BZ_DiscordWebhook
{
	protected static ref BZ_DiscordCallback s_Callback;

	// ----- Public entrypoint --------------------------------------------------

	static void Send(string title, string description, int color = 5763719, array<ref BZ_DiscordField> fields = null)
	{
		if (!Replication.IsServer())
			return;

		string url = BZ_DiscordConfig.WEBHOOK_URL;
		if (url.IsEmpty() || !url.Contains("discord.com/api/webhooks"))
		{
			Print("[BZ][Discord] Webhook URL not configured, skipping send", LogLevel.WARNING);
			return;
		}

		string payload = BuildEmbedJson(title, description, color, fields);
		SendRaw(url, payload);
	}

	// ----- Helpers exposed to hook code --------------------------------------

	// Strip "/path/to/Prefab_Name.et" → "Prefab_Name".
	static string PrefabToName(ResourceName prefab)
	{
		string s = prefab;
		if (s.IsEmpty())
			return "(unknown)";

		int lastSlash = s.LastIndexOf("/");
		if (lastSlash >= 0)
			s = s.Substring(lastSlash + 1, s.Length() - lastSlash - 1);

		int dot = s.LastIndexOf(".");
		if (dot >= 0)
			s = s.Substring(0, dot);

		return s;
	}

	// Sum the currency quantity across all payment methods × shop quantity.
	// Only ADM_PaymentMethodCurrency entries contribute; non-currency payments
	// are skipped (e.g. exchange items would show 0, intentionally).
	static int SumCurrencyPrice(array<ref ADM_PaymentMethodBase> payments, int quantity)
	{
		int total = 0;
		if (!payments)
			return total;

		foreach (ADM_PaymentMethodBase p : payments)
		{
			ADM_PaymentMethodCurrency curr = ADM_PaymentMethodCurrency.Cast(p);
			if (curr)
				total += curr.GetQuantity() * quantity;
		}

		return total;
	}

	// Read total currency the player is carrying right now. Uses the same
	// ADM_CurrencyComponent.FindTotalCurrencyInInventory call the shop itself
	// uses to validate payment, so the number matches in-game state exactly.
	static int GetPlayerBalance(IEntity player)
	{
		if (!player)
			return 0;

		SCR_InventoryStorageManagerComponent inventory = SCR_InventoryStorageManagerComponent.Cast(player.FindComponent(SCR_InventoryStorageManagerComponent));
		if (!inventory)
			return 0;

		return ADM_CurrencyComponent.FindTotalCurrencyInInventory(inventory);
	}

	// Returns the numeric value of a single banknote prefab. Used to count
	// loose notes carried outside any wallet (e.g. picked up from a corpse
	// and stuffed in a vest pocket). GUIDs and denominations mirror the ones
	// declared in BZ_WalletContentsPersistence.
	protected static int NoteDenomination(ResourceName prefab)
	{
		string p = prefab;
		// Order matters for prefix collisions: check longer denominations first
		// (1000 before 100, etc.) — most pairs don't actually collide as
		// substrings but ordering longest-first keeps it unambiguous.
		if (p.Contains("1000dol")) return 1000;
		if (p.Contains("500dol"))  return 500;
		if (p.Contains("100dol"))  return 100;
		if (p.Contains("50dol"))   return 50;
		if (p.Contains("20dol"))   return 20;
		if (p.Contains("10dol"))   return 10;
		if (p.Contains("5dol"))    return 5;
		if (p.Contains("1dol"))    return 1;
		return 0;
	}

	// Sum value of loose money notes carried outside any wallet entity.
	// Iterates recursively all inventory storages (including nested in
	// backpack / vest). Notes inside a wallet are skipped because the wallet's
	// own ADM_CurrencyComponent.GetValue() already covers them.
	protected static int SumLooseNotes(IEntity character)
	{
		int total = 0;
		if (!character)
			return total;

		array<Managed> storages = {};
		character.FindComponents(BaseInventoryStorageComponent, storages);

		foreach (Managed sRef : storages)
		{
			BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(sRef);
			if (!storage)
				continue;

			array<IEntity> items = {};
			storage.GetAll(items);

			foreach (IEntity item : items)
			{
				if (!item)
					continue;

				// Skip wallets — their contents are counted via ADM_CurrencyComponent.
				if (item.FindComponent(ADM_CurrencyComponent))
					continue;

				ResourceName prefab;
				auto data = item.GetPrefabData();
				if (data)
					prefab = data.GetPrefabName();

				int v = NoteDenomination(prefab);
				if (v > 0)
					total += v;
			}
		}

		return total;
	}

	// Compute wallet total, loose-note total, and grand total in one pass.
	// Returns values via out params so callers can build their own field set.
	static void GetPlayerBalanceDetailed(IEntity player, out int walletTotal, out int looseTotal, out int grandTotal)
	{
		walletTotal = 0;
		looseTotal = 0;
		grandTotal = 0;

		if (!player)
			return;

		SCR_InventoryStorageManagerComponent inventory = SCR_InventoryStorageManagerComponent.Cast(player.FindComponent(SCR_InventoryStorageManagerComponent));
		if (inventory)
		{
			array<IEntity> wallets = ADM_CurrencyComponent.FindCurrencyInInventory(inventory);
			if (wallets)
			{
				foreach (IEntity w : wallets)
				{
					if (!w) continue;
					ADM_CurrencyComponent c = ADM_CurrencyComponent.Cast(w.FindComponent(ADM_CurrencyComponent));
					if (!c) continue;
					walletTotal += c.GetValue();
				}
			}
		}

		looseTotal = SumLooseNotes(player);
		grandTotal = walletTotal + looseTotal;
	}

	// Append the standard 3-line money breakdown to an embed fields array.
	// Use this from every event hook so the format stays identical across
	// connect / disconnect / spawn / purchase / sale messages.
	static void AddBalanceFields(IEntity player, notnull array<ref BZ_DiscordField> fields)
	{
		int walletTotal, looseTotal, grandTotal;
		GetPlayerBalanceDetailed(player, walletTotal, looseTotal, grandTotal);

		fields.Insert(new BZ_DiscordField("Valor Na Carteira", "$" + walletTotal.ToString()));
		fields.Insert(new BZ_DiscordField("Valor Fora da Carteira", "$" + looseTotal.ToString()));
		fields.Insert(new BZ_DiscordField("Total", "$" + grandTotal.ToString()));
	}

	// Build a per-wallet breakdown string. Player can carry multiple wallet
	// entities (each has its own ADM_CurrencyComponent.GetValue()) — for
	// example one in the vest slot, one inside the backpack. Shows total + a
	// list of individual wallet values when more than one is present.
	//
	// Also counts loose notes carried outside any wallet so a player who
	// pocketed bills from a corpse still shows the full bankroll.
	//
	// Returns "$1500" for single wallet, "$1500 (2x: $1000, $500)" for
	// multiple, "$1500 (Carteiras: $1000 + Solto: $500)" when there are also
	// loose notes outside the wallets.
	static string GetPlayerBalanceBreakdown(IEntity player)
	{
		if (!player)
			return "$0";

		SCR_InventoryStorageManagerComponent inventory = SCR_InventoryStorageManagerComponent.Cast(player.FindComponent(SCR_InventoryStorageManagerComponent));
		if (!inventory)
			return "$0";

		array<IEntity> wallets = ADM_CurrencyComponent.FindCurrencyInInventory(inventory);
		int walletTotal = 0;
		array<int> values = {};
		if (wallets)
		{
			foreach (IEntity w : wallets)
			{
				if (!w) continue;
				ADM_CurrencyComponent c = ADM_CurrencyComponent.Cast(w.FindComponent(ADM_CurrencyComponent));
				if (!c) continue;

				int v = c.GetValue();
				walletTotal += v;
				values.Insert(v);
			}
		}

		int looseTotal = SumLooseNotes(player);
		int grandTotal = walletTotal + looseTotal;

		if (grandTotal == 0)
			return "$0";

		// Single wallet, no loose → simple format
		if (values.Count() <= 1 && looseTotal == 0)
			return "$" + grandTotal.ToString();

		string detail = "$" + grandTotal.ToString() + " (";
		bool needComma = false;

		if (values.Count() == 1)
		{
			detail += "Carteira: $" + values[0].ToString();
			needComma = true;
		}
		else if (values.Count() > 1)
		{
			detail += values.Count().ToString() + " Carteiras: ";
			for (int i = 0; i < values.Count(); i++)
			{
				if (i > 0) detail += ", ";
				detail += "$" + values[i].ToString();
			}
			needComma = true;
		}

		if (looseTotal > 0)
		{
			if (needComma) detail += " + ";
			detail += "Solto: $" + looseTotal.ToString();
		}

		detail += ")";
		return detail;
	}

	// ----- Internals ----------------------------------------------------------

	protected static void SendRaw(string url, string body)
	{
		RestApi api = GetGame().GetRestApi();
		if (!api)
		{
			Print("[BZ][Discord] RestApi unavailable, cannot send", LogLevel.WARNING);
			return;
		}

		RestContext ctx = api.GetContext(url);
		if (!ctx)
		{
			Print("[BZ][Discord] RestContext null, cannot send", LogLevel.WARNING);
			return;
		}

		// Reforger's RestContext.SetHeaders REPLACES the entire header set on
		// each call — calling it twice would erase Content-Type and Discord
		// would then reject the body as "empty message" because it can't parse
		// without the JSON content type. Only the Content-Type is critical for
		// Discord; User-Agent is optional and was dropping the previous header.
		ctx.SetHeaders("Content-Type,application/json");

		if (!s_Callback)
			s_Callback = new BZ_DiscordCallback();

		// Empty endpoint string because the full URL was passed to GetContext.
		ctx.POST(s_Callback, "", body);
	}

	protected static string BuildEmbedJson(string title, string description, int color, array<ref BZ_DiscordField> fields)
	{
		string json = "{";
		json += "\"username\":\"" + EscapeJson(BZ_DiscordConfig.BOT_USERNAME) + "\",";
		json += "\"embeds\":[{";
		json += "\"title\":\""       + EscapeJson(title)       + "\",";
		json += "\"description\":\"" + EscapeJson(description) + "\",";
		json += "\"color\":" + color.ToString();

		if (fields && fields.Count() > 0)
		{
			json += ",\"fields\":[";
			for (int i = 0; i < fields.Count(); i++)
			{
				if (i > 0)
					json += ",";

				BZ_DiscordField f = fields[i];
				string inlineStr;
				if (f.m_bInline)
					inlineStr = "true";
				else
					inlineStr = "false";

				json += "{\"name\":\""  + EscapeJson(f.m_sName)
				     +  "\",\"value\":\"" + EscapeJson(f.m_sValue)
				     +  "\",\"inline\":" + inlineStr + "}";
			}
			json += "]";
		}

		json += "}]}";
		return json;
	}

	protected static string EscapeJson(string s)
	{
		// JSON escape. Enfusion parser doesn't accept the literal "\\\\" /
		// "\\\"" sequences inline, so build replacement strings from concat
		// of single-character primitives instead. We only escape the two chars
		// that actually break Discord webhook JSON: backslash and double-quote.
		string bs = "\\";       // 1 backslash literal
		string quote = "\"";    // 1 double-quote literal

		string escBs = bs + bs;       // backslash → "\\" in JSON
		string escQuote = bs + quote; // quote → "\"" in JSON

		s.Replace(bs, escBs);
		s.Replace(quote, escQuote);
		return s;
	}
}
