// Finds nearby moving vehicle approaching the agent and injects MoveFromDanger behavior
class BZ_AICheckIncomingVehicle : AITaskScripted
{
    // Working variables for query callback
    protected IEntity m_BestVehicle;
    protected float m_fBestDist;
    protected vector m_vMyPos;
    protected IEntity m_MyEntity;
    protected SCR_AIUtilityComponent m_Util;

    [Attribute("12.0", UIWidgets.EditBox, "React distance (m)")]
    protected float m_fReactDistance;

    [Attribute("0.15", UIWidgets.EditBox, "Min vehicle speed (m/s)")]
    protected float m_fMinSpeed;

    [Attribute("0", UIWidgets.CheckBox, "Enable debug logs")]
    protected bool m_bDebug;

    override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
    {
        IEntity me = owner.GetControlledEntity();
        if (!me) return ENodeResult.SUCCESS; // don't block sequence

        m_MyEntity = me;
        m_vMyPos = me.GetOrigin();
        m_Util = SCR_AIUtilityComponent.Cast(owner.FindComponent(SCR_AIUtilityComponent));
        if (!m_Util) return ENodeResult.SUCCESS; // don't block sequence
        // Initialize per-query state accessed from callback
        m_BestVehicle = null;
        m_fBestDist = m_fReactDistance;

        // Gather nearby dynamic entities and find a likely vehicle moving toward me
        GetGame().GetWorld().QueryEntitiesBySphere(
            m_vMyPos,
            m_fReactDistance,
            Callbacks,
            null,
            EQueryEntitiesFlags.DYNAMIC
        );

        if (m_BestVehicle)
        {
            if (SCR_AIMoveFromDangerBehavior.ExistsBehaviorForEntity(m_Util, m_BestVehicle))
            {
                // Also try to force a move request if the behavior did not execute yet
                TryImmediateMoveRequest(m_BestVehicle);
                return ENodeResult.SUCCESS;
            }
            SCR_AIMoveFromDangerBehavior behavior = new SCR_AIMoveFromIncomingVehicleBehavior(m_Util, null, vector.Zero, dangerEntity: m_BestVehicle);
            m_Util.AddAction(behavior);
            // Fallback: push a move request immediately so we don't rely solely on utility selection
            TryImmediateMoveRequest(m_BestVehicle);
            return ENodeResult.SUCCESS;
        }

        // No candidate this tick — succeed to keep perception loop going
        return ENodeResult.SUCCESS;
    }

    // Query callback; scans for vehicles moving toward the agent
    bool Callbacks(IEntity e)
    {
        if (!e) return true;
        SCR_BaseCompartmentManagerComponent comp = SCR_BaseCompartmentManagerComponent.Cast(e.FindComponent(SCR_BaseCompartmentManagerComponent));
        if (!comp) return true; // not a vehicle with compartments

        // Optional: driver check disabled to be robust with various vehicles
        // array<IEntity> occ = {};
        // comp.GetOccupantsOfType(occ, ECompartmentType.PILOT);
        // if (occ.Count() == 0)
        // {
        //     if (m_bDebug)
        //         Print("[BZ_AICheckIncomingVehicle] Skipping vehicle with no driver", LogLevel.NORMAL);
        //     return true; // ignore empty vehicles
        // }

        Physics phy = e.GetPhysics();
        if (!phy)
        {
            if (m_bDebug)
                Print("[BZ_AICheckIncomingVehicle] Vehicle missing physics", LogLevel.WARNING);
            return true;
        }
        vector vel = phy.GetVelocity();
        float speed = vel.Length();
        if (speed <= m_fMinSpeed)
        {
            if (m_bDebug)
                Print(string.Format("[BZ_AICheckIncomingVehicle] Too slow: %1 m/s (min %2)", speed, m_fMinSpeed), LogLevel.NORMAL);
            return true;
        }

        // Do NOT gate by approach angle here; let the behavior validate.
        // Base game performs the precise approach/cover check inside the behavior condition.

        // Keep the nearest within react distance
        float dist = vector.Distance(m_vMyPos, e.GetOrigin());
        if (dist < m_fBestDist)
        {
            m_fBestDist = dist;
            m_BestVehicle = e;
            if (m_bDebug)
                Print(string.Format("[BZ_AICheckIncomingVehicle] Candidate %1 at %2m (speed=%3)", e, dist, speed), LogLevel.NORMAL);
        }
        return true;
    }

    protected void TryImmediateMoveRequest(IEntity vehicleEntity)
    {
        SCR_AICombatMoveState state = null;
        if (m_Util)
            state = m_Util.m_CombatMoveState;
        if (!state)
            return;

        if (!SCR_AIMoveFromIncomingVehicleBehavior.ExecuteBehaviorCondition(m_Util, vehicleEntity))
            return;

        SCR_AICombatMoveRequest_Move rq = new SCR_AICombatMoveRequest_Move();
        rq.m_eReason = SCR_EAICombatMoveReason.MOVE_FROM_DANGER;
        rq.m_vTargetPos = vehicleEntity.GetOrigin();
        rq.m_vMovePos = rq.m_vTargetPos;
        rq.m_bTryFindCover = true;
        rq.m_bUseCoverSearchDirectivity = false;
        rq.m_bCheckCoverVisibility = false;
        rq.m_bFailIfNoCover = false;
        rq.m_eStanceMoving = ECharacterStance.STAND;
        rq.m_eStanceEnd = ECharacterStance.STAND;
        rq.m_eMovementType = EMovementType.SPRINT;
        rq.m_fCoverSearchDistMax = 9;
        rq.m_fCoverSearchDistMin = 0;
        rq.m_fMoveDuration_s = rq.m_fCoverSearchDistMax / SCR_AICombatMoveUtils.CHARACTER_SPEED_STAND_SPRINT;

        vector myPos;
        if (m_MyEntity)
            myPos = m_MyEntity.GetOrigin();
        else
            myPos = m_vMyPos;
        vector myPosVehicleSpace = vehicleEntity.CoordToLocal(myPos);
        vector vehicleAside = vehicleEntity.GetTransformAxis(0);
        vehicleAside[1] = 0;

        if (vehicleAside.LengthSq() < 0.01)
        {
            rq.m_eDirection = SCR_EAICombatMoveDirection.BACKWARD;
        }
        else if (myPosVehicleSpace[0] < 0)
        {
            rq.m_eDirection = SCR_EAICombatMoveDirection.RIGHT;
        }
        else
        {
            rq.m_eDirection = SCR_EAICombatMoveDirection.LEFT;
        }

        rq.m_fCoverSearchSectorHalfAngleRad = -Math.PI;
        rq.m_bAimAtTarget = false;
        rq.m_bAimAtTargetEnd = true;

        state.ApplyNewRequest(rq);
        if (m_bDebug)
        {
            Print(string.Format("[BZ_AICheckIncomingVehicle] Fallback ApplyNewRequest dir=%1 duration=%2", rq.m_eDirection, rq.m_fMoveDuration_s), LogLevel.NORMAL);
        }
    }

    static override bool VisibleInPalette() { return true; }
}
