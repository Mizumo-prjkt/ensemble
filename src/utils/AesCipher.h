#pragma once

#include <QByteArray>
#include <QString>

class AesCipher
{
public:
    static constexpr int kBlockSize = 16;
    static constexpr int kKeySize = 32; // AES-256
    static constexpr int kIvSize = 16;

    // Raw AES-256-ECB single block encrypt / decrypt
    static void encryptBlock(const uint8_t in[16], uint8_t out[16], const uint8_t roundKeys[240]);
    static void decryptBlock(const uint8_t in[16], uint8_t out[16], const uint8_t roundKeys[240]);

    // Key expansion for AES-256 (32 bytes key -> 240 bytes round keys)
    static void keyExpansion(const uint8_t key[32], uint8_t roundKeys[240]);

    // AES-256-CBC with PKCS#7 padding
    static QByteArray encryptCbc(const QByteArray &plaintext, const QByteArray &key, const QByteArray &iv);
    static QByteArray decryptCbc(const QByteArray &ciphertext, const QByteArray &key, const QByteArray &iv, bool *ok = nullptr);

    // High-level Authenticated Encryption Envelope (AES-256-CBC + HMAC-SHA256 with PBKDF2)
    // Produces a JSON document envelope with salt, iv, ciphertext, and hmac
    static QByteArray encryptEnvelope(const QByteArray &plaintext, const QByteArray &passphraseOrSecret, QString *error = nullptr);
    static QByteArray decryptEnvelope(const QByteArray &envelopeJson, const QByteArray &passphraseOrSecret, QString *error = nullptr);

    // Derive a stable project master secret if none provided
    static QByteArray defaultProjectSecret();
};
