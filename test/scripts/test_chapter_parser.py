import urllib.request
import re

url = "https://archiveofourown.org/works/83195556?view_full_work=true"
req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
with urllib.request.urlopen(req) as resp:
    html = resp.read().decode("utf-8")

# Look for chapter blocks
blocks = re.split(r'<div[^>]+id=["\']chapter-\d+["\'][^>]*>', html)
print(f"Split html into {len(blocks)-1} chapter blocks!")

for idx, ch in enumerate(blocks[1:], 1):
    title_match = re.search(r'<h3[^>]*class=["\']title["\'][^>]*>(.*?)</h3>', ch, re.DOTALL)
    title = re.sub(r'<[^>]+>', '', title_match.group(1)).strip() if title_match else f"Chapter {idx}"
    body_match = re.search(r'<div[^>]+class=["\']userstuff[^"\'\w]*["\'][^>]*>(.*?)</div>', ch, re.DOTALL)
    body_len = len(body_match.group(1)) if body_match else 0
    print(f" Chapter {idx}: Title=\"{title}\" BodyLength={body_len} bytes")
