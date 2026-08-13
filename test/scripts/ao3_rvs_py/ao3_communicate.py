"""
AO3 Reverse Engineering & Communication Study Script

This script studies and documents the AO3 network interactions, CSRF authentication flow,
HTML page structures (pseuds, works catalogue, work skins, chapter body), and rate limiting.

Usage:
  python3 test/scripts/ao3_rvs_py/ao3_communicate.py --parse-fixtures
  python3 test/scripts/ao3_rvs_py/ao3_communicate.py --username USER --password PASS
"""

import sys
import os
import re
import time
import argparse
import ssl
import urllib.request
import urllib.parse
from http.cookiejar import CookieJar

# Rate Limiter enforcing Ensemble policies
class RateLimiter:
    def __init__(self, single_delay=10.0, bulk_delay=60.0):
        self.single_delay = single_delay
        self.bulk_delay = bulk_delay
        self.last_request_time = 0.0

    def wait(self, is_bulk=False):
        delay = self.bulk_delay if is_bulk else self.single_delay
        elapsed = time.time() - self.last_request_time
        if elapsed < delay and self.last_request_time > 0:
            sleep_time = delay - elapsed
            print(f"[RateLimiter] Waiting {sleep_time:.1f}s before next request (policy enforcement)...")
            time.sleep(sleep_time)
        self.last_request_time = time.time()

rate_limiter = RateLimiter(single_delay=10.0, bulk_delay=60.0)

# Parsing Helpers using Regular Expressions (matching C++ Ao3Parser logic)

def parse_authenticity_token(html: str) -> str:
    """Extract CSRF authenticity_token from AO3 HTML forms."""
    match = re.search(r'name=["\']authenticity_token["\']\s+value=["\']([^"\']+)["\']', html)
    if not match:
        match = re.search(r'value=["\']([^"\']+)["\']\s+name=["\']authenticity_token["\']', html)
    return match.group(1) if match else ""

def parse_username_from_redirect(url: str) -> str:
    """Extract username from redirect URL like https://archiveofourown.org/users/USERNAME."""
    match = re.search(r'/users/([^/]+)', url)
    return match.group(1) if match else ""

def parse_pseuds(html: str) -> list:
    """Parse list of pseuds from /users/USERNAME/pseuds HTML."""
    pseuds = []
    # AO3 pseud list items or heading links
    pattern = r'<a[^>]+href=["\']/users/[^/]+/pseuds/([^"\']+)["\'][^>]*>([^<]+)</a>'
    matches = re.findall(pattern, html)
    seen = set()
    for pseud_slug, display_name in matches:
        display_name = display_name.strip()
        if display_name not in seen and display_name.lower() != "edit":
            seen.add(display_name)
            is_default = "(default)" in html[html.find(display_name):html.find(display_name)+50].lower() if display_name in html else False
            pseuds.append({
                "name": display_name,
                "url": f"/users/{pseud_slug}/pseuds/{pseud_slug}",
                "is_default": is_default
            })
    return pseuds

def parse_works_list(html: str) -> list:
    """Parse works list items from /users/USERNAME/works HTML."""
    works = []
    # Split by work item container <li class="work group..." id="work_12345">
    work_blocks = re.split(r'<li[^>]+id=["\']work_(\d+)["\']', html)
    
    for i in range(1, len(work_blocks), 2):
        work_id = int(work_blocks[i])
        block = work_blocks[i+1]
        
        # Title & Author
        title_match = re.search(r'<a[^>]+href=["\']/works/' + str(work_id) + r'["\'][^>]*>([^<]+)</a>', block)
        title = title_match.group(1).strip() if title_match else f"Work {work_id}"
        
        pseud_match = re.search(r'rel=["\']author["\'][^>]*>([^<]+)</a>', block)
        pseud = pseud_match.group(1).strip() if pseud_match else ""
        
        # Fandoms
        fandom_match = re.search(r'<h5 class="fandoms[^"]*">\s*<a[^>]*>([^<]+)</a>', block)
        fandom = fandom_match.group(1).strip() if fandom_match else "Unspecified"
        
        # Words & Chapters
        words_match = re.search(r'<dd class="words">([^<]+)</dd>', block)
        word_count = int(words_match.group(1).replace(',', '')) if words_match and words_match.group(1).strip().isdigit() else 0
        
        chapters_match = re.search(r'<dd class="chapters">(\d+)/(\d+|\?)</dd>', block)
        chap_curr = int(chapters_match.group(1)) if chapters_match else 1
        chap_tot_str = chapters_match.group(2) if chapters_match else "1"
        chap_tot = int(chap_tot_str) if chap_tot_str.isdigit() else -1
        
        # Updated date
        date_match = re.search(r'<p class="datetime">([^<]+)</p>', block)
        updated = date_match.group(1).strip() if date_match else ""
        
        is_complete = (chap_curr == chap_tot) if chap_tot != -1 else False
        is_draft = "Unposted Work" in block or "Draft" in block
        
        works.append({
            "work_id": work_id,
            "title": title,
            "pseud": pseud,
            "fandom": fandom,
            "word_count": word_count,
            "chapter_count": chap_curr,
            "total_chapters": chap_tot,
            "last_updated": updated,
            "is_complete": is_complete,
            "is_draft": is_draft
        })
        
    return works

def parse_next_page_url(html: str) -> str:
    """Extract next page pagination URL if present."""
    match = re.search(r'<a[^>]+rel=["\']next["\'][^>]+href=["\']([^"\']+)["\']', html)
    if not match:
        match = re.search(r'<a[^>]+href=["\']([^"\']+)["\'][^>]*>Next\s*&rarr;</a>', html, re.IGNORECASE)
    return match.group(1) if match else ""

