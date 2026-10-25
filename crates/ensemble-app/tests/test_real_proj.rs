use ensemble_core::project::Ao3Project;
use ensemble_core::serializer::ProjectSerializer;
use std::path::Path;

#[test]
fn test_load_all_user_projects() {
    let valid_files = [
        "/home/miku/Documents/fromthesekai-toteyvat.ao3proj",
        "/home/miku/Documents/1t.ao3proj",
        "/home/miku/Documents/thechimerasaria.ao3proj",
    ];

    for file_str in valid_files {
        let p = Path::new(file_str);
        if !p.exists() {
            println!("Project file {} not found, skipping.", file_str);
            continue;
        }
        let mut proj = Ao3Project::new();
        let report = ProjectSerializer::load_with_validation(p, &mut proj, None);
        assert!(
            report.success,
            "Failed to load {}: {}",
            file_str, report.summary_error
        );
        assert!(!proj.chapters.is_empty());
    }

    // Verify invalid/corrupt container detection on test_zip.ao3proj
    let zip_path = Path::new("/home/miku/Documents/test_zip.ao3proj");
    if zip_path.exists() {
        let mut proj = Ao3Project::new();
        let report = ProjectSerializer::load_with_validation(zip_path, &mut proj, None);
        assert!(!report.success);
    }
}
