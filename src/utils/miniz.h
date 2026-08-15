/* Gemini 3.7 Flash (High)'s miniz Implementation.

    It is not really miniz in a sense. However, logics and concepts remain the
   same However, this code is still under study.
*/
#ifndef MINIZ_H
#define MINIZ_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char mz_uint8;
typedef unsigned short mz_uint16;
typedef unsigned int mz_uint32;
typedef unsigned int mz_uint;
typedef uint64_t mz_uint64;
typedef int16_t mz_int16;
typedef int mz_int;
typedef int mz_bool;

#define MZ_FALSE (0)
#define MZ_TRUE (1)

#define MZ_DEFAULT_COMPRESSION (-1)
#define MZ_NO_COMPRESSION (0)
#define MZ_BEST_SPEED (1)
#define MZ_BEST_COMPRESSION (9)

#define MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE (512)
#define MZ_ZIP_MAX_ARCHIVE_FILE_COMMENT_SIZE (512)

typedef void *(*mz_alloc_func)(void *opaque, size_t items, size_t size);
typedef void (*mz_free_func)(void *opaque, void *address);
typedef void *(*mz_realloc_func)(void *opaque, void *address, size_t items,
                                 size_t size);

typedef size_t (*mz_file_read_func)(void *pOpaque, mz_uint64 file_ofs,
                                    void *pBuf, size_t n);
typedef size_t (*mz_file_write_func)(void *pOpaque, mz_uint64 file_ofs,
                                     const void *pBuf, size_t n);

typedef enum {
  MZ_ZIP_MODE_INVALID = 0,
  MZ_ZIP_MODE_READING = 1,
  MZ_ZIP_MODE_WRITING = 2,
  MZ_ZIP_MODE_WRITING_HAS_BEEN_FINALIZED = 3
} mz_zip_mode;

typedef enum {
  MZ_ZIP_TYPE_INVALID = 0,
  MZ_ZIP_TYPE_USER = 1,
  MZ_ZIP_TYPE_MEMORY = 2,
  MZ_ZIP_TYPE_HEAP = 3,
  MZ_ZIP_TYPE_FILE = 4,
  MZ_ZIP_TYPE_CFILE = 5
} mz_zip_type;

typedef struct {
  mz_uint32 m_file_index;
  mz_uint64 m_central_dir_ofs;
  mz_uint16 m_version_made_by;
  mz_uint16 m_version_needed;
  mz_uint16 m_bit_flag;
  mz_uint16 m_method;
  mz_uint32 m_crc32;
  mz_uint64 m_comp_size;
  mz_uint64 m_uncomp_size;
  mz_uint16 m_internal_attr;
  mz_uint32 m_external_attr;
  mz_uint64 m_local_header_ofs;
  mz_uint32 m_comment_size;
  mz_bool m_is_directory;
  mz_bool m_is_encrypted;
  mz_bool m_is_supported;
  char m_filename[MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE];
  char m_comment[MZ_ZIP_MAX_ARCHIVE_FILE_COMMENT_SIZE];
} mz_zip_archive_file_stat;

struct mz_zip_internal_state_tag;
typedef struct mz_zip_internal_state_tag mz_zip_internal_state;

typedef struct {
  mz_uint64 m_archive_size;
  mz_uint64 m_central_directory_file_ofs;
  mz_uint32 m_total_files;
  mz_zip_mode m_zip_mode;
  mz_zip_type m_zip_type;

  mz_alloc_func m_pAlloc;
  mz_free_func m_pFree;
  mz_realloc_func m_pRealloc;
  void *m_pAlloc_opaque;

  mz_file_read_func m_pRead;
  mz_file_write_func m_pWrite;
  void *m_pIO_opaque;

  mz_zip_internal_state *m_pState;
} mz_zip_archive;

void mz_zip_zero_struct(mz_zip_archive *pZip);

mz_bool mz_zip_reader_init_mem(mz_zip_archive *pZip, const void *pMem,
                               size_t size, mz_uint flags);
mz_uint mz_zip_reader_get_num_files(mz_zip_archive *pZip);
mz_bool mz_zip_reader_file_stat(mz_zip_archive *pZip, mz_uint file_index,
                                mz_zip_archive_file_stat *pStat);
void *mz_zip_reader_extract_to_heap(mz_zip_archive *pZip, mz_uint file_index,
                                    size_t *pSize, mz_uint flags);
mz_bool mz_zip_reader_end(mz_zip_archive *pZip);

mz_bool mz_zip_writer_init_heap(mz_zip_archive *pZip,
                                size_t size_to_reserve_at_beginning,
                                size_t initial_allocation_size);
mz_bool mz_zip_writer_add_mem(mz_zip_archive *pZip, const char *pArchive_name,
                              const void *pBuf, size_t buf_size,
                              mz_uint level_and_flags);
mz_bool mz_zip_writer_finalize_heap_archive(mz_zip_archive *pZip, void **ppBuf,
                                            size_t *pSize);
mz_bool mz_zip_writer_end(mz_zip_archive *pZip);

void mz_free(void *p);

#ifdef __cplusplus
}
#endif

#endif /* MINIZ_H */
