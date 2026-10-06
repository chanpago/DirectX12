"""날짜별 학습 노트 페이지(docs/<YYYY-MM-DD>/index.html) 도구.

사용법:
  python tools/build_note.py new 2026-10-08 --title "제목" --desc "한두 줄 요약" --tags "A, B, C"
      tools/note_template.html로 새 노트 뼈대를 만든다.
  python tools/build_note.py build docs/2026-10-08/index.html
      페이지 안의 코드 마커를 실제 코드로 바꾸고(제자리), 사이드바 목차를 장(section) 목록으로 다시 만든다.
      여러 번 실행해도 된다 (이미 바뀐 마커는 남아 있지 않음).

장 형식 (<!--CHAPTERS-END--> 줄 바로 위에 추가):
  <!-- ===================================================================== -->
  <section class="chapter" id="영문-id">
  <h2><span class="num">1</span>장 제목</h2>
  <p class="qa">사용자 질문 (원문 그대로)</p>
  <p>설명 ...</p>
  </section>

코드 마커 (각각 한 줄을 통째로 차지, 경로는 저장소 루트 기준):
  <!--CODE:lang:path:a-b-->       파일의 a~b줄
  <!--FILE:lang:path-->           접을 수 있는 전체 파일
  <!--FILEBODY:lang:path-->       전체 파일 코드 블록만 (<details>는 직접 작성)
  <!--DIFF:path-->                git diff HEAD -- path (커밋 전 변경)
  <!--DIFF:rev:path-->            git show rev -- path (커밋된 변경, 예: a82c855)
  <!--SNIP:lang:title--> ... <!--/SNIP-->   직접 쓴 코드 (HTML 이스케이프만)
  lang: cpp (C++/HLSL), text, diff

그 밖에 쓸 수 있는 블록 (docs/assets/style.css):
  <div class="callout">, <div class="callout warn">, <div class="callout key"> (+ <span class="label">)
  <div class="table-wrap"><table>...</table></div>, <div class="summary-grid"><div class="card">
  <div class="diagram" role="img" aria-label="..."><svg viewBox=...> (클래스 box, box-accent, box-note, box-warn, line, line-accent, dim, mono, strong)
  <figure><img src="img/..."><figcaption>
"""
import argparse
import html
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DOCS = REPO / "docs"
TEMPLATE = Path(__file__).resolve().parent / "note_template.html"
LANG = {"cpp": "cpp", "text": "plaintext", "diff": "diff"}


def read_source(rel):
	text = (REPO / rel).read_text(encoding="utf-8-sig")
	return text.replace("\r\n", "\n").rstrip("\n")


def codeblock(lang, title, body):
	if lang not in LANG:
		sys.exit(f"알 수 없는 lang: {lang} (cpp / text / diff)")
	return (
		'<div class="codeblock"><div class="cb-head"><span>' + html.escape(title) + "</span>"
		'<button class="copy" type="button">복사</button></div>'
		'<pre><code class="language-' + LANG[lang] + '">' + html.escape(body) + "</code></pre></div>"
	)


def git(*args):
	out = subprocess.run(["git", "-c", "core.quotepath=false", *args],
		cwd=REPO, capture_output=True, text=True, encoding="utf-8", check=True).stdout
	return out.replace("\r\n", "\n").replace("﻿", "").rstrip("\n")


def diff_block(spec):
	if ":" in spec and not (REPO / spec).exists():
		rev, rel = spec.split(":", 1)
		out = git("show", "--format=", rev, "--", rel)
		title = f"git show {rev} -- {rel}"
	else:
		out = git("diff", "HEAD", "--", spec)
		title = f"git diff HEAD -- {spec}"
	if not out:
		sys.exit(f"diff가 비어 있음: {spec}")
	return codeblock("diff", title, out)


