import urllib.request
import re

url = "https://archiveofourown.org/works/83195556?view_full_work=true"
req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
with urllib.request.urlopen(req) as resp:
    html = resp.read().decode("utf-8")

# 1. Work Title
t_match = re.search(r'<h2 class="title heading">\s*([^<]+)\s*</h2>', html)
work_title = t_match.group(1).strip() if t_match else "Unknown Title"
print("Work Title:", work_title)

# 2. Work Skin CSS
skin_match = re.search(r'<style[^>]*id=["\']workskin["\'][^>]*>(.*?)</style>', html, re.DOTALL)
if not skin_match:
    skin_match = re.search(r'<style[^>]*>(.*?)</style>', html, re.DOTALL)
skin_css = skin_match.group(1).strip() if skin_match else ""
print(f"Work Skin CSS length: {len(skin_css)} bytes")

# 3. Chapter parsing
chapter_blocks = re.split(r'<div[^>]+id=["\']chapter-\d+["\'][^>]*>', html)
chapters = []

def extract_body(block):
    idx = block.find('role="article"')
    if idx == -1:
        idx = block.find('class="userstuff')
    if idx == -1:
        return ""
    start_pos = block.find('>', idx) + 1

    end_pos = len(block)
    for term in ['<div class="chapter preface', '<div id="work_endnotes"', '<div id="feedback"', '<!--chapter end-->', '<!--/chapter-->']:
        pos = block.find(term, start_pos)
        if pos != -1 and pos < end_pos:
            end_pos = pos

    body = block[start_pos:end_pos].strip()
    # Strip landmark heading if present
    body = re.sub(r'^\s*<h3[^>]*class=["\']landmark[^"\'\w]*heading["\'][^>]*>.*?</h3>', '', body, flags=re.DOTALL).strip()
    return body

if len(chapter_blocks) > 1:
    for idx, block in enumerate(chapter_blocks[1:], 1):
        # Title
        title_match = re.search(r'<h3[^>]*class=["\']title["\'][^>]*>\s*<a[^>]*>[^<]+</a>:?\s*(.*?)\s*</h3>', block, re.DOTALL)
        if title_match:
            ch_title = re.sub(r'<[^>]+>', '', title_match.group(1)).strip()
        else:
            ch_title = f"Chapter {idx}"
        if not ch_title:
            ch_title = f"Chapter {idx}"

        body = extract_body(block)
        chapters.append((ch_title, len(body)))

print(f"\nParsed {len(chapters)} chapters:")
for idx, (title, length) in enumerate(chapters, 1):
    print(f"  [{idx:02d}] {title} ({length} bytes)")
