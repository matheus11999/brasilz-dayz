// BrasilZ Wallet Contents Persistence v2 (Entity-UUID key)
//
// Salva o conteúdo (notas de dinheiro) de cada wallet num arquivo .db cujo
// nome eh o UUID persistente da entity wallet (via SCR_PersistenceSystem.GetId).
//
// Diferenca da v1 (player-key): wallet pode ser guardada em qualquer storage
// (mochila, container do mundo, vehicle, outro player) e o conteudo persiste.
// Sem dupe quando wallet troca de dono.
//
// Triggers:
//  - Save:  ADM_PaymentMethodCurrency.DistributePayment (apos pagamento)
//           SCR_BaseGameMode.OnPlayerDisconnected     (player desconecta)
//           Timer periodico (default 60s)             (escaneia registry)
//  - Load:  ADM_CurrencyComponent.OnPostInit          (wallet spawna no mundo)
//           (delay para SCR_PersistenceSystem atribuir UUID antes de ler)

class BZ_WalletMoneyRecord
{
	ResourceName m_sPrefab;
	int m_iCount;
}

class BZ_WalletContentsPersistence
{
	protected static const string DB_DIR = "$profile:BrasilZ/Wallets";
	protected static const string DB_VERSION = "BZ_WALLET_V2_UUID";
	protected static const int PERIODIC_SAVE_MS = 60000;
	protected static const int UUID_WAIT_MS = 1500;
	protected static const int UUID_MAX_RETRIES = 8;

	protected static const ResourceName WALLET_PHYSICAL = "{F539F8AD9A0D2249}Prefabs/Items/WalletFisicalValue.et";
	protected static const ResourceName WALLET_VIRTUAL = "{2730C123BE8FA101}Prefabs/Items/WalletVirtualValue.et";
	protected static const ResourceName NOTE_1000 = "{9A7C8A96853B03B6}Prefabs/Items/1000dol.et";
	protected static const ResourceName NOTE_500 = "{0DAB239CAC6D40BF}Prefabs/Items/500dol.et";
	protected static const ResourceName NOTE_100 = "{1A7C2C830126AA67}Prefabs/Items/100dol.et";
	protected static const ResourceName NOTE_50 = "{386CB78806FA2B15}Prefabs/Items/50dol.et";
	protected static const ResourceName NOTE_20 = "{717FEDEC2535943E}Prefabs/Items/20dol.et";
	protected static const ResourceName NOTE_10 = "{C3A8AF077A97A7A6}Prefabs/Items/10dol.et";
	protected static const ResourceName NOTE_5 = "{6F6B6A2281DE4D6D}Prefabs/Items/5dol.et";
	protected static const ResourceName NOTE_1 = "{1A32E3232C7C5619}Prefabs/Items/1dol.et";

	protected static bool s_bStarted;
	protected static ref map<string, IEntity> s_mWallets = new map<string, IEntity>();
	protected static ref map<IEntity, int> s_mRetries = new map<IEntity, int>();

