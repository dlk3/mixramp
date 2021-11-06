/* Copyright 2010 Boris Phipps */
/* See the file called COPYING for license details. */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <audiofile.h>
#include <errno.h>
#include "gain_analysis.h"


/* Monotonically increasing dB levels for the tags. Maybe an option one day. */
#define DBS   {-90,-60,-40,-30,-24,-21,-18,-15,-12, -9, -6, -3,  0,  3,  6}
#define TIMES {NAN,NAN,NAN,NAN,NAN,NAN,NAN,NAN,NAN,NAN,NAN,NAN,NAN,NAN,NAN}
static const double db[] = DBS;
static double start_db[] = DBS;
static double end_db[] = DBS;
static double start_time[] = TIMES;
static double end_time[] = TIMES;
static const unsigned points = sizeof(db) / sizeof(db[0]);

/* Analysis in 100ms chunks, 20ms is the recommended minimum by ReplayGain
 * but results in errors dues to insufficient samples. */
static const double chunkSeconds = 0.10;

static void usage(const char *name) {
  fprintf(stderr, "Usage: %s <audiofile>\n", name);
  fprintf(stderr, "<audiofile> must be a file that libaudiofile can read.\n");
  fprintf(stderr, "Check with \"sfinfo <audiofile>\".\n");
}


static void analyzeChunk(const double * const lSamples,
                         const double * const rSamples,
                         const size_t chunkSamples, const int channelCount,
                         const double chunkTime, const double length) {

  /* ReplayGain analysis. */
  if (GAIN_ANALYSIS_OK != AnalyzeSamples(lSamples, rSamples,
                                         chunkSamples, channelCount)) {
    fprintf(stderr, "Error in AnalyzeSamples().\n");
    exit(1);
  }
  
  /* Get the volume for the chunk. Resets the analyis for the next chunk. */
  double gain;
  gain = GetTitleGain();
  
  /* Check validity. */
  if (GAIN_NOT_ENOUGH_SAMPLES == (int)gain) {
    // FIXME: should double chunkSamples and start over.
    fprintf(stderr, "chunkSamples %d too small. Recompile.\n", (int)chunkSamples);
    exit(1);
  }
  
  /* The chunk volume is the negative of the computed replaygain. */
  double vol;
  vol = -gain;
  
  /* The start_time records the first time a sample exceeds the threshold. */
  unsigned i;
  for (i = 0; i < points; i++) {
    if (isnan(start_time[i]) && (vol >= db[i])) {
      start_db[i] = vol;
      start_time[i] = chunkTime;
    }
  }
  
  /* The end_time records the time after a sample exceeds the threshold. */
  for (i = 0; i < points; i++) {
    if (vol >= db[i]) {
      end_db[i] = vol;
      end_time[i] = length - chunkTime;
    }
  }

}


static void dumpRamps()
{
  unsigned i;
  double last_db = 0;
  double last_time = NAN;

  printf("MIXRAMP_START=");
  for (i = 0; i < points; i++) {
    if ((last_time == start_time[i] && last_db == start_db[i]) || isnan(start_time[i])) {
      continue;
    }
    printf("%.2f %.2f;", last_db = start_db[i], last_time = start_time[i]);
  }
  puts("");

  last_db = 0;
  last_time = NAN;

  printf("MIXRAMP_END=");
  for (i = 0; i < points; i++) {
    if ((last_time == end_time[i] && last_db == end_db[i]) || isnan(end_time[i])) {
      continue;
    }
    printf("%.2f %.2f;", last_db = end_db[i], last_time = end_time[i]);
  }
  puts("");
}


int main(int argc, char **argv) {

  /* Check usage. */
  if (2 != argc) {
    usage(argv[0]);
    exit(1);
  }

  /* Open the input file. */
  AFfilehandle input;
  input = afOpenFile(argv[1], "r", NULL);
  if (AF_NULL_FILEHANDLE == input) {
    perror(NULL);
    return errno;
  }

  /* Get metadata. */
  int64_t frameCount;
  frameCount = (int64_t)afGetFrameCount(input, AF_DEFAULT_TRACK);

  int channelCount;
  channelCount = afGetVirtualChannels(input, AF_DEFAULT_TRACK);
  if ((1 != channelCount) && (2 != channelCount)) {
    fprintf(stderr, "%s: %d channels not supported.\n", argv[0], channelCount);
    exit(1);
  }

  double sampleRate;
  sampleRate = afGetRate(input, AF_DEFAULT_TRACK);

  /* Add 1 chunk to track length since end_ramp times are recorded with start of
     chunk time and not end of chunk time. */
  double length = frameCount / sampleRate + chunkSeconds;

  /* Setup replaygain code and check sample rate. */
  if (INIT_GAIN_ANALYSIS_OK != InitGainAnalysis(sampleRate)) {
    fprintf(stderr, "Unsupported sample frequency %f.\n", sampleRate);
    exit(1);
  }

  /* Use libaudiofile to convert to double for ReplayGain. */
  afSetVirtualSampleFormat(input, AF_DEFAULT_TRACK, AF_SAMPFMT_DOUBLE, 0);

  /* libaudiofile scales samples to 1.0 > sample > 1.0 when converting. */
  /* gain_analysis.c wants 16bit signed (converted to double). */
  const double scale = 1 << 15;

  /* Allocate data buffers. */
  size_t chunkSamples;
  double *iSamples; /* libaudiofile presents interleaved stereo data */
  double *lSamples;
  double *rSamples;
  chunkSamples = chunkSeconds * sampleRate;
  iSamples = malloc(chunkSamples * sizeof(double) * channelCount);
  if (NULL == iSamples) {
    perror(NULL);
    return errno;
  }
  lSamples = malloc(chunkSamples * sizeof(double));
  if (NULL == lSamples) {
    perror(NULL);
    return errno;
  }
  if (2 == channelCount) {
    rSamples = malloc(chunkSamples * sizeof(double));
    if (NULL == rSamples) {
      perror(NULL);
      return errno;
    }
  } else {
    rSamples = NULL;
  }


  puts("MIXRAMP_REF=89.00");


  /* Process the file one chunk at a time. */
  int chunkCount = 0;
  double chunkTime;
  while ((int)chunkSamples == afReadFrames(input, AF_DEFAULT_TRACK,
                                           iSamples, chunkSamples)) {

    /* Scale and de-interleave sample data. */
    if (1 == channelCount ) {
      unsigned i;
      for (i = 0; i < chunkSamples; i++) {
        lSamples[i] = scale * iSamples[i];
      }
    } else {
      unsigned i;
      for (i = 0; i < chunkSamples; i++) {
        lSamples[i] = scale * iSamples[i * 2];
        rSamples[i] = scale * iSamples[i * 2 + 1];
      }
    }

    chunkTime = chunkCount * chunkSamples / sampleRate;
    analyzeChunk(lSamples, rSamples, chunkSamples, channelCount,
                 chunkTime, length);

    chunkCount++;
  } /* End of loop. */


  dumpRamps();


  exit(0);
}
