// Zombie target detection using base game perception system + danger events
class BZ_AIDetectPlayer : AITaskScripted
{
	protected static const string PORT_OUT_TARGET = "DetectedTarget";

	protected static ref TStringArray s_aVarsOut = {
		PORT_OUT_TARGET
	};

	[Attribute("10", UIWidgets.EditBox, "Max time since target was last seen (seconds)")]
	protected float m_fTimeSinceSeenMax;

	[Attribute("5", UIWidgets.EditBox, "Max time since target was detected by sound/danger (seconds)")]
	protected float m_fTimeSinceDetectedMax;

	[Attribute("35", UIWidgets.EditBox, "Maximum detection range in meters")]
	protected float m_fMaxDetectionRange;

	[Attribute("120", UIWidgets.EditBox, "Time to give up investigating if no new danger (seconds)")]
	protected float m_fInvestigateTimeout;

	[Attribute("0", UIWidgets.CheckBox, "Enable debug logging")]
	protected bool m_bDebugMode;

		[Attribute("0.4", UIWidgets.EditBox, "Gunshot debounce window (seconds)")]
		protected float m_fGunshotDebounceSeconds;

		[Attribute("2.0", UIWidgets.EditBox, "Gunshot position change threshold (meters)")]
		protected float m_fGunshotPosThreshold;

		[Attribute("10", UIWidgets.EditBox, "Danger target timeout - give up if unreachable (seconds)")]
		protected float m_fDangerTargetTimeout;

		[Attribute("2.5", UIWidgets.EditBox, "Assumed chase speed when responding to sounds (m/s)")]
		protected float m_fInvestigateTravelSpeed;

		[Attribute("4.0", UIWidgets.EditBox, "Extra commit time once zombie reaches gunshot (seconds)")]
		protected float m_fInvestigateArrivalHold;

		[Attribute("6.0", UIWidgets.EditBox, "Minimum investigation commit time (seconds)")]
		protected float m_fInvestigateCommitMin;

		[Attribute("180.0", UIWidgets.EditBox, "Maximum investigation commit time (seconds)")]
		protected float m_fInvestigateCommitMax;

		[Attribute("25", UIWidgets.EditBox, "Detection range for suppressed weapons (meters)")]
		protected float m_fSuppressedDetectionRange;

		[Attribute("18", UIWidgets.EditBox, "Alert radius for suppressed weapons (meters)")]
		protected float m_fSuppressedAlertRadius;


	protected PerceptionComponent m_PerceptionComp;
	protected BZ_ZombieCharacter m_ZombieCharacter;
		protected float m_fLastProcessedShotTime;
		protected vector m_vLastProcessedShotPos;

	protected BZ_ZombieAlertSystem m_AlertSystem;
	protected IEntity m_LastDetectedTarget;

		protected bool m_bLastGunshotDetected;
		protected bool m_bLastGunshotInRange;
		protected bool m_bLastGunshotSuppressed;
		protected vector m_vLastGunshotPos;

		protected float m_fDangerTargetAcquiredTime;
		protected IEntity m_DangerTarget;
	
	// Performance optimization: Cache perception queries per frame
	protected BaseTarget m_CachedClosestDetected;
	protected BaseTarget m_CachedClosestEnemy;
	protected float m_fLastPerceptionCacheTime;


	override TStringArray GetVariablesOut()
	{
		return s_aVarsOut;
	}

	override void OnInit(AIAgent owner)
	{
		GenericEntity entity = GenericEntity.Cast(owner.GetControlledEntity());
		if (!entity)
			return;

		m_ZombieCharacter = BZ_ZombieCharacter.Cast(entity);
		m_PerceptionComp = PerceptionComponent.Cast(entity.FindComponent(PerceptionComponent));
		m_AlertSystem = BZ_ZombieAlertSystem.Cast(entity.FindComponent(BZ_ZombieAlertSystem));
	}

	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		// Cache world time at start to avoid multiple queries
		float currentTime = GetGame().GetWorld().GetWorldTime() * 0.001;
		
		// PRIORITY 1: Check if zombie was damaged - IMMEDIATELY switch to attacker (ALWAYS OVERRIDES COMMITMENT)
		IEntity lastAttacker = m_ZombieCharacter.GetLastAttacker();
		if (lastAttacker)
		{
			float timeSinceDamage = currentTime - m_ZombieCharacter.GetLastDamageTime();
			// Only consider recent damage (within 15 seconds)
			if (timeSinceDamage < 15.0)
			{
				// Validate attacker is still alive and in range
				if (m_ZombieCharacter.IsTargetValid(lastAttacker))
				{
					float distance = vector.Distance(m_ZombieCharacter.GetOrigin(), lastAttacker.GetOrigin());
					if (distance <= GetEffectiveDetectionRange(m_fMaxDetectionRange, lastAttacker))
					{
						IEntity resolvedAttacker = ResolveTargetEntityPreferCharacter(lastAttacker);
						if (resolvedAttacker)
						{
							if (m_bDebugMode)
								Print(string.Format("[BZ_AIDetectPlayer] Switching to attacker: %1 at %.1fm", 
									resolvedAttacker.GetName(), distance), LogLevel.VERBOSE);
							
							m_ZombieCharacter.SetCurrentTarget(resolvedAttacker);
							m_ZombieCharacter.SetThreatState(EZombieThreatState.CHASING, resolvedAttacker.GetOrigin(), resolvedAttacker);
							SetVariableOut(PORT_OUT_TARGET, resolvedAttacker);
							
							// Set commitment time (15 seconds for damage retaliation)
							m_ZombieCharacter.SetTargetCommitmentTime(15.0);
							
							// Clear attacker memory after switching (to avoid redundant checks)
							m_ZombieCharacter.ClearLastAttacker();
							
							// DON'T broadcast damage retaliation - this is personal!
							// Play scream only
							BZ_ZombieScreamComponent screamComp = BZ_ZombieScreamComponent.Cast(m_ZombieCharacter.FindComponent(BZ_ZombieScreamComponent));
							if (screamComp)
								screamComp.PlayScream();
							
							return ENodeResult.SUCCESS;
						}
					}
				}
				else
				{
					// Attacker dead or out of range - clear it
					m_ZombieCharacter.ClearLastAttacker();
				}
			}
			else
			{
				// Damage too old - clear it
				m_ZombieCharacter.ClearLastAttacker();
			}
		}
		
