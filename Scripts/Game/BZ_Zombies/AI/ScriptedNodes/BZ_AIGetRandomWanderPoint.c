// BrasilZ Zombies - AI Get Random Wander Point Node
// Generates a random point for zombie to wander to

class BZ_AIGetRandomWanderPoint : AITaskScripted
{
	// Output port names
	static const string PORT_OUT_POSITION = "WanderPoint";

	[Attribute("0", UIWidgets.CheckBox, "Enable debug logging")]
	protected bool m_bDebugMode;
	
	[Attribute("10", UIWidgets.EditBox, "Water proximity check radius (meters) - rejects points near water")]
	protected float m_fWaterCheckRadius;

	protected BZ_ZombieCharacter m_ZombieCharacter;

	//------------------------------------------------------------------------------------------------
	// Expose output ports to behavior tree editor
	protected static ref TStringArray s_aVarsOut = {
		PORT_OUT_POSITION
	};
	override TStringArray GetVariablesOut()
	{
		return s_aVarsOut;
	}

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
		if (!m_ZombieCharacter)
			return ENodeResult.FAIL;

		// Try to generate a valid wander point (with retries)
		vector wanderPoint;
		int maxAttempts = 5;
		bool foundValidPoint = false;
		
		for (int i = 0; i < maxAttempts; i++)
		{
			// Generate random wander point
			wanderPoint = m_ZombieCharacter.GenerateRandomWanderPoint();

			// Validate the point (including water proximity check)
			if (IsValidWanderPoint(wanderPoint))
			{
				foundValidPoint = true;
				break;
			}
		}

		// If all attempts failed, use fallback point near current position
		if (!foundValidPoint)
		{
			wanderPoint = GenerateFallbackPoint();
		}

		// Set output variable
		SetVariableOut(PORT_OUT_POSITION, wanderPoint);

		return ENodeResult.SUCCESS;
	}
	
	//------------------------------------------------------------------------------------------------
	protected bool IsValidWanderPoint(vector point)
	{
		// Basic validation - check if point is not zero
		if (point == vector.Zero)
			return false;

		// Safety check for world
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return false;

		// Check if point is on valid terrain
		float terrainY = world.GetSurfaceY(point[0], point[2]);
		
		// If terrain height is very different from point height, might be invalid
		float heightDiff = Math.AbsFloat(point[1] - terrainY);
		if (heightDiff > 5.0)
			return false;
		
		// Check if point is in water by testing if terrain surface is below sea level
		// In Arma Reforger, sea level is typically at Y=0
		// If terrain surface is below 0, it's likely underwater
		if (terrainY < 0)
		{
			return false;
		}

		// Check for water proximity - sample points around the target to ensure safe buffer zone
		if (!IsWaterProximitySafe(point, world))
		{
			return false;
		}
		
		return true;
	}
	
	//------------------------------------------------------------------------------------------------
	// Check if there's water nearby by sampling points in a radius around the target point
	protected bool IsWaterProximitySafe(vector centerPoint, BaseWorld world)
	{
		// Sample 8 points in a circle around the center point
		int numSamples = 8;
		float angleStep = Math.PI2 / numSamples;
		
		for (int i = 0; i < numSamples; i++)
		{
			float angle = angleStep * i;
			
			// Calculate sample point position
			vector sampleOffset = Vector(
				Math.Cos(angle) * m_fWaterCheckRadius,
				0,
				Math.Sin(angle) * m_fWaterCheckRadius
			);
			
			vector samplePoint = centerPoint + sampleOffset;
			
			// Check if sample point is in water
			float sampleTerrainY = world.GetSurfaceY(samplePoint[0], samplePoint[2]);

			if (sampleTerrainY < 0)
			{
				// Found water nearby - this point is too close to water
				return false;
			}
		}
		
		// All sample points are safe - no water nearby
		return true;
	}
	
	//------------------------------------------------------------------------------------------------
	protected vector GenerateFallbackPoint()
	{
		// Generate a point very close to current position as fallback
		vector currentPos = m_ZombieCharacter.GetOrigin();
		
		float angle = Math.RandomFloat(0, Math.PI2);
		float distance = Math.RandomFloat(5.0, 10.0);
		
		vector offset = Vector(
			Math.Cos(angle) * distance,
			0,
			Math.Sin(angle) * distance
		);
		
		vector fallbackPoint = currentPos + offset;
		
		// Get terrain height
		BaseWorld world = GetGame().GetWorld();
		if (world)
		{
			float terrainY = world.GetSurfaceY(fallbackPoint[0], fallbackPoint[2]);
			fallbackPoint[1] = terrainY;
		}
		else
		{
			fallbackPoint[1] = currentPos[1];
		}

		return fallbackPoint;
	}
	
	//------------------------------------------------------------------------------------------------
	protected static override bool VisibleInPalette()
	{
		return true;
	}
	
	//------------------------------------------------------------------------------------------------
	protected static override string GetOnHoverDescription()
	{
		return "Generates a random wander point within the zombie's wander radius";
	}
}

