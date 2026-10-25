use crate::help::HelpGuideController;
use gtk4::prelude::*;
use libadwaita as adw;
use libadwaita::prelude::*;
use std::cell::RefCell;
use std::rc::Rc;
use webkit6::prelude::*;

pub fn show_help_guide_dialog(parent: &impl IsA<gtk4::Window>) {
    let window = adw::Window::builder()
        .title("Ensemble Help Guide")
        .transient_for(parent)
        .modal(true)
        .default_width(980)
        .default_height(680)
        .build();

    let controller = Rc::new(RefCell::new(HelpGuideController::new()));

    let root = gtk4::Box::new(gtk4::Orientation::Vertical, 0);
    let header = adw::HeaderBar::new();
    let title = adw::WindowTitle::new(
        "Ensemble Help Guide",
        "Complete Documentation, Plugin SDK & Shortcuts",
    );
    header.set_title_widget(Some(&title));
    root.append(&header);

    let split = gtk4::Paned::new(gtk4::Orientation::Horizontal);
    split.set_position(330);
    split.set_vexpand(true);
    split.set_hexpand(true);
    split.set_shrink_start_child(false);
    split.set_shrink_end_child(true);

    // Left pane: search bar + match counter + topic list
    let left_box = gtk4::Box::new(gtk4::Orientation::Vertical, 6);
    left_box.set_margin_top(10);
    left_box.set_margin_bottom(10);
    left_box.set_margin_start(10);
    left_box.set_margin_end(10);

    let search_entry = gtk4::SearchEntry::builder()
        .placeholder_text("Search guide, plugins, MCP, shortcuts…")
        .build();
    left_box.append(&search_entry);

    let match_status_label = gtk4::Label::builder()
        .label("All 8 Guide Sections")
        .xalign(0.0)
        .margin_start(4)
        .margin_bottom(2)
        .css_classes(["caption", "dim-label"])
        .build();
    left_box.append(&match_status_label);

    let scrolled_list = gtk4::ScrolledWindow::builder()
        .vexpand(true)
        .hscrollbar_policy(gtk4::PolicyType::Never)
        .vscrollbar_policy(gtk4::PolicyType::Automatic)
        .propagate_natural_height(false)
        .build();

    let list_box = gtk4::ListBox::builder()
        .selection_mode(gtk4::SelectionMode::Single)
        .css_classes(["navigation-sidebar"])
        .build();
    scrolled_list.set_child(Some(&list_box));
    left_box.append(&scrolled_list);
    split.set_start_child(Some(&left_box));

    // Right pane: WebKit WebView rendering rich styled HTML inside a ScrolledWindow
    let right_scroll = gtk4::ScrolledWindow::builder()
        .vexpand(true)
        .hexpand(true)
        .propagate_natural_height(false)
        .propagate_natural_width(false)
        .build();
    let web_view = webkit6::WebView::new();
    web_view.set_hexpand(true);
    web_view.set_vexpand(true);
    web_view.set_size_request(320, 200);
    right_scroll.set_child(Some(&web_view));
    split.set_end_child(Some(&right_scroll));

    root.append(&split);
    window.set_content(Some(&root));

    // Helper to refresh topics list based on current search filter
    let populate_topics = {
        let list_box = list_box.clone();
        let controller = controller.clone();
        let web_view = web_view.clone();
        let match_status_label = match_status_label.clone();
        Rc::new(move || {
            while let Some(child) = list_box.first_child() {
                list_box.remove(&child);
            }
            let ctrl = controller.borrow();
            let total = ctrl.topic_count();
            let visible_topics: Vec<_> = ctrl.topics().iter().filter(|t| !t.hidden).collect();
            let query = ctrl.search_query().to_string();

            if query.is_empty() {
                match_status_label.set_text(&format!("All {} Guide Sections", total));
            } else {
                match_status_label.set_text(&format!(
                    "Found {} of {} sections matching \"{}\"",
                    visible_topics.len(),
                    total,
                    query
                ));
            }

            if visible_topics.is_empty() {
                let empty_row = adw::ActionRow::builder()
                    .use_markup(false)
                    .title("No Matching Topics Found")
                    .subtitle("Try a different search keyword (e.g. plugin, ui, css, mcp, ao3proj).")
                    .build();
                list_box.append(&empty_row);
                let empty_html = compose_help_html(
                    "<h1>🔍 No Matching Help Topics</h1><p class='lead'>No guide sections matched your search query. Try searching for <code>plugin</code>, <code>ui.create_window</code>, <code>mcp</code>, <code>workskin</code>, <code>lore</code>, or <code>shortcuts</code>.</p>",
                    "",
                );
                web_view.load_html(&empty_html, None);
                return;
            }

            for (vis_idx, topic) in visible_topics.iter().enumerate() {
                let row = adw::ActionRow::builder()
                    .use_markup(false)
                    .title(&topic.title)
                    .subtitle(&topic.summary)
                    .subtitle_lines(2)
                    .build();
                row.set_widget_name(&topic.id);

                let icon_lbl = gtk4::Label::builder()
                    .label(&topic.icon)
                    .valign(gtk4::Align::Center)
                    .build();
                row.add_prefix(&icon_lbl);

                list_box.append(&row);
                if vis_idx == 0 {
                    let html = compose_help_html(&topic.content_html, &query);
                    web_view.load_html(&html, None);
                }
            }
            if let Some(first) = list_box.row_at_index(0) {
                list_box.select_row(Some(&first));
            }
        })
    };

    populate_topics();

    // Connect list selection change using widget_name (topic.id) so filtered indices always resolve accurately
    {
        let controller = controller.clone();
        let web_view = web_view.clone();
        list_box.connect_row_selected(move |_, row| {
            if let Some(r) = row {
                let topic_id = r.widget_name().to_string();
                if topic_id.is_empty() {
                    return;
                }
                let mut ctrl = controller.borrow_mut();
                ctrl.select_topic(&topic_id);
                let query = ctrl.search_query().to_string();
                let topic_html = ctrl.selected_topic().content_html.clone();
                let html = compose_help_html(&topic_html, &query);
                web_view.load_html(&html, None);
            }
        });
    }

    // Connect search entry
    {
        let controller = controller.clone();
        let pop = populate_topics.clone();
        search_entry.connect_search_changed(move |entry| {
            let text = entry.text();
            controller.borrow_mut().set_search_filter(&text);
            pop();
        });
    }

    window.present();
}

