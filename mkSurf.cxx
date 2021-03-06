// Try to locate the highest gradient near the surface of the scalp.
// Uses a marker derived from a presurgical scan
#include <iostream>
#include <cstdio>
#include <vector>
#include <algorithm>
#define DIRGRAD

#include "tclap/CmdLine.h"

#include "ioutils.h"
#include <itkBinaryShapeOpeningImageFilter.h>
#include <itkMaskImageFilter.h>
#include "itkBinaryDilateParaImageFilter.h"
#include "itkBinaryErodeParaImageFilter.h"
#include <itkMaximumImageFilter.h>
#include "itkBinaryFillholeImageFilter.h"
#include <itkSubtractImageFilter.h>
#include <itkBinaryShapeKeepNObjectsImageFilter.h>
#include <itkMorphologicalWatershedFromMarkersImageFilter.h>
#include <itkThresholdImageFilter.h>

#ifdef DIRGRAD

#include "itkDirectionalGradientImageFilter.h"
#include <itkSmoothingRecursiveGaussianImageFilter.h>

#else

#include <itkGradientMagnitudeRecursiveGaussianImageFilter.h>

#endif
#include <itkSmartPointer.h>
namespace itk
{
template <typename T>
class Instance : public T::Pointer {
public:
  Instance() : SmartPointer<T>( T::New() ) {}
};
}


typedef class CmdLineType
{
public:
  std::string InputIm, OutputIm, MaskIm, FiducialIm;
  float  erodesize, dilatesize, smoothsize;
  bool LightToDark;
} CmdLineType;

bool debug=false;
std::string debugprefix="/tmp/align";
std::string debugsuffix=".nii.gz";

template <class TImage>
void writeImDbg(const typename TImage::Pointer Im, std::string filename)
{
  if (debug)
    {
    writeIm<TImage>(Im, debugprefix + "_" + filename + debugsuffix);
    }
}

void ParseCmdLine(int argc, char* argv[],
                  CmdLineType &CmdLineObj
  )
{
  using namespace TCLAP;
  try
    {
    // Define the command line object.
    CmdLine cmd("mkSurf", ' ', "0.9");

    ValueArg<std::string> inArg("i","input","T1 input image",true,"result","string");
    cmd.add( inArg );

    ValueArg<std::string> outArg("o","output","output image",true,"result","string");
    cmd.add( outArg );

    ValueArg<std::string> maskArg("m","mask","mask image - used to generate markers",true,"result","string");
    cmd.add( maskArg );

    ValueArg<std::string> fidArg("f","fiducial","fiducial mask image - used to help scal seg when frame in place",false,"","string");
    cmd.add( fidArg );

    ValueArg<float> smoothArg("", "smoothing", "size of small gradient smoothing (mm)", false, 2, "float");
    cmd.add(smoothArg);

    ValueArg<float> eroArg("", "erode", "size of erosion to create marker(mm)", 
                              false, 3, "float");
    cmd.add(eroArg);

    ValueArg<float> dilArg("", "dilate", "size of dilation background marker (mm)", 
                              false, 3, "float");
    cmd.add(dilArg);

    SwitchArg debugArg("d", "debug", "save debug images", debug);
    cmd.add( debugArg );

    SwitchArg signArg("", "darktolight", "look for dark to light edge", false);
    cmd.add( signArg );

    // Parse the args.
    cmd.parse( argc, argv );

    CmdLineObj.InputIm = inArg.getValue();
    CmdLineObj.OutputIm = outArg.getValue();
    CmdLineObj.MaskIm = maskArg.getValue();
    CmdLineObj.FiducialIm = fidArg.getValue();

    debug = debugArg.getValue();
    CmdLineObj.erodesize = eroArg.getValue();
    CmdLineObj.smoothsize = smoothArg.getValue();
    CmdLineObj.dilatesize = dilArg.getValue();
    CmdLineObj.LightToDark = !signArg.getValue();
    }
  catch (ArgException &e)  // catch any exceptions
    {
    std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
    }
}

/////////////////////////////////////////////////////////////////

