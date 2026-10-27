#include "UI/KHUFeedbackStore.h"
#include "Mcp/KHUMcpProtocol.h"
#include "Dom/JsonObject.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

FKHUFeedbackStore::FKHUFeedbackStore(FString InPath) : Path(MoveTemp(InPath))
{
    FString Text;
    if (!FPaths::FileExists(Path)) { return; }
    TSharedPtr<FJsonObject> Root; const TArray<TSharedPtr<FJsonValue>>* Items = nullptr; double Version = 0;
    if (!FFileHelper::LoadFileToString(Text, *Path) || !KHUMcpProtocol::ReadObject(Text, Root)
        || !Root->TryGetNumberField(TEXT("version"), Version) || Version != 1 || !Root->TryGetArrayField(TEXT("entries"), Items))
    { bDamaged = true; LastError = TEXT("피드백 기록 파일을 읽지 못해 새 선택을 저장하지 않았어요. 원본은 그대로 두었어요."); return; }
    for (const auto& Value : *Items)
    {
        const TSharedPtr<FJsonObject>* O = nullptr; FEntry E; double Hint = 0;
        if (!Value->TryGetObject(O) || !(*O)->TryGetStringField(TEXT("id"), E.Id) || !(*O)->TryGetStringField(TEXT("rating"), E.Rating)
            || (E.Rating != TEXT("up") && E.Rating != TEXT("down")) || !(*O)->TryGetNumberField(TEXT("hint"), Hint))
        { bDamaged = true; LastError = TEXT("피드백 기록 파일 형식이 맞지 않아 새 선택을 저장하지 않았어요. 원본은 그대로 두었어요."); Entries.Reset(); return; }
        (*O)->TryGetStringField(TEXT("conversation"), E.Conversation); (*O)->TryGetStringField(TEXT("updated_utc"), E.UpdatedUtc);
        E.Hint = FMath::Clamp(static_cast<int32>(Hint), 0, 3); Entries.Add(MoveTemp(E));
    }
}
FString FKHUFeedbackStore::AnswerId(const FString& Conversation, const FString& Text)
{
    return (Conversation.IsEmpty() ? FString(TEXT("local")) : Conversation) + FString::Printf(TEXT(":%08x:%d"), FCrc::StrCrc32(*Text), Text.Len());
}
FString FKHUFeedbackStore::Get(const FString& Id) const
{
    const FEntry* Found = Entries.FindByPredicate([&Id](const FEntry& E) { return E.Id == Id; });
    return Found ? Found->Rating : FString();
}
bool FKHUFeedbackStore::Toggle(const FString& Id, const FString& Rating, const FString& Conversation, int32 Hint, FString& OutRating)
{
    OutRating = Get(Id);
    if (bDamaged) { return false; }
    if (Id.IsEmpty() || (Rating != TEXT("up") && Rating != TEXT("down"))) { LastError = TEXT("알 수 없는 피드백 선택이에요."); return false; }
    TArray<FEntry> Next = Entries;
    Next.RemoveAll([&Id](const FEntry& E) { return E.Id == Id; });
    FString Result;
    if (OutRating != Rating)
    {
        FEntry E; E.Id = Id; E.Rating = Rating; E.Conversation = Conversation; E.Hint = FMath::Clamp(Hint, 0, 3);
        E.UpdatedUtc = FDateTime::UtcNow().ToIso8601(); Next.Add(MoveTemp(E)); Result = Rating;
    }
    if (Next.Num() > MaxEntries) { Next.RemoveAt(0, Next.Num() - MaxEntries); }
    if (!Save(Next)) { return false; }
    Entries = MoveTemp(Next); OutRating = Result; return true;
}
bool FKHUFeedbackStore::Save(const TArray<FEntry>& Next)
{
    auto Root = MakeShared<FJsonObject>(); Root->SetNumberField(TEXT("version"), 1);
    TArray<TSharedPtr<FJsonValue>> Items;
    for (const auto& E : Next)
    {
        auto O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("id"), E.Id); O->SetStringField(TEXT("rating"), E.Rating);
        O->SetStringField(TEXT("conversation"), E.Conversation); O->SetNumberField(TEXT("hint"), E.Hint); O->SetStringField(TEXT("updated_utc"), E.UpdatedUtc);
        Items.Add(MakeShared<FJsonValueObject>(O));
    }
    Root->SetArrayField(TEXT("entries"), Items);
    const FString Temp = Path + TEXT(".tmp");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    if (!FFileHelper::SaveStringToFile(KHUMcpProtocol::WriteObject(Root), *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
        || !IFileManager::Get().Move(*Path, *Temp, true, true))
    { IFileManager::Get().Delete(*Temp, false, true, true); LastError = TEXT("피드백을 저장하지 못했어요. 이전 선택을 유지했어요."); return false; }
    LastError.Reset(); return true;
}
