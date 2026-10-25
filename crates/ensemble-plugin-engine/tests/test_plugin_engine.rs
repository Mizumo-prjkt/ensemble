use ensemble_plugin_engine::{
    PluginActionType, PluginHostContext, PluginLogger, PluginManager, PluginPermissions,
};
use std::fs;

#[test]
fn test_plugin_engine_full_suite() {
    let mut pm = PluginManager::new();

    // 1. Test normal plugin execution + editor API + isolated storage
    {
        let script1 = concat!(
            "# @name: Word Counter Test\n",
            "# @version: 1.0.0\n",
            "\n",
            "@on_chapter_change\n",
            "def handle_change(idx):\n",
            "    w = editor.get_word_count()\n",
            "    title = editor.get_chapter_title()\n",
            "    storage.set(\"saved_title\", title)\n",
            "    status.set_badge(title + \": \" + str(w))\n",
            "\n",
            "@on_toolbar_click(\"Upper Selection\")\n",
            "def make_upper():\n",
            "    sel = editor.get_selection()\n",
            "    saved = storage.get(\"saved_title\", \"none\")\n",
            "    editor.replace_selection(sel.upper() + \" [\" + saved + \"]\")\n",
        );

        assert!(
            pm.load_plugin_from_source(
                "test_plugin_a",
                "Test Plugin A",
                script1,
                "",
                "uidp_test_alpha_secret_111"
            )
            .is_ok()
        );

        let ctx = PluginHostContext {
            current_chapter_index: 0,
            current_chapter_title: "Prologue".to_string(),
            current_chapter_html: "<p>Hello world</p>".to_string(),
            current_plain_text: "Hello world".to_string(),
            selected_text: "hello".to_string(),
            word_count: 420,
            ..Default::default()
        };

        pm.notify_chapter_changed(&ctx);
        let captured_actions = pm.take_emitted_actions();
        assert_eq!(captured_actions.len(), 1);
        assert_eq!(
            captured_actions[0].action_type,
            PluginActionType::ShowStatusBadge
        );
        assert_eq!(captured_actions[0].payload, "Prologue: 420");

        pm.trigger_toolbar_action("test_plugin_a", "make_upper", &ctx);
        let captured_actions = pm.take_emitted_actions();
        assert_eq!(captured_actions.len(), 1);
        assert_eq!(
            captured_actions[0].action_type,
            PluginActionType::ReplaceSelection
        );
        assert_eq!(captured_actions[0].payload, "HELLO [Prologue]");
    }

    // 2. Test storage isolation between plugins (Plugin B cannot read Plugin A's storage)
    {
        let script2 = concat!(
            "@on_toolbar_click(\"Try Steal\")\n",
            "def try_steal():\n",
            "    val = storage.get(\"saved_title\", \"isolated!\")\n",
            "    status.set_badge(val)\n",
        );

        assert!(
            pm.load_plugin_from_source(
                "test_plugin_b",
                "Test Plugin B",
                script2,
                "",
                "uidp_test_beta_secret_222"
            )
            .is_ok()
        );

        let ctx = PluginHostContext::default();
        pm.trigger_toolbar_action("test_plugin_b", "try_steal", &ctx);
        let captured_actions = pm.take_emitted_actions();
        assert_eq!(captured_actions.len(), 1);
        assert_eq!(captured_actions[0].payload, "isolated!");
    }

    // 3. Test path traversal defense in storage key
    {
        let script_traversal = concat!(
            "@on_toolbar_click(\"Traversal\")\n",
            "def attempt_traversal():\n",
            "    storage.get(\"../uidp_test_alpha_secret_111/saved_title\")\n",
        );

        assert!(
            pm.load_plugin_from_source(
                "test_traversal",
                "Traversal",
                script_traversal,
                "",
                "uidp_test_trav_333"
            )
            .is_ok()
        );

        let ctx = PluginHostContext::default();
        let _ = pm.take_emitted_errors();
        pm.trigger_toolbar_action("test_traversal", "attempt_traversal", &ctx);
        let errs = pm.take_emitted_errors();
        assert!(!errs.is_empty());
        assert!(errs[0].1.contains("SecurityError"));
    }

    // 4. Test forbidden keywords (import / open / __dunder__)
    {
        let bad_import = "import os\n";
        let res = pm.load_plugin_from_source(
            "bad1",
            "Bad1",
            bad_import,
            "",
            "uidp_bad1",
        );
        assert!(res.is_err());
        assert!(res.unwrap_err().contains("SecurityError"));

        let bad_dunder = "x = __builtins__\n";
        let res2 = pm.load_plugin_from_source(
            "bad2",
            "Bad2",
            bad_dunder,
            "",
            "uidp_bad2",
        );
        assert!(res2.is_err());
        assert!(res2.unwrap_err().contains("SecurityError"));
    }

    // 5. Test infinite loop protection (step limit guard)
    {
        let inf_loop = concat!(
            "@on_toolbar_click(\"Freeze\")\n",
            "def freeze_app():\n",
            "    i = 0\n",
            "    while True:\n",
            "        i += 1\n",
        );

        assert!(
            pm.load_plugin_from_source(
                "test_inf",
                "InfLoop",
                inf_loop,
                "",
                "uidp_inf"
            )
            .is_ok()
        );
        let _ = pm.take_emitted_errors();
        let ctx = PluginHostContext::default();
        pm.trigger_toolbar_action("test_inf", "freeze_app", &ctx);
        let errs = pm.take_emitted_errors();
        assert!(!errs.is_empty());
        assert!(errs[0].1.contains("step limit exceeded"));
    }

    // 6. Test memory allocation cap (8 MB limit)
    {
        let mem_bomb = concat!(
            "@on_toolbar_click(\"Bomb\")\n",
            "def allocate_huge():\n",
            "    s = \"A\" * 1000000\n",
            "    big = s * 12\n",
        );

        assert!(
            pm.load_plugin_from_source(
                "test_mem",
                "MemBomb",
                mem_bomb,
                "",
                "uidp_mem"
            )
            .is_ok()
        );
        let _ = pm.take_emitted_errors();
        let ctx = PluginHostContext::default();
        pm.trigger_toolbar_action("test_mem", "allocate_huge", &ctx);
        let errs = pm.take_emitted_errors();
        assert!(!errs.is_empty());
        assert!(errs[0].1.contains("Memory Allocation Failure"));
    }

    // 7. Test mediated inter-plugin messaging
    {
        let receiver_script = concat!(
            "@on_message\n",
            "def on_msg(sender, topic, payload):\n",
            "    if topic == \"ping\":\n",
            "        status.set_badge(\"pong from \" + sender + \": \" + payload)\n",
        );
        let sender_script = concat!(
            "@on_toolbar_click(\"Send Ping\")\n",
            "def do_ping():\n",
            "    plugins.send_message(\"plugin_receiver\", \"ping\", \"hello_ipc\")\n",
        );

        assert!(
            pm.load_plugin_from_source(
                "plugin_receiver",
                "Receiver",
                receiver_script,
                "",
                "uidp_recv"
            )
            .is_ok()
        );
        assert!(
            pm.load_plugin_from_source(
                "plugin_sender",
                "Sender",
                sender_script,
                "",
                "uidp_send"
            )
            .is_ok()
        );

        let _ = pm.take_emitted_actions();
        let ctx = PluginHostContext::default();
        pm.trigger_toolbar_action("plugin_sender", "do_ping", &ctx);
        let captured_actions = pm.take_emitted_actions();
        assert_eq!(captured_actions.len(), 1);
        assert_eq!(
            captured_actions[0].action_type,
            PluginActionType::ShowStatusBadge
        );
        assert_eq!(
            captured_actions[0].payload,
            "pong from plugin_sender: hello_ipc"
        );
    }

    // 8. Test static scanner, UI compositing API, and domain-level network permission gate
    {
        let net_ui_script = concat!(
            "# @name: Web & UI Test\n",
            "@on_toolbar_click(\"Open Window\")\n",
            "def open_win():\n",
            "    ui.create_window(\"My Plugin Tool\", 500, 400)\n",
            "    ui.add_label(\"lbl1\", \"Hello UI\")\n",
            "    ui.add_input(\"inp1\", \"Type here\", \"default_val\")\n",
            "    ui.add_button(\"btn1\", \"Run Action\", \"on_btn_clicked\")\n",
            "    ui.show_window()\n",
            "\n",
            "def on_btn_clicked(wid):\n",
            "    val = ui.get_text(\"inp1\")\n",
            "    status.set_badge(\"Input was: \" + val)\n",
            "\n",
            "@on_toolbar_click(\"Fetch Web\")\n",
            "def fetch_web():\n",
            "    resp = http.get(\"https://api.example.com/v1/status\")\n",
        );

        assert!(
            pm.load_plugin_from_source(
                "plugin_net_ui",
                "NetUI",
                net_ui_script,
                "",
                "uidp_net_ui"
            )
            .is_ok()
        );

        let mut found = false;
        for m in pm.plugins() {
            if m.id == "plugin_net_ui" {
                found = true;
                assert!(m.scan_report.uses_ui);
                assert!(m.scan_report.uses_network);
                assert!(m
                    .scan_report
                    .detected_domains
                    .contains(&"api.example.com".to_string()));
                assert!(!m.permissions.allow_network);
                assert!(!m
                    .permissions
                    .allowed_domains
                    .get("api.example.com")
                    .copied()
                    .unwrap_or(true));
            }
        }
        assert!(found);

        // Trigger UI window creation
        let _ = pm.take_emitted_actions();
        let mut ctx = PluginHostContext::default();
        pm.trigger_toolbar_action("plugin_net_ui", "open_win", &ctx);
        let captured_actions = pm.take_emitted_actions();
        assert_eq!(captured_actions.len(), 5);
        assert_eq!(
            captured_actions[0].action_type,
            PluginActionType::UiCreateWindow
        );
        assert_eq!(captured_actions[0].payload, "My Plugin Tool");

        // Trigger UI button callback
        let _ = pm.take_emitted_actions();
        ctx.ui_widget_values
            .insert("inp1".to_string(), "custom_user_text".to_string());
        pm.trigger_ui_widget_callback("plugin_net_ui", "on_btn_clicked", "btn1", &ctx);
        let captured_actions = pm.take_emitted_actions();
        assert_eq!(captured_actions.len(), 1);
        assert_eq!(captured_actions[0].payload, "Input was: custom_user_text");

        // Trigger network call while allowNetwork is false -> must trap with PermissionError
        let _ = pm.take_emitted_errors();
        pm.trigger_toolbar_action("plugin_net_ui", "fetch_web", &ctx);
        let errs = pm.take_emitted_errors();
        assert!(!errs.is_empty());
        assert!(errs[0].1.contains("PermissionError"));

        // Enable allowNetwork, but leave api.example.com unchecked -> must still trap with domain PermissionError
        let mut perms = PluginPermissions::default();
        perms.allow_network = true;
        perms
            .allowed_domains
            .insert("api.example.com".to_string(), false);
        pm.update_plugin_permissions("plugin_net_ui", perms);

        let _ = pm.take_emitted_errors();
        pm.trigger_toolbar_action("plugin_net_ui", "fetch_web", &ctx);
        let errs = pm.take_emitted_errors();
        assert!(!errs.is_empty());
        assert!(errs[0].1.contains("api.example.com"));

        // Test non-HTTPS downgrade attempt -> must immediately quarantine and disable the plugin!
        let downgrade_script = concat!(
            "# @name: Downgrade Plugin\n",
            "@on_toolbar_click(\"Try HTTP\")\n",
            "def try_http():\n",
            "    http.get(\"http://api.example.com/v1/insecure\")\n",
        );
        assert!(
            pm.load_plugin_from_source(
                "plugin_downgrade",
                "DowngradePlugin",
                downgrade_script,
                "",
                "uidp_downgrade"
            )
            .is_ok()
        );
        let mut down_perms = PluginPermissions::default();
        down_perms.allow_network = true;
        down_perms
            .allowed_domains
            .insert("api.example.com".to_string(), true);
        pm.update_plugin_permissions("plugin_downgrade", down_perms);

        let _ = pm.take_emitted_quarantines();
        pm.trigger_toolbar_action("plugin_downgrade", "try_http", &ctx);
        let quarantines = pm.take_emitted_quarantines();
        assert_eq!(quarantines.len(), 1);
        assert_eq!(quarantines[0].plugin_id, "plugin_downgrade");
        assert!(quarantines[0].reason.contains("HTTPS"));

        for m in pm.plugins() {
            if m.id == "plugin_downgrade" {
                assert!(m.quarantined);
                assert!(!m.enabled);
            }
        }

        // Test Cyrillic homograph / lookalike letter ('а' U+0430) spoofing attempt ->
        // Must invoke Character IDs (U+0430) instead of trusting the visual glyph, warn that it is an attempt to misguide users, and quarantine!
        let homograph_script = concat!(
            "# @name: Homograph Phishing Test\n",
            "@on_toolbar_click(\"Try Homograph\")\n",
            "def try_homograph():\n",
            "    http.get(\"https://\u{0430}rchiveofourown.org/works\")\n",
        );
        assert!(
            pm.load_plugin_from_source(
                "plugin_homograph",
                "HomographPlugin",
                homograph_script,
                "",
                "uidp_homograph"
            )
            .is_ok()
        );
        let mut homo_perms = PluginPermissions::default();
        homo_perms.allow_network = true;
        homo_perms
            .allowed_domains
            .insert("archiveofourown.org".to_string(), true);
        pm.update_plugin_permissions("plugin_homograph", homo_perms);

        let _ = pm.take_emitted_quarantines();
        pm.trigger_toolbar_action("plugin_homograph", "try_homograph", &ctx);
        let homo_quarantines = pm.take_emitted_quarantines();
        assert_eq!(homo_quarantines.len(), 1);
        assert_eq!(homo_quarantines[0].plugin_id, "plugin_homograph");
        assert!(homo_quarantines[0].reason.contains("U+0430"));
        assert!(homo_quarantines[0].reason.contains("Cyrillic"));
        assert!(homo_quarantines[0].reason.contains("misguide users"));
        assert!(homo_quarantines[0].target_url.contains("CHAR_ID:U+0430"));

        for m in pm.plugins() {
            if m.id == "plugin_homograph" {
                assert!(m.quarantined);
                assert!(!m.enabled);
                assert!(!m.scan_report.url_security_findings.is_empty());
                assert!(m.scan_report.url_security_findings[0].has_homograph_or_non_ascii);
            }
        }

        // Verify activity log recorded events
        let logs = PluginLogger::read_recent_logs(50);
        assert!(logs.contains("plugin_net_ui"));
        assert!(logs.contains("SECURITY_VIOLATION"));
    }

    // 9. Test uninstallPlugin deletes script file and removes plugin instance
    {
        let temp_er_path = PluginManager::plugins_dir().join("temp_uninstall_test.er");
        fs::write(&temp_er_path, "# @name: Temp Uninstall\nx = 1\n").unwrap();
        assert!(temp_er_path.exists());

        assert!(pm.load_plugin_from_file(&temp_er_path).is_ok());
        assert!(pm.uninstall_plugin("temp_uninstall_test").is_ok());
        assert!(!temp_er_path.exists());
    }
}
