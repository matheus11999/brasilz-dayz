[EntityEditorProps(category: "BrasilZ/Shops", description: "Editor marker for shop vehicle delivery positions", color: "255 180 0 255")]
class BZ_ShopDeliveryPointClass : SCR_PositionClass
{
}

class BZ_ShopDeliveryPoint : SCR_Position
{
	[Attribute("Delivery Point", UIWidgets.EditBox, "Label shown in Workbench.")]
	protected string m_sDeliveryLabel;

	//------------------------------------------------------------------------------------------------
	string GetDeliveryLabel()
	{
		return m_sDeliveryLabel;
	}
}
