use aes::cipher::{block_padding::Pkcs7, BlockDecryptMut, BlockEncryptMut, KeyIvInit};
use base64::{engine::general_purpose::STANDARD as BASE64, Engine as _};
use hmac::{Hmac, Mac};
use pbkdf2::pbkdf2_hmac;
use rand::RngCore;
use serde::{Deserialize, Serialize};
use sha2::Sha256;
use subtle::ConstantTimeEq;

type Aes256CbcEnc = cbc::Encryptor<aes::Aes256>;
type Aes256CbcDec = cbc::Decryptor<aes::Aes256>;
type HmacSha256 = Hmac<Sha256>;

#[derive(Debug, Serialize, Deserialize)]
struct EncryptedEnvelope {
    cipher: String,
    version: u32,
    salt: String,
    iv: String,
    ciphertext: String,
    hmac: String,
}

/// Authenticated AES-256-CBC + HMAC-SHA256 + PBKDF2-SHA256 (1024 iterations) cipher.
/// Maintains 100% format compatibility with Ensemble v1.1/v1.2 `agentifiers.json` envelopes.
pub struct AesCipher;

impl AesCipher {
    pub const BLOCK_SIZE: usize = 16;
    pub const KEY_SIZE: usize = 32;
    pub const IV_SIZE: usize = 16;

    pub fn default_project_secret() -> &'static [u8] {
        b"Ensemble::Agentic::IdentityMasterSecret::v1"
    }

    pub fn encrypt_cbc(plaintext: &[u8], key: &[u8], iv: &[u8]) -> Option<Vec<u8>> {
        if key.len() != Self::KEY_SIZE || iv.len() != Self::IV_SIZE {
            return None;
        }
        let encryptor = Aes256CbcEnc::new_from_slices(key, iv).ok()?;
        Some(encryptor.encrypt_padded_vec_mut::<Pkcs7>(plaintext))
    }

    pub fn decrypt_cbc(ciphertext: &[u8], key: &[u8], iv: &[u8]) -> Option<Vec<u8>> {
        if key.len() != Self::KEY_SIZE
            || iv.len() != Self::IV_SIZE
            || ciphertext.is_empty()
            || ciphertext.len() % Self::BLOCK_SIZE != 0
        {
            return None;
        }
        let decryptor = Aes256CbcDec::new_from_slices(key, iv).ok()?;
        decryptor.decrypt_padded_vec_mut::<Pkcs7>(ciphertext).ok()
    }

    pub fn encrypt_envelope(plaintext: &[u8], passphrase_or_secret: &[u8]) -> Result<Vec<u8>, String> {
        let secret = if passphrase_or_secret.is_empty() {
            Self::default_project_secret()
        } else {
            passphrase_or_secret
        };

        let mut salt = [0u8; 16];
        let mut iv = [0u8; 16];
        let mut rng = rand::thread_rng();
        rng.fill_bytes(&mut salt);
        rng.fill_bytes(&mut iv);

        let mut derived = [0u8; 64];
        pbkdf2_hmac::<Sha256>(secret, &salt, 1024, &mut derived);

        let enc_key = &derived[0..32];
        let mac_key = &derived[32..64];

        let ciphertext = Self::encrypt_cbc(plaintext, enc_key, &iv)
            .ok_or_else(|| "AES encryption failed".to_string())?;

        // Encrypt-then-MAC: HMAC-SHA256(macKey, salt || iv || ciphertext)
        let mut mac = <HmacSha256 as Mac>::new_from_slice(mac_key)
            .map_err(|_| "HMAC initialization failed".to_string())?;
        mac.update(&salt);
        mac.update(&iv);
        mac.update(&ciphertext);
        let mac_bytes = mac.finalize().into_bytes();

        let envelope = EncryptedEnvelope {
            cipher: "aes-256-cbc-hmac-sha256".to_string(),
            version: 1,
            salt: BASE64.encode(salt),
            iv: BASE64.encode(iv),
            ciphertext: BASE64.encode(&ciphertext),
            hmac: BASE64.encode(mac_bytes),
        };

        serde_json::to_vec_pretty(&envelope)
            .map_err(|e| format!("Envelope JSON serialization failed: {}", e))
    }

    pub fn decrypt_envelope(
        envelope_json: &[u8],
        passphrase_or_secret: &[u8],
    ) -> Result<Vec<u8>, String> {
        let envelope: EncryptedEnvelope = serde_json::from_slice(envelope_json)
            .map_err(|e| format!("Invalid envelope JSON: {}", e))?;

        if envelope.cipher != "aes-256-cbc-hmac-sha256" {
            return Err(format!("Unsupported cipher format: {}", envelope.cipher));
        }

        let salt = BASE64
            .decode(envelope.salt.as_bytes())
            .map_err(|_| "Invalid base64 salt".to_string())?;
        let iv = BASE64
            .decode(envelope.iv.as_bytes())
            .map_err(|_| "Invalid base64 iv".to_string())?;
        let ciphertext = BASE64
            .decode(envelope.ciphertext.as_bytes())
            .map_err(|_| "Invalid base64 ciphertext".to_string())?;
        let expected_hmac = BASE64
            .decode(envelope.hmac.as_bytes())
            .map_err(|_| "Invalid base64 hmac".to_string())?;

        if salt.len() != 16 || iv.len() != 16 {
            return Err("Invalid salt or IV length".to_string());
        }

        let secret = if passphrase_or_secret.is_empty() {
            Self::default_project_secret()
        } else {
            passphrase_or_secret
        };

        let mut derived = [0u8; 64];
        pbkdf2_hmac::<Sha256>(secret, &salt, 1024, &mut derived);

        let enc_key = &derived[0..32];
        let mac_key = &derived[32..64];

        let mut mac = <HmacSha256 as Mac>::new_from_slice(mac_key)
            .map_err(|_| "HMAC initialization failed".to_string())?;
        mac.update(&salt);
        mac.update(&iv);
        mac.update(&ciphertext);
        let actual_hmac = mac.finalize().into_bytes();

        if actual_hmac.as_slice().ct_ne(expected_hmac.as_slice()).into() {
            return Err("HMAC verification failed: corrupted data or wrong key".to_string());
        }

        Self::decrypt_cbc(&ciphertext, enc_key, &iv)
            .ok_or_else(|| "AES decryption or padding verification failed".to_string())
    }
}
