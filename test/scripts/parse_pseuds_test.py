import urllib.request
import re

url = "https://archiveofourown.org/users/carrisa_lyna/pseuds"
req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
with urllib.request.urlopen(req) as resp:
    html = resp.read().decode("utf-8")

matches = re.findall(r'<a\s+href=["\']/users/[^/]+/pseuds/([^"\'/>]+)["\'][^>]*>([^<]+)</a>', html)
seen = set()
pseuds = []
for slug, name in matches:
    name = name.strip()
    if name and name not in seen and "edit" not in name.lower() and "new" not in name.lower():
        seen.add(name)
        pseuds.append(name)

print("Parsed pseuds:", pseuds)
