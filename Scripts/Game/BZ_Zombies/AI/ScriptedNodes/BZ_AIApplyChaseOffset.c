// Pushes a combat move request toward a precomputed chase offset position
class BZ_AIApplyChaseOffset : AITaskScripted
{
	protected static const string PORT_IN_TARGET = "Target";
	protected static const string PORT_IN_OFFSET = "OffsetPosition";

	[Attribute("0.4", UIWidgets.EditBox, "Cooldown between offset requests (seconds)")]
	protected float m_fRequestInterval;

	[Attribute("0.8", UIWidgets.EditBox, "Lifetime of each offset move request (seconds)")]
	protected float m_fRequestDuration;

	[Attribute("0", UIWidgets.CheckBox, "Enable debug logging")]
	protected bool m_bDebug;

	protected BZ_ZombieCharacter m_ZombieCharacter;
	protected SCR_AIUtilityComponent m_Utility;
	protected SCR_AICombatMoveState m_CombatMoveState;
	protected float m_fNextRequestTime;
	protected vector m_vLastIssuedPos;

	//------------------------------------------------------------------------------------------------
	override void OnInit(AIAgent owner)
	{
		IEntity ent = owner.GetControlledEntity();
		if (ent)
			m_ZombieCharacter = BZ_ZombieCharacter.Cast(ent);

		m_Utility = SCR_AIUtilityComponent.Cast(owner.FindComponent(SCR_AIUtilityComponent));
		if (m_Utility)
			m_CombatMoveState = m_Utility.m_CombatMoveState;
	}

	//------------------------------------------------------------------------------------------------
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		if (!EnsureComponents(owner))
			return ENodeResult.FAIL;

		vector offsetPos;
		GetVariableIn(PORT_IN_OFFSET, offsetPos);
		if (offsetPos == vector.Zero)
			return ENodeResult.FAIL;

		IEntity targetEntity;
		GetVariableIn(PORT_IN_TARGET, targetEntity);
		if (!targetEntity && m_ZombieCharacter)
			targetEntity = m_ZombieCharacter.GetCurrentTarget();
		if (!targetEntity)
			return ENodeResult.FAIL;

		BaseWorld world = owner.GetWorld();
		if (!world)
			world = GetGame().GetWorld();
		float now = 0;
		if (world)
			now = world.GetWorldTime() * 0.001;

		// Rate limit requests and skip if offset hasn't changed much
		if (now < m_fNextRequestTime && vector.DistanceSq(offsetPos, m_vLastIssuedPos) < 0.25)
			return ENodeResult.SUCCESS;

		SCR_AICombatMoveRequest_Move rq = new SCR_AICombatMoveRequest_Move();
		rq.m_eReason = SCR_EAICombatMoveReason.STANDARD;
		rq.m_vTargetPos = targetEntity.GetOrigin();
		rq.m_vMovePos = offsetPos;
		rq.m_bTryFindCover = false;
		rq.m_bUseCoverSearchDirectivity = false;
		rq.m_bCheckCoverVisibility = false;
		rq.m_bFailIfNoCover = false;
		rq.m_eStanceMoving = ECharacterStance.STAND;
		rq.m_eStanceEnd = ECharacterStance.STAND;
		rq.m_eMovementType = EMovementType.SPRINT;
		rq.m_bAimAtTarget = false;
		rq.m_bAimAtTargetEnd = false;
		rq.m_fCoverSearchDistMax = 0;
		rq.m_fCoverSearchDistMin = 0;
		rq.m_fMoveDuration_s = Math.Max(m_fRequestDuration, 0.3);

		m_CombatMoveState.ApplyNewRequest(rq);
		m_fNextRequestTime = now + Math.Max(m_fRequestInterval, 0.1);
		m_vLastIssuedPos = offsetPos;

		if (m_bDebug)
			Print(string.Format("[BZ_AIApplyChaseOffset] Move request toward %1", offsetPos), LogLevel.VERBOSE);

		return ENodeResult.SUCCESS;
	}

	//------------------------------------------------------------------------------------------------
	protected bool EnsureComponents(AIAgent owner)
	{
		if (!m_CombatMoveState || !m_Utility)
		{
			m_Utility = SCR_AIUtilityComponent.Cast(owner.FindComponent(SCR_AIUtilityComponent));
			if (m_Utility)
				m_CombatMoveState = m_Utility.m_CombatMoveState;
		}

		if (!m_ZombieCharacter)
		{
			IEntity ent = owner.GetControlledEntity();
			if (ent)
				m_ZombieCharacter = BZ_ZombieCharacter.Cast(ent);
		}

		return m_CombatMoveState != null;
	}

	static override bool VisibleInPalette()
	{
		return true;
	}
}