		// Get current target from zombie character (persistent state)
		IEntity currentTarget = m_ZombieCharacter.GetCurrentTarget();

		if (!m_PerceptionComp || !m_ZombieCharacter)
		{
			return ENodeResult.FAIL;
		}

		// CRITICAL FIX: If zombie already has a target (from ReceiveAlert/horde system),
		// output it to BT variable immediately so the behavior tree switches to Chase!
		// Distance validation happens later in the chase logic (lines 280 and 390)
		if (currentTarget && !m_ZombieCharacter.IsTargetValid(currentTarget))
		{
			// Target died/destroyed - clear it
			m_ZombieCharacter.SetCurrentTarget(null);
			ClearVariable(PORT_OUT_TARGET);
			currentTarget = null;
		}
		else if (currentTarget)
		{
			// Check detection distance first - ultimate safety net
			float distanceToTarget = vector.Distance(m_ZombieCharacter.GetOrigin(), currentTarget.GetOrigin());
			
			if (distanceToTarget > GetEffectiveDetectionRange(m_fMaxDetectionRange, currentTarget))
			{
				// TOO FAR - give up immediately
				if (m_bDebugMode)
					Print(string.Format("[BZ_AIDetectPlayer] Target too far at start (%.1fm), clearing target", distanceToTarget), LogLevel.NORMAL);
				
				m_ZombieCharacter.ForceReturnToWander(1.5);
				ClearVariable(PORT_OUT_TARGET);
				return ENodeResult.FAIL;
			}
			
			// Within detection range - output to BT so zombie continues chase
			SetVariableOut(PORT_OUT_TARGET, currentTarget);
			
			if (m_bDebugMode)
				Print(string.Format("[BZ_AIDetectPlayer] Zombie has target from horde alert (%.1fm), activating chase", distanceToTarget), LogLevel.VERBOSE);
			
			return ENodeResult.SUCCESS;
		}

		// Auto-resolve investigation if it’s stale and there are no new danger events
		if (m_ZombieCharacter.IsThreatState(EZombieThreatState.INVESTIGATING))
		{
			BaseWorld _world = GetGame().GetWorld();
			if (_world)
			{
				float now = _world.GetWorldTime() * 0.001;
				float threatStateTime = m_ZombieCharacter.GetThreatStateTime();
				float timeSinceThreat = now - threatStateTime;

				if (!m_ZombieCharacter.IsInvestigationCommitActive())
				{
					if (timeSinceThreat > m_fInvestigateTimeout)
					{
						m_ZombieCharacter.ClearThreatState();
					}
					else
					{
						float remaining = m_fInvestigateTimeout - timeSinceThreat;
						if (remaining > 0)
							m_ZombieCharacter.CommitInvestigationFor(remaining);
					}
				}
			}
		}

		// Always process danger events every tick (base pattern)
		BaseTarget dangerTarget = CheckDangerEvents(owner, currentTime);

		// Check if we have an alerted target from another zombie
		BaseTarget outTarget = CheckAlertedTarget();
		if (!outTarget && dangerTarget)
		{
			outTarget = dangerTarget;
			// For danger events, skip LOS requirement and go straight to validation
			// This allows zombies to respond to gunshots even without seeing the shooter
			if (outTarget)
			{
				float distance = outTarget.GetDistance();
				if (distance <= m_fMaxDetectionRange && !outTarget.IsDisarmed())
				{
					IEntity targetEntity = outTarget.GetTargetEntity();
                    if (targetEntity)
                    {
                        // If target is a vehicle, prefer its driver (PILOT); if empty, skip
                        IEntity resolvedTarget = ResolveTargetEntityPreferCharacter(targetEntity);
                        if (!resolvedTarget)
                        {
                            // Vehicle has no driver; do not lock onto empty vehicles
                            if (m_bDebugMode)
                                Print("[BZ_AIDetectPlayer] Skipping empty vehicle target from danger", LogLevel.NORMAL);
                            // Fall through to normal detection below
                        }
                        // Check if this is a NEW danger target or same one
                        if (targetEntity != m_DangerTarget)
                        {
							m_DangerTarget = targetEntity;
							m_fDangerTargetAcquiredTime = GetGame().GetWorld().GetWorldTime() * 0.001;
							if (m_bDebugMode)
								Print(string.Format("[BZ_AIDetectPlayer] NEW DANGER TARGET: %1", targetEntity.GetName()), LogLevel.NORMAL);
						}

                            if (resolvedTarget)
                            {
                                m_ZombieCharacter.SetCurrentTarget(resolvedTarget);
                                SetVariableOut(PORT_OUT_TARGET, resolvedTarget);

                                if (m_bDebugMode)
                                    Print(string.Format("[BZ_AIDetectPlayer] DANGER EVENT TARGET: %1 at %2m", resolvedTarget.GetName(), distance), LogLevel.NORMAL);

                                return ENodeResult.SUCCESS;
                            }
                        }
                    }
                }
            }