template <class PixType, int dimension>
void doSeg(const CmdLineType &CmdLineObj)
{
  // floating point image for spm prob maps
  typedef itk::Image<PixType, dimension> ImageType;
  typedef typename ImageType::Pointer IPtr;
  typedef typename itk::Image<unsigned char, ImageType::ImageDimension> MaskImType;
  typedef typename MaskImType::Pointer MIPtr;

  IPtr T1 = readIm<ImageType>(CmdLineObj.InputIm);
  MIPtr mask = readIm<MaskImType>(CmdLineObj.MaskIm);

  // fill holes
  itk::Instance<itk::BinaryFillholeImageFilter<MaskImType > > Fill;
  Fill->SetInput(mask);
  // set up the marker
  itk::Instance<itk::BinaryErodeParaImageFilter<MaskImType> > Erode;
  itk::Instance<itk::BinaryDilateParaImageFilter<MaskImType> > Dilate;

  // A special mask to use for directional gradient
  itk::Instance<itk::BinaryDilateParaImageFilter<MaskImType> > HEADDilate;
  HEADDilate->SetInput(Fill->GetOutput());
  HEADDilate->SetRadius(std::max(CmdLineObj.dilatesize, 5.0f));
  HEADDilate->SetUseImageSpacing(true);

  Erode->SetInput(Fill->GetOutput());
  Erode->SetRadius(CmdLineObj.erodesize);
  Erode->SetUseImageSpacing(true);

  Dilate->SetInput(Fill->GetOutput());
  Dilate->SetRadius(CmdLineObj.dilatesize);
  Dilate->SetUseImageSpacing(true);
  
  itk::Instance<itk::BinaryThresholdImageFilter<MaskImType,MaskImType> > Invert;
  Invert->SetInput(Dilate->GetOutput());
  Invert->SetUpperThreshold(0);
  Invert->SetLowerThreshold(0);
  Invert->SetInsideValue(2);
  Invert->SetOutsideValue(0);

  itk::Instance<itk::MaximumImageFilter<MaskImType, MaskImType, MaskImType> >Comb;
  Comb->SetInput(Erode->GetOutput());
  Comb->SetInput2(Invert->GetOutput());

  // Directional gradient
#ifdef DIRGRAD
  itk::Instance< itk::DirectionalGradientImageFilter<ImageType, MaskImType, ImageType> > GradD;
  GradD->SetInput(T1);
  GradD->SetMaskImage(HEADDilate->GetOutput());
  GradD->SetOutsideValue(0);
  if (!CmdLineObj.LightToDark) 
    {
    GradD->SetScale(-1);
    std::cout << "Looking for dark to light edge" << std::endl;
    }
  // thresholding will stop the negative edges influencing the
  // smoothing
  
  itk::Instance< itk::ThresholdImageFilter< ImageType > > DGThresh;
  DGThresh->SetInput(GradD->GetOutput());
  DGThresh->ThresholdBelow(0);
  DGThresh->SetLower(0);

  itk::Instance< itk::SmoothingRecursiveGaussianImageFilter <ImageType, ImageType> > Grad;
  Grad->SetInput(DGThresh->GetOutput());
  Grad->SetSigma(CmdLineObj.smoothsize);
#else  
  itk::Instance< itk::GradientMagnitudeRecursiveGaussianImageFilter<ImageType, ImageType> > Grad;
  Grad->SetInput(T1);
  Grad->SetSigma(CmdLineObj.smoothsize);
#endif

  IPtr gradient = Grad->GetOutput();

  // include the fiducual information if it is there.
  if (CmdLineObj.FiducialIm != "") 
    {
    MIPtr fiducials = readIm<MaskImType>(CmdLineObj.FiducialIm);
    // use an arbitary value of 100 for fiducial gradient - should
    // do the job - do we need to dilate too?
    itk::Instance< itk::ShiftScaleImageFilter<MaskImType, ImageType> > scaler;
    scaler->SetScale(100);
    scaler->SetInput(fiducials);
    itk::Instance< itk::MaximumImageFilter<ImageType, ImageType, ImageType> > MaxComb;
    MaxComb->SetInput(scaler->GetOutput());
    MaxComb->SetInput2(Grad->GetOutput());

    gradient=MaxComb->GetOutput();
    gradient->Update();
    gradient->DisconnectPipeline();
    }


  itk::Instance<itk::MorphologicalWatershedFromMarkersImageFilter<ImageType, MaskImType> > WS;
  WS->SetInput(gradient);
  WS->SetMarkerImage(Comb->GetOutput());
  WS->SetMarkWatershedLine(false);

  itk::Instance<itk::BinaryThresholdImageFilter<MaskImType,MaskImType> > Select;
  Select->SetInput(WS->GetOutput());
  Select->SetUpperThreshold(1);
  Select->SetLowerThreshold(1);
  Select->SetInsideValue(1);
  Select->SetOutsideValue(0);

  // Now we do a second stage to look for the peak brightness.
  // Only makes sense when the first stage was looking for the 
  // inner fat layer
  writeIm<MaskImType>(Select->GetOutput(), "/tmp/stage1.nii.gz");

  itk::Instance< itk::SmoothingRecursiveGaussianImageFilter <ImageType, ImageType> > Grad2;
  Grad2->SetInput(T1);
  Grad2->SetSigma(CmdLineObj.smoothsize);

  IPtr gradient2 = Grad2->GetOutput();
  // include the fiducual information if it is there.
  if (CmdLineObj.FiducialIm != "") 
    {
    MIPtr fiducials = readIm<MaskImType>(CmdLineObj.FiducialIm);
    // use an arbitary value of 200 for fiducial gradient - should
    // do the job - do we need to dilate too?
    itk::Instance< itk::ShiftScaleImageFilter<MaskImType, ImageType> > scaler;
    scaler->SetScale(200);
    scaler->SetInput(fiducials);
    itk::Instance< itk::MaximumImageFilter<ImageType, ImageType, ImageType> > MaxComb;
    MaxComb->SetInput(scaler->GetOutput());
    MaxComb->SetInput2(Grad2->GetOutput());

    gradient2=MaxComb->GetOutput();
    gradient2->Update();
    gradient2->DisconnectPipeline();
    }

  // new marker - stage 1 result and initial background
  itk::Instance<itk::MaximumImageFilter<MaskImType, MaskImType, MaskImType> >Comb2;
  Comb2->SetInput(Select->GetOutput());
  Comb2->SetInput2(Invert->GetOutput());

  itk::Instance<itk::MorphologicalWatershedFromMarkersImageFilter<ImageType, MaskImType> > WS2;
  WS2->SetInput(gradient2);
  WS2->SetMarkerImage(Comb2->GetOutput());
  WS2->SetMarkWatershedLine(false);

  itk::Instance<itk::BinaryThresholdImageFilter<MaskImType,MaskImType> > Select2;
  Select2->SetInput(WS2->GetOutput());
  Select2->SetUpperThreshold(1);
  Select2->SetLowerThreshold(1);
  Select2->SetInsideValue(1);
  Select2->SetOutsideValue(0);

  writeIm<MaskImType>(Select2->GetOutput(), CmdLineObj.OutputIm);
  writeIm<ImageType>(gradient2, "/tmp/grad2.nii.gz");
  writeIm<MaskImType>(Comb2->GetOutput(), "/tmp/marker2.nii.gz");

}

