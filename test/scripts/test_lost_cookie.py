import urllib.request
import re

# Test fetching with all cookies vs session cookie
url = "https://archiveofourown.org/users/carrisa_lyna/works"

req = urllib.request.Request(url, headers={
    "User-Agent": "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
    "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
})

try:
    with urllib.request.urlopen(req) as resp:
        html = resp.read().decode('utf-8')
        print("Response length:", len(html))
        title = re.search(r'<title>(.*?)</title>', html)
        print("Page Title:", title.group(1) if title else "No title")
except Exception as e:
    print("Error:", e)