		// If we have a current target, validate it and check for closer targets
		if (currentTarget)
		{
			BaseTarget currentBaseTarget = m_PerceptionComp.FindTargetPerceptionObject(currentTarget);

        if (currentBaseTarget)
        {
            // If current target is a vehicle and now empty, drop it so we can reacquire the driver
            IEntity curEnt = currentTarget;
            if (curEnt && !ChimeraCharacter.Cast(curEnt))
            {
                SCR_BaseCompartmentManagerComponent compCur = SCR_BaseCompartmentManagerComponent.Cast(curEnt.FindComponent(SCR_BaseCompartmentManagerComponent));
                if (compCur)
                {
                    array<IEntity> occCur = {};
                    compCur.GetOccupantsOfType(occCur, ECompartmentType.PILOT);
                    if (occCur.Count() == 0)
                    {
                        m_ZombieCharacter.SetCurrentTarget(null);
                        ClearVariable(PORT_OUT_TARGET);
                        if (m_bDebugMode)
                            Print("[BZ_AIDetectPlayer] Dropping empty vehicle as current target", LogLevel.NORMAL);
                        // Continue to normal detection below
                        return ENodeResult.FAIL;
                    }
                }
            }
				float currentDistance = currentBaseTarget.GetDistance();
				float timeSinceSeen = currentBaseTarget.GetTimeSinceSeen();
				float timeSinceDetected = currentBaseTarget.GetTimeSinceDetected();

				// Check if current target is still valid
				bool currentTargetValid = currentDistance <= GetEffectiveDetectionRange(m_fMaxDetectionRange, currentTarget) &&
					(timeSinceSeen <= m_fTimeSinceSeenMax || timeSinceDetected <= m_fTimeSinceDetectedMax) &&
					!currentBaseTarget.IsDisarmed() &&
					HasLineOfSight(m_ZombieCharacter.GetOrigin(), currentTarget.GetOrigin());

				// Always check detection distance first - this supersedes commitment
			if (currentDistance > GetEffectiveDetectionRange(m_fMaxDetectionRange, currentTarget))
			{
				// Too far - give up chase regardless of commitment
				if (m_bDebugMode)
					Print(string.Format("[BZ_AIDetectPlayer] Target too far (%.1fm), giving up", currentDistance), LogLevel.NORMAL);
				
				m_ZombieCharacter.SetCurrentTarget(null);
				ClearVariable(PORT_OUT_TARGET);
				return ENodeResult.FAIL;
			}

			if (currentTargetValid)
			{
				// Check if we're in commitment period - if so, STICK with current target
				if (m_ZombieCharacter.IsInTargetCommitmentPeriod())
				{
					if (m_bDebugMode)
						Print(string.Format("[BZ_AIDetectPlayer] Committed to target: %1", currentTarget.GetName()), LogLevel.VERBOSE);
					
					SetVariableOut(PORT_OUT_TARGET, currentTarget);
					return ENodeResult.SUCCESS;
				}
					
					// Current target is valid - but check if there's a CLOSER target we should switch to
					// PERFORMANCE: Use cached perception results if same frame
					if (m_fLastPerceptionCacheTime != currentTime)
					{
						m_CachedClosestDetected = m_PerceptionComp.GetClosestTarget(ETargetCategory.DETECTED, m_fTimeSinceSeenMax, m_fTimeSinceDetectedMax);
						m_CachedClosestEnemy = m_PerceptionComp.GetClosestTarget(ETargetCategory.ENEMY, m_fTimeSinceSeenMax, m_fTimeSinceDetectedMax);
						m_fLastPerceptionCacheTime = currentTime;
					}
					
					BaseTarget closestTarget = null;
					closestTarget = PickClosestValidTarget(m_CachedClosestDetected, m_CachedClosestEnemy);
					
					// If we found a different target that's closer, switch to it (VERY AGGRESSIVE)
					if (closestTarget && closestTarget.GetTargetEntity() != currentTarget)
					{
						float closestDistance = closestTarget.GetDistance();
						
						// Switch if new target is closer (break away immediately)
						if (closestDistance < currentDistance)
						{
							if (m_bDebugMode)
								Print(string.Format("[BZ_AIDetectPlayer] Switching to closer target: %.1fm vs %.1fm", 
									currentDistance, closestDistance), LogLevel.VERBOSE);
							
							// LONG commitment when breaking away from perception target (30 seconds)
							// This creates natural target distribution - zombies stick to their chosen targets
							m_ZombieCharacter.SetTargetCommitmentTime(30.0);
							
							// Clear current and let the normal detection logic below handle the new target
							m_ZombieCharacter.SetCurrentTarget(null);
							ClearVariable(PORT_OUT_TARGET);
							// Don't return - fall through to detect the new target below
						}
						else
						{
							// Keep current target - not closer
							SetVariableOut(PORT_OUT_TARGET, currentTarget);
							return ENodeResult.SUCCESS;
						}
					}
					else
					{
						// No other targets or same target - keep current
						SetVariableOut(PORT_OUT_TARGET, currentTarget);
						return ENodeResult.SUCCESS;
					}
				}
				else
				{
					// Current target lost - mark as lost but DON'T clear it permanently
					// This allows re-engagement if the target comes back into range
					m_ZombieCharacter.SetCurrentTarget(null);
					// SetCurrentTarget will automatically set target lost flag for investigation

					// Clear BT variable and return FAIL to allow transition to Wander
					ClearVariable(PORT_OUT_TARGET);
					return ENodeResult.FAIL;
				}
			}
			else
			{
				// HORDE ALERT PATH: Target not in perception - from ReceiveAlert()
				// This happens when zombie is alerted to distant target
				if (m_bDebugMode)
					Print(string.Format("[BZ_AIDetectPlayer] Horde target (not in perception)"), LogLevel.WARNING);
				
				// Check if target is still alive
				if (!m_ZombieCharacter.IsTargetValid(currentTarget))
				{
					if (m_bDebugMode)
						Print("[BZ_AIDetectPlayer] Horde target dead, clearing", LogLevel.WARNING);
					m_ZombieCharacter.SetCurrentTarget(null);
					ClearVariable(PORT_OUT_TARGET);
					return ENodeResult.FAIL;
				}
				
				if (!currentTarget.IsDeleted())
				{
					float currentDistance = vector.Distance(m_ZombieCharacter.GetOrigin(), currentTarget.GetOrigin());
					
					// Always check detection distance first - supersedes everything
					if (currentDistance > GetEffectiveDetectionRange(m_fMaxDetectionRange, currentTarget))
					{
						// Too far - give up chase regardless of commitment or horde
						if (m_bDebugMode)
							Print(string.Format("[BZ_AIDetectPlayer] Horde target too far (%.1fm), giving up", currentDistance), LogLevel.NORMAL);
						
						m_ZombieCharacter.SetCurrentTarget(null);
						ClearVariable(PORT_OUT_TARGET);
						return ENodeResult.FAIL;
					}
					
					// Check if we're in commitment period - if so, STICK with horde target
					if (m_ZombieCharacter.IsInTargetCommitmentPeriod())
					{
						if (m_bDebugMode)
							Print(string.Format("[BZ_AIDetectPlayer] Committed to horde target: %.1fm", currentDistance), LogLevel.VERBOSE);
						
						SetVariableOut(PORT_OUT_TARGET, currentTarget);
						return ENodeResult.SUCCESS;
					}
					
					// CRITICAL: Check for closer targets in perception BEFORE committing to horde target
					// This allows zombies to break away from horde when closer target appears
					if (m_fLastPerceptionCacheTime != currentTime)
					{
						m_CachedClosestDetected = m_PerceptionComp.GetClosestTarget(ETargetCategory.DETECTED, m_fTimeSinceSeenMax, m_fTimeSinceDetectedMax);
						m_CachedClosestEnemy = m_PerceptionComp.GetClosestTarget(ETargetCategory.ENEMY, m_fTimeSinceSeenMax, m_fTimeSinceDetectedMax);
						m_fLastPerceptionCacheTime = currentTime;
					}
					
					BaseTarget closestTarget = null;
					closestTarget = PickClosestValidTarget(m_CachedClosestDetected, m_CachedClosestEnemy);
					
					// If we found a closer target in perception, BREAK AWAY FROM HORDE IMMEDIATELY
					if (closestTarget && closestTarget.GetDistance() < currentDistance)
					{
						float closestDistance = closestTarget.GetDistance();
						
						if (m_bDebugMode)
							Print(string.Format("[BZ_AIDetectPlayer] Breaking away from horde for closer target: %.1fm vs %.1fm", 
								closestDistance, currentDistance), LogLevel.VERBOSE);
						
						// Get the closer target entity and validate
						IEntity closerTargetEntity = closestTarget.GetTargetEntity();
						if (!closerTargetEntity || closestTarget.IsDisarmed())
						{
							// Invalid closer target, continue with horde
							if (m_bDebugMode)
								Print("[BZ_AIDetectPlayer] Closer target invalid, staying with horde", LogLevel.WARNING);
							
							SetVariableOut(PORT_OUT_TARGET, currentTarget);
							return ENodeResult.SUCCESS;
						}
						
						// Resolve to character if vehicle
						IEntity resolvedTarget = ResolveTargetEntityPreferCharacter(closerTargetEntity);
						if (!resolvedTarget)
						{
							// Empty vehicle, stay with horde
							SetVariableOut(PORT_OUT_TARGET, currentTarget);
							return ENodeResult.SUCCESS;
						}
						
						// BREAK AWAY! Switch to closer target immediately
						m_ZombieCharacter.SetCurrentTarget(resolvedTarget);
						m_ZombieCharacter.SetThreatState(EZombieThreatState.CHASING, resolvedTarget.GetOrigin(), resolvedTarget);
						SetVariableOut(PORT_OUT_TARGET, resolvedTarget);
						
						// LONG commitment when breaking away from horde (45 seconds)
						// Once they break away, they're fully committed to the closer target
						m_ZombieCharacter.SetTargetCommitmentTime(45.0);
						
						// DON'T broadcast when breaking away - prevents ping-pong effect
						// The zombie should focus on their own target without alerting others
						// This prevents feedback loops where zombies keep switching back and forth
						
							
						return ENodeResult.SUCCESS;
					}
					else
					{
						// No closer targets - continue with horde target (already passed range check)
						if (m_bDebugMode)
							Print(string.Format("[BZ_AIDetectPlayer] Continuing horde chase: %.1fm", currentDistance), LogLevel.VERBOSE);
						
						SetVariableOut(PORT_OUT_TARGET, currentTarget);
						return ENodeResult.SUCCESS;
					}
				}
				else
				{
					// Target deleted
					m_ZombieCharacter.SetCurrentTarget(null);
					ClearVariable(PORT_OUT_TARGET);
					return ENodeResult.FAIL;
				}
			}
		}

