"""docs/ 아래 날짜 폴더(YYYY-MM-DD)를 훑어서 docs/index.html (노트 목록)을 만든다.

각 노트 index.html의 <head>에서 읽는 값 (없으면 다음 값으로 대체):
  제목: <meta name="note-title">  →  첫 <h1>  →  <title>
  설명: <meta name="description">
  태그: <meta name="note-tags" content="A, B, C">

GitHub Actions(.github/workflows/pages.yml)가 push할 때마다 실행한다.
로컬에서 미리 보려면: python tools/build_notes_index.py
"""
import html
import re
from pathlib import Path

DOCS = Path(__file__).resolve().parent.parent / "docs"
DATE_DIR = re.compile(r"^\d{4}-\d{2}-\d{2}$")


def meta(src, name):
	m = re.search(r'<meta\s+name="' + re.escape(name) + r'"\s+content="([^"]*)"', src)
	return html.unescape(m.group(1)).strip() if m else ""


def strip_tags(s):
	return html.unescape(re.sub(r"<[^>]+>", "", s)).strip()


def read_note(folder):
	src = (folder / "index.html").read_text(encoding="utf-8-sig")
	title = meta(src, "note-title")
	if not title:
		m = re.search(r"<h1[^>]*>(.*?)</h1>", src, re.S)
		title = strip_tags(m.group(1)) if m else ""
	if not title:
		m = re.search(r"<title>(.*?)</title>", src, re.S)
		title = strip_tags(m.group(1)) if m else folder.name
	tags = [t.strip() for t in meta(src, "note-tags").split(",") if t.strip()]
	return {"date": folder.name, "title": title, "desc": meta(src, "description"), "tags": tags}


def render(notes):
	cards = []
	for n in notes:
		tags = "".join("<span>" + html.escape(t) + "</span>" for t in n["tags"])
		cards.append(
			'\t<a class="entry" href="' + n["date"] + '/">\n'
			'\t\t<div class="d">' + n["date"] + "</div>\n"
			'\t\t<div class="t">' + html.escape(n["title"]) + "</div>\n"
			+ ('\t\t<div class="s">' + html.escape(n["desc"]) + "</div>\n" if n["desc"] else "")
			+ ('\t\t<div class="tags">' + tags + "</div>\n" if tags else "")
			+ "\t</a>"
		)
	body = "\n\n".join(cards) if cards else '\t<p class="lead">아직 노트가 없습니다.</p>'
	return f"""<!doctype html>
<html lang="ko">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>DirectX 12 학습 노트</title>
<meta name="description" content="DirectX 12 렌더링 파이프라인을 직접 만들면서 정리한 날짜별 학습 노트">
<link rel="stylesheet" href="https://cdn.jsdelivr.net/gh/orioncactus/pretendard@v1.3.9/dist/web/variable/pretendardvariable-dynamic-subset.min.css">
<link rel="stylesheet" href="assets/style.css">
</head>
<body>
<!-- 이 파일은 tools/build_notes_index.py가 자동 생성함. 직접 고치지 말 것 -->
<div class="index-wrap">
	<div class="eyebrow">DirectX 12 + Dear ImGui</div>
	<h1>DirectX 12 학습 노트</h1>
	<p class="lead">렌더링 파이프라인을 직접 만들면서 공부한 내용을 날짜별로 정리한다. 각 노트의 코드는 그날 저장소의 실제 파일에서 가져왔다. ({len(notes)}개)</p>

{body}
</div>
</body>
</html>
"""


def main():
	folders = sorted((p for p in DOCS.iterdir() if p.is_dir() and DATE_DIR.match(p.name) and (p / "index.html").exists()),
		key=lambda p: p.name, reverse=True)
	notes = [read_note(f) for f in folders]
	(DOCS / "index.html").write_text(render(notes), encoding="utf-8", newline="\n")
	print(f"docs/index.html: {len(notes)} notes ({', '.join(n['date'] for n in notes)})")


if __name__ == "__main__":
	main()
