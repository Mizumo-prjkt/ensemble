import urllib.request
import re

url = "https://archiveofourown.org/works/83195556?view_full_work=true"
req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
with urllib.request.urlopen(req) as resp:
    html = resp.read().decode("utf-8")

blocks = re.split(r'<div[^>]+id=["\']chapter-\d+["\'][^>]*>', html)

# Find Chapter 19 block (index 19)
if len(blocks) > 19:
    ch19 = blocks[19]
    print("=== CHAPTER 19 BLOCK FULL SNIPPET (FIRST 3000 CHARS) ===")
    print(ch19[:3000])
else:
    print("Chapter 19 block not found!")
