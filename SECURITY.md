# Security Policy

## Supported Versions

These versions are actively maintained with security patches, dependency audits, and stability fixes:

| Versions | Status | EOL          |
|----------|--------|--------------|
| 1.2.x    | Active | March 2027   |
| 1.1.x    | Active | January 2027 |

---

## Reporting a Vulnerability

Ensemble is developed as an open-source, privacy-first desktop authoring suite. While there is no paid bug bounty program, security disclosures are taken with the highest priority to safeguard authors and readers.

If you discover a vulnerability or security issue:
1. Open a confidential or standard issue on GitHub with detailed reproduction steps, proof of concept, and affected versions.
2. If the issue pertains to malicious plugin diversion or a security trap bypass, please attach relevant excerpts from `plugin_activity.log` and the corresponding `.er` script.
3. Submissions will be investigated promptly. Pull requests providing fixes are deeply appreciated.

---

## Plugin Security Standards & Architecture (.er)

To allow community plugins without endangering host stability, user manuscripts, or private credentials, Ensemble implements a sandboxed Domain-Specific Language (DSL) and Tree-Walking AST interpreter in modern C++.

All third-party plugins (running `.er`, Ensemble Runner scripts) are subject to non-negotiable security requirements:

### 1. Zero Native Execution & Sandboxed AST Evaluator
- **No Dynamic Libraries:** Ensemble never loads native dynamic libraries (`.dll`, `.so`, `.dylib`) for plugins.
- **No Embedded External Runtimes:** The engine does not embed standard Python, Lua, Node.js, or WebAssembly engines. All execution is handled by an in-house, deterministic C++ AST interpreter.
- **Forbidden Constructs:** The language grammar strictly omits and rejects `import`, `eval`, `exec`, `open`, `compile`, or double-underscore attribute introspection (`__class__`, `__globals__`, etc.) at both the lexer and parser levels.

### 2. Thread Isolation & Memory Bounds
- **Worker Thread Isolation:** Every plugin runs in a dedicated background `QThread` worker completely decoupled from the main Qt GUI thread.
- **Zero UI Pointer Sharing:** No `QWidget*`, `QTextEdit*`, or raw document pointers are ever passed to worker threads. All communication uses `Qt::QueuedConnection` with immutable value copies (`QString`, `QVariantMap`).
- **Safe Memory Operations:** The evaluator enforces strict bounds checking on all containers (`std::vector::at()`, checked string indexing). Out-of-bounds access raises catchable `ScriptTrap` exceptions instead of causing segmentation faults (`SIGSEGV`).
- **Resource Quotas (`ResourceGuard`):**
  - **Memory Cap:** Default 8 MB per plugin instance. All string and container allocations are pre-checked; exceeding the limit triggers an immediate trap.
  - **Instruction & Time Watchdog:** Default 150,000 AST steps or 250ms wall-clock time per hook to prevent infinite loops.
  - **Call Stack Depth:** Maximum 64 nested frames to prevent C++ thread stack overflow.

### 3. Isolated Storage & Opaque `uidp`
- **Private UUID Token (`secret_uidp`):** Every plugin is assigned an internal UUID token strictly held in C++ memory. Scripts can never read, query, or leak their own or other plugins' `uidp`.
- **Path Traversal Protection:** All storage keys are sanitized and checked against path traversal (`..`, `/`, `\`). Keys attempting traversal are blocked with a `SecurityError`.
- **Storage Quota:** Each plugin's persistent key-value store is capped at 5 MB under `<appdata>/plugin_store/<secret_uidp>/`.

### 4. Network Security Standards
Third-party plugins that interact with remote services must adhere to strict transport and integrity standards:

1. **Mandatory HTTPS Only:**
   - All network communications must strictly use `https://`.
   - Insecure plaintext HTTP (`http://`) and attempts to downgrade transport security are strictly prohibited. Attempting an insecure connection triggers an immediate security violation and quarantines the plugin.
2. **Internal Mediated Access (Zero OS Sockets):**
   - Plugins cannot open raw TCP/UDP sockets or invoke system network libraries.
   - All HTTP traffic is mediated through Ensemble's internal Qt `QNetworkAccessManager`.
3. **Granular Permissions & Explicit Domain Whitelist:**
   - Network access is disabled by default (`allowNetwork = false`).
   - Network requests are restricted to explicitly whitelisted domains configured in the Plugin Manager.
   - Static analysis pre-scans `.er` code to detect and disclose target domains and capabilities to the user prior to activation.
4. **Anti-Redirection & Anti-Diversion Quarantine Safeguard:**
   - Automated HTTP redirects are strictly disabled (`QNetworkRequest::ManualRedirectPolicy`).
   - If a remote provider or endpoint attempts to redivert / redirect traffic (HTTP 3xx or `Location` header) to a different website or URL, the engine immediately terminates execution, shuts down the plugin, and places it into **quarantine**.
   - The user is alerted with the mandatory security warning:
     > *"Security Warning: The plugin or plugin provider or the link that the plugin used attempted to redivert to a different website (%1). The plugin was forcibly closed and quarantined for a security violation. Report this immediately to Ensemble developers regarding this incident."*
   - Quarantined plugins cannot be executed or loaded until explicitly reviewed.

### 5. Declarative UI Isolation
- Scripts compose user interfaces using a declarative builder (`ui.create_window()`, `add_label()`, `add_text()`, `add_input()`, `add_button()`).
- All dialog rendering occurs on the main GUI thread in `PluginHostDialog`. User actions and input field values are marshaled as value copies across thread boundaries.

### 6. Audit Logging (`plugin_activity.log`)
- All plugin activities—including allowed and blocked network attempts, domain checks, UI dialog requests, and security traps—are recorded in a persistent, thread-safe audit log (`plugin_activity.log`) located in Ensemble's application data directory.
- Audit logs are maintained in both debug and public release builds to ensure full transparency and aid in forensic incident investigations.
