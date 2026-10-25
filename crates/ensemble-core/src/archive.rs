use std::collections::BTreeMap;
use std::fs::{self, File};
use std::io::{Cursor, Read, Write};
use std::path::Path;
use zip::write::SimpleFileOptions;
use zip::{CompressionMethod, ZipArchive, ZipWriter};

pub const ZSTD_MAGIC: [u8; 4] = [0x28, 0xB5, 0x2F, 0xFD];
pub const ZIP_MAGIC: [u8; 4] = [0x50, 0x4B, 0x03, 0x04];
pub const ZIP_EMPTY_MAGIC: [u8; 4] = [0x50, 0x4B, 0x05, 0x06];
pub const ZIP_SPANNED_MAGIC: [u8; 4] = [0x50, 0x4B, 0x07, 0x08];

/// Pure-Rust in-memory archive handler for `.ao3proj` and `.ao3projasve` containers.
/// Supports both native Zstandard tar frames (`0x28B52FFD`) and standard PKZIP (`0x504B0304`),
/// with zero external subprocesses (`python`, `powershell`, or `tar`).
pub struct ZstdArchive;

impl ZstdArchive {
    pub fn is_zstd_archive(path: impl AsRef<Path>) -> bool {
        let Ok(mut f) = File::open(path.as_ref()) else {
            return false;
        };
        let mut header = [0u8; 64];
        let Ok(n) = f.read(&mut header) else {
            return false;
        };
        if n < 4 {
            return false;
        }
        let h4 = &header[..4];
        h4 == ZSTD_MAGIC || h4 == ZIP_MAGIC || h4 == ZIP_EMPTY_MAGIC || h4 == ZIP_SPANNED_MAGIC
    }

    /// Compresses `file_entries` into `dest_path` using a Zstandard-compressed TAR container (`0x28B52FFD`).
    pub fn compress_archive(
        dest_path: impl AsRef<Path>,
        file_entries: &BTreeMap<String, Vec<u8>>,
    ) -> Result<(), String> {
        let mut tar_buf = Vec::new();
        {
            let mut builder = tar::Builder::new(&mut tar_buf);
            for (rel_path, data) in file_entries {
                let clean_path = rel_path.trim_start_matches("./").replace('\\', "/");
                let mut header = tar::Header::new_gnu();
                header.set_size(data.len() as u64);
                header.set_mode(0o644);
                header.set_cksum();
                builder
                    .append_data(&mut header, &clean_path, Cursor::new(data))
                    .map_err(|e| format!("Failed to append '{}' to tar builder: {}", clean_path, e))?;
            }
            builder
                .finish()
                .map_err(|e| format!("Failed to finalize tar container: {}", e))?;
        }

        let compressed = zstd::stream::encode_all(Cursor::new(tar_buf), 3)
            .map_err(|e| format!("Zstandard compression failed: {}", e))?;

        let dest = dest_path.as_ref();
        if let Some(parent) = dest.parent() {
            if !parent.as_os_str().is_empty() {
                let _ = fs::create_dir_all(parent);
            }
        }
        let mut out_file =
            File::create(dest).map_err(|e| format!("Could not create '{}': {}", dest.display(), e))?;
        out_file
            .write_all(&compressed)
            .map_err(|e| format!("Could not write archive '{}': {}", dest.display(), e))?;
        out_file
            .flush()
            .map_err(|e| format!("Could not flush archive '{}': {}", dest.display(), e))?;
        Ok(())
    }

    /// Compresses `file_entries` into a standard PKZIP Deflate container (`0x504B0304`).
    pub fn compress_zip_archive(
        dest_path: impl AsRef<Path>,
        file_entries: &BTreeMap<String, Vec<u8>>,
    ) -> Result<(), String> {
        let mut zip_buf = Cursor::new(Vec::new());
        {
            let mut zip = ZipWriter::new(&mut zip_buf);
            let options = SimpleFileOptions::default()
                .compression_method(CompressionMethod::Deflated)
                .unix_permissions(0o644);
            for (rel_path, data) in file_entries {
                let clean_path = rel_path.trim_start_matches("./").replace('\\', "/");
                zip.start_file(&clean_path, options)
                    .map_err(|e| format!("ZIP start_file failed: {}", e))?;
                zip.write_all(data)
                    .map_err(|e| format!("ZIP write_all failed: {}", e))?;
            }
            zip.finish()
                .map_err(|e| format!("ZIP finish failed: {}", e))?;
        }

        let dest = dest_path.as_ref();
        if let Some(parent) = dest.parent() {
            if !parent.as_os_str().is_empty() {
                let _ = fs::create_dir_all(parent);
            }
        }
        fs::write(dest, zip_buf.into_inner())
            .map_err(|e| format!("Could not write ZIP archive '{}': {}", dest.display(), e))
    }

