#pragma once
#include "CoreMinimal.h"
class FBlueprintEditor;
class FWorkflowAllowedTabSet;
class FLayoutExtender;
class FTabManager;
class SDockTab;
class SKHUTutorPanel;
class FSlateStyleSet;

/** Owns menu registration, the dock tab, and module-safe UI teardown. */
class FKHUTutorUI
{
public:
    void Register();
    void Unregister();
private:
    void RegisterMenus();
    void RegisterBlueprintTabs(FWorkflowAllowedTabSet& Factories, FName Mode, TSharedPtr<FBlueprintEditor> Editor);
    void ExtendBlueprintLayout(FLayoutExtender& Extender);
    void ExtendLevelLayout(FLayoutExtender& Extender);
    TSharedPtr<TArray<TWeakPtr<SKHUTutorPanel>>> BlueprintPanels;
    TArray<TWeakPtr<FTabManager>> BlueprintManagers;
    FDelegateHandle BlueprintTabsHandle, BlueprintLayoutHandle;
    FDelegateHandle LevelLayoutHandle;
    TSharedPtr<FSlateStyleSet> Style;
    TWeakPtr<SDockTab> Tab;
    TWeakPtr<SKHUTutorPanel> Panel;
    FDelegateHandle MenuHandle;
};
