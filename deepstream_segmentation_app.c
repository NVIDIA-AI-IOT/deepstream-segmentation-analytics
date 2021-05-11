/*
 * Copyright (c) 2019-2020, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <gst/gst.h>
#include <glib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>
#include <time.h>
#include <iostream>
#include <fstream>
#include <string>
using namespace std;

#include "gstnvdsmeta.h"
#include "gst-nvmessage.h"

/* The muxer output resolution must be set if the input streams will be of
 * different resolution. The muxer will scale all the input frames to this
 * resolution. */

static gint MUXER_OUTPUT_WIDTH;
static gint MUXER_OUTPUT_HEIGHT;

/* Muxer batch formation timeout, for e.g. 40 millisec. Should ideally be set
 * based on the fastest source's framerate. */
//for loading more picture case, need to increase MUXER_BATCH_TIMEOUT_USEC to
//avoid the empty display holes in the .jpg file 
//If use many image files each time run, set following to -1. this will make muxer wait for all sources to be up, but if one of the image is offline, then ithang the pipeline

#define MUXER_BATCH_TIMEOUT_USEC -1

static gint TILED_OUTPUT_WIDTH;
static gint TILED_OUTPUT_HEIGHT;
//#define TILED_OUTPUT_WIDTH 1024
//#define TILED_OUTPUT_HEIGHT 1024

#define NVINFER_PLUGIN "nvinfer"
#define NVINFERSERVER_PLUGIN "nvinferserver"


static int production = 0;
static int pro_per_sec;
static int no_streams;
static int stream_index = 0;
static guint num_sources = 1;
static gint MAX_NUM_FILE = 8;

static gint frame_number;
static struct timeval g_start;
static struct timeval g_end;
static struct timeval current_time;
static float g_accumulated_time_macro = 0;
static gint pic_no = 0;

static void profile_start() {
    gettimeofday(&g_start, 0);
}

static void profile_end() {
    gettimeofday(&g_end, 0);
}

static float profile_delta() {

    int delta;
    g_accumulated_time_macro += 1000000 * (g_end.tv_sec - g_start.tv_sec)
                                + g_end.tv_usec - g_start.tv_usec;
    delta = g_accumulated_time_macro/1000000;
    //std::cout << "The Delta time = " << delta << std::endl;
    return delta;
}


static void profile_result() {

    frame_number = MAX_NUM_FILE;

    g_accumulated_time_macro += 1000000 * (g_end.tv_sec - g_start.tv_sec)
                                + g_end.tv_usec - g_start.tv_usec;
    float fps = (float)((frame_number) / (float)(g_accumulated_time_macro/1000000));
    std::cout << "The average frame rate is " << fps<< ", frame num " << frame_number << ", time accumulated " << g_accumulated_time_macro/1000000 << std::endl;

}


static void cpu_profile() {

    frame_number = MAX_NUM_FILE;

    g_accumulated_time_macro += 1000000 * (g_end.tv_sec - g_start.tv_sec)
                                + g_end.tv_usec - g_start.tv_usec;
    std::cout << "For frame = " << frame_number << ", CPU time accumulated " << g_accumulated_time_macro/1000000 << std::endl;

}


/* tiler_sink_pad_buffer_probe  will extract metadata received on segmentation
 *  src pad */
static GstPadProbeReturn
tiler_src_pad_buffer_probe (GstPad * pad, GstPadProbeInfo * info,
    gpointer u_data)
{
    GstBuffer *buf = (GstBuffer *) info->data;
    NvDsMetaList * l_frame = NULL;
    NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta (buf);

    for (l_frame = batch_meta->frame_meta_list; l_frame != NULL;
      l_frame = l_frame->next) {
        // TODO:
    }
    return GST_PAD_PROBE_OK;
}

