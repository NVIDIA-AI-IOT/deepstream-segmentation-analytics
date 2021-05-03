*****************************************************************************
* Copyright (c) 2019-2021 NVIDIA Corporation.  All rights reserved.
*
* NVIDIA Corporation and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto.  Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA Corporation is strictly prohibited.
* 
*****************************************************************************
# Instroduction

This deepstream-segmentation-analytics application uses the Nvidia DeepStream-5.1 SDK
to generate the segmentation ground truth JPEG output to display the industrial component defect.
It simulates the real industrial production line env.. The apps run 24 hours / day until it is shut off.

The segmentation accuracy analysis results are in term of:
  * Intersaction of Union
  * Dice Coefficient (F1-Core)

# Prequisites:

Please follow instructions in the apps/sample_apps/deepstream-app/README on how
to install the prequisites for Deepstream SDK, the DeepStream SDK itself and the
apps.


One must have the following development packages installed
  *  GStreamer-1.0 <br>
  *  GStreamer-1.0 Base Plugins <br>
  *  GStreamer-1.0 gstrtspserver <br>
  *  X11 client-side library <br>
  *  DeepStream-5.1 SDK : https://docs.nvidia.com/metropolis/deepstream/dev-guide/index.html

# DeepStream Pipeline 
  * DeepStream SDK is based on the GStreamer framework. GStreamer is a pipeline based multimedia framework that links together a wide variety of media processing systems to complete workflows. Following is the pipleline for this segmentation application. It supports for both binary and multi class model for the segmentation.
![gst-pipleline-png](gst-pipeline.png)


# This DeepStream Segmentation Apps Overview
  * The usr_input.txt gethers the user input information as example as following:

    * batch_size - how many images will need going through the segmentation process for a stream directory

    * width - the output jpg file width (usually same as the input image width)

    * height -the output jpg file height (usually same as the input image height)

    * stream0 - /path/for/the/images0/dir. stream1, streamN will be in the same fasion. 

    * pro_per_sec - repeat the segmentation run after how many seconds 

    * no_streams - how many stream dirs are in the env.

  * each time of apps run, it will go through all the stream directory, i.e, stream0, stream1, streamN to perform a batch size image segmentation <br> 

  * The output jpg file will be saved in the masks directory with the unique name while the input file will be saved in input directory

  * The saved output and input files can be used for the re-training purpose to improve the segmentation accuracy

    

# How to Compile the Application Package
  * git clone this application into /opt/nvidia/deeepstream/deepstream-5.1/sources/apps/sample_apps

  * $ cd deepstream-segmentation-analytics

  * $ make

# How to Run the Application Using the Released Segmentation Models in DeepStream SDK

  * $ ./deepstream-segmentation-analytics -c dstest_segmentation_config_industrial.txt -i usr_input.txt  -for binary segmentation

  * $ ./deepstream-segmentation-analytics -c dstest_segmentation_config_semantic.txt -i usr_input.txt  -for multi class 

  * The program run will generate the output jpg as the masked ground truth after the segmentation which is saved in the masks directory.

      ![segmentation-result](segmentation-result.png)

 

 
 # Segmentation Model Accuracy Analysis Methods

  * Intersection-Over-Union: IoU as known as the Jaccard Index, is one of the most commonly used metrices in semantici segmentaion 

  * Dice Coefficient (F1-Score): It measures the similarities between two sets. 
 
 
 

 
 # Nvidia Transfer Learning Toolking 3.0 for Re-Training, Evaluation, Export, and Quick Deployment




  *  TLT Converter Information (how to download) : https://developer.nvidia.com/tlt-get-started

![tlt-coverter](tlt-converter.png)





  * Use Nvidia TLT 3.0 for Re-Training, Evaluation, Export, and Quick Deployment 

![unet-retrain](unet-retrain.png) 

 



  ## The information for Nvidia Transfer Learning Toolkit 3.0 User Guide on the UNET
  
   * https://docs.nvidia.com/metropolis/TLT/tlt-user-guide/text/semantic_segmentation/unet.html#training-the-model




# Quickly Deploying the Apps to DeepStream-5.1 Using Transfer Learning Toolkit-3.0 <br>

  * Use the .etlt or .engine file after TLT train, export, and coverter

  * Use the Jetson version of the tlt converter to generate the .engine file used in the Jetson devices

    example: ./tlt-converter -k $key -e trt.fp16.tlt.unet.engine -t fp16 -p input_1 1,1x3x320x320, 4x3x320x320,16x3x320x320 model_unet.etlt <br>
    here: $key is the key when do the tlt train

  * Define the .etlt or .engine file path in the config file for dGPU and Jetson for the DS-5.1 application

  * example:  model-engine-file = ../../models/unet/trt.fp16.tlt.unet.engine

  * The performance using different GPU devices

  ![performance-jetson-dgpu](performance-jetson-dgpu.png)
  

# References

  * All the images are from the DAGM 2007 competition dataset: https://www.kaggle.com/mhskjelvareid/dagm-2007-competition-dataset-optical-inspection

  * DAGM-2007 License information reference file:  CDLA-Sharing-v1.0.pdf
 
  * [1] Nvidia DeepStream Referenced Unet Models: https://github.com/qubvel/segmentation_models 
  
  * [2] The example Jupyter Notebook program for Unet training process
        https://github.com/qubvel/segmentation_models/blob/master/examples/binary%20segmentation%20(camvid).ipynb

