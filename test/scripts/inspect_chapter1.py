import urllib.request
import re

url = "https://archiveofourown.org/works/83195556?view_full_work=true"
req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
with urllib.request.urlopen(req) as resp:
    html = resp.read().decode("utf-8")

blocks = re.split(r'<div[^>]+id=["\']chapter-\d+["\'][^>]*>', html)
ch1 = blocks[1]
print("CHAPTER 1 BLOCK SNIPPET (first 2000 chars):\n", ch1[:2000])
