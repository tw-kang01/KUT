#include "AI/KHUSecretStore.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Containers/StringConv.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include <dpapi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

bool FKHUSecretStore::IsValidKey(const FString& Key)
{
    if (Key.Len() < 8 || Key.Len() > 4096) { return false; }
    for (TCHAR C : Key) { if (!(C >= 'a' && C <= 'z') && !(C >= 'A' && C <= 'Z') && !(C >= '0' && C <= '9') && C != '_' && C != '-') { return false; } }
    return true;
}
FString FKHUSecretStore::Mask(const FString& Key) { return Key.IsEmpty() ? TEXT("[unset]") : TEXT("sk-****") + Key.Right(4); }
FString FKHUSecretStore::Redact(FString Text, const FString& Key)
{ if (!Key.IsEmpty()) { Text.ReplaceInline(*Key, TEXT("[API_KEY_REDACTED]"), ESearchCase::CaseSensitive); } return Text; }
bool FKHUSecretStore::Save(const FString& Key, FString& Error) const
{
    if (!IsValidKey(Key)) { Error = TEXT("API 키 형식이 올바르지 않습니다."); return false; }
#if PLATFORM_WINDOWS
    const FTCHARToUTF8 Utf8(*Key); DATA_BLOB Input{static_cast<DWORD>(Utf8.Length()), reinterpret_cast<BYTE*>(const_cast<ANSICHAR*>(Utf8.Get()))}, Output{};
    if (!CryptProtectData(&Input, L"KHU UE Tutor", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &Output))
    { Error = TEXT("Windows API 키 암호화에 실패했습니다."); return false; }
    TArray<uint8> Bytes; Bytes.Append(Output.pbData, static_cast<int32>(Output.cbData));
    SecureZeroMemory(Output.pbData, Output.cbData); LocalFree(Output.pbData);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    if (!FFileHelper::SaveArrayToFile(Bytes, *Path)) { Error = TEXT("암호화된 API 키 파일을 저장하지 못했습니다."); return false; }
    return true;
#else
    Error = TEXT("Windows DPAPI가 필요합니다."); return false;
#endif
}
bool FKHUSecretStore::Load(FString& Key, FString& Error, bool bAllowEnvironment) const
{
    Key.Reset(); Error.Reset();
    if (IFileManager::Get().FileExists(*Path))
    {
#if PLATFORM_WINDOWS
        TArray<uint8> Bytes;
        if (IFileManager::Get().FileSize(*Path) > 16384 || !FFileHelper::LoadFileToArray(Bytes, *Path) || Bytes.IsEmpty())
        { Error = TEXT("암호화된 API 키 파일이 올바르지 않습니다."); return false; }
        DATA_BLOB Input{static_cast<DWORD>(Bytes.Num()), Bytes.GetData()}, Output{};
        if (!CryptUnprotectData(&Input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &Output))
        { Error = TEXT("API 키를 복호화하지 못했습니다. 같은 Windows 계정인지 확인하세요."); return false; }
        const FUTF8ToTCHAR Decoded(reinterpret_cast<const ANSICHAR*>(Output.pbData), static_cast<int32>(Output.cbData));
        Key = FString(Decoded.Length(), Decoded.Get()); SecureZeroMemory(Output.pbData, Output.cbData); LocalFree(Output.pbData);
#else
        Error = TEXT("Windows DPAPI가 필요합니다."); return false;
#endif
    }
    else if (bAllowEnvironment)
    {
        Key = FPlatformMisc::GetEnvironmentVariable(TEXT("CHATKHU_API_KEY"));
        if (Key.IsEmpty()) { Key = FPlatformMisc::GetEnvironmentVariable(TEXT("BAZE_API_KEY")); }
    }
    if (!IsValidKey(Key)) { Key.Reset(); Error = TEXT("API 키가 없거나 올바르지 않습니다."); return false; }
    return true;
}
bool FKHUSecretStore::Delete(FString& Error) const
{
    if (!IFileManager::Get().FileExists(*Path) || IFileManager::Get().Delete(*Path)) { return true; }
    Error = TEXT("암호화된 API 키 파일을 삭제하지 못했습니다."); return false;
}
