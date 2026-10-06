# DirectX12 학습 프로젝트

DirectX 12 + Dear ImGui로 렌더링 파이프라인을 직접 만들며 공부하는 연습 프로젝트. 여러 기기(데스크톱, 노트북)에서 같은 저장소를 clone해서 작업한다. 기기별 메모리는 공유되지 않으므로, 모든 기기에 공통으로 필요한 규칙은 이 파일에 둔다.

## 작업 방식

- 사용자는 한국어로 질문한다. 답변도 한국어로.
- 설명은 정확한 코드 위치(`main.cpp:182`)와 연결해서 한다. 이미 다룬 개념은 처음부터 다시 설명하지 말고 이어서 설명한다(지난 노트: `docs/`).
- 사용자가 직접 코드를 치면서 배우는 프로젝트다. 렌더링 파이프라인 코드(루트 시그니처, 셰이더, PSO, 버퍼, 드로우)는 사용자가 명시적으로 요청할 때만 작성한다.
- 사용자가 "확인해 볼래?" / "봐 줄래?"라고 하면: 저장된 파일을 읽고 → 빌드하고 → 실행해 보고, 문제마다 줄 번호와 이유를 짚는다. 직접 고치는 건 사용자가 맡길 때만("그것들은 너가 해줘"). 코드가 안 보이면 VS에서 저장(Ctrl+S)을 안 한 것일 수 있다.
- 실행 확인은 띄우고, 살아 있는지 보고, 스크린샷까지만 한다. **키보드 / 마우스 입력을 합성해서 보내지 않는다**(SendInput 등). 포커스가 다른 창(게임 등)에 있으면 그쪽으로 입력이 간다. 조작이 필요한 기능은 사용자에게 직접 테스트를 부탁한다.
- 코드 변경은 사용자가 요청할 때만 커밋한다. 메시지는 `feat:` / `fix:` / `chore:` / `docs:` 접두어 + 한국어 본문.

## 빌드 / 실행

```
& "<VS 설치 경로>\MSBuild\Current\Bin\MSBuild.exe" DirectX12.sln /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal
```
- 데스크톱: `C:\Program Files\Microsoft Visual Studio\2022\Community`
- 다른 기기에서는 경로가 다를 수 있다: `& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -find MSBuild\**\Bin\MSBuild.exe`
- 실행 파일: `x64\Debug\DirectX12.exe` (셰이더는 빌드 때 FXC로 컴파일되어 헤더로 들어감)

## 학습 노트 자동 업로드 (상시 규칙)

사용자가 DirectX 12 학습 질문(개념, "X는 어떻게 동작해?", "Y는 코드 어디 있어?", 코드 설명, 구현 과정)을 하면, 채팅으로 답한 뒤 **확인을 묻지 않고** 그날의 학습 노트에 추가해 GitHub Pages에 올린다. 어느 기기에서든 같다. (2026-10-07 사용자 상시 허락)

### 절차

0. 노트를 고치기 전에 다른 기기에서 올린 내용을 받는다: `git pull --rebase origin main` (작업 중인 코드 변경이 있으면 `--autostash`).
1. 오늘(로컬 날짜) 파일 `docs/<YYYY-MM-DD>/index.html`. 없으면 만든다. 다른 기기에서 이미 만들었으면 거기에 이어 쓴다.
   ```
   python tools/build_note.py new <YYYY-MM-DD> --title "제목" --desc "한두 줄 요약" --tags "A, B, C"
   ```
   하루 동안 주제가 늘어나면 `<head>`의 `note-title` / `description` / `note-tags` 메타, `<h1>`, 리드 문단도 함께 갱신한다.
2. `<!--CHAPTERS-END-->` 줄 바로 위에 장(`<section class="chapter" id="...">`)을 추가한다.
   - 사용자 질문은 원문 그대로 `<p class="qa">`에 넣는다.
   - 설명은 자세하게. 표, callout, 필요하면 SVG 다이어그램. 작은 버그 수정 과정보다 개념 위주로.
   - 이어지는 후속 질문은 새 장 대신 같은 장의 `<h3>` 소제목으로 묶는다.
   - 코드는 기억으로 옮겨 적지 않고 마커로 실제 파일 / git에서 가져온다 (`CODE`, `FILE`, `FILEBODY`, `DIFF`, `SNIP`). 형식과 쓸 수 있는 블록은 `tools/build_note.py` 독스트링 참고.
3. 빌드:
   ```
   python tools/build_note.py build docs/<YYYY-MM-DD>/index.html
   python tools/build_notes_index.py
   ```
4. `docs/`만 커밋(`docs: <날짜> 노트 - <주제>`)하고 `git push origin main`.
   - push가 거절되면(다른 기기가 먼저 올림) `git pull --rebase origin main` 후 다시 push. `docs/index.html` 충돌은 `python tools/build_notes_index.py`로 다시 만들어서 해결한다.
   - GitHub Actions(`.github/workflows/pages.yml`)가 목록을 다시 만들고 1분 안에 배포한다.
5. 채팅 답변 끝에 한 줄 + 노트 링크: `https://chanpago.github.io/DirectX12/<YYYY-MM-DD>/`
   push가 실패하면 조용히 넘어가지 말고 답변에 그 사실을 적는다.

### 올리지 않는 것

- 잡담, 작업 요청(커밋 / 빌드 / 설정 변경 등), 민감한 내용
- 사용자가 "이건 올리지 마" / "노트 자동 업로드 그만"이라고 하면 따른다.
- 저장소가 public이므로 노트 내용은 공개된다.

### 구조

- `docs/<날짜>/index.html`: 날짜별 노트, 이미지는 `docs/<날짜>/img/`
- `docs/index.html`: 목록. `tools/build_notes_index.py`가 자동 생성하므로 직접 고치지 않는다.
- `docs/assets/style.css`: 공용 스타일
- `tools/note_template.html`: 새 노트 뼈대
- 도구는 Python 3 표준 라이브러리만 쓴다(`python`이 없으면 `py`).
- GitHub API가 필요하면 `gh`가 있으면 쓰고, 없으면 `git credential fill`로 저장된 토큰을 쓴다. 토큰은 출력하지 않는다.
