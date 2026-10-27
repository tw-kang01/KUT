# KHU UE Tutor (KUT)

게임엔진기초 수업용 Unreal Engine 5.8 (Win64) 에디터 플러그인. 학생이 에디터 안에서 Blueprint 그래프, 컴파일 오류, 출력 로그, C++ 빌드 오류를 근거로 단계별 힌트(L0~L3)를 받는다. 모델 호출은 학생 개인의 ChatKHU 키로 Claude Code CLI를 통해 이루어지고, 에디터 도구는 읽기·표시 전용이다. 에디터를 대신 조작하거나 코드를 고쳐 주지는 않는다.

```mermaid
flowchart LR
    S[학생 질문 + 선택 노드/그래프 요약] --> P[KHU Tutor 패널]
    P --> L[FKHUAgentLoop<br/>힌트 정책·문맥 예산·기록]
    L --> C[claude.exe<br/>ChatKHU 게이트웨이]
    C -->|tools/call| M[플러그인 루프백 MCP<br/>127.0.0.1]
    M --> T[읽기 도구 9개 · 표시 도구 3개<br/>로그·그래프·컴파일·소스·액터]
    T --> M --> C --> L --> P
    L --> R[(Saved/KHUTutor<br/>대화·학습 기록·키)]
```

## 저장소 구성

```
TutorSandbox/                      UE 5.8 호스트 프로젝트(최소 게임 모듈 + 테스트용 Blueprint 9개)
  Plugins/KHUTutor/
    Source/KHUTutorTools/          센서·다이제스트·AICallable 툴셋 (Editor 모듈)
    Source/KHUTutor/               설정·에이전트 루프·Claude 런타임·MCP·Slate UI (Editor 모듈)
    Config/                        모델·단가·예산·문맥·도구 화이트리스트·프롬프트
    Resources/                     아이콘 SVG
  Content/KHUTest/                 오류를 일부러 넣은 테스트 Blueprint 9개
templates/Config/                  학생 배포 패키지에 적용하는 설정 (반복 상한·예산)
scripts/                           빌드·테스트·패키징·패키지 감사 PowerShell
fixtures/                          자동화 테스트가 읽는 로그·응답 샘플
docs/                              설치 안내
```

## 요구 사항

- Windows 10/11 x64, Unreal Engine 5.8 (5.8.4에서 빌드·테스트), 엔진 내장 플러그인 `ToolsetRegistry`. 제품 경로에는 `ModelContextProtocol` 플러그인이 필요 없다.
- Visual Studio 2022 또는 2026, C++ 게임 개발 워크로드 (`TutorSandbox/.vsconfig` 참고).
- 실제 질문을 보내려면 개인 ChatKHU API 키와 로컬에 설치한 Claude Code CLI(`claude.exe`)가 필요하다. 실행 파일 위치는 `Config/ClaudeRuntime.json`의 `executable` 또는 에디터 설정 *Plugins → KHU Tutor* 에서 지정한다. 키는 패널 설정 화면에서 입력하며 Windows DPAPI로 `Saved/KHUTutor/api_key.dpapi`에 저장된다.

## 빌드·테스트·패키징

```powershell
# 에디터 타깃 빌드 (TutorSandbox 에디터는 닫은 상태)
.\scripts\build.ps1 -EditorClosed

# 헤드리스 자동화 테스트 178개, 모델 호출 없음
.\scripts\test.ps1 -NoMcp

# 학생 배포 후보 패키지 → dist/, 감사와 빈 프로젝트 헤드리스 로드
.\scripts\package.ps1
.\scripts\smoke_package.ps1 -Package .\dist\<후보 폴더>
.\scripts\test_package.ps1 -Package .\dist\<후보 폴더>   # 감사가 잘못된 설정을 거부하는지 확인
```

`UE_ROOT`와 `KHU_PROJECT` 환경변수를 지정하지 않으면 `C:\Program Files\Epic Games\UE_5.8`과 저장소 안의 `TutorSandbox.uproject`를 사용한다. `test.ps1`의 옵트인 스위치(`-ClaudeLive`, `-CoachLive`, `-D7Live` 등)는 실제 모델을 호출해 크레딧을 쓴다.

## 설정 파일

| 파일 | 내용 |
|---|---|
| `Config/Models.json` | 티어별 모델 체인·요청 반복 상한·입력 토큰 예산·API 종류. 학생은 `T1`. 개발 검증용 `D7`/`H1`은 패키징 시 제거된다. |
| `Config/Pricing.json` | 모델별 USD/1M 토큰 단가와 크레딧 환산 |
| `Config/Budget.json` | 작업당 soft/hard 크레딧 예산. 학생 값은 `templates/Config/Budget.json` |
| `Config/Context.json` | 요청 바이트 상한·토큰 추정 비율·문맥 절단·프로젝트 기억 상한 |
| `Config/Gateway.json` | ChatKHU 주소·출력 토큰 상한·도구 결과 토큰 상한·타임아웃 |
| `Config/ToolWhitelist.json` | 모델에 노출하는 읽기/표시 도구와 금지 패턴 |
| `Config/ClaudeRuntime.json` | 패널의 Claude Code 사용 여부·티어·타임아웃·캐시 TTL·실행 파일 |
| `Config/DevelopmentBudget.json` | 개발 검증용 유료 호출 허용. 학생 패키지에서는 비활성으로 덮어쓴다 |
| `Config/Prompts/system.txt`, `l0_contract.txt` | 튜터 지침과 L0 답변 규칙 |
| `Config/LogDigest.json`, `ErrorPatterns.json`, `Mcp.json`, `Scenarios/` | 로그 노이즈 카테고리, 오류 패턴, 개발용 MCP 주소, Mock 시나리오 |

모델 ID·단가·예산은 코드에 넣지 않고 전부 이 파일에서 읽는다. 파일이 없거나 값이 범위를 벗어나면 설정은 무효가 되어 유료 요청이 차단되고 Mock만 동작한다.

## 사용 흐름

1. TutorSandbox(또는 플러그인을 설치한 프로젝트)를 열고 `Window → KHU Tutor` 또는 Blueprint Editor의 같은 메뉴로 패널을 연다.
2. 설정 화면에서 ChatKHU 키를 저장하고 연결을 확인한다.
3. 도움 수준을 고른다: L0 질문으로 생각하기 · L1 위치 힌트 · L2 부분 단계 · L3 전체 단계. L4 변경 대행은 없다.
4. 질문을 보내면 패널이 선택 노드 또는 현재 Graph 요약을 읽어 함께 전달하고, 모델이 읽기 도구를 호출한 영수증이 답변 아래 카드로 남는다.
5. 대화·학습 기록·피드백은 프로젝트의 `Saved/KHUTutor/`에만 저장된다.

## 제한

- 자동화 테스트 178개는 프로토콜·루프·예산·기록·다이제스트·센서의 회귀 테스트이고, 답변 품질이나 학습 효과를 재지 않는다.
- 에디터 GUI와 실제 claude.exe 호출은 자동화로 확인하지 않는다.
- Win64 전용(DPAPI, Windows 프로세스/파이프 API). 학생 배포 방식(Claude Code 제공)은 [docs/INSTALL_KO.md](docs/INSTALL_KO.md) 참고.

## 문서

- [docs/INSTALL_KO.md](docs/INSTALL_KO.md) 후보 패키지 설치와 오류 대응
- [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 외부 구성 요소와 상표 고지

## 라이선스

KHU 자체 코드의 공개 라이선스는 아직 정하지 않았다(LICENSE 파일 없음). 사용·재배포 조건은 저장소 소유자에게 문의한다.