	//------------------------------------------------------------------------------------------------
	static void EnsureStarted()
	{
		if (s_bStarted || !Replication.IsServer())
			return;

		s_bStarted = true;
		FileIO.MakeDirectory("$profile:BrasilZ");
		FileIO.MakeDirectory(DB_DIR);
		GetGame().GetCallqueue().CallLater(PeriodicSaveAll, PERIODIC_SAVE_MS, true);
		Print("[BrasilZ][Wallet] Entity-UUID wallet persistence enabled (V2).", LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	// Chamado pelo modded ADM_CurrencyComponent.OnPostInit quando wallet inicializa.
	// Aguarda atribuicao do UUID pelo SCR_PersistenceSystem, depois registra e restaura.
	static void RegisterWallet(IEntity wallet)
	{
		if (!wallet || !Replication.IsServer())
			return;

		EnsureStarted();

		if (!IsWalletEntity(wallet))
			return;

		s_mRetries.Set(wallet, 0);
		GetGame().GetCallqueue().CallLater(TryFinalizeRegistration, UUID_WAIT_MS, false, wallet);
	}

	//------------------------------------------------------------------------------------------------
	protected static void TryFinalizeRegistration(IEntity wallet)
	{
		if (!wallet)
			return;

		string uuid = GetWalletUUID(wallet);
		if (uuid.IsEmpty())
		{
			int retries = s_mRetries.Get(wallet);
			if (retries < UUID_MAX_RETRIES)
			{
				s_mRetries.Set(wallet, retries + 1);
				GetGame().GetCallqueue().CallLater(TryFinalizeRegistration, UUID_WAIT_MS, false, wallet);
				return;
			}

			Print(string.Format("[BrasilZ][Wallet] Wallet has no UUID after %1 retries, skip.", UUID_MAX_RETRIES), LogLevel.WARNING);
			s_mRetries.Remove(wallet);
			return;
		}

		s_mRetries.Remove(wallet);
		s_mWallets.Set(uuid, wallet);
		Print(string.Format("[BrasilZ][Wallet] Registered uuid=%1.", uuid), LogLevel.NORMAL);

		RestoreWalletContent(uuid, wallet);
	}

	//------------------------------------------------------------------------------------------------
	protected static void RestoreWalletContent(string uuid, IEntity wallet)
	{
		array<ref BZ_WalletMoneyRecord> records = {};
		if (!ReadWalletFile(uuid, records) || records.IsEmpty())
			return;

		int requested = CountRecords(records);
		if (requested <= 0)
			return;

		array<ref BZ_WalletMoneyRecord> current = {};
		CollectWalletMoney(wallet, current);
		int currentCount = CountRecords(current);

		// Vanilla persistence ja restaurou o mesmo conteudo? Pula para evitar duplicacao.
		if (currentCount >= requested)
		{
			Print(string.Format("[BrasilZ][Wallet] Skip restore uuid=%1 (vanilla already has %2/%3).", uuid, currentCount, requested), LogLevel.NORMAL);
			return;
		}

		ClearWalletMoney(wallet);
		int restored = InjectMoneyIntoWallet(wallet, records);
		Print(string.Format("[BrasilZ][Wallet] Restored uuid=%1 notes=%2/%3.", uuid, restored, requested), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	static void SaveEntityWallet(IEntity wallet)
	{
		if (!wallet || !Replication.IsServer())
			return;

		string uuid = GetWalletUUID(wallet);
		if (uuid.IsEmpty())
			return;

		SaveWallet(uuid, wallet);
	}

	//------------------------------------------------------------------------------------------------
	// Server-side payout used by portal rewards. Adds real money notes to the
	// player's existing wallet (or creates one) and persists it immediately.
	static bool AddMoneyToPlayer(IEntity player, int amount)
	{
		if (!player || !Replication.IsServer() || amount <= 0)
			return false;

		EnsureStarted();

		if (!BZ_ShopCurrencyHelper.EnsureCurrencyWallet(player))
			return false;

		SCR_InventoryStorageManagerComponent inventory = SCR_InventoryStorageManagerComponent.Cast(player.FindComponent(SCR_InventoryStorageManagerComponent));
		if (!inventory)
			return false;

		array<IEntity> wallets = ADM_CurrencyComponent.FindCurrencyInInventory(inventory);
		if (!wallets || wallets.IsEmpty())
			return false;

		IEntity wallet = wallets[0];
		array<ref BZ_WalletMoneyRecord> records = {};
		BuildMoneyRecords(amount, records);
		int requested = CountRecords(records);
		if (requested <= 0)
			return false;

		int inserted = InjectMoneyIntoWallet(wallet, records);
		SaveEntityWallet(wallet);

		if (inserted != requested)
		{
			Print(string.Format("[BrasilZ][Wallet] Portal payout partial: amount=%1 notes=%2/%3.", amount, inserted, requested), LogLevel.WARNING);
			return false;
		}

		Print(string.Format("[BrasilZ][Wallet] Portal payout added $%1 to player wallet (%2 notes).", amount, inserted), LogLevel.NORMAL);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected static void SaveWallet(string uuid, IEntity wallet)
	{
		if (!wallet || uuid.IsEmpty())
			return;

		array<ref BZ_WalletMoneyRecord> records = {};
		CollectWalletMoney(wallet, records);
		int noteCount = CountRecords(records);

		if (noteCount <= 0)
		{
			// Wallet vazia -> apaga JSON para evitar restore fantasma.
			DeleteWalletFile(uuid);
			return;
		}

		WriteWalletFile(uuid, records);
	}

	//------------------------------------------------------------------------------------------------
	static void PeriodicSaveAll()
	{
		if (!Replication.IsServer())
			return;

		array<string> stale = {};
		foreach (string uuid, IEntity wallet : s_mWallets)
		{
			if (!wallet)
			{
				stale.Insert(uuid);
				continue;
			}
			SaveWallet(uuid, wallet);
		}

		foreach (string staleUuid : stale)
			s_mWallets.Remove(staleUuid);
	}

	//------------------------------------------------------------------------------------------------
	// Salva todos wallets que estao na hierarquia de um determinado char (player desconectando).
	static void SaveWalletsInCharacter(IEntity character)
	{
		if (!character || !Replication.IsServer())
			return;

		EnsureStarted();

		array<string> uuids = {};
		array<IEntity> wallets = {};
		foreach (string uuid, IEntity wallet : s_mWallets)
		{
			if (!wallet)
				continue;
			if (IsEntityChildOf(wallet, character))
			{
				uuids.Insert(uuid);
				wallets.Insert(wallet);
			}
		}

		for (int i = 0; i < uuids.Count(); i++)
		{
			SaveWallet(uuids[i], wallets[i]);
		}

		Print(string.Format("[BrasilZ][Wallet] Saved %1 wallet(s) of disconnecting char.", uuids.Count()), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsEntityChildOf(IEntity child, IEntity ancestor)
	{
		if (!child || !ancestor)
			return false;

		IEntity p = child.GetParent();
		int safety = 20;
		while (p && safety > 0)
		{
			if (p == ancestor)
				return true;
			p = p.GetParent();
			safety--;
		}
		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected static string GetWalletUUID(IEntity wallet)
	{
		if (!wallet)
			return string.Empty;

		SCR_PersistenceSystem persSys = SCR_PersistenceSystem.GetByEntityWorld(wallet);
		if (!persSys)
			return string.Empty;

		UUID id = persSys.GetId(wallet);
		if (id == UUID.NULL_UUID)
			return string.Empty;

		return string.Format("%1", id);
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsWalletEntity(IEntity entity)
	{
		if (!entity)
			return false;
		ResourceName prefab = GetEntityPrefab(entity);
		if (prefab == WALLET_PHYSICAL || prefab == WALLET_VIRTUAL)
			return true;
		string prefabName = prefab;
		return prefabName.Contains("Wallet");
	}

	//------------------------------------------------------------------------------------------------
	protected static ResourceName GetEntityPrefab(IEntity entity)
	{
		if (!entity)
			return string.Empty;
		auto data = entity.GetPrefabData();
		if (!data)
			return string.Empty;
		return data.GetPrefabName();
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsMoneyNote(ResourceName prefab)
	{
		string prefabName = prefab;
		return prefab == NOTE_1000 || prefab == NOTE_500 || prefab == NOTE_100
			|| prefab == NOTE_50 || prefab == NOTE_20 || prefab == NOTE_10
			|| prefab == NOTE_5 || prefab == NOTE_1 || prefabName.Contains("dol.et");
	}

	//------------------------------------------------------------------------------------------------
	protected static void BuildMoneyRecords(int amount, notnull array<ref BZ_WalletMoneyRecord> records)
	{
		records.Clear();
		amount = AddDenomination(records, amount, 1000, NOTE_1000);
		amount = AddDenomination(records, amount, 500, NOTE_500);
		amount = AddDenomination(records, amount, 100, NOTE_100);
		amount = AddDenomination(records, amount, 50, NOTE_50);
		amount = AddDenomination(records, amount, 20, NOTE_20);
		amount = AddDenomination(records, amount, 10, NOTE_10);
		amount = AddDenomination(records, amount, 5, NOTE_5);
		amount = AddDenomination(records, amount, 1, NOTE_1);
	}

	//------------------------------------------------------------------------------------------------
	protected static int AddDenomination(notnull array<ref BZ_WalletMoneyRecord> records, int amount, int value, ResourceName prefab)
	{
		if (amount < value)
			return amount;
		int count = amount / value;
		amount = amount - (count * value);
		AddRecord(records, prefab, count);
		return amount;
	}

	//------------------------------------------------------------------------------------------------
	protected static void CollectWalletMoney(IEntity wallet, notnull array<ref BZ_WalletMoneyRecord> records)
	{
		if (!wallet)
			return;

		array<Managed> storages = {};
		wallet.FindComponents(BaseInventoryStorageComponent, storages);
		foreach (Managed sRef : storages)
		{
			BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(sRef);
			if (!storage)
				continue;
			array<IEntity> items = {};
			storage.GetAll(items);
			foreach (IEntity item : items)
			{
				ResourceName p = GetEntityPrefab(item);
				if (IsMoneyNote(p))
					AddRecord(records, p, 1);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static void ClearWalletMoney(IEntity wallet)
	{
		if (!wallet)
			return;

		array<Managed> storages = {};
		wallet.FindComponents(BaseInventoryStorageComponent, storages);
		foreach (Managed sRef : storages)
		{
			BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(sRef);
			if (!storage)
				continue;
			array<IEntity> items = {};
			storage.GetAll(items);
			foreach (IEntity item : items)
			{
				if (!item || !IsMoneyNote(GetEntityPrefab(item)))
					continue;
				SCR_EntityHelper.DeleteEntityAndChildren(item);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static int InjectMoneyIntoWallet(IEntity wallet, notnull array<ref BZ_WalletMoneyRecord> records)
	{
		if (!wallet)
			return 0;

		BaseInventoryStorageComponent walletStorage = BaseInventoryStorageComponent.Cast(wallet.FindComponent(BaseInventoryStorageComponent));
		if (!walletStorage)
			return 0;

		// Procura InventoryStorageManagerComponent na wallet ou nos ancestrais.
		InventoryStorageManagerComponent sm = InventoryStorageManagerComponent.Cast(wallet.FindComponent(InventoryStorageManagerComponent));
		if (!sm)
		{
			IEntity parent = wallet.GetParent();
			int safety = 20;
			while (parent && !sm && safety > 0)
			{
				sm = InventoryStorageManagerComponent.Cast(parent.FindComponent(InventoryStorageManagerComponent));
				if (!sm)
					parent = parent.GetParent();
				safety--;
			}
		}

		int restored;
		foreach (BZ_WalletMoneyRecord rec : records)
		{
			if (!rec || rec.m_sPrefab.IsEmpty() || rec.m_iCount <= 0)
				continue;

			Resource res = Resource.Load(rec.m_sPrefab);
			if (!res || !res.IsValid())
				continue;

			for (int i = 0; i < rec.m_iCount; i++)
			{
				vector mat[4];
				wallet.GetTransform(mat);

				EntitySpawnParams sp();
				sp.TransformMode = ETransformMode.WORLD;
				for (int k = 0; k < 4; k++)
					sp.Transform[k] = mat[k];

				IEntity note = GetGame().SpawnEntityPrefab(res, wallet.GetWorld(), sp);
				if (!note)
					continue;

				bool inserted = false;
				if (sm)
					inserted = sm.TryInsertItemInStorage(note, walletStorage);

				if (inserted)
				{
					restored++;
				}
				else
				{
					SCR_EntityHelper.DeleteEntityAndChildren(note);
				}
			}
		}
		return restored;
	}

	//------------------------------------------------------------------------------------------------
	protected static void AddRecord(notnull array<ref BZ_WalletMoneyRecord> records, ResourceName prefab, int count)
	{
		if (count <= 0)
			return;
		foreach (BZ_WalletMoneyRecord r : records)
		{
			if (r && r.m_sPrefab == prefab)
			{
				r.m_iCount += count;
				return;
			}
		}
		BZ_WalletMoneyRecord nr = new BZ_WalletMoneyRecord();
		nr.m_sPrefab = prefab;
		nr.m_iCount = count;
		records.Insert(nr);
	}

	//------------------------------------------------------------------------------------------------
	protected static int CountRecords(notnull array<ref BZ_WalletMoneyRecord> records)
	{
		int c;
		foreach (BZ_WalletMoneyRecord r : records)
		{
			if (r)
				c += r.m_iCount;
		}
		return c;
	}

	//------------------------------------------------------------------------------------------------
	protected static string BuildWalletPath(string uuid)
	{
		return string.Format("%1/%2.db", DB_DIR, SanitizeFileName(uuid));
	}

	//------------------------------------------------------------------------------------------------
	protected static string SanitizeFileName(string value)
	{
		string r = value;
		r.Replace("/", "_");
		r.Replace(":", "_");
		r.Replace("*", "_");
		r.Replace("?", "_");
		r.Replace("<", "_");
		r.Replace(">", "_");
		r.Replace("|", "_");
		r.Replace("\\", "_");
		return r;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool WriteWalletFile(string uuid, notnull array<ref BZ_WalletMoneyRecord> records)
	{
		FileIO.MakeDirectory("$profile:BrasilZ");
		FileIO.MakeDirectory(DB_DIR);

		FileHandle h = FileIO.OpenFile(BuildWalletPath(uuid), FileMode.WRITE);
		if (!h)
			return false;

		h.WriteLine(DB_VERSION);
		foreach (BZ_WalletMoneyRecord r : records)
		{
			if (!r || r.m_sPrefab.IsEmpty() || r.m_iCount <= 0)
				continue;
			h.WriteLine(string.Format("%1|%2", r.m_sPrefab, r.m_iCount));
		}
		h.Close();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool ReadWalletFile(string uuid, notnull array<ref BZ_WalletMoneyRecord> records)
	{
		FileHandle h = FileIO.OpenFile(BuildWalletPath(uuid), FileMode.READ);
		if (!h)
			return false;

		records.Clear();
		string line;
		while (h.ReadLine(line) > 0)
		{
			if (line.IsEmpty() || line == DB_VERSION)
				continue;
			array<string> cols = {};
			line.Split("|", cols, false);
			if (cols.Count() < 2)
				continue;
			ResourceName p = cols[0];
			if (!IsMoneyNote(p))
				continue;
			AddRecord(records, p, cols[1].ToInt());
		}
		h.Close();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	// FileIO.DeleteFile pode nao existir, usa overwrite vazio.
	protected static void DeleteWalletFile(string uuid)
	{
		FileHandle h = FileIO.OpenFile(BuildWalletPath(uuid), FileMode.WRITE);
		if (h)
		{
			h.WriteLine(DB_VERSION);
			h.Close();
		}
	}
}

//================================================================================================
// Hook do componente da wallet: registra na criacao da entity, restaura conteudo
// uma vez que UUID estiver disponivel.
modded class ADM_CurrencyComponent
{
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (Replication.IsServer() && owner)
			BZ_WalletContentsPersistence.RegisterWallet(owner);
	}
}

//================================================================================================
// Hook pos-pagamento: salva wallet do payer (e qualquer outra registrada).
modded class ADM_PaymentMethodCurrency
{
	override bool DistributePayment(IEntity player, int quantity = 1)
	{
		bool ok = super.DistributePayment(player, quantity);
		if (ok && Replication.IsServer())
			GetGame().GetCallqueue().CallLater(BZ_WalletContentsPersistence.PeriodicSaveAll, 1000, false);
		return ok;
	}
}

//================================================================================================
// Hook disconnect: garante save final das wallets na hierarquia do char antes
// do Option B remover a entity.
modded class SCR_BaseGameMode : BaseGameMode
{
	override protected void OnPlayerDisconnected(int playerId, KickCauseCode cause, int timeout)
	{
		if (Replication.IsServer())
		{
			BZ_WalletContentsPersistence.EnsureStarted();

			PlayerManager playerManager = GetGame().GetPlayerManager();
			IEntity character = null;
			if (playerManager)
				character = playerManager.GetPlayerControlledEntity(playerId);

			if (character)
				BZ_WalletContentsPersistence.SaveWalletsInCharacter(character);
		}

		super.OnPlayerDisconnected(playerId, cause, timeout);
	}
}