		// No valid current target - look for new target
		// Check perception system for targets
		// PERFORMANCE: Reuse cached results if available this frame
		if (m_fLastPerceptionCacheTime != currentTime)
		{
			m_CachedClosestDetected = m_PerceptionComp.GetClosestTarget(ETargetCategory.DETECTED, m_fTimeSinceSeenMax, m_fTimeSinceDetectedMax);
			m_CachedClosestEnemy = m_PerceptionComp.GetClosestTarget(ETargetCategory.ENEMY, m_fTimeSinceSeenMax, m_fTimeSinceDetectedMax);
			m_fLastPerceptionCacheTime = currentTime;
		}
		
		BaseTarget detectedTarget = m_CachedClosestDetected;
		BaseTarget enemyTarget = m_CachedClosestEnemy;



		// Pick the closest of all available targets
		outTarget = PickClosestValidTarget(detectedTarget, enemyTarget);

		// No new target found
		if (!outTarget)
		{
			ClearVariable(PORT_OUT_TARGET);
			return ENodeResult.FAIL;
		}

		// Validate new target
		float distance = outTarget.GetDistance();

		// Check if disarmed
		if (outTarget.IsDisarmed())
		{
			// Don't investigate dead targets
			ClearVariable(PORT_OUT_TARGET);
			return ENodeResult.FAIL;
		}

