// BZ Map Marker — dados replicados via RplProp + modded EE_AreaMarker.Update.
//
// Por que não EE_AreaMarkerPrefab entities:
//   Entities Reforger só replicam dentro do network range do player. Mesmo com
//   RplComponent.InsertToReplication, AI streaming + entity hibernation podem cull entities
//   longe → s_aInstances no client só tem markers próximos → mapa só desenha os near.
//
// Solução: BZ_MapMarkerManager component em GameMode armazena arrays paralelos
// (pos, radius, color packed) replicados como RplProp. Como GameMode component está
// sempre replicado a todos clients, dados chegam global. Modded EE_AreaMarker.Update
// lê desse manager + desenha círculos.

class BZ_MapMarkerManagerClass : ScriptComponentClass {}

class BZ_MapMarkerManager : ScriptComponent
{
	protected static BZ_MapMarkerManager s_Instance;

	[RplProp()]
	protected ref array<vector> m_aPos = new array<vector>();

	[RplProp()]
	protected ref array<float> m_aRadius = new array<float>();

	[RplProp()]
	protected ref array<int> m_aColor = new array<int>();

	static BZ_MapMarkerManager GetInstance()
	{
		return s_Instance;
	}

	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);
		s_Instance = this;
		string side = "CLIENT";
		if (Replication.IsServer())
			side = "SERVER";
		Print("[BrasilZ][MapMarker] Manager EOnInit on " + side, LogLevel.NORMAL);
		if (!Replication.IsServer())
			return;
		GetGame().GetCallqueue().CallLater(BZ_SetupMarkers, 3000, false);
	}

	override void OnPostInit(IEntity owner)
	{
		SetEventMask(owner, EntityEvent.INIT);
	}

	protected void BZ_SetupMarkers()
	{
		// (X Y Z) radius (R G B A) — alpha 0..1
		BZ_AddMarker("5418 299.5 5655",       150, 0.0, 0.4, 1.0, 0.35); // safezone trader hub (azul)
		BZ_AddMarker("4782.941 300 10214.674", 250, 1.0, 0.0, 0.0, 0.35); // NW airfield
		BZ_AddMarker("12042.8 300 12683.5",    250, 1.0, 0.0, 0.0, 0.35); // NE airfield
		BZ_AddMarker("2124.538 300 3368.201",  250, 1.0, 0.0, 0.0, 0.35); // Pavlovo MB
		BZ_AddMarker("4621.095 300 2490.774",  250, 1.0, 0.0, 0.0, 0.35); // Balota airfield
		BZ_AddMarker("4020.303 300 11738.991", 250, 1.0, 0.0, 0.0, 0.35); // Base Militar Norte
		BZ_AddMarker("4530.655 300 8308.429",  250, 1.0, 0.0, 0.0, 0.35); // Pavlovo Norte
		BZ_AddMarker("2474.086 300 5099.498",  250, 1.0, 0.0, 0.0, 0.35); // Base Zeleno
		Replication.BumpMe();
		Print(string.Format("[BrasilZ][MapMarker] Manager BumpMe — %1 markers replicating.", m_aPos.Count()), LogLevel.NORMAL);
	}

	protected void BZ_AddMarker(string posStr, float radius, float r, float g, float b, float a)
	{
		m_aPos.Insert(posStr.ToVector());
		m_aRadius.Insert(radius);
		int packed = ARGBF(a, r, g, b);
		m_aColor.Insert(packed);
	}

	int GetCount() { return m_aPos.Count(); }
	vector GetPos(int i) { return m_aPos[i]; }
	float GetRadius(int i) { return m_aRadius[i]; }
	int GetColor(int i) { return m_aColor[i]; }
}

// Modded EE_AreaMarker display: além dos s_aInstances entities, desenha markers do BZ manager.
modded class EE_AreaMarker
{
	override void Update(float timeSlice)
	{
		super.Update(timeSlice); // desenha markers tradicionais (EE_AreaMarkerPrefab via s_aInstances) primeiro

		BZ_MapMarkerManager mgr = BZ_MapMarkerManager.GetInstance();
		if (!mgr || mgr.GetCount() == 0)
			return;

		if (!m_Canvas)
			return;

		// Append BZ markers ao m_DrawingCommands (super.Update já clear+repopulate com EE markers)
		int count = mgr.GetCount();
		for (int i = 0; i < count; i++)
		{
			PolygonDrawCommand cmd = new PolygonDrawCommand();
			cmd.m_iColor = mgr.GetColor(i);
			cmd.m_Vertices = {};

			int x, y;
			vector pos = mgr.GetPos(i);
			m_MapEntity.WorldToScreen(pos[0], pos[2], x, y, true);

			float radius = mgr.GetRadius(i) * m_MapEntity.GetCurrentZoom();

			for (float angle = 0; angle < Math.PI2; angle += Math.PI2 / m_iCircleVertexCount)
			{
				cmd.m_Vertices.Insert(x + radius * Math.Cos(angle));
				cmd.m_Vertices.Insert(y + radius * Math.Sin(angle));
			}

			m_DrawingCommands.Insert(cmd);
		}

		m_Canvas.SetDrawCommands(m_DrawingCommands);
	}
}