fn compose_help_html(body_html: &str, search_query: &str) -> String {
    let is_dark = adw::StyleManager::default().is_dark();
    let (bg_color, text_color, card_bg, border_color, code_bg, accent_color) = if is_dark {
        (
            "#1e1e22",
            "#ececf1",
            "rgba(255, 255, 255, 0.05)",
            "rgba(255, 255, 255, 0.14)",
            "rgba(255, 255, 255, 0.11)",
            "#62a0ea",
        )
    } else {
        (
            "#fafafa",
            "#1c1c1f",
            "rgba(0, 0, 0, 0.035)",
            "rgba(0, 0, 0, 0.14)",
            "rgba(0, 0, 0, 0.07)",
            "#1c71d8",
        )
    };

    let query_json = serde_json::to_string(search_query.trim()).unwrap_or_else(|_| "\"\"".to_string());

    format!(
        r#"<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<style>
  :root {{
    color-scheme: {scheme};
  }}
  body {{
    font-family: system-ui, -apple-system, BlinkMacSystemFont, 'Cantarell', 'Segoe UI', Roboto, sans-serif;
    font-size: 14px;
    line-height: 1.65;
    margin: 0;
    padding: 24px 30px 40px 30px;
    background-color: {bg};
    color: {fg};
  }}
  h1 {{
    font-size: 22px;
    font-weight: 700;
    margin-top: 0;
    margin-bottom: 10px;
    border-bottom: 1px solid {border};
    padding-bottom: 10px;
  }}
  h2 {{
    font-size: 16.5px;
    font-weight: 600;
    margin-top: 24px;
    margin-bottom: 8px;
    color: {accent};
  }}
  h3 {{
    font-size: 14.5px;
    font-weight: 600;
    margin-top: 18px;
    margin-bottom: 6px;
  }}
  p.lead {{
    font-size: 14.5px;
    opacity: 0.94;
    margin-bottom: 16px;
    font-weight: 500;
  }}
  .callout {{
    background: {card};
    border-left: 4px solid {accent};
    border-radius: 8px;
    padding: 12px 16px;
    margin: 14px 0;
    font-size: 13.5px;
  }}
  code {{
    font-family: 'JetBrains Mono', 'Fira Code', 'DejaVu Sans Mono', monospace;
    font-size: 12.5px;
    padding: 2px 6px;
    border-radius: 4px;
    background: {code};
  }}
  pre {{
    font-family: 'JetBrains Mono', 'Fira Code', 'DejaVu Sans Mono', monospace;
    font-size: 12.5px;
    line-height: 1.5;
    padding: 12px 16px;
    border-radius: 8px;
    background: {card};
    overflow-x: auto;
    border: 1px solid {border};
    margin: 12px 0;
  }}
  pre code {{
    background: transparent;
    padding: 0;
  }}
  kbd {{
    font-family: 'JetBrains Mono', monospace;
    font-size: 11.5px;
    font-weight: 600;
    padding: 2px 6px;
    border-radius: 5px;
    background: {code};
    border: 1px solid {border};
    box-shadow: 0 1px 0 {border};
  }}
  table {{
    width: 100%;
    border-collapse: collapse;
    margin: 14px 0;
    font-size: 13px;
    background: {card};
    border-radius: 8px;
    overflow: hidden;
    border: 1px solid {border};
  }}
  th, td {{
    text-align: left;
    padding: 9px 12px;
    border-bottom: 1px solid {border};
    vertical-align: top;
  }}
  th {{
    font-weight: 600;
    background: {code};
  }}
  ul, ol {{
    padding-left: 22px;
    margin: 10px 0;
  }}
  li {{
    margin-bottom: 6px;
  }}
  mark.search-hit {{
    background: #f5c211;
    color: #111111;
    padding: 1px 3px;
    border-radius: 3px;
    font-weight: 600;
  }}
</style>
</head>
<body>
  <div id="help-content">{body}</div>
  <script>
    (function() {{
      const q = {query};
      if (!q || q.length < 2) return;
      const root = document.getElementById('help-content');
      const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT, null);
      const nodes = [];
      while (walker.nextNode()) nodes.push(walker.currentNode);
      const lowerQ = q.toLowerCase();
      let firstMark = null;
      for (const node of nodes) {{
        const text = node.nodeValue;
        const idx = text.toLowerCase().indexOf(lowerQ);
        if (idx !== -1 && node.parentNode && node.parentNode.tagName !== 'SCRIPT' && node.parentNode.tagName !== 'STYLE') {{
          const span = document.createElement('span');
          const before = document.createTextNode(text.slice(0, idx));
          const mark = document.createElement('mark');
          mark.className = 'search-hit';
          mark.textContent = text.slice(idx, idx + q.length);
          const after = document.createTextNode(text.slice(idx + q.length));
          span.appendChild(before);
          span.appendChild(mark);
          span.appendChild(after);
          node.parentNode.replaceChild(span, node);
          if (!firstMark) firstMark = mark;
        }}
      }}
      if (firstMark) {{
        firstMark.scrollIntoView({{ behavior: 'smooth', block: 'center' }});
      }}
    }})();
  </script>
</body>
</html>"#,
        scheme = if is_dark { "dark" } else { "light" },
        bg = bg_color,
        fg = text_color,
        card = card_bg,
        border = border_color,
        code = code_bg,
        accent = accent_color,
        body = body_html,
        query = query_json,
    )
}