		// Get the actual entity
		IEntity targetEntity = outTarget.GetTargetEntity();
        if (!targetEntity)
        {
            ClearVariable(PORT_OUT_TARGET);
            return ENodeResult.FAIL;
        }

		if (!m_ZombieCharacter.IsTargetValid(targetEntity))
		{
			ClearVariable(PORT_OUT_TARGET);
			return ENodeResult.FAIL;
		}

		// Allow targets up to configured max detection range
		// If beyond range, ignore but don't permanently block re-engagement
		if (distance > GetEffectiveDetectionRange(m_fMaxDetectionRange, targetEntity))
		{
			m_ZombieCharacter.ForceReturnToWander(1.5);
			ClearVariable(PORT_OUT_TARGET);
			return ENodeResult.FAIL;
		}

        // If target is a vehicle, prefer its driver (PILOT), else chase vehicle entity
        IEntity resolvedTarget = ResolveTargetEntityPreferCharacter(targetEntity);

		IEntity chaseEntity = resolvedTarget;
		if (!chaseEntity)
			chaseEntity = targetEntity;

		if (!m_ZombieCharacter.IsTargetValid(chaseEntity))
		{
			ClearVariable(PORT_OUT_TARGET);
			return ENodeResult.FAIL;
		}

		float actualDistance = vector.Distance(m_ZombieCharacter.GetOrigin(), chaseEntity.GetOrigin());
		if (actualDistance > GetEffectiveDetectionRange(m_fMaxDetectionRange, chaseEntity))
		{
			ClearVariable(PORT_OUT_TARGET);
			return ENodeResult.FAIL;
		}

		// Valid new target found!
		m_ZombieCharacter.SetCurrentTarget(chaseEntity);
		// Set threat state to CHASING
        m_ZombieCharacter.SetThreatState(EZombieThreatState.CHASING, chaseEntity.GetOrigin(), chaseEntity);
		SetVariableOut(PORT_OUT_TARGET, chaseEntity);

		// Commit to this new target for a long time (30 seconds)
		// This ensures natural target distribution when multiple targets appear
		m_ZombieCharacter.SetTargetCommitmentTime(30.0);

		// HORDE ALERT: Scream and alert nearby idle zombies when detecting new target
		// The alert system only alerts zombies WITHOUT targets (no target stealing)
		if (chaseEntity != m_LastDetectedTarget)
		{
			m_LastDetectedTarget = chaseEntity;
			
			// Alert nearby wandering zombies to join the horde
				if (m_AlertSystem)
					m_AlertSystem.AlertNearbyZombies(chaseEntity, GetEffectiveAlertRadius(25, chaseEntity));
		}

