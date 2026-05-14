using System.Windows;

namespace REAssetExplorer.TestUI;

public enum HierarchyItemType
{
    GameObject,
    Prefab,
    Folder,
    Scene,
    Mesh,
}

public static class HierarchyItemProperties
{
    public static readonly DependencyProperty IsSelectedProperty =
        DependencyProperty.RegisterAttached(
            "IsSelected",
            typeof(bool),
            typeof(HierarchyItemProperties),
            new PropertyMetadata(false));

    public static bool GetIsSelected(DependencyObject obj) => (bool)obj.GetValue(IsSelectedProperty);
    public static void SetIsSelected(DependencyObject obj, bool value) => obj.SetValue(IsSelectedProperty, value);

    public static readonly DependencyProperty ItemTypeProperty =
        DependencyProperty.RegisterAttached(
            "ItemType",
            typeof(HierarchyItemType),
            typeof(HierarchyItemProperties),
            new PropertyMetadata(HierarchyItemType.GameObject));

    public static HierarchyItemType GetItemType(DependencyObject obj) => (HierarchyItemType)obj.GetValue(ItemTypeProperty);
    public static void SetItemType(DependencyObject obj, HierarchyItemType value) => obj.SetValue(ItemTypeProperty, value);
}
