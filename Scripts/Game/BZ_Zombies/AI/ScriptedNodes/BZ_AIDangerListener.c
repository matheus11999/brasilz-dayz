// Simple BT node to log incoming danger events from the AIAgent
class BZ_AIDangerListener : AITaskScripted
{
    override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
    {
        int index = 0;
        while (true)
        {
            int aggCount;
            AIDangerEvent ev = owner.GetDangerEvent(index, aggCount);
            if (!ev)
                break;

            EAIDangerEventType type = ev.GetDangerType();
            IEntity victim = ev.GetVictim();
            vector pos = ev.GetPosition();

            index++;
        }

        if (index > 0)
            owner.ClearDangerEvents(index);

        return ENodeResult.SUCCESS;
    }

    static override bool VisibleInPalette() { return true; }
}
