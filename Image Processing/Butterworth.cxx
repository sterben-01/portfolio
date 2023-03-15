#include <iostream>
#include <vtkNIFTIImageReader.h>
#include <vtkNIFTIImageWriter.h>
#include <vtkImageWriter.h>
#include <vtkSmartPointer.h>
#include <vtkImageReader.h>
#include <vtkImageData.h>
#include <vtkImageButterworthHighPass.h>
#include <vtkImageFFT.h>
#include <vtkImageRFFT.h>
#include <vtkNew.h>
#include <vtkImageReader2.h>
#include <vtkXMLImageDataWriter.h>
#include <vtkImageExtractComponents.h>
#include <vtkVersion.h>
#include <vtkImageMathematics.h>

int main (int argc, char **argv)
{
  if ( argc < 3 )
    {
      std::cout << "Usage: " << argv[0] << " " << "inputFileName1 outputFileName1 " << std::endl ;
      return 0 ;
    }
  
  //read file via NIFTII
  vtkSmartPointer < vtkNIFTIImageReader > NiftReader = vtkSmartPointer < vtkNIFTIImageReader> ::New() ;
  NiftReader->SetFileName ( argv[1] ) ;

  //apply FFT filter
  vtkSmartPointer < vtkImageFFT > fft = vtkSmartPointer <vtkImageFFT> ::New() ;
  fft->AddInputConnection(NiftReader->GetOutputPort());

  //apply Butterworth filter
  vtkSmartPointer < vtkImageButterworthHighPass> butterworthHighPass = vtkSmartPointer <vtkImageButterworthHighPass> ::New() ;
  butterworthHighPass->AddInputConnection(fft->GetOutputPort());
  butterworthHighPass->SetXCutOff(0.16);
  butterworthHighPass->SetYCutOff(0.16);
  butterworthHighPass->SetOrder(2);

  //apply RFFT filter
  vtkSmartPointer < vtkImageRFFT > rfft = vtkSmartPointer <vtkImageRFFT> ::New() ;
  rfft->AddInputConnection(butterworthHighPass->GetOutputPort());

  vtkSmartPointer < vtkImageExtractComponents > realExtract = vtkSmartPointer <vtkImageExtractComponents> ::New() ;
  realExtract->AddInputConnection(rfft->GetOutputPort());

  // get ABS value
  vtkSmartPointer<vtkImageMathematics> abs = vtkSmartPointer<vtkImageMathematics> ::New();
  abs->AddInputConnection(realExtract->GetOutputPort());
  abs->SetOperationToAbsoluteValue();
  
  //write file out
  vtkSmartPointer < vtkNIFTIImageWriter > NiftWriter = vtkSmartPointer < vtkNIFTIImageWriter > ::New() ;
  NiftWriter->SetFileName ( argv[2] ) ;
  NiftWriter->SetInputConnection (abs->GetOutputPort()); 
  NiftWriter->Write();
  
  return 0;

}
 