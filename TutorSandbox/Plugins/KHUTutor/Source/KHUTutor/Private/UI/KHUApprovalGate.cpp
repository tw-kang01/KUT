#include "UI/KHUApprovalGate.h"
#include "Misc/MessageDialog.h"
#include "Misc/App.h"
#include "Framework/Application/SlateApplication.h"

bool FKHUApprovalGate::Confirm(const FText& Preview)
{
    if (!IsInGameThread() || IsRunningCommandlet() || FApp::IsUnattended() || !FSlateApplication::IsInitialized() || Preview.IsEmpty()) { return false; }
    return FMessageDialog::Open(EAppMsgType::YesNo, EAppReturnType::No,
        FText::Format(NSLOCTEXT("KHUTutor", "ActPreview", "다음 변경을 실행할까요?\n\n{0}\n\n실행 후 Ctrl+Z로 되돌릴 수 있습니다."), Preview),
        NSLOCTEXT("KHUTutor", "ActTitle", "KHU Tutor — 변경 승인")) == EAppReturnType::Yes;
}
