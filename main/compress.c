#include "common.h"
#include "esp_err.h"
#include "esp_log.h"
#include "miniz.h"
#include "zlib.h"

#define ZLIB_CHUNK 16384
// #define ZLIB_CHUNK (1024 * 4)
// #define ZLIB_CHUNK 512
#define MINIZ_CHUNK 16384

// use low-level API to compress data
esp_err_t compress_mem_to_file_zlib(const char *filename, const uint8_t *data,
                                    size_t length, int level) {
  int ret = ESP_FAIL, flush;
  unsigned have;
  z_stream strm;
  // unsigned char in[ZLIB_CHUNK];
  // unsigned char out[ZLIB_CHUNK];

  unsigned char *out = malloc(ZLIB_CHUNK);
  if (out == NULL) {
    ESP_LOGE(__func__, "malloc failed");
    ret = ESP_FAIL;
    goto error;
  }
  const uint8_t *p = data;
  FILE *dest = NULL;
  dest = fopen(filename, "wb");
  if (dest == NULL) {
    ESP_LOGE(__func__, "fopen %s failed", filename);
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
  int64_t time_start = esp_timer_get_time();

  /* compress until end of file */
  do {
    // strm.avail_in = fread(in, 1, ZLIB_CHUNK, source);
    strm.avail_in = length - (p - data);
    if (strm.avail_in > ZLIB_CHUNK)
      strm.avail_in = ZLIB_CHUNK;
    // flush = feof(source) ? Z_FINISH : Z_NO_FLUSH;
    strm.next_in = (unsigned char *)p;
    p += strm.avail_in;
    flush = p >= data + length ? Z_FINISH : Z_NO_FLUSH;

    /* run deflate() on input until output buffer not full, finish
       compression if all of source has been read in */
    do {
      strm.avail_out = ZLIB_CHUNK;
      strm.next_out = out;
      ret = deflate(&strm, flush);   /* no bad return value */
      assert(ret != Z_STREAM_ERROR); /* state not clobbered */
      have = ZLIB_CHUNK - strm.avail_out;
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
  fclose(dest);
  if (strm.total_in) {
    ESP_LOGI(__func__, "compressed %dKiB to %dKiB, %d%% in %lldms",
             strm.total_in / 1024, strm.total_out / 1024,
             strm.total_out * 100 / strm.total_in,
             (esp_timer_get_time() - time_start) / 1000);
  } else {
    ESP_LOGE(__func__, "compressed size is 0, removing file");
    unlink(filename);
  }
  (void)deflateEnd(&strm);
  dest = NULL;
  free(out);
  return ESP_OK;
error:
  // if (dest) {
  //   fclose(dest);
  //   dest = NULL;
  // }
  ESP_LOGE(__func__, "failed, ret=%d", ret);
  if (out) {
    free(out);
    out = NULL;
  }
  return ret;
}

esp_err_t decompress_file_to_mem_zlib(const char *filename, uint8_t *dest,
                                      size_t max_len) {
  int ret;
  unsigned have;
  z_stream strm;
  // unsigned char in[ZLIB_CHUNK];
  unsigned char *in = malloc(ZLIB_CHUNK);
  if (in == NULL) {
    ESP_LOGE(__func__, "malloc failed");
    ret = ESP_FAIL;
    goto error;
  }
  size_t offset = 0;
  FILE *source = fopen(filename, "rb");
  if (source == NULL) {
    ESP_LOGE(__func__, "fopen failed");
    ret = Z_ERRNO;
    goto error;
  }
  int64_t time_start = esp_timer_get_time();

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
    strm.avail_in = fread(in, 1, ZLIB_CHUNK, source);
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
      strm.avail_out = ZLIB_CHUNK;
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
      have = ZLIB_CHUNK - strm.avail_out;
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
    ESP_LOGI(__func__, "decompressed %dKiB to %dKiB, %d%% in %lldms",
             strm.total_in / 1024, strm.total_out / 1024,
             strm.total_in * 100 / strm.total_out,
             (esp_timer_get_time() - time_start) / 1000);
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

int compress_mem_to_file_miniz_stream(const void *pBuf, int len, void *pUser) {
  unsigned int written = fwrite(pBuf, 1, len, (FILE *)pUser);
  if (written != len) {
    ESP_LOGE(__func__, "fwrite failed! expected %d, got %d", len, written);
  } else {
    ESP_LOGI(__func__, "fwrite %d bytes", len);
  }
  return written == len;
}

esp_err_t compress_mem_to_file_miniz(const char *filename, const uint8_t *data,
                                     size_t length, int _level) {
  int ret = ESP_FAIL;
  FILE *fp = fopen(filename, "wb");
  if (!fp) {
    ESP_LOGE(__func__, "Failed to open file for writing");
    return ESP_FAIL;
  }
  int64_t time_start = esp_timer_get_time();
  bool r = tdefl_compress_mem_to_output(
      data, length, compress_mem_to_file_miniz_stream, fp, TDEFL_WRITE_ZLIB_HEADER | TDEFL_GREEDY_PARSING_FLAG);
  size_t total_out = ftell(fp);
  ESP_LOGI(__func__, "compressed %d bytes to %d bytes in %lldms, r=%d", length,
           total_out, (esp_timer_get_time() - time_start) / 1000, r);
  fclose(fp);
  if (total_out == 0) {
    ESP_LOGW(__func__, "compressed size is 0, removing file");
    unlink(filename);
  }
  ret = ESP_OK;
  return ret;
}

esp_err_t decompress_file_to_mem_miniz(const char *filename, uint8_t *dest,
                                       size_t max_len) {
  // Decompression.
  esp_err_t ret = ESP_FAIL;
  size_t avail_in = 0;
  size_t avail_out = MINIZ_CHUNK;
  uint infile_remaining = max_len;
  size_t total_in = 0;
  size_t total_out = 0;
  uint8_t *s_inbuf = malloc(MINIZ_CHUNK);
  if (s_inbuf == NULL) {
    ESP_LOGE(__func__, "malloc failed");
    goto error;
  }
  // outbuf
  void *next_out = dest;
  FILE *pInfile = fopen(filename, "rb");
  if (pInfile == NULL) {
    ESP_LOGE(__func__, "fopen failed");
    goto error;
  }
  int64_t time_start = esp_timer_get_time();
  uint8_t *next_in = s_inbuf;

  tinfl_decompressor inflator;
  tinfl_init(&inflator);

  for (;;) {
    size_t in_bytes, out_bytes;
    tinfl_status status;
    if (!avail_in) {
      // Input buffer is empty, so read more bytes from input file.
      uint n = MINIZ_CHUNK < infile_remaining ? MINIZ_CHUNK : infile_remaining;

      if (fread(s_inbuf, 1, n, pInfile) != n) {
        printf("Failed reading from input file!\n");
        goto error;
      }

      next_in = s_inbuf;
      avail_in = n;

      infile_remaining -= n;
    }

    in_bytes = avail_in;
    out_bytes = avail_out;
    status =
        tinfl_decompress(&inflator, (const mz_uint8 *)next_in, &in_bytes, dest,
                         (mz_uint8 *)next_out, &out_bytes,
                         (infile_remaining ? TINFL_FLAG_HAS_MORE_INPUT : 0) |
                             TINFL_FLAG_PARSE_ZLIB_HEADER);
    printf("status=%d, in_bytes=%d, out_bytes=%d\n", status, in_bytes,
           out_bytes);

    avail_in -= in_bytes;
    next_in = next_in + in_bytes;
    total_in += in_bytes;

    avail_out -= out_bytes;
    next_out = (mz_uint8 *)next_out + out_bytes;
    total_out += out_bytes;

    if ((status <= TINFL_STATUS_DONE) || (!avail_out)) {
      // Output buffer is full, or decompression is done, so write buffer to
      // output file.
      uint n = MINIZ_CHUNK - (uint)avail_out;
      next_out = dest + n;
      avail_out = MINIZ_CHUNK;
    }

    // If status is <= TINFL_STATUS_DONE then either decompression is done or
    // something went wrong.
    if (status <= TINFL_STATUS_DONE) {
      if (status == TINFL_STATUS_DONE) {
        // Decompression completed successfully.
        break;
      } else {
        // Decompression failed.
        printf("tinfl_decompress() failed with status %i!\n", status);
        // return EXIT_FAILURE;
        goto error;
      }
    }
  }
  ret = ESP_OK;
  ESP_LOGI(__func__, "decompressed %dKiB to %dKiB in %lldms", total_in / 1024,
           total_out / 1024, (esp_timer_get_time() - time_start) / 1000);
  return ret;
error:
  if (s_inbuf) {
    free(s_inbuf);
    s_inbuf = NULL;
  }
  return ret;
}