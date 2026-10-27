#include "UI/KHUTutorUI.h"
#include "UI/SKHUTutorPanel.h"
#include "UI/KHUTutorStyle.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "ToolMenus.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/App.h"
#include "BlueprintEditor.h"
#include "BlueprintEditorModule.h"
#include "BlueprintEditorTabs.h"
#include "WorkflowOrientedApp/WorkflowTabManager.h"
#include "Framework/Docking/LayoutExtender.h"
#include "LevelEditor.h"

#define LOCTEXT_NAMESPACE "KHUTutorUI"
namespace
{
    const FName BlueprintTutorTab(TEXT("KHUTutorBlueprintTab"));
    class FKHUTutorBlueprintTab final : public FWorkflowTabFactory
    {
    public:
        FKHUTutorBlueprintTab(TSharedPtr<FBlueprintEditor> InEditor, TSharedRef<TArray<TWeakPtr<SKHUTutorPanel>>> InPanels)
            : FWorkflowTabFactory(BlueprintTutorTab, InEditor), Editor(InEditor), Panels(InPanels)
        {
            TabLabel = LOCTEXT("BlueprintTitle", "KHU Tutor"); bIsSingleton = true;
            ViewMenuDescription = TabLabel; ViewMenuTooltip = LOCTEXT("BlueprintTip", "이 Blueprint를 대상으로 튜터에게 질문하기");
            TabIcon = FSlateIcon(TEXT("KHUTutorStyle"), TEXT("Tutor"));
        }
        virtual TSharedRef<SDockTab> SpawnTab(const FWorkflowTabSpawnInfo&) const override
        {
            auto Widget = SNew(SKHUTutorPanel).BlueprintEditor(Editor); Panels->Add(Widget);
            auto Dock = SNew(SDockTab).TabRole(ETabRole::PanelTab).Label(TabLabel)[Widget];
            const TWeakPtr<SKHUTutorPanel> Weak = Widget;
            Dock->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateLambda([Weak](TSharedRef<SDockTab>)
            { if (auto P = Weak.Pin()) { P->Stop(); } }));
            return Dock;
        }
    private:
        TWeakPtr<FBlueprintEditor> Editor;
        TSharedRef<TArray<TWeakPtr<SKHUTutorPanel>>> Panels;
    };
}
void FKHUTutorUI::RegisterBlueprintTabs(FWorkflowAllowedTabSet& Factories, FName, TSharedPtr<FBlueprintEditor> Editor)
{
    Factories.RegisterFactory(MakeShared<FKHUTutorBlueprintTab>(Editor, BlueprintPanels.ToSharedRef()));
    if (Editor) { BlueprintManagers.AddUnique(Editor->GetTabManager()); }
}
void FKHUTutorUI::ExtendBlueprintLayout(FLayoutExtender& Extender)
{
    Extender.ExtendLayout(FBlueprintEditorTabs::DetailsID, ELayoutExtensionPosition::Before,
        FTabManager::FTab(BlueprintTutorTab, ETabState::ClosedTab));
}
void FKHUTutorUI::ExtendLevelLayout(FLayoutExtender& Extender)
{
    Extender.ExtendLayout(LevelEditorTabIds::LevelEditorSelectionDetails, ELayoutExtensionPosition::Before,
        FTabManager::FTab(FName(TEXT("KHUTutorTab")), ETabState::ClosedTab));
}

void FKHUTutorUI::Register()
{
    if (IsRunningCommandlet() || !FApp::CanEverRender() || !FSlateApplication::IsInitialized()) { return; }
    Style = FKHUTutorStyle::Get();
    auto& Level = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
    LevelLayoutHandle = Level.OnRegisterLayoutExtensions().AddRaw(this, &FKHUTutorUI::ExtendLevelLayout);
    BlueprintPanels = MakeShared<TArray<TWeakPtr<SKHUTutorPanel>>>();
    auto& Kismet = FModuleManager::LoadModuleChecked<FBlueprintEditorModule>(TEXT("Kismet"));
    BlueprintTabsHandle = Kismet.OnRegisterTabsForEditor().AddRaw(this, &FKHUTutorUI::RegisterBlueprintTabs);
    BlueprintLayoutHandle = Kismet.OnRegisterLayoutExtensions().AddRaw(this, &FKHUTutorUI::ExtendBlueprintLayout);
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TEXT("KHUTutorTab"), FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs&)
    {
        auto Widget = SNew(SKHUTutorPanel); Panel = Widget;
        auto Dock = SNew(SDockTab).TabRole(ETabRole::NomadTab)[Widget]; Tab = Dock;
        const TWeakPtr<SKHUTutorPanel> Weak = Widget;
        Dock->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateLambda([Weak](TSharedRef<SDockTab>) { if (auto P = Weak.Pin()) { P->Stop(); } }));
        return Dock;
    })).SetDisplayName(LOCTEXT("Title", "KHU Tutor")).SetMenuType(ETabSpawnerMenuType::Hidden);
    MenuHandle = UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FKHUTutorUI::RegisterMenus));
}
void FKHUTutorUI::RegisterMenus()
{
    FToolMenuOwnerScoped Owner(this);
    const FUIAction Action(FExecuteAction::CreateLambda([] { FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("KHUTutorTab"))); }));
    const FSlateIcon Icon(TEXT("KHUTutorStyle"), TEXT("Tutor"));
    UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Window"))->FindOrAddSection(TEXT("WindowLayout"))
        .AddEntry(FToolMenuEntry::InitMenuEntry(TEXT("KHUTutor"), LOCTEXT("Title", "KHU Tutor"), LOCTEXT("Open", "튜터에게 질문하기"), Icon, Action));
    UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.LevelEditorToolBar.User"))->FindOrAddSection(TEXT("KHUTutor"))
        .AddEntry(FToolMenuEntry::InitToolBarButton(TEXT("KHUTutor"), Action, LOCTEXT("Title", "KHU Tutor"), LOCTEXT("Open", "튜터에게 질문하기"), Icon));
}
void FKHUTutorUI::Unregister()
{
    if (auto* Level = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor")))
    { Level->OnRegisterLayoutExtensions().Remove(LevelLayoutHandle); }
    if (auto* Kismet = FModuleManager::GetModulePtr<FBlueprintEditorModule>(TEXT("Kismet")))
    {
        Kismet->OnRegisterTabsForEditor().Remove(BlueprintTabsHandle);
        Kismet->OnRegisterLayoutExtensions().Remove(BlueprintLayoutHandle);
    }
    if (BlueprintPanels) { for (const auto& Weak : *BlueprintPanels) { if (auto P = Weak.Pin()) { P->Stop(); } } }
    for (const auto& Weak : BlueprintManagers)
    {
        if (auto Manager = Weak.Pin())
        {
            if (auto Dock = Manager->FindExistingLiveTab(BlueprintTutorTab)) { Dock->SetContent(SNullWidget::NullWidget); Dock->RequestCloseTab(); }
            Manager->UnregisterTabSpawner(BlueprintTutorTab);
        }
    }
    BlueprintManagers.Reset(); BlueprintPanels.Reset();
    if (auto P = Panel.Pin()) { P->Stop(); }
    if (auto T = Tab.Pin()) { T->SetContent(SNullWidget::NullWidget); T->RequestCloseTab(); }
    UToolMenus::UnRegisterStartupCallback(MenuHandle); UToolMenus::UnregisterOwner(this);
    if (FSlateApplication::IsInitialized()) { FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TEXT("KHUTutorTab")); }
    Panel.Reset(); Tab.Reset(); Style.Reset();
}
#undef LOCTEXT_NAMESPACE
