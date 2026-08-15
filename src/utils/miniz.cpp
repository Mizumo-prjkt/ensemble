/* Gemini 3.7 Flash (High)'s miniz Implementation.

    It is not really miniz in a sense. However, logics and concepts remain the
   same However, this code is still under study.
*/
#include "miniz.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define MZ_MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MZ_MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MZ_CLEAR_OBJ(obj) memset(&(obj), 0, sizeof(obj))

#define MZ_ZIP_END_OF_CENTRAL_DIR_HEADER_SIG 0x06054b50
#define MZ_ZIP_CENTRAL_DIR_HEADER_SIG 0x02014b50
#define MZ_ZIP_LOCAL_DIR_HEADER_SIG 0x04034b50

#define MZ_ZIP_CDH_SIZE 46
#define MZ_ZIP_LDH_SIZE 30
#define MZ_ZIP_ECDH_SIZE 22

/* CRC-32 calculation */
static mz_uint32 mz_crc32(mz_uint32 crc, const mz_uint8 *ptr, size_t buf_len) {
  return (mz_uint32)crc32((uLong)crc, (const Bytef *)ptr, (uInt)buf_len);
}

/* Decompress raw Deflate stream (method 8) or Stored (method 0) */
static mz_bool mz_decompress_data(const void *pSrc, size_t src_len,
                                  void **ppDst, size_t *pDst_len,
                                  mz_uint16 method) {
  if (method == 0) { /* Stored / Uncompressed */
    *ppDst = malloc(src_len ? src_len : 1);
    if (!*ppDst)
      return MZ_FALSE;
    if (src_len > 0)
      memcpy(*ppDst, pSrc, src_len);
    *pDst_len = src_len;
    return MZ_TRUE;
  }

  if (method == 8) { /* Deflated */
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.next_in = (Bytef *)pSrc;
    strm.avail_in = (uInt)src_len;

    /* -MAX_WBITS tells zlib to decode raw Deflate stream (RFC 1951) without
     * zlib wrapper */
    if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) {
      return MZ_FALSE;
    }

    size_t expected_size = (*pDst_len > 0) ? *pDst_len : (src_len * 4 + 1024);
    Bytef *pOut = (Bytef *)malloc(expected_size ? expected_size : 1);
    if (!pOut) {
      inflateEnd(&strm);
      return MZ_FALSE;
    }

    strm.next_out = pOut;
    strm.avail_out = (uInt)expected_size;

    int status = inflate(&strm, Z_SYNC_FLUSH);
    while (status == Z_OK && strm.avail_out == 0) {
      size_t current_len = expected_size;
      expected_size = expected_size * 2 + 1024;
      Bytef *pNew = (Bytef *)realloc(pOut, expected_size);
      if (!pNew) {
        free(pOut);
        inflateEnd(&strm);
        return MZ_FALSE;
      }
      pOut = pNew;
      strm.next_out = pOut + current_len;
      strm.avail_out = (uInt)(expected_size - current_len);
      status = inflate(&strm, Z_SYNC_FLUSH);
    }

    if (status != Z_STREAM_END && status != Z_OK) {
      free(pOut);
      inflateEnd(&strm);
      return MZ_FALSE;
    }

    *pDst_len = strm.total_out;
    *ppDst = pOut;
    inflateEnd(&strm);
    return MZ_TRUE;
  }

  return MZ_FALSE;
}

/* Struct internal state */
struct mz_zip_internal_state_tag {
  mz_zip_archive_file_stat *m_pStats;
  mz_uint8 *m_pMem;
  size_t m_mem_size;
  size_t m_mem_capacity;
};

