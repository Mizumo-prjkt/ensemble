"""
Cloudflare Behavior & Compliance Study for AO3 (Archive of Our Own)

Purpose:
  Test and document Cloudflare protection mechanisms, HTTP headers, session cookie behavior,
  and verify compliance requirements for Ensemble's AO3 Story Extractor.

Key Findings:
  1. Server & Edge: AO3 uses Cloudflare ('Server': 'cloudflare', 'CF-Ray' headers).
  2. Session Initialization: Initial GET to /users/login returns HTTP 200 with _otwarchive_session cookie.
  3. CSRF Protection: authenticity_token is embedded as a hidden form field in /users/login.
  4. Bot Detection: Direct HTTP GET with a standard User-Agent currently returns HTTP 200. However,
     Cloudflare Turnstile / JavaScript challenges trigger dynamically under high request frequency,
     data center IP ranges, or missing JavaScript execution capabilities.
  5. Compliance Requirement: Ensemble uses an embedded QWebEngineView (Chromium) for user authentication
     to ensure 100% native compliance with Cloudflare challenges, combined with mandatory 10s/60s rate limiting.
"""

import sys
import ssl
import urllib.request
import urllib.parse
from http.cookiejar import CookieJar

def test_cloudflare_response():
    print("[+] Testing Cloudflare & AO3 HTTP Response...")
    url = "https://archiveofourown.org/users/login"
    headers = {
        "User-Agent": "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
        "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
        "Accept-Language": "en-US,en;q=0.5"
    }

    ctx = ssl.create_default_context()
    cookie_jar = CookieJar()
    opener = urllib.request.build_opener(
        urllib.request.HTTPCookieProcessor(cookie_jar),
        urllib.request.HTTPSHandler(context=ctx)
    )

    req = urllib.request.Request(url, headers=headers)
    try:
        with opener.open(req) as resp:
            status = resp.status
            server = resp.headers.get("Server", "")
            cf_ray = resp.headers.get("CF-Ray", "")
            content_type = resp.headers.get("Content-Type", "")
            body = resp.read().decode("utf-8", errors="replace")

            print(f"  -> HTTP Status Code: {status}")
            print(f"  -> Server Header: {server}")
            print(f"  -> CF-Ray Header: {cf_ray}")
            print(f"  -> Content-Type: {content_type}")
            print(f"  -> Response Length: {len(body)} bytes")
            print(f"  -> Contains authenticity_token: {'authenticity_token' in body}")
            print(f"  -> Cookies Captured: {len(cookie_jar)}")

            for cookie in cookie_jar:
                print(f"     Cookie: {cookie.name}={cookie.value[:30]}...")

            return body
    except urllib.error.HTTPError as e:
        print(f"  [!] HTTP Error: {e.code} - {e.reason}")
        if e.code in (403, 503):
            print("  [!] Cloudflare Challenge or WAF Mitigation Triggered!")
        return None
    except Exception as e:
        print(f"  [!] Request Failed: {e}")
        return None

if __name__ == "__main__":
    test_cloudflare_response()