static gboolean
bus_call (GstBus * bus, GstMessage * msg, gpointer data)
{
  GMainLoop *loop = (GMainLoop *) data;
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:
      g_print ("End of stream\n\n\n");
      // Add the delay to show the result
      usleep(1000000);
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_WARNING:
    {
      gchar *debug;
      GError *error;
      gst_message_parse_warning (msg, &error, &debug);
      g_printerr ("WARNING from element %s: %s\n",
          GST_OBJECT_NAME (msg->src), error->message);
      g_free (debug);
      g_printerr ("Warning: %s\n", error->message);
      g_error_free (error);
      break;
    }
    case GST_MESSAGE_ERROR:
    {
      gchar *debug;
      GError *error;
      gst_message_parse_error (msg, &error, &debug);
      g_printerr ("ERROR from element %s: %s\n",
          GST_OBJECT_NAME (msg->src), error->message);
      if (debug)
        g_printerr ("Error details: %s\n", debug);
      g_free (debug);
      g_error_free (error);
      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_ELEMENT:
    {
      if (gst_nvmessage_is_stream_eos (msg)) {
        guint str_id;
        if (gst_nvmessage_parse_stream_eos (msg, &str_id)) {
          pic_no++;
          g_print ("Got EOS from stream %d\n", str_id);
          if(pic_no == MAX_NUM_FILE){
             //get the the profiling data
             profile_end();
             profile_result();
          }
        }
      }
      break;
    }
    default:
      break;
  }
  return TRUE;
}

static GstElement *
create_source_bin (guint index, gchar * uri)
{
  GstElement *bin = NULL;
  gchar bin_name[16] = { };

  g_snprintf (bin_name, 15, "source-bin-%02d", index);
  /* Create a source GstBin to abstract this bin's content from the rest of the
   * pipeline */
  bin = gst_bin_new (bin_name);

  GstElement *source, *jpegparser, *decoder;

  source = gst_element_factory_make ("filesrc", "source");

  jpegparser = gst_element_factory_make ("jpegparse", "jpeg-parser");

  decoder = gst_element_factory_make ("nvv4l2decoder", "nvv4l2-decoder");

  if (!source || !jpegparser || !decoder)
  {
    g_printerr ("One element could not be created. Exiting.\n");
    return NULL;
  }
  g_object_set (G_OBJECT (source), "location", uri, NULL);
  const char *dot = strrchr(uri, '.');
  if ((!strcmp (dot+1, "mjpeg")) || (!strcmp (dot+1, "mjpg")))
  {
#ifdef PLATFORM_TEGRA
    g_object_set (G_OBJECT (decoder), "mjpeg", 1, NULL);
#endif
  }

  gst_bin_add_many (GST_BIN (bin), source, jpegparser, decoder, NULL);

  gst_element_link_many (source, jpegparser, decoder, NULL);

  /* We need to create a ghost pad for the source bin which will act as a proxy
   * for the video decoder src pad. The ghost pad will not have a target right
   * now. Once the decode bin creates the video decoder and generates the
   * cb_newpad callback, we will set the ghost pad target to the video decoder
   * src pad. */
  if (!gst_element_add_pad (bin, gst_ghost_pad_new_no_target ("src",
              GST_PAD_SRC))) {
    g_printerr ("Failed to add ghost pad in source bin\n");
    return NULL;
  }

  GstPad *srcpad = gst_element_get_static_pad (decoder, "src");
  if (!srcpad) {
    g_printerr ("Failed to get src pad of source bin. Exiting.\n");
    return NULL;
  }
  GstPad *bin_ghost_pad = gst_element_get_static_pad (bin, "src");
  if (!gst_ghost_pad_set_target (GST_GHOST_PAD (bin_ghost_pad),
        srcpad)) {
    g_printerr ("Failed to link decoder src pad to source bin ghost pad\n");
  }

  return bin;
}

static void printUsage(const char* cmd) {
    g_printerr ("\tUsage: %s -c dstest_segmentation_config_industrial.txt -b tatch-size -h img_out_height -w img_out_width -d image_dir\n", cmd);
    g_printerr ("-h: \n\timage output height \n");
    g_printerr ("-i: \n\timage output width \n");
    g_printerr ("-c: \n\tseg config file, e.g. dstest_segmentation_config_industrial.txt  \n");
    g_printerr ("-b: \n\tbatch size, this will override the value of \"baitch-size\" in config file  \n");
    g_printerr ("-d: \n\tThe image directory  \n");
}