		return ENodeResult.SUCCESS;
	}

	//------------------------------------------------------------------------------------------------
	// Check for danger events (weapon fire, explosions, etc)
	// currentTime parameter to avoid redundant GetWorldTime() calls
	protected BaseTarget CheckDangerEvents(AIAgent owner, float currentTime)
	{
		if (!owner)
			return null;

		// Get all danger events from the AI agent (cache count like base threat system)
		int max = owner.GetDangerEventsCount();
		if (m_bDebugMode)
			Print(string.Format("[BZ_AIDetectPlayer] CheckDangerEvents called - %1 events", max), LogLevel.WARNING);

		BaseTarget foundTarget = null;
		m_bLastGunshotDetected = false;
		m_bLastGunshotInRange = false;
		m_bLastGunshotSuppressed = false;
		m_vLastGunshotPos = vector.Zero;

		int i = 0;
		for (i = 0; i < max; i++)
		{
			int eventAggregationCount;
			AIDangerEvent dangerEvent = owner.GetDangerEvent(i, eventAggregationCount);

			if (!dangerEvent)
				continue;

			EAIDangerEventType dangerType = dangerEvent.GetDangerType();
			if (m_bDebugMode)
				Print(string.Format("[BZ_AIDetectPlayer] Event %1 type: %2", i, dangerType), LogLevel.WARNING);
			IEntity sourceEntity = null;

			// Handle zombie detection events (horde behavior - alert nearby zombies!)
			if (dangerType == EAIDangerEventType.BZ_ZombieDetection)
			{
				// Get the target from the zombie detection event
				sourceEntity = dangerEvent.GetObject();
				
				if (sourceEntity && m_bDebugMode)
					Print(string.Format("[BZ_AIDetectPlayer] BZ_ZombieDetection event - target: %1", sourceEntity.GetName()), LogLevel.WARNING);
				
				// Check distance and accept this as a target for horde behavior
				if (sourceEntity)
				{
					float distance = vector.Distance(m_ZombieCharacter.GetOrigin(), sourceEntity.GetOrigin());
					if (distance <= GetEffectiveDetectionRange(m_fMaxDetectionRange, sourceEntity))
					{
						// Try to find this target in our perception system
						BaseTarget perceivedTarget = m_PerceptionComp.FindTargetPerceptionObject(sourceEntity);
						
						if (perceivedTarget)
						{
							// Target is in perception - validate and use normal processing path
							if (!perceivedTarget.IsDisarmed())
							{
								foundTarget = perceivedTarget;
								if (m_bDebugMode)
									Print("[BZ_AIDetectPlayer] Target found in perception, will process normally", LogLevel.WARNING);
							}
							else if (m_bDebugMode)
							{
								Print("[BZ_AIDetectPlayer] Target in perception but disarmed (dead/unconscious), ignoring", LogLevel.NORMAL);
							}
						}
						else
						{
							// Target NOT in perception (no LOS) - this is the horde behavior!
							// Validate target is still alive before forcing chase
							if (m_bDebugMode)
								Print("[BZ_AIDetectPlayer] Target NOT in perception - checking if valid for horde chase", LogLevel.WARNING);
							
							// Check if target is valid (alive)
							if (!m_ZombieCharacter.IsTargetValid(sourceEntity))
							{
								if (m_bDebugMode)
									Print("[BZ_AIDetectPlayer] Horde target is invalid (dead/destroyed), skipping", LogLevel.NORMAL);
								continue;
							}
							
							// Resolve to character entity
							IEntity resolvedTarget = ResolveTargetEntityPreferCharacter(sourceEntity);
							if (resolvedTarget)
							{
								// Double-check resolved target is valid too
								if (!m_ZombieCharacter.IsTargetValid(resolvedTarget))
								{
									if (m_bDebugMode)
										Print("[BZ_AIDetectPlayer] Resolved horde target is invalid, skipping", LogLevel.NORMAL);
									continue;
								}
								
								m_ZombieCharacter.SetCurrentTarget(resolvedTarget);
								m_ZombieCharacter.SetThreatState(EZombieThreatState.CHASING, resolvedTarget.GetOrigin(), resolvedTarget);
								
								float actualDist = vector.Distance(m_ZombieCharacter.GetOrigin(), resolvedTarget.GetOrigin());
								if (actualDist > GetEffectiveDetectionRange(m_fMaxDetectionRange, resolvedTarget))
								{
									if (m_bDebugMode)
										Print("[BZ_AIDetectPlayer] Horde alert target out of range, skipping", LogLevel.NORMAL);
									continue;
								}

								// HORDE ALERT: Alert nearby idle zombies
								if (m_AlertSystem)
									m_AlertSystem.AlertNearbyZombies(resolvedTarget, GetEffectiveAlertRadius(25, resolvedTarget));
							}
							else if (m_bDebugMode)
							{
								Print("[BZ_AIDetectPlayer] Could not resolve target to character", LogLevel.ERROR);
							}
						}
					}
					else if (m_bDebugMode)
					{
						Print(string.Format("[BZ_AIDetectPlayer] BZ_ZombieDetection target too far: %.1fm", distance), LogLevel.VERBOSE);
					}
				}
				continue;
			}
			// Handle weapon fire events
			else if (dangerType == EAIDangerEventType.Danger_WeaponFire)
			{
				AIDangerEventWeaponFire weaponFireEvent = AIDangerEventWeaponFire.Cast(dangerEvent);
				if (!weaponFireEvent)
					continue;

				sourceEntity = weaponFireEvent.GetInstigatorEntity();
				vector shotPos = weaponFireEvent.GetPosition();
				bool isSuppressed = weaponFireEvent.IsSuppressed();

				// Check if this is a new shot (not a duplicate from same burst)
				bool isNewShot = true;
				if (m_fLastProcessedShotTime > 0)
				{
					float timeSinceLastShot = currentTime - m_fLastProcessedShotTime;
					float distanceFromLastShot = vector.Distance(m_vLastProcessedShotPos, shotPos);

					// Only debounce if shot is within debounce window AND at same location
					if (timeSinceLastShot < m_fGunshotDebounceSeconds && distanceFromLastShot < m_fGunshotPosThreshold)
					{
						isNewShot = false;
					}
				}

				if (isNewShot)
				{
					m_bLastGunshotDetected = true;
					m_bLastGunshotSuppressed = isSuppressed;
					m_vLastGunshotPos = shotPos;

					if (m_bDebugMode)
					{
						float debugRange = m_fMaxDetectionRange;
						if (isSuppressed)
							debugRange = m_fSuppressedDetectionRange;
						Print(string.Format("[BZ_AIDetectPlayer] Gunshot detected - suppressed: %1, range: %2m", isSuppressed, debugRange), LogLevel.NORMAL);
					}
				}
			}
			// Handle explosion events
			else if (dangerType == EAIDangerEventType.Danger_Explosion)
			{
				sourceEntity = dangerEvent.GetObject();
			}

			// If we found a source entity, check distance
			if (sourceEntity)
			{
				float gunshotBaseRange = m_fMaxDetectionRange;
				if (m_bLastGunshotSuppressed)
					gunshotBaseRange = m_fSuppressedDetectionRange;

				float distance = vector.Distance(m_ZombieCharacter.GetOrigin(), sourceEntity.GetOrigin());
				if (distance <= GetEffectiveDetectionRange(gunshotBaseRange, sourceEntity))
				{
					m_bLastGunshotInRange = true;
					// For danger events, we DON'T return the shooter as a target
					// Instead, we investigate the gunshot position
					// This prevents zombies from getting stuck chasing hidden shooters
					// The investigation will be handled below via AlertInvestigateAt()
				}
			}
		}

		// If we detected a gunshot within range (and it's a new shot per debounce)
		if (m_bLastGunshotDetected && m_bLastGunshotInRange)
		{
			if (foundTarget)
			{
				IEntity targetEntity = foundTarget.GetTargetEntity();
                if (targetEntity)
                {
                    IEntity resolvedTarget = ResolveTargetEntityPreferCharacter(targetEntity);
                    if (resolvedTarget)
                    {
                        float gunshotActualDist = vector.Distance(m_ZombieCharacter.GetOrigin(), resolvedTarget.GetOrigin());
                        float gunshotChaseRange = m_fMaxDetectionRange;
                        if (m_bLastGunshotSuppressed)
                            gunshotChaseRange = m_fSuppressedDetectionRange;

                        if (gunshotActualDist <= GetEffectiveDetectionRange(gunshotChaseRange, resolvedTarget))
                        {
                            // Set as current target so zombie will chase
                            m_ZombieCharacter.SetCurrentTarget(resolvedTarget);

                            // HORDE ALERT: Alert nearby idle zombies about gunshot target
                            float alertBase = 25;
                            if (m_bLastGunshotSuppressed)
                                alertBase = m_fSuppressedAlertRadius;

                            if (m_AlertSystem)
                                m_AlertSystem.AlertNearbyZombies(resolvedTarget, GetEffectiveAlertRadius(alertBase, resolvedTarget));
                            
                            // Set threat state to CHASING
                            m_ZombieCharacter.SetThreatState(EZombieThreatState.CHASING, resolvedTarget.GetOrigin(), resolvedTarget);

                            // Return the target so it gets set in the BT variable
                            return foundTarget;
                        }
                        else if (m_bDebugMode)
                        {
                            Print("[BZ_AIDetectPlayer] Gunshot target beyond limit, investigating instead", LogLevel.NORMAL);
                        }
                    }
                    else if (m_bDebugMode)
                    {
                        Print("[BZ_AIDetectPlayer] Gunshot target is empty vehicle; investigating instead", LogLevel.NORMAL);
                    }
                }
			}

			// If no perceivable target, investigate the sound position
			// Only set threat state if not already investigating (to avoid resetting the timer)
			if (m_ZombieCharacter.IsThreatState(EZombieThreatState.INVESTIGATING))
			{
				m_ZombieCharacter.UpdateThreatPosition(m_vLastGunshotPos);
			}
			else
			{
				m_ZombieCharacter.SetThreatState(EZombieThreatState.INVESTIGATING, m_vLastGunshotPos, null);
			}

			ArmInvestigationCommit(m_vLastGunshotPos);

			// Mark this shot as processed for debounce
			m_fLastProcessedShotTime = currentTime;
			m_vLastProcessedShotPos = m_vLastGunshotPos;
		}

		// Clear processed danger events EXACTLY like base game (SCR_AIThreatSystem line 237)
		// Clear using the loop counter to clear exactly the events we processed
		if (i > 0)
		{
			owner.ClearDangerEvents(i);
		}

		return foundTarget;
	}

	//------------------------------------------------------------------------------------------------
	protected float GetTargetDetectionReduction(IEntity targetEntity)
	{
		return 0;
	}

	//------------------------------------------------------------------------------------------------
	protected float GetEffectiveDetectionRange(float baseRange, IEntity targetEntity)
	{
		float reduction = GetTargetDetectionReduction(targetEntity);
		float effectiveRange = baseRange * (1.0 - reduction);
		if (effectiveRange < 5.0)
			effectiveRange = 5.0;

		return effectiveRange;
	}

	//------------------------------------------------------------------------------------------------
	protected float GetEffectiveAlertRadius(float baseRadius, IEntity targetEntity)
	{
		float reduction = GetTargetDetectionReduction(targetEntity);
		float effectiveRadius = baseRadius * (1.0 - reduction);
		if (effectiveRadius < 10.0)
			effectiveRadius = 10.0;

		return effectiveRadius;
	}

	//------------------------------------------------------------------------------------------------
	//! Check line of sight between two positions (used for validating active targets)
	protected bool HasLineOfSight(vector fromPos, vector toPos)
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return false;

		// Give each point a small eye-height offset so terrain undulations don't cause false negatives
		vector eyePos = fromPos + Vector(0, 1.6, 0);
		vector targetPos = toPos + Vector(0, 1.6, 0);

		autoptr TraceParam traceParams = new TraceParam();
		traceParams.Start = eyePos;
		traceParams.End = targetPos;
		traceParams.Flags = TraceFlags.WORLD;

		float traceCoef = world.TraceMove(traceParams, null);
		return traceCoef >= 1.0;
	}

	//------------------------------------------------------------------------------------------------
	protected void ArmInvestigationCommit(vector investigatePosition)
	{
		if (!m_ZombieCharacter)
			return;

		float distance = vector.Distance(m_ZombieCharacter.GetOrigin(), investigatePosition);
		float travelTime = 0;
		if (m_fInvestigateTravelSpeed > 0.01)
			travelTime = distance / m_fInvestigateTravelSpeed;

		float commitSeconds = travelTime + m_fInvestigateArrivalHold;
		float minCommit = Math.Max(m_fInvestigateCommitMin, 0);
		commitSeconds = Math.Max(commitSeconds, minCommit);

		if (m_fInvestigateCommitMax > 0)
		{
			float maxCommit = Math.Max(m_fInvestigateCommitMax, minCommit);
			commitSeconds = Math.Min(commitSeconds, maxCommit);
		}

		m_ZombieCharacter.CommitInvestigationFor(commitSeconds);
	}

	//------------------------------------------------------------------------------------------------
	//! Check if zombie character has been alerted and should investigate
	protected BaseTarget CheckAlertedTarget()
	{
		// Check if zombie was alerted (has target lost flag set by alert)
		if (!m_ZombieCharacter.IsTargetLost())
			return null;

		// Zombie was alerted - this will trigger investigation behavior
		// Return null here so detection fails and investigation takes over
		return null;
	}
	
	//------------------------------------------------------------------------------------------------
    //! Prefer driver character when target is a vehicle; if empty, return null to skip vehicle
    protected IEntity ResolveTargetEntityPreferCharacter(IEntity targetEntity)
    {
        if (ChimeraCharacter.Cast(targetEntity))
            return targetEntity;
        
        SCR_BaseCompartmentManagerComponent comp = SCR_BaseCompartmentManagerComponent.Cast(targetEntity.FindComponent(SCR_BaseCompartmentManagerComponent));
        if (comp)
        {
            array<IEntity> occ = {};
            comp.GetOccupantsOfType(occ, ECompartmentType.PILOT);
            if (occ.Count() > 0)
                return occ[0];
            // Empty vehicle -> do not target the vehicle entity
            return null;
        }
        return null; // Non-character and no compartments considered non-targetable
    }

	//------------------------------------------------------------------------------------------------
	protected bool IsUsableTarget(BaseTarget target)
	{
		if (!target)
			return false;

		if (target.IsDisarmed())
			return false;

		IEntity targetEntity = target.GetTargetEntity();
		if (!targetEntity)
			return false;

		if (!m_ZombieCharacter.IsTargetValid(targetEntity))
			return false;

		IEntity resolvedTarget = ResolveTargetEntityPreferCharacter(targetEntity);
		if (resolvedTarget && !m_ZombieCharacter.IsTargetValid(resolvedTarget))
			return false;

		if (!resolvedTarget && !ChimeraCharacter.Cast(targetEntity))
			return false;

		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected BaseTarget PickClosestValidTarget(BaseTarget first, BaseTarget second)
	{
		bool firstValid = IsUsableTarget(first);
		bool secondValid = IsUsableTarget(second);

		if (firstValid && secondValid)
		{
			if (first.GetDistance() <= second.GetDistance())
				return first;

			return second;
		}

		if (firstValid)
			return first;

		if (secondValid)
			return second;

		return null;
	}

	static override bool VisibleInPalette()
	{
		return true;
	}
}
