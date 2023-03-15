#include <iostream>

// include stuff here
#include <itkImage.h>
#include <itkImageFileReader.h>
#include <itkImageFileWriter.h>
#include "itkImageFileReader.h"
#include "itkImageFileWriter.h"
#include "itkAccumulateImageFilter.h"
#include <itkAbsImageFilter.h>

int main (int argc, char **argv)
{
  if ( argc <= 3 )
    {
      std::cout << "Usage: " << argv[0] << " " << " <InputFileName> <OutputFileName> <Dimension>" << std::endl ;
      return 0 ;
    }
  auto accumulateDimension = static_cast<unsigned int>(std::stoi(argv[3]));
  // declare our image type 
  typedef itk::Image < double, 3 > myInputImageType ;
  typedef itk::Image < double, 3 > myOutputImageType ;
  // declare our image reader type
  typedef itk::ImageFileReader < myInputImageType > myFileReaderType ;

  // read the file in
  myFileReaderType::Pointer myFileReader = myFileReaderType::New() ;
  myFileReader->SetFileName ( argv[1] ) ;
  myFileReader->Update() ;


  // do something to the image - print the origin of the image 
  myInputImageType::Pointer myImage = myFileReader->GetOutput() ;

  using AbsImageFilterType = itk::AbsImageFilter<myInputImageType, myOutputImageType>;

  auto absFilter = AbsImageFilterType::New();
  absFilter->SetInput(myImage);

  typedef itk::AccumulateImageFilter< myInputImageType, myOutputImageType > FilterType;
  FilterType::Pointer filter = FilterType::New();
  filter->SetInput( absFilter->GetOutput());
  filter->SetAccumulateDimension( accumulateDimension );
  


  typedef itk::ImageFileWriter < myOutputImageType > myFileWriterType ;
  myFileWriterType::Pointer myFileWriter = myFileWriterType::New() ;
  myFileWriter->SetFileName ( argv[2] ) ;
  myFileWriter->SetInput ( filter->GetOutput()) ;
  myFileWriter->Write();
  
  return 0 ;
}