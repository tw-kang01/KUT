#pragma once
#include "Engine/DeveloperSettings.h"
#include "KHUTutorSettings.generated.h"

/** Per-user editor preferences. Credentials are deliberately absent. */
UCLASS(config=EditorPerProjectUserSettings, meta=(DisplayName="KHU Tutor"))
class KHUTUTOR_API UKHUTutorSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    virtual FName GetContainerName() const override { return TEXT("Editor"); }
    virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
    virtual FText GetSectionText() const override;
    UPROPERTY(EditAnywhere, config, Category="연결", meta=(DisplayName="ChatKHU Base URL 재정의", ToolTip="비워 두면 Gateway.json 설정을 사용합니다. HTTPS 주소만 허용합니다."))
    FString BaseUrl;
    UPROPERTY(EditAnywhere, config, Category="연결", meta=(DisplayName="Claude 연결 프로그램 위치", ToolTip="자동 검색되지 않을 때 설치된 claude.exe의 전체 경로를 지정합니다. API 키를 넣지 마세요."))
    FString ClaudeExecutable;
    UPROPERTY(EditAnywhere, config, Category="힌트", meta=(DisplayName="기본 힌트 레벨", ClampMin="0", ClampMax="3"))
    int32 DefaultHintLevel = 1;
    UPROPERTY(EditAnywhere, config, Category="안내", meta=(DisplayName="언어 안내 표시"))
    bool bShowLanguageBanner = true;
};