    /// Decompresses either a Zstandard frame (`0x28B52FFD`) or a PKZIP archive (`0x504B0304`) into `BTreeMap`.
    pub fn decompress_archive(
        src_path: impl AsRef<Path>,
    ) -> Result<BTreeMap<String, Vec<u8>>, String> {
        let src = src_path.as_ref();
        let archive_bytes = fs::read(src)
            .map_err(|e| format!("Could not read archive '{}': {}", src.display(), e))?;
        if archive_bytes.len() < 4 {
            return Err("Archive is smaller than 4-byte magic header".to_string());
        }

        let h4 = &archive_bytes[..4];
        if h4 == ZSTD_MAGIC {
            let decompressed = zstd::stream::decode_all(Cursor::new(&archive_bytes))
                .map_err(|e| format!("Zstandard decompression failed: {}", e))?;
            if decompressed.len() >= 4 && &decompressed[..4] == ZIP_MAGIC {
                return Self::extract_zip_bytes(&decompressed);
            }
            return Self::extract_tar_bytes(&decompressed);
        }

        if h4 == ZIP_MAGIC || h4 == ZIP_EMPTY_MAGIC || h4 == ZIP_SPANNED_MAGIC {
            return Self::extract_zip_bytes(&archive_bytes);
        }

        Err(format!(
            "Unrecognized archive signature for '{}'",
            src.display()
        ))
    }

    fn extract_tar_bytes(bytes: &[u8]) -> Result<BTreeMap<String, Vec<u8>>, String> {
        let mut archive = tar::Archive::new(Cursor::new(bytes));
        let mut out_entries = BTreeMap::new();
        let entries = archive
            .entries()
            .map_err(|e| format!("Invalid TAR stream: {}", e))?;
        for entry_res in entries {
            let mut entry = entry_res.map_err(|e| format!("Corrupt TAR entry: {}", e))?;
            if entry.header().entry_type().is_dir() {
                continue;
            }
            let path_str = entry
                .path()
                .map_err(|e| format!("Invalid TAR entry path: {}", e))?
                .to_string_lossy()
                .replace('\\', "/");
            let clean_path = path_str.trim_start_matches("./").to_string();
            if clean_path.is_empty() {
                continue;
            }
            let mut data = Vec::new();
            entry
                .read_to_end(&mut data)
                .map_err(|e| format!("Failed reading TAR entry '{}': {}", clean_path, e))?;
            out_entries.insert(clean_path, data);
        }
        if out_entries.is_empty() {
            return Err("Extracted TAR archive contained no files".to_string());
        }
        Ok(out_entries)
    }

    fn extract_zip_bytes(bytes: &[u8]) -> Result<BTreeMap<String, Vec<u8>>, String> {
        let mut zip = ZipArchive::new(Cursor::new(bytes))
            .map_err(|e| format!("Invalid ZIP archive: {}", e))?;
        let mut out_entries = BTreeMap::new();
        for i in 0..zip.len() {
            let mut file = zip
                .by_index(i)
                .map_err(|e| format!("Corrupt ZIP entry #{}: {}", i, e))?;
            if file.is_dir() {
                continue;
            }
            let name = file.name().replace('\\', "/");
            let clean_path = name.trim_start_matches("./").to_string();
            if clean_path.is_empty() {
                continue;
            }
            let mut data = Vec::with_capacity(file.size() as usize);
            file.read_to_end(&mut data)
                .map_err(|e| format!("Failed reading ZIP entry '{}': {}", clean_path, e))?;
            out_entries.insert(clean_path, data);
        }
        if out_entries.is_empty() {
            return Err("Extracted ZIP archive contained no files".to_string());
        }
        Ok(out_entries)
    }
}
