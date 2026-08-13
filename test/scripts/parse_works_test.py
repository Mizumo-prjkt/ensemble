import urllib.request
import re

url = "https://archiveofourown.org/users/carrisa_lyna/works"
req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
with urllib.request.urlopen(req) as resp:
    html = resp.read().decode("utf-8")

# Split HTML by work ID blocks
blocks = re.split(r'<li\s+id=["\']work_(\d+)["\']', html)

works = []
for i in range(1, len(blocks), 2):
    work_id = blocks[i]
    block = blocks[i+1]

    # Title
    t_match = re.search(r'<a\s+href=["\']/works/' + work_id + r'["\'][^>]*>([^<]+)</a>', block)
    title = t_match.group(1).strip() if t_match else f"Work {work_id}"

    # Pseud / Authors
    authors = re.findall(r'rel=["\']author["\'][^>]*>([^<]+)</a>', block)
    pseud = ", ".join(authors) if authors else "Unknown"

    # Fandom
    fandoms = re.findall(r'<h5\s+class=["\']fandoms heading["\'][^>]*>(.*?)</h5>', block, re.DOTALL)
    fandom_list = []
    if fandoms:
        fandom_list = re.findall(r'<a\s+class=["\']tag["\'][^>]*>([^<]+)</a>', fandoms[0])
    fandom = ", ".join(fandom_list) if fandom_list else "Unspecified"

    # Words
    w_match = re.search(r'<dd\s+class=["\']words["\']>([0-9,]+)</dd>', block)
    words = int(w_match.group(1).replace(",", "")) if w_match else 0

    # Chapters
    c_match = re.search(r'<dd\s+class=["\']chapters["\']>(?:<a[^>]*>)?(\d+)(?:</a>)?/(\d+|\?)', block)
    chapters = c_match.group(1) if c_match else "1"
    total_chap = c_match.group(2) if c_match else "1"

    works.append((work_id, title, pseud, fandom, words, f"{chapters}/{total_chap}"))

print(f"Successfully parsed {len(works)} works for carrisa_lyna:")
for w in works:
    print("  ", w)
