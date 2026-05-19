// Global zombie horde manager - tracks all zombies and coordinates alerts
class BZ_ZombieHordeManager
{
	protected static ref BZ_ZombieHordeManager s_Instance;
	protected ref array<BZ_ZombieCharacter> m_aRegisteredZombies = new array<BZ_ZombieCharacter>();
	protected bool m_bDebugMode = false;

	// Zombie population cap
	protected static int s_iMaxZombies = 100;

	// PERFORMANCE: Spatial grid for fast nearby zombie lookups
	protected ref map<int, ref array<BZ_ZombieCharacter>> m_mSpatialGrid = new map<int, ref array<BZ_ZombieCharacter>>();
	protected static const float GRID_CELL_SIZE = 50.0; // 50 meter cells
	protected float m_fLastCleanupTime = 0;
	
	//------------------------------------------------------------------------------------------------
	// Singleton access
	static BZ_ZombieHordeManager GetInstance()
	{
		if (!s_Instance)
			s_Instance = new BZ_ZombieHordeManager();
		
		return s_Instance;
	}
	
	//------------------------------------------------------------------------------------------------
	// Register a zombie to the horde
	void RegisterZombie(BZ_ZombieCharacter zombie)
	{
		if (!zombie)
			return;

		// Check if already registered
		if (m_aRegisteredZombies.Contains(zombie))
			return;

		m_aRegisteredZombies.Insert(zombie);

		// Add to spatial grid
		AddToSpatialGrid(zombie);
	}
	
	//------------------------------------------------------------------------------------------------
	// Unregister a zombie from the horde
	void UnregisterZombie(BZ_ZombieCharacter zombie)
	{
		if (!zombie)
			return;

		// Remove from array
		int index = m_aRegisteredZombies.Find(zombie);
		if (index != -1)
		{
			m_aRegisteredZombies.Remove(index);
		}

		// Remove from spatial grid
		RemoveFromSpatialGrid(zombie);
	}
	
	//------------------------------------------------------------------------------------------------
	// Alert all zombies within radius that don't have a target
	void AlertHorde(BZ_ZombieCharacter alerter, IEntity target, float radius)
	{
		if (!alerter || !target)
		{
			return;
		}

		if (alerter.IsZombieEntity(target))
			return;

		vector alertPos = alerter.GetOrigin();
		int alertCount = 0;
		int zombiesChecked = 0;

		// PERFORMANCE: Use spatial grid to only check nearby zombies instead of all zombies
		array<int> nearbyKeys = new array<int>();
		GetNearbyGridKeys(alertPos, nearbyKeys);

		// Iterate through zombies in nearby grid cells only
		foreach (int key : nearbyKeys)
		{
			array<BZ_ZombieCharacter> cell = m_mSpatialGrid.Get(key);
			if (!cell)
				continue;

			foreach (BZ_ZombieCharacter zombie : cell)
			{
				zombiesChecked++;

				// Skip if null or self
				if (!zombie || zombie == alerter)
					continue;

				// Check distance first
				float distance = vector.Distance(alertPos, zombie.GetOrigin());
				if (distance > radius)
					continue;

				// Check if zombie already has a target
				IEntity currentTarget = zombie.GetCurrentTarget();
				if (currentTarget)
				{
					// Calculate distance to current target vs new target
					float distToCurrentTarget = vector.Distance(zombie.GetOrigin(), currentTarget.GetOrigin());
					float distToNewTarget = vector.Distance(zombie.GetOrigin(), target.GetOrigin());

					bool commitmentActive = zombie.IsInTargetCommitmentPeriod();
					// Allow switching if commitment expired or new target is much closer
					if (!commitmentActive || distToNewTarget < distToCurrentTarget * 0.6)
					{
						// Continue to alert below
					}
					else
					{
						continue;
					}
				}

				// Alert this zombie!
				zombie.ReceiveAlert(target, alertPos);
				alertCount++;
			}
		}
	}
	
	//------------------------------------------------------------------------------------------------
	// Get count of registered zombies
	int GetZombieCount()
	{
		return m_aRegisteredZombies.Count();
	}

	//------------------------------------------------------------------------------------------------
	// Zombie population cap
	static int GetMaxZombies()
	{
		return s_iMaxZombies;
	}

	//------------------------------------------------------------------------------------------------
	static void SetMaxZombies(int max)
	{
		s_iMaxZombies = max;
	}

