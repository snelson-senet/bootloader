/*     _____                _        __     _
 *    /__   \_ __ __ _  ___| | __ /\ \ \___| |_
 *      / /\/ '__/ _` |/ __| |/ //  \/ / _ \ __|
 *     / /  | | | (_| | (__|   '/ /\  /  __/ |_
 *     \_\  |_|  \__,_|\___|_|\_\_\ \_\\___|\__|
 *
 * Copyright (c) 2016-2018 Trackio International AG
 * All rights reserved.
 *
 * This file is subject to the terms and conditions
 * defined in file 'LICENSE', which is part of this
 * source code package.
 *
 */

#include "bootloader.h"
#include "update.h"
#include "janpatch.h"

#if !defined(UP_PAGEBUFFER_SZ) || ((UP_PAGEBUFFER_SZ & 3) != 0) || \
  ((UP_PAGEBUFFER_SZ & (UP_PAGEBUFFER_SZ - 1)) != 0)
#error "UP_PAGEBUFFER_SZ must be defined as a multiple of 4 and a power of 2"
#endif

#define PB_WORDS (UP_PAGEBUFFER_SZ >> 2)

extern void unlz4(const void* src, void* destination);
extern void unlz4_len(const void* src, void* destination, uint32_t length);

static int fread_pos_threshold = 0;

typedef struct {
  void*    bl_up_ctxt;
  uint32_t file_sz;
} jpatch_stream_ctx;

// ------------------------------------------------
// Update functions

static uint32_t update_plain(void* ctx, boot_uphdr* fwup, bool install) {
  int       n   = fwup->fwsize;
  uint32_t* src = (uint32_t*)(fwup + 1);
  uint32_t* dst;
  uint32_t  rv;

  // size must be a multiple of 4 (word size)
  if ((n & 3) != 0) {
    return BOOT_E_SIZE;
  }
  // perform size check and get install address
  if ((rv = up_install_init(ctx, n, (void**)&dst)) != BOOT_OK) {
    return rv;
  }

  // copy new firmware to destination
  if (install) {
    up_flash_unlock(ctx);
    while (n > 0) {
      uint32_t buf[PB_WORDS];
      int      i, m = (n < PB_WORDS) ? n : PB_WORDS;
      n -= m;
      for (i = 0; i < m; i++) {
        buf[i] = *src++;
      }
      for (; i < PB_WORDS; i++) {
        buf[i] = 0;  // pad last page with 0
      }
      up_flash_wr_page(ctx, dst, buf);
      dst += PB_WORDS;
    }
    up_flash_lock(ctx);
  }
  return BOOT_OK;
}

static uint32_t update_lz4(void* ctx, boot_uphdr* fwup, bool install) {
  int       n   = fwup->fwsize;
  uint32_t* src = (uint32_t*)(fwup + 1);
  uint32_t* dst;
  uint32_t  rv;

  // size must be a multiple of 4 (word size)
  if ((n & 3) != 0) {
    return BOOT_E_SIZE;
  }
  // perform size check and get install address
  if ((rv = up_install_init(ctx, n, (void**)&dst)) != BOOT_OK) {
    return rv;
  }

  // copy new firmware to destination
  if (install) {
    up_flash_unlock(ctx);
    while (n > 0) {
      uint32_t buf[PB_WORDS];
      int      i, m = (n < PB_WORDS) ? n : PB_WORDS;
      n -= m;
      for (i = 0; i < m; i++) {
        buf[i] = *src++;
      }
      for (; i < PB_WORDS; i++) {
        buf[i] = 0;  // pad last page with 0
      }
      up_flash_wr_page(ctx, dst, buf);
      dst += PB_WORDS;
    }
    up_flash_lock(ctx);
  }
  return BOOT_OK;
}

size_t jpatch_fread(void* buffer, size_t size, size_t count, JANPATCH_STREAM* stream) {
  size_t             bytes_read = 0;
  jpatch_stream_ctx* ctx        = (jpatch_stream_ctx*)stream->context;
  uint8_t*           src        = stream->src + stream->position;

  if (ctx->file_sz < 0x100) {
    if (stream->position >= fread_pos_threshold) {
      fread_pos_threshold = stream->position + 10;
      if (fread_pos_threshold >= ctx->file_sz)
        fread_pos_threshold = stream->position + 1;
    }
  }

  if ((stream->position + count) >= ctx->file_sz)
    count = ctx->file_sz - stream->position;

  for (; (bytes_read < count) && (stream->position < ctx->file_sz); bytes_read++)
    ((uint8_t*)buffer)[bytes_read] = *src++;
  stream->position += bytes_read;

  return bytes_read;
}

