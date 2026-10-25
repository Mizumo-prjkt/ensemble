use crate::types::{
    Ao3ChapterContent, Ao3FullWork, Ao3ImageEmbed, Ao3Pseud, Ao3WorkSkin, Ao3WorkSummary,
};
use once_cell::sync::Lazy;
use regex::Regex;
use std::collections::HashSet;

pub struct Ao3Parser;

fn sanitize_tag_text(tag: &str) -> String {
    tag.replace("&amp;", "&")
        .replace("&quot;", "\"")
        .replace("&#39;", "'")
        .replace("&lt;", "<")
        .replace("&gt;", ">")
        .trim()
        .to_string()
}

fn strip_html_tags(input: &str) -> String {
    static TAG_RE: Lazy<Regex> = Lazy::new(|| Regex::new(r"<[^>]+>").unwrap());
    TAG_RE.replace_all(input, "").trim().to_string()
}

fn extract_tags_from_dd(html: &str, dd_class: &str) -> Vec<String> {
    let mut result = Vec::new();
    let pattern = format!(
        r#"(?s)<dd[^>]+class=["'][^"']*{}[^"']*tags[^"']*["'][^>]*>(.*?)</dd>"#,
        regex::escape(dd_class)
    );
    let Ok(re_dd) = Regex::new(&pattern) else {
        return result;
    };
    static RE_TAG: Lazy<Regex> =
        Lazy::new(|| Regex::new(r#"<a[^>]+class=["']tag["'][^>]*>([^<]+)</a>"#).unwrap());

    if let Some(caps) = re_dd.captures(html) {
        let inner = &caps[1];
        for t_caps in RE_TAG.captures_iter(inner) {
            let tag = sanitize_tag_text(&t_caps[1]);
            if !tag.is_empty() && !result.contains(&tag) {
                result.push(tag);
            }
        }
    }
    result
}

fn extract_authors(html: &str) -> Vec<String> {
    static RE_BYLINE: Lazy<Regex> = Lazy::new(|| {
        Regex::new(r#"(?s)<h3[^>]+class=["'][^"']*byline[^"']*heading[^"']*["'][^>]*>(.*?)</h3>"#)
            .unwrap()
    });
    static RE_AUTHOR: Lazy<Regex> =
        Lazy::new(|| Regex::new(r#"<a[^>]+rel=["']author["'][^>]*>([^<]+)</a>"#).unwrap());

    let search_area = RE_BYLINE
        .captures(html)
        .map(|c| c[1].to_string())
        .unwrap_or_else(|| html.to_string());

    let mut authors = Vec::new();
    for caps in RE_AUTHOR.captures_iter(&search_area) {
        let author = sanitize_tag_text(&caps[1]);
        if !author.is_empty() && !authors.contains(&author) {
            authors.push(author);
        }
    }
    authors
}

impl Ao3Parser {
    pub fn parse_authenticity_token(html_data: &[u8]) -> String {
        let html = String::from_utf8_lossy(html_data);
        static RE1: Lazy<Regex> = Lazy::new(|| {
            Regex::new(r#"name=["']authenticity_token["']\s+value=["']([^"']+)["']"#).unwrap()
        });
        static RE2: Lazy<Regex> = Lazy::new(|| {
            Regex::new(r#"value=["']([^"']+)["']\s+name=["']authenticity_token["']"#).unwrap()
        });

        if let Some(caps) = RE1.captures(&html) {
            return caps[1].to_string();
        }
        if let Some(caps) = RE2.captures(&html) {
            return caps[1].to_string();
        }
        String::new()
    }

    pub fn parse_username(html_data: &[u8]) -> String {
        let html = String::from_utf8_lossy(html_data);
        static RE_GREETING: Lazy<Regex> = Lazy::new(|| {
            Regex::new(r#"(?s)id=["']greeting["'].*?href=["']/users/([^/"']+)["']"#).unwrap()
        });
        static RE_PROFILE: Lazy<Regex> = Lazy::new(|| {
            Regex::new(r#"href=["']/users/([^/"']+)/(?:profile|preferences|subscriptions)["']"#)
                .unwrap()
        });
        static RE_HI: Lazy<Regex> =
            Lazy::new(|| Regex::new(r"Hi,\s*([A-Za-z0-9_-]+)!").unwrap());

        if let Some(caps) = RE_GREETING.captures(&html) {
            return caps[1].to_string();
        }
        if let Some(caps) = RE_PROFILE.captures(&html) {
            return caps[1].to_string();
        }
        if let Some(caps) = RE_HI.captures(&html) {
            return caps[1].to_string();
        }
        String::new()
    }

    pub fn parse_pseuds_list(html_data: &[u8]) -> Vec<Ao3Pseud> {
        let html = String::from_utf8_lossy(html_data);
        static RE: Lazy<Regex> = Lazy::new(|| {
            Regex::new(r#"<a[^>]+href=["']/users/[^/]+/pseuds/([^"']+)["'][^>]*>([^<]+)</a>"#)
                .unwrap()
        });

        let mut pseuds = Vec::new();
        let mut seen = HashSet::new();

        for caps in RE.captures_iter(&html) {
            let slug = caps[1].trim();
            let name = caps[2].trim();
            let lower = name.to_lowercase();
            if !name.is_empty()
                && !seen.contains(name)
                && !lower.contains("edit")
                && !lower.contains("new")
                && !lower.contains("delete")
            {
                seen.insert(name.to_string());
                pseuds.push(Ao3Pseud {
                    name: name.to_string(),
                    url: format!("/users/{}/pseuds/{}", slug, slug),
                    is_default: html.contains(&format!("{} (default)", name)),
                });
            }
        }
        pseuds
    }

    pub fn parse_works_list(html_data: &[u8]) -> Vec<Ao3WorkSummary> {
        let html = String::from_utf8_lossy(html_data);
        static RE_SPLIT: Lazy<Regex> =
            Lazy::new(|| Regex::new(r#"<li[^>]+id=["']work_(\d+)["']"#).unwrap());
        static RE_AUTHOR: Lazy<Regex> =
            Lazy::new(|| Regex::new(r#"rel=["']author["'][^>]*>([^<]+)</a>"#).unwrap());
        static RE_FANDOM_BLOCK: Lazy<Regex> = Lazy::new(|| {
            Regex::new(r#"(?s)<h5[^>]+class=["'][^"']*fandoms[^"']*["'][^>]*>(.*?)</h5>"#).unwrap()
        });
        static RE_TAG_LINK: Lazy<Regex> =
            Lazy::new(|| Regex::new(r#"<a[^>]+class=["']tag["'][^>]*>([^<]+)</a>"#).unwrap());
        static RE_WORDS: Lazy<Regex> =
            Lazy::new(|| Regex::new(r#"<dd[^>]+class=["']words["'][^>]*>([0-9,]+)</dd>"#).unwrap());
        static RE_CHAP: Lazy<Regex> = Lazy::new(|| {
            Regex::new(
                r#"<dd[^>]+class=["']chapters["'][^>]*>(?:<a[^>]*>)?(\d+)(?:</a>)?/(\d+|\?)"#,
            )
            .unwrap()
        });
        static RE_TAGS_LIST: Lazy<Regex> = Lazy::new(|| {
            Regex::new(r#"<li[^>]*class=["']([^"']+)["'][^>]*>\s*<a[^>]*class=["']tag["'][^>]*>([^<]+)</a>"#).unwrap()
        });
        static RE_REQ_RATING: Lazy<Regex> = Lazy::new(|| {
            Regex::new(r#"<span[^>]+class=["'][^"']*rating-([a-z-]+)[^"']*["'][^>]*title=["']([^"']+)["']"#).unwrap()
        });
        static RE_REQ_CAT: Lazy<Regex> = Lazy::new(|| {
            Regex::new(r#"<span[^>]+class=["'][^"']*category-([a-z-]+)[^"']*["'][^>]*title=["']([^"']+)["']"#).unwrap()
        });

        let mut matches: Vec<(i32, usize)> = Vec::new();
        for caps in RE_SPLIT.captures_iter(&html) {
            if let Some(m) = caps.get(0) {
                let wid = caps[1].parse::<i32>().unwrap_or(0);
                matches.push((wid, m.start()));
            }
        }

        let mut works = Vec::new();
        for i in 0..matches.len() {
            let (work_id, start_pos) = matches[i];
            let end_pos = if i + 1 < matches.len() {
                matches[i + 1].1
            } else {
                html.len()
            };
            let block = &html[start_pos..end_pos];

            let mut w = Ao3WorkSummary {
                work_id,
                ..Default::default()
            };

            let title_pattern = format!(
                r#"<a[^>]+href=["']/works/{}["'][^>]*>([^<]+)</a>"#,
                work_id
            );
            if let Ok(re_title) = Regex::new(&title_pattern) {
                if let Some(tc) = re_title.captures(block) {
                    w.title = tc[1].trim().to_string();
                }
            }
            if w.title.is_empty() {
                w.title = format!("Work {}", work_id);
            }

            let authors: Vec<String> = RE_AUTHOR
                .captures_iter(block)
                .map(|c| c[1].trim().to_string())
                .collect();
            w.pseud = if authors.is_empty() {
                "Anonymous".to_string()
            } else {
                authors.join(", ")
            };

            let mut fandom_list = Vec::new();
            if let Some(fb) = RE_FANDOM_BLOCK.captures(block) {
                for fc in RE_TAG_LINK.captures_iter(&fb[1]) {
                    fandom_list.push(fc[1].trim().to_string());
                }
            }
            w.fandom = if fandom_list.is_empty() {
                "Unspecified".to_string()
            } else {
                fandom_list.join(", ")
            };

            if let Some(wc) = RE_WORDS.captures(block) {
                let num_str = wc[1].replace(',', "");
                w.word_count = num_str.parse::<usize>().unwrap_or(0);
            }

            if let Some(cc) = RE_CHAP.captures(block) {
                w.chapter_count = cc[1].parse::<usize>().unwrap_or(1);
                let tot_str = &cc[2];
                w.total_chapters = if tot_str == "?" {
                    -1
                } else {
                    tot_str.parse::<i32>().unwrap_or(1)
                };
            } else {
                w.chapter_count = 1;
                w.total_chapters = 1;
            }

            for tc in RE_TAGS_LIST.captures_iter(block) {
                let cls = &tc[1];
                let tag_text = sanitize_tag_text(&tc[2]);
                if cls.contains("warning") {
                    if !w.archive_warnings.contains(&tag_text) {
                        w.archive_warnings.push(tag_text);
                    }
                } else if cls.contains("relationship") {
                    if !w.relationships.contains(&tag_text) {
                        w.relationships.push(tag_text);
                    }
                } else if cls.contains("character") {
                    if !w.characters.contains(&tag_text) {
                        w.characters.push(tag_text);
                    }
                } else if cls.contains("freeform") {
                    if !w.tags.contains(&tag_text) {
                        w.tags.push(tag_text);
                    }
                }
            }

            if let Some(rc) = RE_REQ_RATING.captures(block) {
                w.rating = rc[2].trim().to_string();
            }

            if let Some(cat_c) = RE_REQ_CAT.captures(block) {
                for c in cat_c[2].split(',') {
                    let trimmed = c.trim().to_string();
                    if !trimmed.is_empty() && !w.categories.contains(&trimmed) {
                        w.categories.push(trimmed);
                    }
                }
            }

            works.push(w);
        }

        works
    }

    pub fn parse_next_page_url(html_data: &[u8]) -> String {
        let html = String::from_utf8_lossy(html_data);
        static RE: Lazy<Regex> =
            Lazy::new(|| Regex::new(r#"<a[^>]+rel=["']next["'][^>]+href=["']([^"']+)["']"#).unwrap());
        RE.captures(&html)
            .map(|c| c[1].to_string())
            .unwrap_or_default()
    }

    pub fn parse_skins_list(html_data: &[u8]) -> Vec<Ao3WorkSkin> {
        let html = String::from_utf8_lossy(html_data);
        static RE_SKIN: Lazy<Regex> = Lazy::new(|| {
            Regex::new(r#"(?s)<li[^>]+id=["']skin_(\d+)["'][^>]*>(.*?)</li>"#).unwrap()
        });
        static RE_TITLE: Lazy<Regex> =
            Lazy::new(|| Regex::new(r#"<h4[^>]*>\s*<a[^>]*>([^<]+)</a>"#).unwrap());

        let mut skins = Vec::new();
        for caps in RE_SKIN.captures_iter(&html) {
            let skin_id = caps[1].parse::<i32>().unwrap_or(0);
            let block = &caps[2];
            let name = RE_TITLE
                .captures(block)
                .map(|c| c[1].trim().to_string())
                .unwrap_or_else(|| format!("Skin {}", skin_id));
            let block_lower = block.to_lowercase();
            let is_work_skin =
                block_lower.contains("workskin") || block_lower.contains("work skin");
            skins.push(Ao3WorkSkin {
                skin_id,
                name,
                css_content: String::new(),
                is_work_skin,
            });
        }
        skins
    }

    pub fn parse_skin_css(html_data: &[u8]) -> String {
        let html = String::from_utf8_lossy(html_data);
        static RE1: Lazy<Regex> = Lazy::new(|| {
            Regex::new(r#"(?s)<textarea[^>]+id=["']skin_css["'][^>]*>(.*?)</textarea>"#).unwrap()
        });
        static RE2: Lazy<Regex> =
            Lazy::new(|| Regex::new(r#"(?s)<pre[^>]*>(.*?)</pre>"#).unwrap());

        if let Some(caps) = RE1.captures(&html) {
            return caps[1].trim().to_string();
        }
        if let Some(caps) = RE2.captures(&html) {
            return caps[1].trim().to_string();
        }
        String::new()
    }

    pub fn parse_full_work(html_data: &[u8]) -> Ao3FullWork {
        let html = String::from_utf8_lossy(html_data);
        let mut full_work = Ao3FullWork::default();

        static RE_TITLE_HEADER: Lazy<Regex> = Lazy::new(|| {
            Regex::new(r#"(?s)<h2[^>]*class=["'][^"']*title[^"']*["'][^>]*>\s*(.*?)\s*</h2>"#)
                .unwrap()
        });
        if let Some(caps) = RE_TITLE_HEADER.captures(&html) {
            full_work.summary.title = strip_html_tags(&caps[1]);
        }

        let authors = extract_authors(&html);
        if !authors.is_empty() {
            full_work.summary.pseud = authors.join(", ");
        }

        let ratings = extract_tags_from_dd(&html, "rating");
        if let Some(first_rating) = ratings.first() {
            full_work.summary.rating = first_rating.clone();
        } else {
            static RE_RATE: Lazy<Regex> = Lazy::new(|| {
                Regex::new(r#"(?s)<dd[^>]+class=["'][^"']*rating[^"']*tags[^"']*["'][^>]*>(.*?)</dd>"#).unwrap()
            });
            if let Some(caps) = RE_RATE.captures(&html) {
                full_work.summary.rating = sanitize_tag_text(&strip_html_tags(&caps[1]));
            }
        }
        if full_work.summary.rating.is_empty() {
            full_work.summary.rating = "Not Rated".to_string();
        }

        full_work.summary.archive_warnings = extract_tags_from_dd(&html, "warning");
        full_work.summary.categories = extract_tags_from_dd(&html, "category");
        full_work.summary.fandoms = extract_tags_from_dd(&html, "fandom");
        if !full_work.summary.fandoms.is_empty() {
            full_work.summary.fandom = full_work.summary.fandoms.join(", ");
        }
        full_work.summary.relationships = extract_tags_from_dd(&html, "relationship");
        full_work.summary.characters = extract_tags_from_dd(&html, "character");
        full_work.summary.tags = extract_tags_from_dd(&html, "freeform");

        static RE_SUMMARY: Lazy<Regex> = Lazy::new(|| {
            Regex::new(r#"(?s)<div[^>]+class=["'][^"']*summary[^"']*module[^"']*["'][^>]*>\s*<blockquote[^>]*class=["']userstuff["'][^>]*>(.*?)</blockquote>"#).unwrap()
        });
        if let Some(caps) = RE_SUMMARY.captures(&html) {
            full_work.summary.summary_text = strip_html_tags(&caps[1]);
        }

        static RE_SKIN1: Lazy<Regex> = Lazy::new(|| {
            Regex::new(r#"(?s)<style[^>]*id=["']workskin["'][^>]*>(.*?)</style>"#).unwrap()
        });
        static RE_SKIN2: Lazy<Regex> =
            Lazy::new(|| Regex::new(r#"(?s)<style[^>]*>(.*?)</style>"#).unwrap());

        if let Some(caps) = RE_SKIN1.captures(&html).or_else(|| RE_SKIN2.captures(&html)) {
            full_work.work_skin_css = caps[1].trim().to_string();
        }

        static RE_CHAP_SPLIT: Lazy<Regex> =
            Lazy::new(|| Regex::new(r#"<div[^>]+id=["']chapter-\d+["'][^>]*>"#).unwrap());
        static RE_CHAP_TITLE: Lazy<Regex> = Lazy::new(|| {
            Regex::new(r#"(?s)<h3[^>]*class=["']title["'][^>]*>\s*<a[^>]*>[^<]+</a>:?\s*(.*?)\s*</h3>"#).unwrap()
        });
        static RE_LANDMARK: Lazy<Regex> = Lazy::new(|| {
            Regex::new(r#"(?s)^\s*<h3[^>]*class=["']landmark[^"'\w]*heading["'][^>]*>.*?</h3>"#)
                .unwrap()
        });
        static RE_COMMENTS: Lazy<Regex> =
            Lazy::new(|| Regex::new(r#"(?s)<!--.*?-->"#).unwrap());

        let blocks: Vec<&str> = RE_CHAP_SPLIT.split(&html).collect();
        if blocks.len() > 1 {
            for (idx, block) in blocks.iter().enumerate().skip(1) {
                let mut chap = Ao3ChapterContent {
                    chapter_number: idx,
                    ..Default::default()
                };

                if let Some(tc) = RE_CHAP_TITLE.captures(block) {
                    chap.title = strip_html_tags(&tc[1]);
                }
                if chap.title.is_empty() {
                    chap.title = format!("Chapter {}", idx);
                }

                let start_idx = block
                    .find("role=\"article\"")
                    .or_else(|| block.find("class=\"userstuff"));
                if let Some(s_idx) = start_idx {
                    if let Some(rel_gt) = block[s_idx..].find('>') {
                        let body_start = s_idx + rel_gt + 1;
                        let mut body_end = block.len();
                        let end_terms = [
                            "<div class=\"chapter preface",
                            "<div id=\"work_endnotes\"",
                            "<div id=\"feedback\"",
                            "<!--chapter end-->",
                            "<!--/chapter-->",
                        ];
                        for term in end_terms {
                            if let Some(rel_pos) = block[body_start..].find(term) {
                                let pos = body_start + rel_pos;
                                if pos < body_end {
                                    body_end = pos;
                                }
                            }
                        }
                        let raw_body = block[body_start..body_end].trim();
                        let without_landmark = RE_LANDMARK.replace(raw_body, "");
                        let without_comments = RE_COMMENTS.replace_all(&without_landmark, "");
                        chap.body_html = without_comments.trim().to_string();
                    }
                }
                full_work.chapters.push(chap);
            }
        } else {
            let mut chap = Ao3ChapterContent {
                chapter_number: 1,
                title: if full_work.summary.title.is_empty() {
                    "Chapter 1".to_string()
                } else {
                    full_work.summary.title.clone()
                },
                ..Default::default()
            };
            static RE_SINGLE_BODY: Lazy<Regex> = Lazy::new(|| {
                Regex::new(r#"(?s)<div[^>]+class=["']userstuff[^"']*["'][^>]*>(.*?)</div>"#)
                    .unwrap()
            });
            if let Some(caps) = RE_SINGLE_BODY.captures(&html) {
                let without_comments = RE_COMMENTS.replace_all(caps[1].trim(), "");
                chap.body_html = without_comments.trim().to_string();
            }
            full_work.chapters.push(chap);
        }

        // Scan all chapters for embedded <img> tags
        static RE_IMG: Lazy<Regex> =
            Lazy::new(|| Regex::new(r#"(?i)<img\b([^>]*?)/?>"#).unwrap());
        static RE_SRC: Lazy<Regex> =
            Lazy::new(|| Regex::new(r#"(?i)src\s*=\s*["']([^"']+)["']"#).unwrap());
        static RE_ALT: Lazy<Regex> =
            Lazy::new(|| Regex::new(r#"(?i)alt\s*=\s*["']([^"']*)["']"#).unwrap());
        static RE_IMG_TITLE: Lazy<Regex> =
            Lazy::new(|| Regex::new(r#"(?i)title\s*=\s*["']([^"']*)["']"#).unwrap());

        for chap in &full_work.chapters {
            for img_caps in RE_IMG.captures_iter(&chap.body_html) {
                let attrs = &img_caps[1];
                if let Some(src_caps) = RE_SRC.captures(attrs) {
                    let mut embed = Ao3ImageEmbed {
                        url: src_caps[1].trim().to_string(),
                        chapter_number: chap.chapter_number,
                        ..Default::default()
                    };
                    if let Some(alt_caps) = RE_ALT.captures(attrs) {
                        embed.alt = sanitize_tag_text(&alt_caps[1]);
                    }
                    if let Some(tit_caps) = RE_IMG_TITLE.captures(attrs) {
                        embed.title = sanitize_tag_text(&tit_caps[1]);
                    }
                    full_work.embedded_images.push(embed);
                }
            }
        }

        full_work
    }
}
