# DirectX12 학습 프로젝트

DirectX 12 + Dear ImGui로 렌더링 파이프라인을 직접 만들며 공부하는 연습 프로젝트. 사용자는 한국어로 질문하고, 답변도 한국어로 한다.

- 빌드: `& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" DirectX12.sln /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal`
- 실행 파일: `x64\Debug\DirectX12.exe`
- 코드 변경은 사용자가 요청할 때만 커밋한다. 커밋 메시지는 `feat:` / `fix:` / `chore:` / `docs:` 접두어 + 한국어 본문.

## 학습 노트 자동 업로드 (상시 규칙)

사용자가 DirectX 12 학습 질문(개념, "X는 어떻게 동작해?", "Y는 코드 어디 있어?", 코드 설명, 구현 과정)을 하면, 채팅으로 답한 뒤 **확인을 묻지 않고** 그날의 학습 노트에 추가해 GitHub Pages에 올린다. (2026-10-07 사용자 상시 허락)

### 절차

1. 오늘(로컬 날짜) 파일 `docs/<YYYY-MM-DD>/index.html`. 없으면 만든다.
   ```
   python tools/build_note.py new <YYYY-MM-DD> --title "제목" --desc "한두 줄 요약" --tags "A, B, C"
   ```
   하루 동안 주제가 늘어나면 `<head>`의 `note-title` / `description` / `note-tags` 메타, `<h1>`, 리드 문단도 함께 갱신한다.
2. `<!--CHAPTERS-END-->` 줄 바로 위에 장(`<section class="chapter" id="...">`)을 추가한다.
   - 사용자 질문은 원문 그대로 `<p class="qa">`에 넣는다.
   - 설명은 자세하게. 표, callout, 필요하면 SVG 다이어그램.
   - 이어지는 후속 질문은 새 장 대신 같은 장의 `<h3>` 소제목으로 묶는다.
   - 코드는 기억으로 옮겨 적지 않고 마커로 실제 파일 / git에서 가져온다 (`CODE`, `FILE`, `FILEBODY`, `DIFF`, `SNIP`). 형식과 쓸 수 있는 블록은 `tools/build_note.py` 독스트링 참고.
3. 빌드:
   ```
   python tools/build_note.py build docs/<YYYY-MM-DD>/index.html
   python tools/build_notes_index.py
   ```
4. `docs/`만 커밋(`docs: <날짜> 노트 - <주제>`)하고 `main`에 push. GitHub Actions(`.github/workflows/pages.yml`)가 목록을 다시 만들고 1분 안에 배포한다.
5. 채팅 답변 끝에 한 줄 + 노트 링크: `https://chanpago.github.io/DirectX12/<YYYY-MM-DD>/`

### 올리지 않는 것

- 잡담, 작업 요청(커밋 / 빌드 / 설정 변경 등), 민감한 내용
- 사용자가 "이건 올리지 마" / "노트 자동 업로드 그만"이라고 하면 따른다.
- 저장소가 public이므로 노트 내용은 공개된다.

### 구조

- `docs/<날짜>/index.html`: 날짜별 노트, 이미지는 `docs/<날짜>/img/`
- `docs/index.html`: 목록. `tools/build_notes_index.py`가 자동 생성하므로 직접 고치지 않는다.
- `docs/assets/style.css`: 공용 스타일
- `tools/note_template.html`: 새 노트 뼈대
- GitHub CLI(`gh`)는 없다. GitHub API가 필요하면 `git credential fill`로 저장된 토큰을 쓰고, 토큰은 출력하지 않는다.
