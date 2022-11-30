/*
 * Copyright © 2016 Mozilla Foundation
 *
 * This program is made available under an ISC-style license.  See the
 * accompanying file LICENSE for details.
 */

/* cubeb_logging test  */
#include "gtest/gtest.h"
#if !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 600
#endif
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <memory>
#include "cubeb/cubeb.h"
#include <atomic>

#include "common.h"

#define PRINT_LOGS_TO_STDERR 0

#define SAMPLE_FREQUENCY 48000
#define STREAM_FORMAT CUBEB_SAMPLE_FLOAT32LE
#define OUTPUT_CHANNELS 2
#define OUTPUT_LAYOUT CUBEB_LAYOUT_STEREO

std::atomic<uint32_t> log_statements_received = { 0 };
std::atomic<uint32_t> data_callback_call_count = { 0 };

void test_logging_callback(char const * fmt, ...) {
  log_statements_received++;
#if PRINT_LOGS_TO_STDERR == 1
  va_list args;
  va_start(args, fmt);
  fprintf(stderr, "CUBEB: ");
  fprintf(stderr, fmt, args);
  va_end(args);
#endif // PRINT_LOGS_TO_STDERR
}

long data_cb_load(cubeb_stream * stream, void * user, const void * inputbuffer, void * outputbuffer, long nframes)
{
  data_callback_call_count++;
  return nframes;
}

void state_cb(cubeb_stream * stream, void * /*user*/, cubeb_state state)
{
  if (stream == NULL)
    return;

  switch (state) {
  case CUBEB_STATE_STARTED:
    fprintf(stderr, "stream started\n"); break;
  case CUBEB_STATE_STOPPED:
    fprintf(stderr, "stream stopped\n"); break;
  case CUBEB_STATE_DRAINED:
    fprintf(stderr, "stream drained\n"); break;
  default:
    fprintf(stderr, "unknown stream state %d\n", state);
  }

  return;
}

TEST(cubeb, logging)
{
  cubeb *ctx;
  cubeb_stream *stream;
  cubeb_stream_params output_params;
  int r;
  uint32_t latency_frames = 0;

  cubeb_set_log_callback(CUBEB_LOG_NORMAL, test_logging_callback);

  r = common_init(&ctx, "Cubeb logging test");
  ASSERT_EQ(r, CUBEB_OK) << "Error initializing cubeb library";

  std::unique_ptr<cubeb, decltype(&cubeb_destroy)>
    cleanup_cubeb_at_exit(ctx, cubeb_destroy);


  output_params.format = STREAM_FORMAT;
  output_params.rate = SAMPLE_FREQUENCY;
  output_params.channels = OUTPUT_CHANNELS;
  output_params.layout = OUTPUT_LAYOUT;
  output_params.prefs = CUBEB_STREAM_PREF_NONE;

  r = cubeb_get_min_latency(ctx, &output_params, &latency_frames);
  ASSERT_EQ(r, CUBEB_OK) << "Could not get minimal latency";

  r = cubeb_stream_init(ctx, &stream, "Cubeb logging",
                        NULL, NULL, NULL, &output_params,
                        latency_frames, data_cb_load, state_cb, NULL);
  ASSERT_EQ(r, CUBEB_OK) << "Error initializing cubeb stream";

  std::unique_ptr<cubeb_stream, decltype(&cubeb_stream_destroy)>
    cleanup_stream_at_exit(stream, cubeb_stream_destroy);

  ASSERT_NE(log_statements_received.load(std::memory_order_acquire), 0);

  cubeb_set_log_callback(CUBEB_LOG_DISABLED, nullptr);
  log_statements_received.store(0, std::memory_order_release);

  // This is synchronous and we'll receive log messages on all backends that we
  // test
  cubeb_stream_start(stream);

  ASSERT_EQ(log_statements_received.load(std::memory_order_acquire), 0);

  cubeb_set_log_callback(CUBEB_LOG_VERBOSE, test_logging_callback);

  // Ensure at least one data callback has occured -- there should have been a
  // log message when that happens, but some backends take some time to start.
  while(data_callback_call_count.load(std::memory_order_acquire) == 0 ||
        log_statements_received.load(std::memory_order_acquire) == 0) {
    delay(50);
  }

  ASSERT_NE(log_statements_received.load(std::memory_order_acquire), 0);

  bool log_callback_set = true;
  uint32_t iterations = 100;
  while (iterations--) {
    // This generally needs to be greater than 10ms (probably more under load),
    // because the async logger dequeues the messages every 10ms.
    delay(20);

    if (!log_callback_set) {
      ASSERT_EQ(log_statements_received.load(std::memory_order_acquire), 0);
      // Set a logging callback, start logging
      cubeb_set_log_callback(CUBEB_LOG_VERBOSE, test_logging_callback);
      log_callback_set = true;
    } else {
      // Disable the logging callback, stop logging.
      ASSERT_NE(log_statements_received.load(std::memory_order_acquire), 0);
      cubeb_set_log_callback(CUBEB_LOG_DISABLED, nullptr);
      log_statements_received.store(0, std::memory_order_release);
      // Disabling logging should flush any log message -- wait a bit and check
      // that this is true.
      delay(20);
      ASSERT_EQ(log_statements_received.load(std::memory_order_acquire), 0);
      log_callback_set = false;
    }
  }

  cubeb_stream_stop(stream);
}
