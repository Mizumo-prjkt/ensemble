use ensemble_net::{Ao3Parser, UpdateChecker};
use std::fs;
use std::path::PathBuf;

#[test]
fn test_ao3_parser_full_suite() {
    let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let workspace_root = manifest_dir
        .parent()
        .and_then(|p| p.parent())
        .unwrap_or(&manifest_dir);
    let fixture_path = workspace_root.join("test/fixtures/login_page_sample.html");

    let html = fs::read(&fixture_path).unwrap_or_else(|_| {
        b"<!DOCTYPE html><html><head>\
          <meta name=\"csrf-param\" content=\"authenticity_token\" />\n\
          <meta name=\"csrf-token\" content=\"bWrASw1Erxk-oIyJOy1AAjsy9jZ-9C33Zi8V68hqflDCF3OzbTeoZBq4-QGMSvcCRkXanOQR90t2ChFHHUGKjw\" />\n\
          </head><body>\
          <form id=\"loginform\"><input type=\"hidden\" name=\"authenticity_token\" value=\"XwQBdGQi9F-jFBGeBE0rmXGgul4_h1pMRDA6l8yFM08xc6jdTz6yS2AbH693Mdh3m5Dr_Aa5p1UKTlbMTSUasA\" /></form>\
          </body></html>"
            .to_vec()
    });

    let token = Ao3Parser::parse_authenticity_token(&html);
    assert!(!token.is_empty());
    assert!(token.len() > 10);

    // Test parse_full_work metadata tags extraction
    let sample_work_html = concat!(
        "<h2 class=\"title heading\">The Secret of the Crystal</h2>\n",
        "<h3 class=\"byline heading\"><a rel=\"author\" href=\"/users/miku/pseuds/miku\">miku</a></h3>\n",
        "<dl class=\"work meta group\">\n",
        "  <dd class=\"rating tags\"><a class=\"tag\">Mature</a></dd>\n",
        "  <dd class=\"warning tags\"><a class=\"tag\">Graphic Depictions Of Violence</a></dd>\n",
        "  <dd class=\"category tags\"><a class=\"tag\">F/M</a>, <a class=\"tag\">Multi</a></dd>\n",
        "  <dd class=\"fandom tags\"><a class=\"tag\">Vocaloid &amp; Friends</a></dd>\n",
        "  <dd class=\"relationship tags\"><a class=\"tag\">Miku/Kaito</a></dd>\n",
        "  <dd class=\"character tags\"><a class=\"tag\">Hatsune Miku</a>, <a class=\"tag\">Kaito</a></dd>\n",
        "  <dd class=\"freeform tags\"><a class=\"tag\">Alternate Universe - Sci-Fi</a>, <a class=\"tag\">Time Travel</a></dd>\n",
        "</dl>\n",
        "<div class=\"userstuff\"><p>Chapter one body text.</p></div>\n",
    );

    let parsed_work = Ao3Parser::parse_full_work(sample_work_html.as_bytes());
    assert_eq!(parsed_work.summary.title, "The Secret of the Crystal");
    assert_eq!(parsed_work.summary.pseud, "miku");
    assert_eq!(parsed_work.summary.rating, "Mature");
    assert!(parsed_work
        .summary
        .archive_warnings
        .contains(&"Graphic Depictions Of Violence".to_string()));
    assert!(parsed_work.summary.categories.contains(&"F/M".to_string()));
    assert!(parsed_work
        .summary
        .fandoms
        .contains(&"Vocaloid & Friends".to_string()));
    assert!(parsed_work
        .summary
        .relationships
        .contains(&"Miku/Kaito".to_string()));
    assert!(parsed_work
        .summary
        .characters
        .contains(&"Hatsune Miku".to_string()));
    assert!(parsed_work
        .summary
        .tags
        .contains(&"Alternate Universe - Sci-Fi".to_string()));
    assert!(parsed_work
        .summary
        .tags
        .contains(&"Time Travel".to_string()));

    // Test parse_full_work embedded <img> extraction
    let sample_work_with_images = concat!(
        "<h2 class=\"title heading\">Story with Art</h2>\n",
        "<div class=\"userstuff\">\n",
        "  <p align=\"center\"><a href=\"https://images.example.org/full.png\"><img src=\"https://images.example.org/cover.png\" alt=\"Cover Art\" title=\"Cover Illustration\" width=\"800\" height=\"600\" /></a></p>\n",
        "  <p>Some story prose...</p>\n",
        "  <p><img src=\"https://images.example.org/scene1.jpg\" alt=\"Battle Scene\" /></p>\n",
        "</div>\n",
    );

    let parsed_work_images = Ao3Parser::parse_full_work(sample_work_with_images.as_bytes());
    assert_eq!(parsed_work_images.embedded_images.len(), 2);
    assert_eq!(
        parsed_work_images.embedded_images[0].url,
        "https://images.example.org/cover.png"
    );
    assert_eq!(parsed_work_images.embedded_images[0].alt, "Cover Art");
    assert_eq!(
        parsed_work_images.embedded_images[0].title,
        "Cover Illustration"
    );
    assert_eq!(parsed_work_images.embedded_images[0].chapter_number, 1);
    assert_eq!(
        parsed_work_images.embedded_images[1].url,
        "https://images.example.org/scene1.jpg"
    );
    assert_eq!(parsed_work_images.embedded_images[1].alt, "Battle Scene");

    // Test UpdateChecker version comparison
    assert!(UpdateChecker::is_version_newer("1.4.0", "1.3.0"));
    assert!(!UpdateChecker::is_version_newer("1.3.0", "1.3.0"));
    assert!(!UpdateChecker::is_version_newer("1.2.9", "1.3.0"));
}
