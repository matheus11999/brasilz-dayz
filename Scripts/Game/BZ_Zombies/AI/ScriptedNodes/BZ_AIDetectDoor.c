// BrasilZ Zombies - AI Detect Door Node
// Detects doors blocking the zombie's path to target

class BZ_AIDetectDoor : AITaskScripted
{
	// Output port names
	static const string PORT_OUT_DOOR = "detectedDoor";
	
	[Attribute("5.0", UIWidgets.EditBox, "Maximum distance to detect doors")]
	protected float m_fMaxDetectionDistance;
	
	[Attribute("0", UIWidgets.CheckBox, "Enable debug logging")]
	protected bool m_bDebugMode;
	
    protected BZ_ZombieCharacter m_ZombieCharacter;
    protected IEntity m_LastDetectedDoor;
    protected vector m_vScanPos;
    protected IEntity m_BestDoor;
    protected float m_fBestDist;
	
	//------------------------------------------------------------------------------------------------
	override void OnInit(AIAgent owner)
	{
		IEntity controlledEntity = owner.GetControlledEntity();
		if (controlledEntity)
			m_ZombieCharacter = BZ_ZombieCharacter.Cast(controlledEntity);
	}
	
	//------------------------------------------------------------------------------------------------
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		// ALWAYS clear the variable first
		SetVariableOut(PORT_OUT_DOOR, null);

		if (!m_ZombieCharacter)
		{
			if (m_bDebugMode)
				Print("[BZ_AIDetectDoor] No zombie character - FAIL", LogLevel.WARNING);
			return ENodeResult.FAIL;
		}

		// Get zombie position
		vector zombiePos = m_ZombieCharacter.GetOrigin();

        // Simple proximity check - find any door within detection distance
        IEntity detectedDoor = FindNearbyDoor(zombiePos);

		if (detectedDoor)
		{
			// Check if door is close enough
			float distanceToDoor = vector.Distance(zombiePos, detectedDoor.GetOrigin());

			if (m_bDebugMode)
				Print(string.Format("[BZ_AIDetectDoor] Found door entity '%1' at distance: %2m", detectedDoor.GetName(), distanceToDoor), LogLevel.NORMAL);

			if (distanceToDoor <= m_fMaxDetectionDistance)
			{
				SetVariableOut(PORT_OUT_DOOR, detectedDoor);
				m_LastDetectedDoor = detectedDoor;

				if (m_bDebugMode)
					Print(string.Format("[BZ_AIDetectDoor] SUCCESS - Door detected at %1m", distanceToDoor), LogLevel.NORMAL);

				return ENodeResult.SUCCESS;
			}
			else if (m_bDebugMode)
			{
				Print(string.Format("[BZ_AIDetectDoor] FAIL - Door out of range: %1m (max: %2m)", distanceToDoor, m_fMaxDetectionDistance), LogLevel.NORMAL);
			}
		}
		else if (m_bDebugMode)
		{
			Print("[BZ_AIDetectDoor] FAIL - No door found in raycast", LogLevel.NORMAL);
		}

		// No door detected
		m_LastDetectedDoor = null;
		return ENodeResult.FAIL;
	}
	
	//------------------------------------------------------------------------------------------------
    protected IEntity FindNearbyDoor(vector zombiePos)
    {
        // 1) Cast rays in multiple directions to find doors quickly
        vector directions[6] = {
			Vector(1, 0, 0),    // Right
			Vector(-1, 0, 0),   // Left
			Vector(0, 0, 1),    // Forward
			Vector(0, 0, -1),   // Back
			Vector(1, 0, 1),    // Forward-Right
			Vector(-1, 0, 1)    // Forward-Left
		};

		IEntity closestDoor = null;
		float closestDist = m_fMaxDetectionDistance;

		for (int i = 0; i < 6; i++)
		{
			vector direction = directions[i].Normalized();

			autoptr TraceParam trace = new TraceParam();
			trace.Start = zombiePos;
			trace.End = zombiePos + (direction * m_fMaxDetectionDistance);
			trace.Flags = TraceFlags.ENTS;

			float traceDist = GetGame().GetWorld().TraceMove(trace, null);

			if (trace.TraceEnt)
			{
				IEntity hitEntity = trace.TraceEnt;
				float dist = vector.Distance(zombiePos, hitEntity.GetOrigin());

				if (IsDoor(hitEntity) && dist < closestDist)
				{
					closestDoor = hitEntity;
					closestDist = dist;
					if (m_bDebugMode)
						Print(string.Format("[BZ_AIDetectDoor] Found door: %1 at %2m", hitEntity.GetName(), dist), LogLevel.NORMAL);
				}

				// Check parent
				IEntity parent = hitEntity.GetParent();
				if (parent && IsDoor(parent))
				{
					dist = vector.Distance(zombiePos, parent.GetOrigin());
					if (dist < closestDist)
					{
						closestDoor = parent;
						closestDist = dist;
						if (m_bDebugMode)
							Print(string.Format("[BZ_AIDetectDoor] Found door via parent: %1 at %2m", parent.GetName(), dist), LogLevel.NORMAL);
					}
				}
			}
		}

        // 2) If ray method failed (very common with static hinged doors), fall back to sphere query
        if (!closestDoor)
        {
            m_vScanPos = zombiePos;
            m_BestDoor = null;
            m_fBestDist = m_fMaxDetectionDistance;

            GetGame().GetWorld().QueryEntitiesBySphere(
                zombiePos,
                m_fMaxDetectionDistance,
                DoorQuery,
                null,
                EQueryEntitiesFlags.ALL
            );

            if (m_BestDoor)
            {
                closestDoor = m_BestDoor;
                if (m_bDebugMode)
                    Print(string.Format("[BZ_AIDetectDoor] Found door via sphere query: %1", closestDoor.GetName()), LogLevel.NORMAL);
            }
        }

        if (!closestDoor && m_bDebugMode)
            Print("[BZ_AIDetectDoor] No doors found in any direction or sphere", LogLevel.NORMAL);

        return closestDoor;
    }
	

	
	//------------------------------------------------------------------------------------------------
    protected bool IsDoor(IEntity entity)
    {
		if (!entity)
			return false;

		// Check for DoorComponent
		DoorComponent doorComp = DoorComponent.Cast(entity.FindComponent(DoorComponent));
		if (doorComp)
		{
			// Only target closed or closing doors
			if (!doorComp.IsOpen() || doorComp.IsClosing())
				return true;
		}

		// Check for destructible door entities
		SCR_DestructibleEntity destructible = SCR_DestructibleEntity.Cast(entity);
		if (destructible)
		{
			// Check if entity name contains "door" or "gate"
			string entityName = entity.GetName();
			entityName.ToLower();
			if (entityName.Contains("door") || entityName.Contains("gate"))
				return true;
		}

        return false;
    }

    // Sphere query callback
    bool DoorQuery(IEntity e)
    {
        if (!e)
            return true;
        if (IsDoor(e))
        {
            float d = vector.Distance(m_vScanPos, e.GetOrigin());
            if (d < m_fBestDist)
            {
                m_fBestDist = d;
                m_BestDoor = e;
            }
        }
        return true; // keep scanning
    }
	
	//------------------------------------------------------------------------------------------------
	static override bool VisibleInPalette()
	{
		return true;
	}

	//------------------------------------------------------------------------------------------------
	static override string GetOnHoverDescription()
	{
		return "BZ_AIDetectDoor: Detects doors blocking the zombie's path to target";
	}
}


