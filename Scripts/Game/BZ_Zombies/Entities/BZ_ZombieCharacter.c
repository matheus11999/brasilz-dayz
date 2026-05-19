// BrasilZ Zombies - Zombie Character Class
// Base zombie character that extends SCR_ChimeraCharacter

class BZ_ZombieCharacterClass : Bacon_622120A5448725E3_InfectedCharacterClass
{
}

class BZ_ZombieCharacter : Bacon_622120A5448725E3_InfectedCharacter
{
	// ========================================
	// CONFIGURABLE ATTRIBUTES
	// ========================================

	// Movement Type Settings
	[Attribute("1", UIWidgets.ComboBox, "Wander Movement Type", "", ParamEnumArray.FromEnum(EMovementType))]
	protected EMovementType m_eWanderMovementType;

	[Attribute("0.500", UIWidgets.EditBox, "Wander Speed Night")]
	protected float m_fWanderSpeedNight;

	[Attribute("0.250", UIWidgets.EditBox, "Wander Speed Day")]
	protected float m_fWanderSpeedDay;

	[Attribute("2", UIWidgets.ComboBox, "Chase Movement Type", "", ParamEnumArray.FromEnum(EMovementType))]
	protected EMovementType m_eChaseMovementType;

	[Attribute("0.900", UIWidgets.EditBox, "Chase Speed Night")]
	protected float m_fChaseSpeedNight;

	[Attribute("0.900", UIWidgets.EditBox, "Chase Speed Day")]
	protected float m_fChaseSpeedDay;

	// Detection and Combat Settings
	[Attribute("30.000", UIWidgets.EditBox, "Detection Range")]
	protected float m_fDetectionRange;

	[Attribute("1.200", UIWidgets.EditBox, "Attack Range")]
	protected float m_fAttackRange;

	[Attribute("25.000", UIWidgets.EditBox, "Attack Damage")]
	protected float m_fAttackDamage;

	[Attribute("2.000", UIWidgets.EditBox, "Attack Cooldown")]
	protected float m_fAttackCooldown;

	// Wandering Settings
	[Attribute("200.000", UIWidgets.EditBox, "Max Wander Radius")]
	protected float m_fMaxWanderRadius;

	[Attribute("50.000", UIWidgets.EditBox, "Min Wander Distance")]
	protected float m_fMinWanderDistance;

	// Debug Settings
	[Attribute("0", UIWidgets.CheckBox, "Debug Mode")]
	protected bool m_bDebugMode;

	// ========================================
	// CACHED COMPONENTS
	// ========================================

	protected CharacterControllerComponent m_Controller;
	protected SCR_CharacterDamageManagerComponent m_DamageManager;
	protected CharacterAnimationComponent m_AnimationComponent;
	protected AIControlComponent m_AIControl;
	protected SCR_AIUtilityComponent m_AIUtility;
protected SCR_AICombatMoveState m_CombatMoveState;

	// ========================================
	// RUNTIME STATE
	// ========================================

	protected IEntity m_CurrentTarget;
	protected IEntity m_PreviousTarget;
	protected vector m_vSpawnPosition;
	protected vector m_vWanderPoint;
	protected vector m_vLastKnownTargetPosition;
	protected float m_fLastAttackTime;
	protected float m_fTargetLostTime;
	protected float m_fLastBroadcastTime = -999.0;  // Per-zombie broadcast cooldown
	protected bool m_bIsChasing;
	protected bool m_bTargetLost;
	protected float m_fInvestigateSuppressUntil;
	protected float m_fInvestigateCommitUntil;

	// ========================================
	// DAMAGE TRACKING (NEW)
	// ========================================
	protected IEntity m_LastAttacker;
	protected float m_fLastDamageTime;
	
	// ========================================
	// TARGET COMMITMENT (prevent ping-pong)
	// ========================================
	protected float m_fTargetCommitmentTime;

	// ========================================
	// THREAT STATE SYSTEM (NEW)
	// ========================================
	protected EZombieThreatState m_eThreatState;
	protected float m_fThreatStateTime;
	protected vector m_vThreatPosition;
	protected IEntity m_ThreatEntity;


