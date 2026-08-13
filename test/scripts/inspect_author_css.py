import urllib.request
import re

url = "https://archiveofourown.org/works/83195556?view_full_work=true"
req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
with urllib.request.urlopen(req) as resp:
    html = resp.read().decode("utf-8")

skin_match = re.search(r'<style[^>]*id=["\']workskin["\'][^>]*>(.*?)</style>', html, re.DOTALL)
if skin_match:
    print("=== AUTHOR WORK SKIN CSS ===")
    print(skin_match.group(1).strip())