def parse_skins_list(html: str) -> list:
    """Parse work skins list from /users/USERNAME/skins HTML."""
    skins = []
    blocks = re.split(r'<li[^>]+id=["\']skin_(\d+)["\']', html)
    for i in range(1, len(blocks), 2):
        skin_id = int(blocks[i])
        block = blocks[i+1]
        title_match = re.search(r'<h4[^>]*>\s*<a[^>]*>([^<]+)</a>', block)
        title = title_match.group(1).strip() if title_match else f"Skin {skin_id}"
        is_work_skin = "WorkSkin" in block or "work skin" in block.lower()
        skins.append({
            "skin_id": skin_id,
            "name": title,
            "is_work_skin": is_work_skin
        })
    return skins

def parse_full_work(html: str) -> dict:
    """Parse chapters and body HTML from /works/WORK_ID?view_full_work=true."""
    # Work skin container
    workskin_match = re.search(r'<div[^>]+id=["\']workskin["\'][^>]*>(.*?)</div>\s*<!--\s*/workskin\s*-->', html, re.DOTALL)
    content_area = workskin_match.group(1) if workskin_match else html
    
    # Extract Work Skin CSS if present
    style_match = re.search(r'<style[^>]*>(.*?)</style>', content_area, re.DOTALL)
    work_skin_css = style_match.group(1).strip() if style_match else ""
    
    # Chapters
    chapters = []
    chap_blocks = re.split(r'<div[^>]+class=["\'][^"\']*chapter[^"\']*["\'][^>]*>', content_area)
    
    if len(chap_blocks) > 1:
        for idx, block in enumerate(chap_blocks[1:], start=1):
            title_match = re.search(r'<h3 class="title">\s*<a[^>]*>([^<]+)</a>', block)
            title = title_match.group(1).strip() if title_match else f"Chapter {idx}"
            
            # User story content div
            userstuff_match = re.search(r'<div[^>]+class=["\']userstuff[^"\']*["\'][^>]*>(.*?)</div>', block, re.DOTALL)
            body_html = userstuff_match.group(1).strip() if userstuff_match else ""
            
            chapters.append({
                "chapter_number": idx,
                "title": title,
                "body_html": body_html,
                "notes_begin": "",
                "notes_end": ""
            })
    else:
        # Single chapter work
        userstuff_match = re.search(r'<div[^>]+class=["\']userstuff[^"\']*["\'][^>]*>(.*?)</div>', content_area, re.DOTALL)
        body_html = userstuff_match.group(1).strip() if userstuff_match else ""
        chapters.append({
            "chapter_number": 1,
            "title": "Chapter 1",
            "body_html": body_html,
            "notes_begin": "",
            "notes_end": ""
        })
        
    return {
        "work_skin_css": work_skin_css,
        "chapters": chapters
    }

def test_parse_fixtures():
    print("[+] Testing parsers against saved local fixtures...")
    fixture_path = "/home/miku/Documents/ao3-typewriter/test/fixtures/login_page_sample.html"
    if os.path.exists(fixture_path):
        with open(fixture_path, "r", encoding="utf-8") as f:
            html = f.read()
        token = parse_authenticity_token(html)
        print(f"  -> Extracted authenticity_token from login_page_sample.html: {token[:20]}...")
        assert len(token) > 10, "Token extraction failed!"
        print("  -> Login Page Fixture Test PASSED!")
    else:
        print("  [!] Fixture file not found.")

def main():
    parser = argparse.ArgumentParser(description="AO3 Communication & Parsing Study")
    parser.add_argument("--parse-fixtures", action="store_true", help="Test parsers against local HTML fixtures")
    parser.add_argument("--username", help="AO3 Username")
    parser.add_argument("--password", help="AO3 Password")
    args = parser.parse_args()

    if args.parse_fixtures or not args.username:
        test_parse_fixtures()
        return

    print(f"[+] Starting Live AO3 Communication Study for user: {args.username}")
    # Live HTTP logic if credentials provided
    ctx = ssl.create_default_context()
    cookie_jar = CookieJar()
    opener = urllib.request.build_opener(
        urllib.request.HTTPCookieProcessor(cookie_jar),
        urllib.request.HTTPSHandler(context=ctx)
    )

    headers = {
        "User-Agent": "Ensemble/1.1.0 (Desktop; +https://github.com/mizumo-prjkt/ensemble)",
        "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8"
    }

    # Step 1: GET Login Page
    rate_limiter.wait()
    print("[+] Step 1: Fetching Login Page...")
    req = urllib.request.Request("https://archiveofourown.org/users/login", headers=headers)
    with opener.open(req) as resp:
        login_html = resp.read().decode("utf-8")

    token = parse_authenticity_token(login_html)
    print(f"  -> Extracted authenticity_token: {token[:25]}...")

    # Step 2: POST Login Credentials
    rate_limiter.wait()
    print("[+] Step 2: Submitting Login Credentials...")
    post_data = urllib.parse.urlencode({
        "user[login]": args.username,
        "user[password]": args.password,
        "user[remember_me]": "1",
        "authenticity_token": token,
        "commit": "Log in"
    }).encode("utf-8")

    login_req = urllib.request.Request("https://archiveofourown.org/user_sessions", data=post_data, headers=headers)
    with opener.open(login_req) as resp:
        final_url = resp.geturl()
        print(f"  -> Final Redirect URL: {final_url}")
        logged_user = parse_username_from_redirect(final_url)
        print(f"  -> Logged in user: {logged_user}")

    print("[+] Flow test completed successfully.")

if __name__ == "__main__":
    main()
