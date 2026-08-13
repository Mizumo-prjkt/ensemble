import urllib.request
import re

url = "https://archiveofourown.org/works/83195556?view_full_work=true"
req = urllib.request.Request(url, headers={
    "User-Agent": "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
    "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
})

try:
    with urllib.request.urlopen(req) as resp:
        html = resp.read().decode('utf-8')
        print(f"Fetched full work HTML ({len(html)} bytes)")

        # Title
        t_match = re.search(r'<h2 class="title heading">\s*([^<]+)\s*</h2>', html)
        if not t_match:
            t_match = re.search(r'<h2 class="heading">\s*([^<]+)\s*</h2>', html)
        print("Work Title:", t_match.group(1).strip() if t_match else "Unknown Title")

        # Chapter markers
        chap_divs = re.findall(r'<div[^>]+class=["\'][^"\']*chapter[^"\']*["\'][^>]*>', html)
        print(f"Chapter divs found: {len(chap_divs)}")

        # Print all chapter headers
        chap_titles = re.findall(r'<h3 class="title">\s*<a[^>]*>(.*?)</a>', html, re.DOTALL)
        if not chap_titles:
            chap_titles = re.findall(r'<h3 class="title">(.*?)</h3>', html, re.DOTALL)
        print(f"Chapter titles found ({len(chap_titles)}):")
        for idx, ct in enumerate(chap_titles[:5], 1):
            clean_ct = re.sub(r'<[^>]+>', '', ct).strip()
            print(f"  Ch {idx}: {clean_ct}")

        # Work Skins
        skins = re.findall(r'<style[^>]*id=["\']workskin["\'][^>]*>(.*?)</style>', html, re.DOTALL)
        if not skins:
            skins = re.findall(r'<style[^>]*>(.*?)</style>', html, re.DOTALL)
        print(f"Style blocks found: {len(skins)}")
        for s in skins:
            if "workskin" in s or "chapter" in s or "#workskin" in s:
                print("Work Skin CSS preview:\n", s[:300])

except Exception as e:
    print("Error:", e)