extern "C" {

void mz_zip_zero_struct(mz_zip_archive *pZip) {
  if (pZip)
    memset(pZip, 0, sizeof(*pZip));
}

void mz_free(void *p) { free(p); }

/* ========================================================================= */
/* ZIP WRITER                                                                */
/* ========================================================================= */

mz_bool mz_zip_writer_init_heap(mz_zip_archive *pZip,
                                size_t size_to_reserve_at_beginning,
                                size_t initial_allocation_size) {
  (void)size_to_reserve_at_beginning;
  if (!pZip)
    return MZ_FALSE;
  mz_zip_zero_struct(pZip);

  pZip->m_pState =
      (mz_zip_internal_state *)calloc(1, sizeof(mz_zip_internal_state));
  if (!pZip->m_pState)
    return MZ_FALSE;

  size_t cap = initial_allocation_size ? initial_allocation_size : 16384;
  pZip->m_pState->m_pMem = (mz_uint8 *)malloc(cap);
  if (!pZip->m_pState->m_pMem) {
    free(pZip->m_pState);
    pZip->m_pState = NULL;
    return MZ_FALSE;
  }
  pZip->m_pState->m_mem_capacity = cap;
  pZip->m_pState->m_mem_size = 0;
  pZip->m_zip_mode = MZ_ZIP_MODE_WRITING;
  pZip->m_zip_type = MZ_ZIP_TYPE_HEAP;
  return MZ_TRUE;
}

static mz_bool mz_zip_writer_append_bytes(mz_zip_archive *pZip,
                                          const void *pBuf, size_t size) {
  if (!pZip || !pZip->m_pState)
    return MZ_FALSE;
  if (pZip->m_pState->m_mem_size + size > pZip->m_pState->m_mem_capacity) {
    size_t new_cap = MZ_MAX(pZip->m_pState->m_mem_capacity * 2,
                            pZip->m_pState->m_mem_size + size + 4096);
    mz_uint8 *pNew = (mz_uint8 *)realloc(pZip->m_pState->m_pMem, new_cap);
    if (!pNew)
      return MZ_FALSE;
    pZip->m_pState->m_pMem = pNew;
    pZip->m_pState->m_mem_capacity = new_cap;
  }
  memcpy(pZip->m_pState->m_pMem + pZip->m_pState->m_mem_size, pBuf, size);
  pZip->m_pState->m_mem_size += size;
  return MZ_TRUE;
}

mz_bool mz_zip_writer_add_mem(mz_zip_archive *pZip, const char *pArchive_name,
                              const void *pBuf, size_t buf_size,
                              mz_uint level_and_flags) {
  (void)level_and_flags;
  if (!pZip || !pZip->m_pState || !pArchive_name ||
      pZip->m_zip_mode != MZ_ZIP_MODE_WRITING)
    return MZ_FALSE;

  size_t filename_len = strlen(pArchive_name);
  if (filename_len > MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE - 1)
    return MZ_FALSE;

  mz_uint32 crc = mz_crc32(0, (const mz_uint8 *)pBuf, buf_size);
  mz_uint64 local_hdr_ofs = (mz_uint64)pZip->m_pState->m_mem_size;

  /* Write Local Directory Header (30 bytes + filename) */
  mz_uint8 ldh[MZ_ZIP_LDH_SIZE];
  memset(ldh, 0, sizeof(ldh));
  ldh[0] = 0x50;
  ldh[1] = 0x4b;
  ldh[2] = 0x03;
  ldh[3] = 0x04; /* PK\x03\x04 */
  ldh[4] = 20;
  ldh[5] = 0; /* version needed: 2.0 */
  ldh[6] = 0;
  ldh[7] = 0; /* bit flag: 0 */
  ldh[8] = 0;
  ldh[9] =
      0; /* method: 0 (Store) for universal compatibility and instant speed */
  ldh[10] = 0;
  ldh[11] = 0; /* mod time */
  ldh[12] = 0x21;
  ldh[13] = 0x54; /* mod date: 2022-01-01 */
  ldh[14] = (mz_uint8)(crc);
  ldh[15] = (mz_uint8)(crc >> 8);
  ldh[16] = (mz_uint8)(crc >> 16);
  ldh[17] = (mz_uint8)(crc >> 24);
  ldh[18] = (mz_uint8)(buf_size);
  ldh[19] = (mz_uint8)(buf_size >> 8);
  ldh[20] = (mz_uint8)(buf_size >> 16);
  ldh[21] = (mz_uint8)(buf_size >> 24);
  ldh[22] = ldh[18];
  ldh[23] = ldh[19];
  ldh[24] = ldh[20];
  ldh[25] = ldh[21];
  ldh[26] = (mz_uint8)(filename_len);
  ldh[27] = (mz_uint8)(filename_len >> 8);
  ldh[28] = 0;
  ldh[29] = 0; /* extra len: 0 */

  if (!mz_zip_writer_append_bytes(pZip, ldh, MZ_ZIP_LDH_SIZE))
    return MZ_FALSE;
  if (!mz_zip_writer_append_bytes(pZip, pArchive_name, filename_len))
    return MZ_FALSE;
  if (buf_size > 0 && !mz_zip_writer_append_bytes(pZip, pBuf, buf_size))
    return MZ_FALSE;

  /* Add stat to internal state */
  mz_zip_archive_file_stat *new_stats = (mz_zip_archive_file_stat *)realloc(
      pZip->m_pState->m_pStats,
      (pZip->m_total_files + 1) * sizeof(mz_zip_archive_file_stat));
  if (!new_stats)
    return MZ_FALSE;

  pZip->m_pState->m_pStats = new_stats;
  mz_zip_archive_file_stat *pStat =
      &pZip->m_pState->m_pStats[pZip->m_total_files];
  memset(pStat, 0, sizeof(*pStat));
  pStat->m_file_index = pZip->m_total_files;
  pStat->m_crc32 = crc;
  pStat->m_comp_size = buf_size;
  pStat->m_uncomp_size = buf_size;
  pStat->m_method = 0; /* Store */
  pStat->m_local_header_ofs = local_hdr_ofs;
  strncpy(pStat->m_filename, pArchive_name,
          MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE - 1);

  pZip->m_total_files++;
  return MZ_TRUE;
}

mz_bool mz_zip_writer_finalize_heap_archive(mz_zip_archive *pZip, void **ppBuf,
                                            size_t *pSize) {
  if (!pZip || !pZip->m_pState || pZip->m_zip_mode != MZ_ZIP_MODE_WRITING)
    return MZ_FALSE;

  mz_uint64 cd_ofs = (mz_uint64)pZip->m_pState->m_mem_size;

  /* Write Central Directory Headers for each file */
  for (mz_uint32 i = 0; i < pZip->m_total_files; ++i) {
    mz_zip_archive_file_stat *pStat = &pZip->m_pState->m_pStats[i];
    size_t fn_len = strlen(pStat->m_filename);

    mz_uint8 cdh[MZ_ZIP_CDH_SIZE];
    memset(cdh, 0, sizeof(cdh));
    cdh[0] = 0x50;
    cdh[1] = 0x4b;
    cdh[2] = 0x01;
    cdh[3] = 0x02; /* PK\x01\x02 */
    cdh[4] = 20;
    cdh[5] = 0; /* version made by */
    cdh[6] = 20;
    cdh[7] = 0; /* version needed */
    cdh[8] = 0;
    cdh[9] = 0; /* flags */
    cdh[10] = (mz_uint8)pStat->m_method;
    cdh[11] = 0;
    cdh[12] = 0;
    cdh[13] = 0; /* mod time */
    cdh[14] = 0x21;
    cdh[15] = 0x54; /* mod date */
    cdh[16] = (mz_uint8)(pStat->m_crc32);
    cdh[17] = (mz_uint8)(pStat->m_crc32 >> 8);
    cdh[18] = (mz_uint8)(pStat->m_crc32 >> 16);
    cdh[19] = (mz_uint8)(pStat->m_crc32 >> 24);
    cdh[20] = (mz_uint8)(pStat->m_comp_size);
    cdh[21] = (mz_uint8)(pStat->m_comp_size >> 8);
    cdh[22] = (mz_uint8)(pStat->m_comp_size >> 16);
    cdh[23] = (mz_uint8)(pStat->m_comp_size >> 24);
    cdh[24] = (mz_uint8)(pStat->m_uncomp_size);
    cdh[25] = (mz_uint8)(pStat->m_uncomp_size >> 8);
    cdh[26] = (mz_uint8)(pStat->m_uncomp_size >> 16);
    cdh[27] = (mz_uint8)(pStat->m_uncomp_size >> 24);
    cdh[28] = (mz_uint8)(fn_len);
    cdh[29] = (mz_uint8)(fn_len >> 8);
    cdh[30] = 0;
    cdh[31] = 0; /* extra */
    cdh[32] = 0;
    cdh[33] = 0; /* comment */
    cdh[34] = 0;
    cdh[35] = 0; /* disk */
    cdh[36] = 0;
    cdh[37] = 0; /* internal attr */
    cdh[38] = 0;
    cdh[39] = 0;
    cdh[40] = 0;
    cdh[41] = 0; /* external attr */
    cdh[42] = (mz_uint8)(pStat->m_local_header_ofs);
    cdh[43] = (mz_uint8)(pStat->m_local_header_ofs >> 8);
    cdh[44] = (mz_uint8)(pStat->m_local_header_ofs >> 16);
    cdh[45] = (mz_uint8)(pStat->m_local_header_ofs >> 24);

    if (!mz_zip_writer_append_bytes(pZip, cdh, MZ_ZIP_CDH_SIZE))
      return MZ_FALSE;
    if (!mz_zip_writer_append_bytes(pZip, pStat->m_filename, fn_len))
      return MZ_FALSE;
  }

  mz_uint64 cd_size = (mz_uint64)pZip->m_pState->m_mem_size - cd_ofs;

  /* Write End of Central Directory Record (22 bytes) */
  mz_uint8 ecdh[MZ_ZIP_ECDH_SIZE];
  memset(ecdh, 0, sizeof(ecdh));
  ecdh[0] = 0x50;
  ecdh[1] = 0x4b;
  ecdh[2] = 0x05;
  ecdh[3] = 0x06; /* PK\x05\x06 */
  ecdh[4] = 0;
  ecdh[5] = 0; /* disk number */
  ecdh[6] = 0;
  ecdh[7] = 0; /* disk with CD */
  ecdh[8] = (mz_uint8)(pZip->m_total_files);
  ecdh[9] = (mz_uint8)(pZip->m_total_files >> 8);
  ecdh[10] = ecdh[8];
  ecdh[11] = ecdh[9]; /* total entries */
  ecdh[12] = (mz_uint8)(cd_size);
  ecdh[13] = (mz_uint8)(cd_size >> 8);
  ecdh[14] = (mz_uint8)(cd_size >> 16);
  ecdh[15] = (mz_uint8)(cd_size >> 24);
  ecdh[16] = (mz_uint8)(cd_ofs);
  ecdh[17] = (mz_uint8)(cd_ofs >> 8);
  ecdh[18] = (mz_uint8)(cd_ofs >> 16);
  ecdh[19] = (mz_uint8)(cd_ofs >> 24);
  ecdh[20] = 0;
  ecdh[21] = 0; /* comment len: 0 */

  if (!mz_zip_writer_append_bytes(pZip, ecdh, MZ_ZIP_ECDH_SIZE))
    return MZ_FALSE;

  pZip->m_zip_mode = MZ_ZIP_MODE_WRITING_HAS_BEEN_FINALIZED;
  *ppBuf = pZip->m_pState->m_pMem;
  *pSize = pZip->m_pState->m_mem_size;
  return MZ_TRUE;
}

mz_bool mz_zip_writer_end(mz_zip_archive *pZip) {
  if (!pZip)
    return MZ_FALSE;
  if (pZip->m_pState) {
    if (pZip->m_pState->m_pStats) {
      free(pZip->m_pState->m_pStats);
    }
    if (pZip->m_zip_mode != MZ_ZIP_MODE_WRITING_HAS_BEEN_FINALIZED &&
        pZip->m_pState->m_pMem) {
      free(pZip->m_pState->m_pMem);
    }
    free(pZip->m_pState);
    pZip->m_pState = NULL;
  }
  return MZ_TRUE;
}

/* ========================================================================= */
/* ZIP READER                                                                */
/* ========================================================================= */

mz_bool mz_zip_reader_init_mem(mz_zip_archive *pZip, const void *pMem,
                               size_t size, mz_uint flags) {
  (void)flags;
  if (!pZip || !pMem || size < MZ_ZIP_ECDH_SIZE)
    return MZ_FALSE;
  mz_zip_zero_struct(pZip);

  const mz_uint8 *pBuf = (const mz_uint8 *)pMem;

  /* Locate End of Central Directory Record (scan backwards from end) */
  size_t search_len = MZ_MIN(size, (size_t)65536 + MZ_ZIP_ECDH_SIZE);
  size_t search_start = size - search_len;
  size_t ecdh_ofs = 0;
  mz_bool found_ecdh = MZ_FALSE;

  for (size_t i = size - MZ_ZIP_ECDH_SIZE; i >= search_start; --i) {
    if (pBuf[i] == 0x50 && pBuf[i + 1] == 0x4b && pBuf[i + 2] == 0x05 &&
        pBuf[i + 3] == 0x06) {
      ecdh_ofs = i;
      found_ecdh = MZ_TRUE;
      break;
    }
    if (i == 0)
      break;
  }

  if (!found_ecdh)
    return MZ_FALSE;

  mz_uint32 total_files = pBuf[ecdh_ofs + 8] | (pBuf[ecdh_ofs + 9] << 8);
  mz_uint32 cd_size = pBuf[ecdh_ofs + 12] | (pBuf[ecdh_ofs + 13] << 8) |
                      (pBuf[ecdh_ofs + 14] << 16) | (pBuf[ecdh_ofs + 15] << 24);
  mz_uint32 cd_ofs = pBuf[ecdh_ofs + 16] | (pBuf[ecdh_ofs + 17] << 8) |
                     (pBuf[ecdh_ofs + 18] << 16) | (pBuf[ecdh_ofs + 19] << 24);

  if (cd_ofs + cd_size > size)
    return MZ_FALSE;

  pZip->m_pState =
      (mz_zip_internal_state *)calloc(1, sizeof(mz_zip_internal_state));
  if (!pZip->m_pState)
    return MZ_FALSE;

  pZip->m_pState->m_pMem = (mz_uint8 *)pMem;
  pZip->m_pState->m_mem_size = size;
  pZip->m_pState->m_pStats = (mz_zip_archive_file_stat *)calloc(
      total_files ? total_files : 1, sizeof(mz_zip_archive_file_stat));
  if (!pZip->m_pState->m_pStats) {
    free(pZip->m_pState);
    pZip->m_pState = NULL;
    return MZ_FALSE;
  }

  /* Parse all central directory headers */
  size_t cur_cdh = cd_ofs;
  mz_uint32 actual_files = 0;

  for (mz_uint32 f = 0; f < total_files && cur_cdh + MZ_ZIP_CDH_SIZE <= size;
       ++f) {
    if (pBuf[cur_cdh] != 0x50 || pBuf[cur_cdh + 1] != 0x4b ||
        pBuf[cur_cdh + 2] != 0x01 || pBuf[cur_cdh + 3] != 0x02) {
      break;
    }

    mz_zip_archive_file_stat *pStat = &pZip->m_pState->m_pStats[actual_files];
    pStat->m_file_index = actual_files;
    pStat->m_method = pBuf[cur_cdh + 10] | (pBuf[cur_cdh + 11] << 8);
    pStat->m_crc32 = pBuf[cur_cdh + 16] | (pBuf[cur_cdh + 17] << 8) |
                     (pBuf[cur_cdh + 18] << 16) | (pBuf[cur_cdh + 19] << 24);
    pStat->m_comp_size = pBuf[cur_cdh + 20] | (pBuf[cur_cdh + 21] << 8) |
                         (pBuf[cur_cdh + 22] << 16) |
                         (pBuf[cur_cdh + 23] << 24);
    pStat->m_uncomp_size = pBuf[cur_cdh + 24] | (pBuf[cur_cdh + 25] << 8) |
                           (pBuf[cur_cdh + 26] << 16) |
                           (pBuf[cur_cdh + 27] << 24);

    mz_uint32 fn_len = pBuf[cur_cdh + 28] | (pBuf[cur_cdh + 29] << 8);
    mz_uint32 extra_len = pBuf[cur_cdh + 30] | (pBuf[cur_cdh + 31] << 8);
    mz_uint32 comment_len = pBuf[cur_cdh + 32] | (pBuf[cur_cdh + 33] << 8);

    pStat->m_local_header_ofs = pBuf[cur_cdh + 42] | (pBuf[cur_cdh + 43] << 8) |
                                (pBuf[cur_cdh + 44] << 16) |
                                (pBuf[cur_cdh + 45] << 24);

    size_t copy_len =
        MZ_MIN((size_t)fn_len, (size_t)MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE - 1);
    if (cur_cdh + MZ_ZIP_CDH_SIZE + copy_len <= size) {
      memcpy(pStat->m_filename, pBuf + cur_cdh + MZ_ZIP_CDH_SIZE, copy_len);
      pStat->m_filename[copy_len] = '\0';
    }

    if (copy_len > 0 && pStat->m_filename[copy_len - 1] == '/') {
      pStat->m_is_directory = MZ_TRUE;
    }

    actual_files++;
    cur_cdh += MZ_ZIP_CDH_SIZE + fn_len + extra_len + comment_len;
  }

  pZip->m_total_files = actual_files;
  pZip->m_zip_mode = MZ_ZIP_MODE_READING;
  pZip->m_zip_type = MZ_ZIP_TYPE_MEMORY;
  return MZ_TRUE;
}

mz_uint mz_zip_reader_get_num_files(mz_zip_archive *pZip) {
  return pZip ? pZip->m_total_files : 0;
}

mz_bool mz_zip_reader_file_stat(mz_zip_archive *pZip, mz_uint file_index,
                                mz_zip_archive_file_stat *pStat) {
  if (!pZip || !pZip->m_pState || file_index >= pZip->m_total_files || !pStat)
    return MZ_FALSE;
  *pStat = pZip->m_pState->m_pStats[file_index];
  return MZ_TRUE;
}

void *mz_zip_reader_extract_to_heap(mz_zip_archive *pZip, mz_uint file_index,
                                    size_t *pSize, mz_uint flags) {
  (void)flags;
  if (!pZip || !pZip->m_pState || file_index >= pZip->m_total_files || !pSize)
    return NULL;

  mz_zip_archive_file_stat *pStat = &pZip->m_pState->m_pStats[file_index];
  const mz_uint8 *pBuf = pZip->m_pState->m_pMem;
  size_t mem_size = pZip->m_pState->m_mem_size;

  size_t ldh_ofs = (size_t)pStat->m_local_header_ofs;
  if (ldh_ofs + MZ_ZIP_LDH_SIZE > mem_size)
    return NULL;

  if (pBuf[ldh_ofs] != 0x50 || pBuf[ldh_ofs + 1] != 0x4b ||
      pBuf[ldh_ofs + 2] != 0x03 || pBuf[ldh_ofs + 3] != 0x04)
    return NULL;

  mz_uint32 ldh_fn_len = pBuf[ldh_ofs + 26] | (pBuf[ldh_ofs + 27] << 8);
  mz_uint32 ldh_extra_len = pBuf[ldh_ofs + 28] | (pBuf[ldh_ofs + 29] << 8);

  size_t data_ofs = ldh_ofs + MZ_ZIP_LDH_SIZE + ldh_fn_len + ldh_extra_len;
  if (data_ofs + pStat->m_comp_size > mem_size)
    return NULL;

  void *pOut = NULL;
  size_t out_size = (size_t)pStat->m_uncomp_size;

  if (mz_decompress_data(pBuf + data_ofs, (size_t)pStat->m_comp_size, &pOut,
                         &out_size, pStat->m_method)) {
    *pSize = out_size;
    return pOut;
  }

  return NULL;
}

mz_bool mz_zip_reader_end(mz_zip_archive *pZip) {
  if (!pZip)
    return MZ_FALSE;
  if (pZip->m_pState) {
    if (pZip->m_pState->m_pStats) {
      free(pZip->m_pState->m_pStats);
    }
    free(pZip->m_pState);
    pZip->m_pState = NULL;
  }
  return MZ_TRUE;
}

} // extern "C"