int main(int argc, char * argv[])
{
  CmdLineType CmdLineObj;
  ParseCmdLine(argc, argv, CmdLineObj);

  const int dimension = 3;
  int dim1 = 0;
  itk::ImageIOBase::IOComponentType ComponentType;
  if (!readImageInfo(CmdLineObj.InputIm, &ComponentType, &dim1)) 
    {
    std::cerr << "Failed to open " << CmdLineObj.InputIm << std::endl;
    return(EXIT_FAILURE);
    }
  if (dim1 != dimension) 
    {
      std::cerr << CmdLineObj.InputIm << "isn't 3D" << std::endl;
      return(EXIT_FAILURE);
    }
  // These tolerances are being set high because we rely on spm to pass in
  // appropriate data. Problems arise because spm uses the sform when
  // the sform and qform are different while ITK seems to use the
  // qform.
  // This code doesn't use orientation info, so we'll ignore the
  // headers.
  // We'll use spm code to copy the headers in spm style.

  itk::ImageToImageFilterCommon::SetGlobalDefaultCoordinateTolerance(1000.0);
  itk::ImageToImageFilterCommon::SetGlobalDefaultDirectionTolerance(1000.0);

  switch (ComponentType) 
    {
    case (itk::ImageIOBase::SHORT):
      doSeg<short, dimension>(CmdLineObj);
      break;
    case (itk::ImageIOBase::USHORT):
      doSeg<unsigned short, dimension>(CmdLineObj);
      break;
    case (itk::ImageIOBase::INT):
      doSeg<int, dimension>(CmdLineObj);
      break;
    default:
      doSeg<float, dimension>(CmdLineObj);
      break;
    }

  return(EXIT_SUCCESS);
}