	// ========================================
	// INITIALIZATION
	// ========================================

	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);
		owner.SetEventMask(owner.GetEventMask() | EntityEvent.FRAME);

		// Cache components for performance
		m_Controller = GetCharacterController();
		m_DamageManager = SCR_CharacterDamageManagerComponent.Cast(GetDamageManager());
		m_AnimationComponent = GetAnimationComponent();
		m_AIControl = GetAIControlComponent();
		EnsureUtilityComponents();

		// Store spawn position for wandering
		m_vSpawnPosition = owner.GetOrigin();

		// Initialize state
		m_fLastAttackTime = -m_fAttackCooldown;
		m_bIsChasing = false;

		// Initialize threat state
		m_eThreatState = EZombieThreatState.IDLE;

		// Get world time safely - may be null during initialization
		BaseWorld world = GetGame().GetWorld();
		if (world)
			m_fThreatStateTime = world.GetWorldTime() * 0.001;
		else
			m_fThreatStateTime = 0;

		m_vThreatPosition = vector.Zero;
		m_ThreatEntity = null;

		// Register with horde manager for coordination
		if (Replication.IsServer())
		{
			BZ_ZombieHordeManager.GetInstance().RegisterZombie(this);
			if (m_bDebugMode)
				Print(string.Format("[BZ_Zombie] Registered with horde manager - Total zombies: %1", BZ_ZombieHordeManager.GetInstance().GetZombieCount()), LogLevel.NORMAL);
		}

		// Initialize damage tracking
		m_LastAttacker = null;
		m_fLastDamageTime = -999;
		m_fTargetCommitmentTime = 0;
		m_fInvestigateCommitUntil = 0;

		if (m_bDebugMode)
			Print(string.Format("[BZ_Zombie] Initialized at position: %1", m_vSpawnPosition), LogLevel.NORMAL);
	}
	
	//------------------------------------------------------------------------------------------------
	override void EOnDiag(IEntity owner, float timeSlice)
	{
		super.EOnDiag(owner, timeSlice);
		
		// Unregister on delete
		if (owner.IsDeleted() && Replication.IsServer())
		{
			BZ_ZombieHordeManager.GetInstance().UnregisterZombie(this);
		}
	}
	
	//------------------------------------------------------------------------------------------------
	void ~BZ_ZombieCharacter()
	{
		// Unregister from horde manager on destruction
		if (Replication.IsServer())
		{
			BZ_ZombieHordeManager.GetInstance().UnregisterZombie(this);
		}

	}

	// ========================================
	// DAMAGE SYSTEM
	// ========================================

	//------------------------------------------------------------------------------------------------
	//! Called when zombie takes damage - track the attacker
	protected void OnDamageReceived(BaseDamageContext damageContext)
	{
		if (!Replication.IsServer())
			return;

		if (!damageContext)
			return;

		// Get the instigator (who shot us) - damageContext.instigator is a property
		if (!damageContext.instigator)
			return;

		// Get the instigator entity
		IEntity attackerEntity = damageContext.instigator.GetInstigatorEntity();
		if (!attackerEntity)
			return;

		if (IsZombieEntity(attackerEntity))
			return;

		// Store attacker and time
		m_LastAttacker = attackerEntity;
		m_fLastDamageTime = GetGame().GetWorld().GetWorldTime() * 0.001;

		if (m_bDebugMode)
			Print(string.Format("[BZ_Zombie] 🎯 SHOT BY: %1 (damage: %.1f)", attackerEntity.GetName(), damageContext.damageValue), LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	//! Get the last entity that damaged this zombie
	IEntity GetLastAttacker()
	{
		return m_LastAttacker;
	}

	//------------------------------------------------------------------------------------------------
	//! Get time when last damage was received
	float GetLastDamageTime()
	{
		return m_fLastDamageTime;
	}

	//------------------------------------------------------------------------------------------------
	//! Clear the attacker (after a timeout)
	void ClearLastAttacker()
	{
		m_LastAttacker = null;
	}
	
	//------------------------------------------------------------------------------------------------
	//! Set target commitment time (prevents switching back for X seconds)
	void SetTargetCommitmentTime(float seconds)
	{
		float currentTime = GetGame().GetWorld().GetWorldTime() * 0.001;
		m_fTargetCommitmentTime = currentTime + seconds;
		
		if (m_bDebugMode)
			Print(string.Format("[BZ_Zombie] Committed to target for %.1fs", seconds), LogLevel.VERBOSE);
	}
	
	//------------------------------------------------------------------------------------------------
	//! Check if zombie is in target commitment period
	bool IsInTargetCommitmentPeriod()
	{
		float currentTime = GetGame().GetWorld().GetWorldTime() * 0.001;
		return currentTime < m_fTargetCommitmentTime;
	}

	// ========================================
	// GETTERS - Used by AI nodes
	// ========================================

	//------------------------------------------------------------------------------------------------
	// Movement Type Getters
	//------------------------------------------------------------------------------------------------
	EMovementType GetWanderMovementType()
	{
		return m_eWanderMovementType;
	}

	//------------------------------------------------------------------------------------------------
	EMovementType GetChaseMovementType()
	{
		return m_eChaseMovementType;
	}

	//------------------------------------------------------------------------------------------------
	float GetWanderSpeedNight()
	{
		return m_fWanderSpeedNight;
	}

	//------------------------------------------------------------------------------------------------
	float GetWanderSpeedDay()
	{
		return m_fWanderSpeedDay;
	}

	//------------------------------------------------------------------------------------------------
	float GetChaseSpeedNight()
	{
		return m_fChaseSpeedNight;
	}

	//------------------------------------------------------------------------------------------------
	float GetChaseSpeedDay()
	{
		return m_fChaseSpeedDay;
	}

	//------------------------------------------------------------------------------------------------
	// Legacy speed getters (for backwards compatibility)
	//------------------------------------------------------------------------------------------------
	float GetWalkSpeed()
	{
		return m_fWanderSpeedNight;
	}

	//------------------------------------------------------------------------------------------------
	float GetChaseSpeed()
	{
		return m_fChaseSpeedNight;
	}

	//------------------------------------------------------------------------------------------------
	// Detection and Combat Getters
	//------------------------------------------------------------------------------------------------
	float GetDetectionRange()
	{
		return m_fDetectionRange;
	}

	//------------------------------------------------------------------------------------------------
	float GetAttackRange()
	{
		return m_fAttackRange;
	}

	//------------------------------------------------------------------------------------------------
	float GetAttackDamage()
	{
		return m_fAttackDamage;
	}

	//------------------------------------------------------------------------------------------------
	float GetAttackCooldown()
	{
		return m_fAttackCooldown;
	}

	//------------------------------------------------------------------------------------------------
	// Wandering Getters
	//------------------------------------------------------------------------------------------------
	float GetMaxWanderRadius()
	{
		return m_fMaxWanderRadius;
	}

	//------------------------------------------------------------------------------------------------
	float GetMinWanderDistance()
	{
		return m_fMinWanderDistance;
	}

	//------------------------------------------------------------------------------------------------
	vector GetSpawnPosition()
	{
		return m_vSpawnPosition;
	}

	//------------------------------------------------------------------------------------------------
	// State Getters
	//------------------------------------------------------------------------------------------------
	IEntity GetCurrentTarget()
	{
		return m_CurrentTarget;
	}

	//------------------------------------------------------------------------------------------------
	bool IsChasing()
	{
		return m_bIsChasing;
	}

	//------------------------------------------------------------------------------------------------
	vector GetLastKnownTargetPosition()
	{
		return m_vLastKnownTargetPosition;
	}

	//------------------------------------------------------------------------------------------------
	bool IsTargetLost()
	{
		return m_bTargetLost;
	}

	//------------------------------------------------------------------------------------------------
	float GetTargetLostTime()
	{
		return m_fTargetLostTime;
	}

	//------------------------------------------------------------------------------------------------
	IEntity GetPreviousTarget()
	{
		return m_PreviousTarget;
	}

	//------------------------------------------------------------------------------------------------
	//! Broadcast detection event to nearby zombies (DISABLED - causes group behavior)
	//! Based on base game's danger event system (similar to UnsafeArea, WeaponFire events)
	void BroadcastDetection(IEntity detectedTarget)
	{
		// DISABLED: Broadcasting causes all zombies to switch to the same target
		// This destroys individual target selection and creates swarm behavior
		// Only use AlertNearbyZombies() for explicit horde calls
		
		if (m_bDebugMode)
			Print(string.Format("[BZ_Zombie] BroadcastDetection DISABLED (individual behavior)"), LogLevel.VERBOSE);
		
		return;
	}

	//------------------------------------------------------------------------------------------------
	//! Receive alert from another zombie about a target
	void ReceiveAlert(IEntity target, vector alertPosition)
	{
		if (!target)
			return;

		if (IsZombieEntity(target))
			return;

		// ONLY accept alerts if we DON'T have a target (horde behavior for idle zombies)
		// Zombies with their own targets ignore horde alerts - they're independent
		if (!m_CurrentTarget)
		{
			// Check if we're in target commitment period (recently broke away)
			float currentTime = GetGame().GetWorld().GetWorldTime() * 0.001;
			if (currentTime < m_fTargetCommitmentTime)
			{
				if (m_bDebugMode)
					Print(string.Format("[BZ_Zombie] Ignoring alert - in commitment period (%.1fs remaining)",
						m_fTargetCommitmentTime - currentTime), LogLevel.VERBOSE);
				return;
			}
			
			// No current target - join the horde!
			SetCurrentTarget(target);
			SetThreatState(EZombieThreatState.CHASING, alertPosition, target);

			m_vLastKnownTargetPosition = alertPosition;
			m_bTargetLost = false;
			
			// Clear suppression when we get a direct target alert
			ClearInvestigationSuppression();

			if (m_bDebugMode)
				Print(string.Format("[BZ_Zombie] Received alert, joining horde (target: %1)",
					target.GetName()), LogLevel.VERBOSE);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Clear the target lost flag (called after investigation completes)
	void ClearTargetLost()
	{
		m_bTargetLost = false;
		m_vLastKnownTargetPosition = vector.Zero;

		// Add a short suppression window so Detect can't immediately re-arm investigate
		SuppressInvestigationFor(1.25);

		if (m_bDebugMode)
			Print("[BZ_Zombie] Investigation complete - clearing target lost flag", LogLevel.NORMAL);
	}

		//------------------------------------------------------------------------------------------------
		//! Alert zombie to investigate a position (e.g., gunshot) without a resolved target
		void AlertInvestigateAt(vector pos)
		{
			// Ignore re-arming if suppressed (allows Wander reset like base)
			if (IsInvestigationSuppressed())
				return;

			m_vLastKnownTargetPosition = pos;
			m_bTargetLost = true;
			m_fTargetLostTime = GetGame().GetWorld().GetWorldTime() * 0.001;
			if (m_bDebugMode)
				Print(string.Format("[BZ_Zombie] AlertInvestigateAt: %1", pos.ToString()), LogLevel.NORMAL);
		}

		//------------------------------------------------------------------------------------------------
		//! Temporarily suppress investigation re-arming to allow wander reset
		void SuppressInvestigationFor(float seconds)
		{
			float now = GetGame().GetWorld().GetWorldTime() * 0.001;
			m_fInvestigateSuppressUntil = now + seconds;

			if (m_bDebugMode)
				Print(string.Format("[BZ_Zombie] Suppressing investigate until t=%1", m_fInvestigateSuppressUntil), LogLevel.NORMAL);
		}

		// Allow fresh danger events to bypass suppression window
		void ClearInvestigationSuppression()
		{
			m_fInvestigateSuppressUntil = 0;
			if (m_bDebugMode)
				Print("[BZ_Zombie] Cleared investigation suppression (new danger)", LogLevel.NORMAL);
		}

		protected bool IsInvestigationSuppressed()
		{
			float now = GetGame().GetWorld().GetWorldTime() * 0.001;
			return now < m_fInvestigateSuppressUntil;
		}

			// Public accessor for suppression (used by Decorators)
			bool IsInvestigationSuppressedActive()
			{
				return IsInvestigationSuppressed();
			}

	//------------------------------------------------------------------------------------------------
	//! Force zombie to finish the current investigation for at least X seconds
	void CommitInvestigationFor(float seconds)
	{
		BaseWorld world = GetGame().GetWorld();
		float now = 0;
		if (world)
			now = world.GetWorldTime() * 0.001;

		if (seconds <= 0)
			m_fInvestigateCommitUntil = now;
		else
			m_fInvestigateCommitUntil = now + seconds;

		if (m_bDebugMode)
			Print(string.Format("[BZ_Zombie] Investigation commit set for %.1fs", Math.Max(seconds, 0)), LogLevel.VERBOSE);
	}

	//------------------------------------------------------------------------------------------------
	bool IsInvestigationCommitActive()
	{
		BaseWorld world = GetGame().GetWorld();
		float now = 0;
		if (world)
			now = world.GetWorldTime() * 0.001;
		return now < m_fInvestigateCommitUntil;
	}

	//------------------------------------------------------------------------------------------------
	void ClearInvestigationCommit()
	{
		m_fInvestigateCommitUntil = 0;
	}

	// ========================================
	// THREAT STATE SYSTEM METHODS (NEW)
	// ========================================

	//------------------------------------------------------------------------------------------------
	//! Set zombie threat state (persistent state system)
	void SetThreatState(EZombieThreatState state, vector position, IEntity entity)
	{
		m_eThreatState = state;
		m_fThreatStateTime = GetGame().GetWorld().GetWorldTime() * 0.001;
		m_vThreatPosition = position;
		m_ThreatEntity = entity;

		if (state != EZombieThreatState.INVESTIGATING)
			ClearInvestigationCommit();

		if (m_bDebugMode)
		{
			string stateName = typename.EnumToString(EZombieThreatState, state);
			Print(string.Format("[BZ_Zombie] Threat state changed to: %1 at position: %2", stateName, position.ToString()), LogLevel.NORMAL);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Update threat position without resetting the timer (for ongoing threats)
	void UpdateThreatPosition(vector position)
	{
		m_vThreatPosition = position;

		if (m_bDebugMode)
			Print(string.Format("[BZ_Zombie] Threat position updated to: %1", position.ToString()), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	//! Get current threat state
	EZombieThreatState GetThreatState()
	{
		return m_eThreatState;
	}

	//------------------------------------------------------------------------------------------------
	//! Check if threat state matches expected state
	bool IsThreatState(EZombieThreatState state)
	{
		return m_eThreatState == state;
	}

	//------------------------------------------------------------------------------------------------
	//! Get threat position
	vector GetThreatPosition()
	{
		return m_vThreatPosition;
	}

	//------------------------------------------------------------------------------------------------
	//! Get threat entity (if any)
	IEntity GetThreatEntity()
	{
		return m_ThreatEntity;
	}

	//------------------------------------------------------------------------------------------------
	//! Get time when threat state was set
	float GetThreatStateTime()
	{
		return m_fThreatStateTime;
	}

	//------------------------------------------------------------------------------------------------
	//! Clear threat state (return to IDLE)
	void ClearThreatState()
	{
		m_eThreatState = EZombieThreatState.IDLE;
		m_vThreatPosition = vector.Zero;
		m_ThreatEntity = null;
		m_fThreatStateTime = GetGame().GetWorld().GetWorldTime() * 0.001;
		ClearInvestigationCommit();

		if (m_bDebugMode)
			Print("[BZ_Zombie] Threat state cleared -> IDLE", LogLevel.NORMAL);
	}

	// ========================================
	// SETTERS - Used by AI nodes
	// ========================================

	//------------------------------------------------------------------------------------------------
	void SetCurrentTarget(IEntity target)
	{
		if (target && IsZombieEntity(target))
			target = null;

		// If we're losing a target, decide whether to arm investigate
		if (m_CurrentTarget && !target)
		{
			if (IsInvestigationSuppressed())
			{
				// During suppression window, just drop the target without arming investigate
				if (m_bDebugMode)
					Print("[BZ_Zombie] Drop target during suppression -> not arming investigate", LogLevel.NORMAL);
			}
			else
			{
				m_vLastKnownTargetPosition = m_CurrentTarget.GetOrigin();
				m_fTargetLostTime = GetGame().GetWorld().GetWorldTime() * 0.001; // Convert to seconds
				m_bTargetLost = true;
				m_PreviousTarget = m_CurrentTarget;

				if (m_bDebugMode)
					Print(string.Format("[BZ_Zombie] Target lost! Last known position: %1", m_vLastKnownTargetPosition.ToString()), LogLevel.NORMAL);
			}
		}
		// If we're acquiring a target, update last known position
		else if (target)
		{
			m_vLastKnownTargetPosition = target.GetOrigin();
			m_bTargetLost = false;
		}

		m_CurrentTarget = target;

		if (m_bDebugMode)
		{
			if (target)
				Print(string.Format("[BZ_Zombie] Target set: %1", target), LogLevel.NORMAL);
			else
				Print("[BZ_Zombie] Target cleared", LogLevel.NORMAL);
		}
	}

	//------------------------------------------------------------------------------------------------
	void SetChasing(bool chasing)
	{
		m_bIsChasing = chasing;

		if (m_bDebugMode)
			Print(string.Format("[BZ_Zombie] Chasing: %1", chasing), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	//! Immediately reset zombie back to wander state (used when target hard-resets)
	void ForceReturnToWander(float suppressSeconds = 1.5)
	{
		CancelActiveCombatMove();

		m_CurrentTarget = null;
		m_PreviousTarget = null;
		m_bTargetLost = false;
		m_vLastKnownTargetPosition = vector.Zero;
		m_bIsChasing = false;
		m_fTargetCommitmentTime = 0;
		ClearThreatState();
		if (suppressSeconds > 0)
			SuppressInvestigationFor(suppressSeconds);

		if (m_bDebugMode)
			Print("[BZ_Zombie] ForceReturnToWander invoked", LogLevel.NORMAL);
	}

	void EnsureUtilityComponents()
	{
		if (!m_AIUtility)
			m_AIUtility = SCR_AIUtilityComponent.Cast(FindComponent(SCR_AIUtilityComponent));
		if (m_AIUtility && !m_CombatMoveState)
			m_CombatMoveState = m_AIUtility.m_CombatMoveState;
	}

	void CancelActiveCombatMove()
	{
		EnsureUtilityComponents();
		if (m_CombatMoveState)
			m_CombatMoveState.CancelRequest();
	}


	//------------------------------------------------------------------------------------------------
	void SetWanderPoint(vector point)
	{
		m_vWanderPoint = point;

		if (m_bDebugMode)
			Print(string.Format("[BZ_Zombie] Wander point set: %1", point), LogLevel.NORMAL);
	}

	// ========================================
	// ATTACK SYSTEM
	// ========================================

	//------------------------------------------------------------------------------------------------
	bool CanAttack()
	{
		float currentTime = GetGame().GetWorld().GetWorldTime() / 1000.0;
		float timeSinceLastAttack = currentTime - m_fLastAttackTime;

		return timeSinceLastAttack >= m_fAttackCooldown;
	}

	//------------------------------------------------------------------------------------------------
	bool TryAttack(IEntity target)
	{
		if (!target)
			return false;

		if (IsZombieEntity(target))
			return false;

		if (!CanAttack())
			return false;

		// Check if target is in range
		vector myPos = GetOrigin();
		vector targetPos = target.GetOrigin();
		float distance = vector.Distance(myPos, targetPos);

		if (distance > m_fAttackRange)
			return false;

		// Perform attack: prefer character damage manager if present, otherwise fallback to destructible
		vector hitTransform[3];
		hitTransform[0] = targetPos; // Hit position
		hitTransform[1] = (targetPos - myPos).Normalized(); // Hit direction
		hitTransform[2] = vector.Up; // Hit normal

		// Try character damage manager
		SCR_CharacterDamageManagerComponent charDmg = SCR_CharacterDamageManagerComponent.Cast(target.FindComponent(SCR_CharacterDamageManagerComponent));
		if (charDmg)
		{
			HitZone hz = charDmg.GetDefaultHitZone();
			SCR_DamageContext ctx = new SCR_DamageContext(EDamageType.MELEE, m_fAttackDamage, hitTransform, target, hz, Instigator.CreateInstigator(this), null, -1, -1);
			charDmg.HandleDamage(ctx);
			m_fLastAttackTime = GetGame().GetWorld().GetWorldTime() / 1000.0;
			if (m_bDebugMode)
				Print(string.Format("[BZ_Zombie] Melee damaged character for %1", m_fAttackDamage), LogLevel.NORMAL);
			return true;
		}

		// Fallback to destructible
		SCR_DestructibleEntity destructible = SCR_DestructibleEntity.Cast(target);
		if (destructible)
		{
			destructible.HandleDamage(EDamageType.MELEE, m_fAttackDamage, hitTransform);
			m_fLastAttackTime = GetGame().GetWorld().GetWorldTime() / 1000.0;
			if (m_bDebugMode)
				Print(string.Format("[BZ_Zombie] Melee damaged destructible for %1", m_fAttackDamage), LogLevel.NORMAL);
			return true;
		}

		return false;
	}

	// ========================================
	// WANDER SYSTEM
	// ========================================

	//------------------------------------------------------------------------------------------------
	vector GenerateRandomWanderPoint()
	{
		// Safety check: if spawn position not set, use current position
		if (m_vSpawnPosition == vector.Zero)
		{
			m_vSpawnPosition = GetOrigin();
			if (m_bDebugMode)
				Print("[BZ_Zombie] WARNING: Spawn position was zero, using current position", LogLevel.WARNING);
		}

		// Generate random point within wander radius from spawn position
		float angle = Math.RandomFloat(0, Math.PI2);
		float distance = Math.RandomFloat(m_fMinWanderDistance, m_fMaxWanderRadius);

		vector offset = Vector(
			Math.Cos(angle) * distance,
			0,
			Math.Sin(angle) * distance
		);

		vector wanderPoint = m_vSpawnPosition + offset;

		// Get terrain height at this position
		BaseWorld world = GetGame().GetWorld();
		if (world)
		{
			float terrainY = world.GetSurfaceY(wanderPoint[0], wanderPoint[2]);
			wanderPoint[1] = terrainY;
		}
		else
		{
			wanderPoint[1] = m_vSpawnPosition[1]; // Use spawn height if world not available
		}

		m_vWanderPoint = wanderPoint;

		if (m_bDebugMode)
			Print(string.Format("[BZ_Zombie] Generated wander point: %1", wanderPoint), LogLevel.NORMAL);

		return wanderPoint;
	}

	//------------------------------------------------------------------------------------------------
	vector GetWanderPoint()
	{
		return m_vWanderPoint;
	}

	// ========================================
	// UTILITY FUNCTIONS
	// ========================================

	//------------------------------------------------------------------------------------------------
	bool IsTargetValid(IEntity target)
	{
		if (!target)
			return false;

		if (IsZombieEntity(target))
			return false;

		// Check if target is destroyed
		DamageManagerComponent dmgComp = DamageManagerComponent.Cast(
			target.FindComponent(DamageManagerComponent)
		);

		if (dmgComp)
		{
			if (dmgComp.GetState() == EDamageState.DESTROYED)
				return false;
		}
		
		// Check if character is alive (not dead/unconscious)
		CharacterControllerComponent ctrl = CharacterControllerComponent.Cast(
			target.FindComponent(CharacterControllerComponent)
		);
		
		if (ctrl)
		{
			if (ctrl.GetLifeState() != ECharacterLifeState.ALIVE)
			{
				if (m_bDebugMode)
					Print(string.Format("[BZ_Zombie] Target invalid - not alive (life state: %1)", ctrl.GetLifeState()), LogLevel.NORMAL);
				return false;
			}
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	bool IsZombieEntity(IEntity target)
	{
		if (!target)
			return false;

		if (target == this)
			return true;

		if (BZ_ZombieCharacter.Cast(target))
			return true;

		if (Bacon_622120A5448725E3_InfectedCharacter.Cast(target))
			return true;

		return false;
	}

	//------------------------------------------------------------------------------------------------
	float GetDistanceToTarget(IEntity target)
	{
		if (!target)
			return -1;

		vector myPos = GetOrigin();
		vector targetPos = target.GetOrigin();

		return vector.Distance(myPos, targetPos);
	}

	//------------------------------------------------------------------------------------------------
	bool IsTargetInRange(IEntity target, float range)
	{
		float distance = GetDistanceToTarget(target);

		if (distance < 0)
			return false;

		return distance <= range;
	}

	// ========================================
	// DEBUG VISUALIZATION
	// ========================================

	//------------------------------------------------------------------------------------------------
	override void EOnFrame(IEntity owner, float timeSlice)
	{
		super.EOnFrame(owner, timeSlice);

		if (!m_bDebugMode)
			return;

		// Debug visualization would go here
		// (Arma Reforger debug shapes API)
	}
}
