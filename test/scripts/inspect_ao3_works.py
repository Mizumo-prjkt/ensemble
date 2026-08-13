import urllib.request
import re

url = "https://archiveofourown.org/users/carrisa_lyna/works"
req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
try:
    with urllib.request.urlopen(req) as resp:
        html = resp.read().decode('utf-8')
        print(f"Fetched {len(html)} bytes from {url}")
        
        # Find work IDs
        work_ids = re.findall(r'id=["\']work_(\d+)["\']', html)
        print("Work IDs found:", work_ids)
        
        # Find pseuds on profile / user pages
        pseuds = re.findall(r'/users/carrisa_lyna/pseuds/([^"\'/<]+)', html)
        print("Pseuds found:", set(pseuds))
        
        # Sample work blurb HTML
        match = re.search(r'(<li[^>]+id=["\']work_\d+["\'].*?</li>\s*</li>)', html, re.DOTALL)
        if not match:
            match = re.search(r'(<li[^>]+id=["\']work_\d+["\'].*?)(?=<li[^>]+id=["\']work_|\Z)', html, re.DOTALL)
        if match:
            print("\nSample Work Blurb HTML (first 500 chars):\n", match.group(1)[:500])
except Exception as e:
    print("Error:", e)