	//------------------------------------------------------------------------------------------------
	static bool IsAtCap()
	{
		return GetInstance().GetZombieCount() >= s_iMaxZombies;
	}

	//------------------------------------------------------------------------------------------------
	// Enable/disable debug logging
	void SetDebugMode(bool enable)
	{
		m_bDebugMode = enable;
	}
	
	//------------------------------------------------------------------------------------------------
	// PERFORMANCE: Get spatial grid key from position
	protected int GetGridKey(vector pos)
	{
		// Convert position to grid coordinates
		int x = pos[0] / GRID_CELL_SIZE;
		int z = pos[2] / GRID_CELL_SIZE;
		// Combine x and z into single key using simple formula
		// Multiply x by large prime to avoid collisions
		return x * 73856093 + z * 19349663;
	}
	
	//------------------------------------------------------------------------------------------------
	// PERFORMANCE: Add zombie to spatial grid
	protected void AddToSpatialGrid(BZ_ZombieCharacter zombie)
	{
		if (!zombie)
			return;
		
		int key = GetGridKey(zombie.GetOrigin());
		array<BZ_ZombieCharacter> cell = m_mSpatialGrid.Get(key);
		
		if (!cell)
		{
			cell = new array<BZ_ZombieCharacter>();
			m_mSpatialGrid.Set(key, cell);
		}
		
		cell.Insert(zombie);
	}
	
	//------------------------------------------------------------------------------------------------
	// PERFORMANCE: Remove zombie from spatial grid
	protected void RemoveFromSpatialGrid(BZ_ZombieCharacter zombie)
	{
		if (!zombie)
			return;
		
		int key = GetGridKey(zombie.GetOrigin());
		array<BZ_ZombieCharacter> cell = m_mSpatialGrid.Get(key);
		
		if (cell)
		{
			int index = cell.Find(zombie);
			if (index != -1)
				cell.Remove(index);
			
			// Clean up empty cells
			if (cell.Count() == 0)
				m_mSpatialGrid.Remove(key);
		}
	}
	
	//------------------------------------------------------------------------------------------------
	// PERFORMANCE: Get nearby grid keys (9 cells: center + 8 neighbors)
	protected void GetNearbyGridKeys(vector pos, out array<int> keys)
	{
		keys.Clear();
		
		// Convert position to grid coordinates
		int centerX = pos[0] / GRID_CELL_SIZE;
		int centerZ = pos[2] / GRID_CELL_SIZE;
		
		// Check 3x3 grid (center + 8 neighbors)
		for (int dx = -1; dx <= 1; dx++)
		{
			for (int dz = -1; dz <= 1; dz++)
			{
				int x = centerX + dx;
				int z = centerZ + dz;
				// Use same hash formula as GetGridKey
				int key = x * 73856093 + z * 19349663;
				keys.Insert(key);
			}
		}
	}
	
	//------------------------------------------------------------------------------------------------
	// PERFORMANCE: Cleanup dead/deleted zombie references and rebuild spatial grid positions
	void CleanupAndRebuildGrid()
	{
		// Remove dead/deleted zombies from the main array
		for (int i = m_aRegisteredZombies.Count() - 1; i >= 0; i--)
		{
			BZ_ZombieCharacter zombie = m_aRegisteredZombies[i];
			if (!zombie || zombie.IsDeleted())
				m_aRegisteredZombies.Remove(i);
		}

		// Rebuild entire spatial grid from scratch (zombies move, so grid entries go stale)
		m_mSpatialGrid.Clear();
		foreach (BZ_ZombieCharacter zombie : m_aRegisteredZombies)
		{
			if (!zombie)
				continue;

			int key = GetGridKey(zombie.GetOrigin());
			array<BZ_ZombieCharacter> cell = m_mSpatialGrid.Get(key);
			if (!cell)
			{
				cell = new array<BZ_ZombieCharacter>();
				m_mSpatialGrid.Set(key, cell);
			}
			cell.Insert(zombie);
		}
	}
	
	//------------------------------------------------------------------------------------------------
	// Cleanup on destruction
	void ~BZ_ZombieHordeManager()
	{
		m_aRegisteredZombies.Clear();
		m_mSpatialGrid.Clear();
		if (s_Instance == this)
			s_Instance = null;
	}
}