size_t jpatch_fwrite(const void* buffer, size_t size, size_t count, JANPATCH_STREAM* stream) {
  jpatch_stream_ctx* ctx = (jpatch_stream_ctx*)stream->context;
  size_t             bytes_written;
  uint8_t*           src = stream->src + stream->position;

  up_flash_unlock(stream->context);

  for (bytes_written = 0; (bytes_written < count) && (stream->position < ctx->file_sz);
       bytes_written += UP_PAGEBUFFER_SZ) {
    up_flash_wr_page(ctx->bl_up_ctxt, (uint32_t*)src, buffer + bytes_written);
    src += UP_PAGEBUFFER_SZ;
    stream->position += UP_PAGEBUFFER_SZ;
  }

  up_flash_lock(stream->context);

  return bytes_written;
}

int jpatch_fseek(JANPATCH_STREAM* stream, long int offset, int origin) {
  int                rc = -1;
  long int           next_pos;
  jpatch_stream_ctx* ctx = (jpatch_stream_ctx*)stream->context;

  switch (origin) {
    case SEEK_CUR:
      next_pos = stream->position + offset;
      break;
    case SEEK_SET:
      next_pos = offset;
      break;
    case SEEK_END:
      next_pos = ctx->file_sz + offset;
      break;
    default:
      return -1;
  }
  if ((next_pos >= 0) && (next_pos <= ctx->file_sz)) {
    stream->position = next_pos;
    rc               = 0;
  }
  return rc;
}

long jpatch_ftell(JANPATCH_STREAM* stream) {
  return stream->position;
}

void jpatch_progress(uint8_t percent) {
}

static uint32_t update_jpatch(void* ctx, boot_uphdr* fwup, bool install) {
  uint32_t* dst;
  uint32_t* src;
  uint32_t  src_sz;
  uint32_t  rv;

  fread_pos_threshold = 0;

  // Check size and set destination pointer
  if ((rv = up_install_jpatch_init(ctx, fwup->fwsize, (void**)&dst, (void**)&src, &src_sz)) !=
      BOOT_OK)
    return rv;

  // janpatch_ctx contains buffers, and references to the file system functions
  unsigned char   source_buffer[PB_WORDS * 4];
  unsigned char   patch_buffer[PB_WORDS * 4];
  unsigned char   target_buffer[PB_WORDS * 4];
  JANPATCH_STREAM jan_source;
  JANPATCH_STREAM jan_patch;
  JANPATCH_STREAM jan_target;
  janpatch_ctx    jan_ctx;
  jan_ctx.source_buffer.buffer = source_buffer;
  jan_ctx.source_buffer.size   = PB_WORDS * 4;
  jan_ctx.patch_buffer.buffer  = patch_buffer;
  jan_ctx.patch_buffer.size    = PB_WORDS * 4;
  jan_ctx.target_buffer.buffer = target_buffer;
  jan_ctx.target_buffer.size   = PB_WORDS * 4;
  jan_ctx.fread                = jpatch_fread;
  jan_ctx.fwrite               = jpatch_fwrite;
  jan_ctx.fseek                = jpatch_fseek;
  jan_ctx.ftell                = jpatch_ftell;
  jan_ctx.progress             = jpatch_progress;

  // Eventually fwup will be the patch address, but currently it's on the heap
  uint32_t patch = 0x08030000 - fwup->size;

  jpatch_stream_ctx src_ctx = {.bl_up_ctxt = ctx, .file_sz = src_sz};
  janpatch_stream_init(&jan_source, (uint8_t*)src, (void*)&src_ctx);

  jpatch_stream_ctx patch_ctx = {.bl_up_ctxt = ctx, .file_sz = fwup->size};
  janpatch_stream_init(&jan_patch, (uint8_t*)patch, &patch_ctx);

  jpatch_stream_ctx dest_ctx = {.bl_up_ctxt = ctx, .file_sz = fwup->fwsize};
  janpatch_stream_init(&jan_target, (uint8_t*)dst, (void*)&dest_ctx);

  janpatch(&jan_ctx, &jan_source, &jan_patch, &jan_target);

  return BOOT_OK;
}

uint32_t update(void* ctx, boot_uphdr* fwup, bool install) {
  // Note: The integrity of the update pointed to by fwup has
  // been verified at this point.

  switch (fwup->uptype) {
    case BOOT_UPTYPE_PLAIN:
      return update_plain(ctx, fwup, install);
    case BOOT_UPTYPE_LZ4:
      return update_lz4(ctx, fwup, install);
    case BOOT_UPTYPE_JANPATCH:
      return update_jpatch(ctx, fwup, install);
    default:
      return BOOT_E_NOIMPL;
  }
}
