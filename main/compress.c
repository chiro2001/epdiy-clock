#include "common.h"
#include "esp_err.h"
#include "zlib.h"

#define CHUNK 16384
// #define CHUNK (1024 * 4)
// #define CHUNK 512

// use low-level API to compress data
esp_err_t compress_mem_to_file(const char *filename, const uint8_t *data,
                               size_t length, int level) {
  int ret = ESP_FAIL, flush;
  unsigned have;
  z_stream strm;
  // unsigned char in[CHUNK];
  // unsigned char out[CHUNK];

  unsigned char *out = malloc(CHUNK);
  if (out == NULL) {
    ESP_LOGE(__func__, "malloc failed");
    ret = ESP_FAIL;
    goto error;
  }
  const uint8_t *p = data;
  FILE *dest = NULL;
  dest = fopen(filename, "wb");
  if (dest == NULL) {
    ESP_LOGE(__func__, "fopen failed");
    ret = ESP_FAIL;
    goto error;
  }

  /* allocate deflate state */
  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;
  ret = deflateInit(&strm, level);
  if (ret != Z_OK) {
    goto error;
  }

  /* compress until end of file */
  do {
    // strm.avail_in = fread(in, 1, CHUNK, source);
    strm.avail_in = length - (p - data);
    if (strm.avail_in > CHUNK)
      strm.avail_in = CHUNK;
    // flush = feof(source) ? Z_FINISH : Z_NO_FLUSH;
    strm.next_in = (unsigned char *)p;
    p += strm.avail_in;
    flush = p >= data + length ? Z_FINISH : Z_NO_FLUSH;

    /* run deflate() on input until output buffer not full, finish
       compression if all of source has been read in */
    do {
      strm.avail_out = CHUNK;
      strm.next_out = out;
      ret = deflate(&strm, flush);   /* no bad return value */
      assert(ret != Z_STREAM_ERROR); /* state not clobbered */
      have = CHUNK - strm.avail_out;
      if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
        (void)deflateEnd(&strm);
        ret = Z_ERRNO;
        goto error;
      }
    } while (strm.avail_out == 0);
    assert(strm.avail_in == 0); /* all input will be used */

    /* done when last data in file processed */
  } while (flush != Z_FINISH);
  assert(ret == Z_STREAM_END); /* stream will be complete */

  /* clean up and return */
  if (strm.total_in) {
    ESP_LOGI(__func__, "compressed %dKiB to %dKiB, %d%%", 
      strm.total_in / 1024, strm.total_out / 1024, strm.total_out * 100 / strm.total_in);
  }
  (void)deflateEnd(&strm);
  fclose(dest);
  dest = NULL;
  free(out);
  return ESP_OK;
error:
  // if (dest) {
  //   fclose(dest);
  //   dest = NULL;
  // }
  if (out) {
    free(out);
    out = NULL;
  }
  return ret;
}

esp_err_t decompress_file_to_mem(const char *filename, uint8_t *dest,
                                 size_t max_len) {
  int ret;
  unsigned have;
  z_stream strm;
  // unsigned char in[CHUNK];
  unsigned char *in = malloc(CHUNK);
  if (in == NULL) {
    ESP_LOGE(__func__, "malloc failed");
    ret = ESP_FAIL;
    goto error;
  }
  size_t offset = 0;
  FILE *source = fopen(filename, "rb");
  if (source == NULL) {
    ret = Z_ERRNO;
    goto error;
  }

  /* allocate inflate state */
  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;
  strm.avail_in = 0;
  strm.next_in = Z_NULL;
  ret = inflateInit(&strm);
  if (ret != Z_OK) {
    goto error;
  }

  /* decompress until deflate stream ends or end of file */
  do {
    strm.avail_in = fread(in, 1, CHUNK, source);
    if (ferror(source)) {
      (void)inflateEnd(&strm);
      ret = Z_ERRNO;
      goto error;
    }
    if (strm.avail_in == 0)
      break;
    strm.next_in = in;

    /* run inflate() on input until output buffer not full */
    do {
      strm.avail_out = CHUNK;
      strm.next_out = dest + offset;
      ret = inflate(&strm, Z_NO_FLUSH);
      assert(ret != Z_STREAM_ERROR); /* state not clobbered */
      switch (ret) {
      case Z_NEED_DICT:
        ret = Z_DATA_ERROR; /* and fall through */
      case Z_DATA_ERROR:
      case Z_MEM_ERROR:
        (void)inflateEnd(&strm);
        goto error;
      }
      have = CHUNK - strm.avail_out;
      if (offset + have > max_len) {
        (void)inflateEnd(&strm);
        ret = Z_ERRNO;
        goto error;
      }
      offset += have;
    } while (strm.avail_out == 0);

    /* done when inflate() says it's done */
  } while (ret != Z_STREAM_END);

  /* clean up and return */
  if (strm.total_out) {
    ESP_LOGI(__func__, "decompressed %dKiB to %dKiB, %d%%", 
      strm.total_in / 1024, strm.total_out / 1024, strm.total_in * 100 / strm.total_out);
  }
  (void)inflateEnd(&strm);
  fclose(source);
  source = NULL;
  free(in);
  return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
error:
  // if (source) {
  //   fclose(source);
  //   source = NULL;
  // }
  if (in) {
    free(in);
    in = NULL;
  }
  return ret;
}