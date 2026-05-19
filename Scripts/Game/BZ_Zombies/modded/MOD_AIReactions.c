
modded class SCR_AIDangerReaction_Vehicle
{
    static const float BZ_MIN_VEH_SPEED = 0.15; // m/s
    static const float BZ_REACT_DIST = 12.0;    // meters

    override bool PerformReaction(notnull SCR_AIUtilityComponent utility, notnull SCR_AIThreatSystem threatSystem, AIDangerEvent dangerEvent, int dangerEventCount)
    {
        IEntity vehicleEntity = dangerEvent.GetVictim();
        bool existsAction = false;
        bool hasDriver = false;
        bool isPilot = false;
        float speed = 0;
        float dist = -1;

        if (vehicleEntity)
        {
            existsAction = SCR_AIMoveFromDangerBehavior.ExistsBehaviorForEntity(utility, vehicleEntity);

            SCR_BaseCompartmentManagerComponent compManager = SCR_BaseCompartmentManagerComponent.Cast(vehicleEntity.FindComponent(SCR_BaseCompartmentManagerComponent));
            if (compManager)
            {
                array<IEntity> occupants = {};
                compManager.GetOccupantsOfType(occupants, ECompartmentType.PILOT);
                hasDriver = occupants.Count() > 0;
            }

            Physics phy = vehicleEntity.GetPhysics();
            vector vel = vector.Zero;
            if (phy)
                vel = phy.GetVelocity();
            speed = vel.Length();

            dist = vector.Distance(utility.GetOrigin(), vehicleEntity.GetOrigin());

            isPilot = utility.m_AIInfo.HasUnitState(EUnitState.PILOT);
        }

        // Run base logic first
        bool baseResult = super.PerformReaction(utility, threatSystem, dangerEvent, dangerEventCount);
        if (baseResult)
        {
            return true;
        }

        // If base declined and conditions are clearly dangerous, force an avoidance action (on foot only)
        if (vehicleEntity && !isPilot && !utility.m_AIInfo.HasUnitState(EUnitState.IN_VEHICLE))
        {
            bool closeAndMoving = (speed > BZ_MIN_VEH_SPEED) && (dist >= 0 && dist <= BZ_REACT_DIST);
            if (hasDriver && !existsAction && closeAndMoving)
            {
                SCR_AIMoveFromDangerBehavior behavior = new SCR_AIMoveFromIncomingVehicleBehavior(utility, null, vector.Zero, dangerEntity: vehicleEntity);
                utility.AddAction(behavior);
                return true;
            }
        }

        return false;
    }
}

modded class SCR_AIDangerReaction_VehicleHorn
{
    static const float BZ_HORN_REACT_DIST = 15.0; // meters

    override bool PerformReaction(notnull SCR_AIUtilityComponent utility, notnull SCR_AIThreatSystem threatSystem, AIDangerEvent dangerEvent, int dangerEventCount)
    {
        IEntity vehicleObject = dangerEvent.GetVictim();
        float distSq = -1;
        bool existsAction = false;
        bool sameVehicle = false;

        if (vehicleObject)
        {
            vector vehiclePos = vehicleObject.GetOrigin();
            vector agentPos = utility.GetOrigin();
            distSq = vector.DistanceSq(vehiclePos, agentPos);
            existsAction = SCR_AIMoveFromDangerBehavior.ExistsBehaviorForEntity(utility, vehicleObject);

            // Check if we are in the same vehicle
            IEntity parent = utility.m_OwnerEntity.GetParent();
            while (parent)
            {
                if (parent == vehicleObject) { sameVehicle = true; break; }
                parent = parent.GetParent();
            }
        }

        bool baseResult = super.PerformReaction(utility, threatSystem, dangerEvent, dangerEventCount);
        if (baseResult)
        {
            return true;
        }

        // Force avoidance when honk is very close
        if (vehicleObject && !sameVehicle && !existsAction && distSq >= 0 && distSq <= (BZ_HORN_REACT_DIST*BZ_HORN_REACT_DIST))
        {
            SCR_AIMoveFromDangerBehavior behavior = new SCR_AIMoveFromVehicleHornBehavior(utility, null, vector.Zero, dangerEntity: vehicleObject);
            utility.AddAction(behavior);
            return true;
        }

        return false;
    }
}