def expand_markers(src):
	src = re.sub(r"<!--SNIP:(\w+):(.*?)-->\n(.*?)<!--/SNIP-->",
		lambda m: codeblock(m.group(1), m.group(2), m.group(3).strip("\n")), src, flags=re.S)

	def code(m):
		lang, rel, a, b = m.group(1), m.group(2), int(m.group(3)), int(m.group(4))
		lines = read_source(rel).split("\n")
		if not (1 <= a <= b <= len(lines)):
			sys.exit(f"줄 범위 오류: {rel} {a}-{b} (총 {len(lines)}줄)")
		return codeblock(lang, f"{rel}  L{a}–{b}", "\n".join(lines[a - 1:b]))

	src = re.sub(r"<!--CODE:(\w+):([^:>]+):(\d+)-(\d+)-->", code, src)

	def file_full(m):
		lang, rel = m.group(1), m.group(2)
		body = read_source(rel)
		return ('<details class="file"><summary>' + html.escape(rel) + f" ({body.count(chr(10)) + 1}줄)</summary>"
			+ codeblock(lang, rel, body) + "</details>")

	src = re.sub(r"<!--FILE:(\w+):([^>]+?)-->", file_full, src)
	src = re.sub(r"<!--FILEBODY:(\w+):([^>]+?)-->", lambda m: codeblock(m.group(1), m.group(2), read_source(m.group(2))), src)
	src = re.sub(r"<!--DIFF:([^>]+?)-->", lambda m: diff_block(m.group(1)), src)

	left = re.findall(r"<!--(?:SNIP|CODE|FILE|FILEBODY|DIFF):[^>]*-->", src)
	if left:
		sys.exit(f"처리되지 않은 마커: {left}")
	return src


def rebuild_toc(src):
	items = []
	for m in re.finditer(r'<section class="chapter" id="([^"]+)">\s*<h2>(.*?)</h2>', src, re.S):
		sid, h2 = m.group(1), m.group(2)
		num = re.search(r'<span class="num">([^<]*)</span>', h2)
		text = html.unescape(re.sub(r"<[^>]+>", "", re.sub(r'<span class="num">[^<]*</span>', "", h2))).strip()
		label = (num.group(1) + ". " if num else "") + text
		items.append(f'\t\t<li><a href="#{sid}">{html.escape(label)}</a></li>')
	toc = "<ol>\n" + "\n".join(items) + ("\n" if items else "") + "\t</ol>"
	return re.sub(r"(<nav class=\"sidebar\".*?)<ol>.*?</ol>", lambda m: m.group(1) + toc, src, count=1, flags=re.S)


def cmd_new(args):
	if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", args.date):
		sys.exit("날짜 형식: YYYY-MM-DD")
	out = DOCS / args.date / "index.html"
	if out.exists():
		sys.exit(f"이미 있음: {out} (같은 날이면 그 파일에 장을 추가)")
	page = TEMPLATE.read_text(encoding="utf-8")
	for key, val in (("__DATE__", args.date), ("__TITLE__", args.title), ("__DESC__", args.desc), ("__TAGS__", args.tags)):
		page = page.replace(key, html.escape(val, quote=True))
	(out.parent / "img").mkdir(parents=True, exist_ok=True)
	out.write_text(page, encoding="utf-8", newline="\n")
	print(f"created {out.relative_to(REPO)}")


def cmd_build(args):
	path = Path(args.page)
	if not path.is_absolute():
		path = REPO / path
	src = path.read_text(encoding="utf-8")
	src = rebuild_toc(expand_markers(src))
	path.write_text(src, encoding="utf-8", newline="\n")
	print(f"built {path.relative_to(REPO)}")


def main():
	p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	sub = p.add_subparsers(dest="cmd", required=True)
	n = sub.add_parser("new")
	n.add_argument("date")
	n.add_argument("--title", required=True)
	n.add_argument("--desc", default="")
	n.add_argument("--tags", default="")
	n.set_defaults(func=cmd_new)
	b = sub.add_parser("build")
	b.add_argument("page")
	b.set_defaults(func=cmd_build)
	args = p.parse_args()
	args.func(args)


if __name__ == "__main__":
	main()