int
main (int argc, char *argv[])
{

//define the GstElement pointer
  GMainLoop *loop = NULL;
  GstElement *pipeline = NULL, *streammux = NULL, *sink = NULL, *seg = NULL,
      *nvsegvisual = NULL, *tiler = NULL, *nvvidconv = NULL,
      *parser = NULL, *parser1 = NULL, *source = NULL, *enc = NULL,
      *nvvidconv1 = NULL, *decoder = NULL, *tee = NULL, *nvdsosd = NULL;


#ifdef PLATFORM_TEGRA
  GstElement *transform = NULL;
#endif
  GstBus *bus = NULL;
  guint bus_watch_id;
  GstPad *seg_src_pad = NULL;
  guint i;
  guint tiler_rows, tiler_columns;
  guint pgie_batch_size;
  std::string seg_config;
  std::string usr_config;
  guint c;
  const char* optStr = "b:c:d:h:i:";
  std::string input_file;
  std::string image_dir;
  std::string in_file;
  std::string file_out;
  std::string out_file[2000];

  GList *files = NULL;
  gboolean is_nvinfer_server = FALSE;
  gchar infer_config_file[200];
  gchar usr_config_file[200];
  guint batchSize = 1;
  guint height, width;

  struct dirent *pDirent;
  DIR *pDir;
  char pic_file[100];
  char buff[200];   
  char argv2[2000][2000];
  char file[2000][2000];
  char streams[200][200];
  int idx, idx2, r;
  int num_pic=0;
  int w_loop = 0;

  string images;
  string line;
  string result;
  string delimiter = "=";
  string delimiter2 = " ";
  size_t pos = 0;
  string token;
  string cmd;
  int del;

  profile_start();
  printf("Get CPU profile_start()\n");
 
  //process the command line argument
  while ((c = getopt(argc, argv, optStr)) != -1) {
        switch (c) {
            case 'c':
                seg_config.assign(optarg);
                strcpy(infer_config_file, seg_config.c_str());
                break;
            case 'i': 
                usr_config.assign(optarg);
                strcpy(usr_config_file, usr_config.c_str());
                break;
            default:  
                printUsage(argv[0]);
                return -1;
        }
  }

  /* Check input arguments */
  if (argc < 5) {
      printUsage(argv[0]);
     return -1;
  }

  //process the usr_input.txt as the input for the helm chart
  ifstream myfile ("usr_input.txt");
  if (myfile.is_open())
  {
    while ( getline (myfile,line) )
    {
      printf("Get the line: %s\n", line.c_str());
      while ((pos = line.find(delimiter)) != std::string::npos) {
          token = line.substr(0, pos);
          //std::cout << token << std::endl;
          line.erase(0, pos + delimiter.length());
          if((token.compare("batch_size")) == 0){
             token = line.substr(0, pos);
             //std::cout << token << std::endl;
             batchSize = stoi(token);
          }else if((token.compare("width")) == 0){
             token = line.substr(0, pos);
             //std::cout << token << std::endl;
             width = stoi(token);
          }else if((token.compare("height")) == 0){
             token = line.substr(0, pos);
             //std::cout << token << std::endl;
             height = stoi(token);
          }else if((token.compare("pro_per_sec")) == 0){
             token = line.substr(0, pos);
             //std::cout << token << std::endl;
             pro_per_sec = stoi(token);
          }else if((token.compare("no_streams")) == 0){
             token = line.substr(0, pos);
             //std::cout << token << std::endl;
             no_streams = stoi(token);
          }else if((token.compare("production")) == 0){
             token = line.substr(0, pos);
             //std::cout << token << std::endl;
             production = stoi(token);
          }else{
             result = token.substr (0,6); 
	     if(result.compare("stream") == 0){
                std::cout << token << std::endl;
                strcpy(streams[stream_index++], line.c_str());
	     }
          }
      }
    }
  }else{
    cout << "Unable to open file";
    return 0;
  }
  myfile.close();

  printf("batchSize = %d, width = %d, height = %d\n", batchSize, width, height);
  printf("no_streams = %d, pro_per_sec = %d\n", no_streams, pro_per_sec);
  printf("production = %d\n", production); 

  MAX_NUM_FILE = num_sources;
  MUXER_OUTPUT_WIDTH = width;
  MUXER_OUTPUT_HEIGHT = height;
  TILED_OUTPUT_WIDTH = width;
  TILED_OUTPUT_HEIGHT = height;

  printf("Get the batchSize = %d\n", batchSize);
  printf("Get the num_sources = %d\n", num_sources);
  printf("Get the infer_config_file  = %s\n", infer_config_file);
  printf("Get the MUXER_OUTPUT_WIDTH  = %d\n", MUXER_OUTPUT_WIDTH);
  printf("Get the MUXER_OUTPUT_HEIGHT  = %d\n", MUXER_OUTPUT_HEIGHT);


  //loop forever until shut off
  while(1){ 

     //to get time delta
     profile_end();
     del = profile_delta();

     //start segmentation every pro_er_sec from usr_input.txt
     if(((del % pro_per_sec) == 0) && (del != 0)){

        for(int k=0; k<no_streams; k++){

           image_dir = streams[k];
           printf("Get the image dir = %s\n", image_dir.c_str());
           pDir = opendir (image_dir.c_str());
           if (pDir == NULL) {
               printf ("Cannot open directory '%s'\n", argv[6]);
               return 1;
           }

           num_pic = 0; //reset
           // Process each entry.
           while ((pDirent = readdir(pDir)) != NULL) {
               if((strcmp(pDirent->d_name,".") != 0) &&
                                  (strcmp(pDirent->d_name,"..") != 0)){
                  idx = num_pic;
                  strcpy(pic_file, image_dir.c_str());
                  strcat(pic_file, "/");
                  strcat(pic_file, pDirent->d_name);
                  strcpy(argv2[idx], pic_file);
                  out_file[idx] = pDirent->d_name;
                  //printf("Got the pic file: %s with idx=%d\n", argv2[idx], idx);
                  num_pic++;
               }
           }
           closedir (pDir);

           if(!num_pic){  //after 0 increament
              printf ("There is NO images in the directory! \n");
              //return 1;
           }

           if(num_pic < batchSize){
              batchSize = num_pic;  //in case of num_pic is less
           }

   
          //process the Gst pipeline
          for (int j=0; j<batchSize; j++){


            //for profile use
            printf("Get CPU profile_end()\n");
            profile_end();
            cpu_profile();

            *(argv + j) = argv2[j];
            in_file = argv2[j];
            files = g_list_append (files, argv[j]);
            printf("\nLoading the image file: %s\n", argv[j]);

            //save the file in the input directory for re-training use later
            if(production){
               cmd = "cp " + in_file + " input";
               system(cmd.c_str());
            }

            /* Standard GStreamer initialization */
            gst_init (&argc, &argv);
            loop = g_main_loop_new (NULL, FALSE);

            /* Create gstreamer elements */
            /* Create Pipeline element that will form a connection of other elements */
            pipeline = gst_pipeline_new ("dstest-image-decode-pipeline");

            /* Create nvstreammux instance to form batches from one or more sources. */
            streammux = gst_element_factory_make ("nvstreammux", "stream-muxer");

            if (!pipeline || !streammux) {
              g_printerr ("One element could not be created. Exiting.\n");
              return -1;
            }

            gst_bin_add (GST_BIN (pipeline), streammux);

            GstPad *sinkpad, *srcpad;
            gchar pad_name[16] = { };
            GstElement *source_bin = create_source_bin (j,
                   (char *) g_list_nth_data(files, j));

            if (!source_bin) {
              g_printerr ("Failed to create source bin. Exiting.\n");
              return -1;
            }

            gst_bin_add (GST_BIN (pipeline), source_bin);
   
            g_snprintf (pad_name, 15, "sink_%u", j);
            sinkpad = gst_element_get_request_pad (streammux, pad_name);
            if (!sinkpad) {
              g_printerr ("Streammux request sink pad failed. Exiting.\n");
              return -1;
            }

            srcpad = gst_element_get_static_pad (source_bin, "src");
            if (!srcpad) {
              g_printerr ("Failed to get src pad of source bin. Exiting.\n");
              return -1;
            }

            if (gst_pad_link (srcpad, sinkpad) != GST_PAD_LINK_OK) {
              g_printerr ("Failed to link source bin to stream muxer. Exiting.\n");
              return -1;
            }

            gst_object_unref (srcpad);
            gst_object_unref (sinkpad);
   
            /* Use nvinfer to infer on batched frame. */
            seg = gst_element_factory_make (
                is_nvinfer_server ? NVINFERSERVER_PLUGIN : NVINFER_PLUGIN,
                   "primary-nvinference-engine");

            nvsegvisual = gst_element_factory_make ("nvsegvisual", "nvsegvisual");
   
            /* Use nvtiler to composite the batched frames into a 2D tiled array based
             * on the source of the frames. */
            tiler = gst_element_factory_make ("nvmultistreamtiler", "nvtiler");

#ifdef PLATFORM_TEGRA
            transform = gst_element_factory_make ("nvegltransform", "transform");
#endif

            sink = gst_element_factory_make ("filesink", "file-sink");
            //sink = gst_element_factory_make ("fakesink", "fake-renderer");
            //sink = gst_element_factory_make ("nveglglessink", "nvvideo-renderer");
            //following is for later with the option: display on screee ro goes to a file
            nvvidconv = gst_element_factory_make ("nvvideoconvert", "nvvideo-converter");
            nvvidconv1 = gst_element_factory_make ("nvvideoconvert", "nvvideo-converter1");

            /* Create OSD to draw on the converted RGBA buffer */
            nvdsosd = gst_element_factory_make ("nvdsosd", "nv-onscreendisplay");
            parser = gst_element_factory_make ("jpegparse", "jpeg-parser");
            parser1 = gst_element_factory_make ("jpegparse", "jpeg-parser1");
            enc = gst_element_factory_make ("jpegenc", "jpeg-enc");
  
            //for the output file
            if(production){
               file_out = "out_rgba_" + out_file[j];
               g_object_set (G_OBJECT (sink), "location", file_out.c_str(), NULL);
            }else{ //for helm-chart case
               g_object_set (G_OBJECT (sink), "location", "./out.jpg", NULL);
            }

            decoder = gst_element_factory_make ("nvv4l2decoder", "nvv4l2-decoder");
            tee = gst_element_factory_make("tee", "tee");

/* for debug
            if(!seg) printf("seg == NULL\n");
            if(!nvsegvisual) printf("nvsegvisual == NULL\n");
            if(!tiler) printf("tiler == NULL\n");
            if(!sink) printf("sink == NULL\n");
            if(!enc) printf("enc == NULL\n");
            if(!parser) printf("parser == NULL\n");
            if(!nvdsosd) printf("nvdsosd == NULL\n");
            if(!decoder) printf("decoder == NULL\n");
            if(!tee) printf("tee == NULL\n");
            if(!parser1) printf("parser1 == NULL\n");
            if(!nvvidconv) printf("nvvidconv == NULL\n");
            if(!nvvidconv1) printf("nvvidconv1 == NULL\n");
*/

            if (!seg || !nvsegvisual || !tiler || !sink || !enc || !parser ||
                  !nvdsosd || !decoder || !tee || !parser1
                                       || !nvvidconv || !nvvidconv1) {
              g_printerr ("Here one element could not be created. Exiting.\n");
              return -1;
            }


#ifdef PLATFORM_TEGRA
            if(!transform) {
              g_printerr ("One tegra element could not be created. Exiting.\n");
              return -1;
            }
#endif

            g_object_set (G_OBJECT (streammux), "batch-size", num_sources, NULL);

            g_object_set (G_OBJECT (streammux), "width", MUXER_OUTPUT_WIDTH, "height",
                MUXER_OUTPUT_HEIGHT,
                "batched-push-timeout", MUXER_BATCH_TIMEOUT_USEC, NULL);

            /* Configure the nvinfer element using the nvinfer config file. */
            g_object_set (G_OBJECT (seg), "config-file-path", infer_config_file, NULL);

            /* Override the batch-size set in the config file with the number of sources. */
            g_object_get (G_OBJECT (seg), "batch-size", &pgie_batch_size, NULL);
            if (pgie_batch_size != num_sources && !is_nvinfer_server) {
                 g_printerr
                  ("WARNING: Overriding infer-config batch-size (%d) with number of sources (%d)\n",
                  pgie_batch_size, num_sources);
              g_object_set (G_OBJECT (seg), "batch-size", num_sources, NULL);
            }

            g_object_set (G_OBJECT (nvsegvisual), "batch-size", num_sources, NULL);
            g_object_set (G_OBJECT (nvsegvisual), "width", 512, NULL);
            g_object_set (G_OBJECT (nvsegvisual), "height", 512, NULL);

            tiler_rows = (guint) sqrt (num_sources);
            tiler_columns = (guint) ceil (1.0 * num_sources / tiler_rows);
            /* we set the tiler properties here */
            g_object_set (G_OBJECT (tiler), "rows", tiler_rows, "columns", tiler_columns,
                "width", TILED_OUTPUT_WIDTH, "height", TILED_OUTPUT_HEIGHT, NULL);

            g_object_set(G_OBJECT(sink), "async", FALSE, NULL);

            /* we add a message handler */
            bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
            bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
            gst_object_unref (bus);

            /* Set up the pipeline */
            /* Add all elements into the pipeline */
#ifdef PLATFORM_TEGRA

            //no need to put the transform in since it output to a file
            gst_bin_add_many (GST_BIN (pipeline), seg, nvsegvisual, tiler,
                          nvvidconv, nvdsosd, nvvidconv1, enc, parser1, sink, NULL);
            /* we link the elements together
             * nvstreammux -> nvinfer -> nvsegvidsual -> nvtiler -> filesink */
            if (!gst_element_link_many (streammux, seg, nvsegvisual, tiler,
                       nvvidconv, nvdsosd, nvvidconv1, enc, parser1, sink, NULL))
            {
              g_printerr ("Elements could not be linked. Exiting.\n");
              return -1;
            }

#else

            gst_bin_add_many (GST_BIN (pipeline), seg, nvsegvisual, tiler,
                    nvvidconv, nvdsosd, nvvidconv1, enc, parser1, sink, NULL);

            /* Link the elements together
             * nvstreammux -> nvinfer -> nvsegvisual -> nvtiler -> video-renderer */

            if (!gst_element_link_many (streammux, seg, nvsegvisual, tiler, nvvidconv, nvdsosd, nvvidconv1, enc, parser1, sink, NULL)) {
              g_printerr ("Elements could not be linked. Exiting.\n");
              return -1;
            }

#endif

            /* Lets add probe to get informed of the meta data generated, we add probe to
             * the src pad of the nvseg element, since by that time, the buffer would have
             * had got all the segmentation metadata. */
            seg_src_pad = gst_element_get_static_pad (seg, "src");
            if (!seg_src_pad)
              g_print ("Unable to get src pad\n");
            else
              gst_pad_add_probe (seg_src_pad, GST_PAD_PROBE_TYPE_BUFFER,
                  tiler_src_pad_buffer_probe, NULL, NULL);
            gst_object_unref (seg_src_pad);

            /* Set the pipeline to "playing" state */
            g_print ("\nNow playing:");
            g_print (" %s,", (char *)g_list_nth_data(files, j));
            g_print ("\n");
            gst_element_set_state (pipeline, GST_STATE_PLAYING);

            /* Wait till pipeline encounters an error or EOS */
            g_print ("Running...\n");

            //start the main loop and perform the profile check
            profile_start();
            g_main_loop_run (loop);

            /* Out of the main loop, clean up nicely */
            g_print ("Returned, stopping playback\n");
            gst_element_set_state (pipeline, GST_STATE_NULL);
            g_print ("Deleting pifile_outpeline\n");
            gst_object_unref (GST_OBJECT (pipeline));
            g_source_remove (bus_watch_id);
            g_main_loop_unref (loop);

            //save the output ground truth file masks dir.
            if(production){
               cmd = "mv " + file_out + " mask";
               system(cmd.c_str());
               printf("\nMove the file: %s into the mask directory\n", file_out.c_str());

               //remove the input file used
               cmd = "rm -f " + in_file;
               printf("Delete the file: %s\n\n", in_file.c_str());
               system(cmd.c_str());
            }

          }//end of for (int j=0; j<batchSize; j++){
   
        }//for(int k=0; k<no_streams; k++){

     } //if((del % pro_per_sec) == 0){

  }//end of while(1)

  return 0;
}
