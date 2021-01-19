// This file is part of the AliceVision project.
// Copyright (c) 2017 AliceVision contributors.
// This Source Code Form is subject to the terms of the Mozilla Public License,
// v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include <aliceVision/sfmData/SfMData.hpp>
#include <aliceVision/sfmDataIO/sfmDataIO.hpp>

#include <aliceVision/image/all.hpp>
#include <aliceVision/system/cmdline.hpp>
#include <aliceVision/system/main.hpp>
#include <aliceVision/config.hpp>
#include <aliceVision/utils/regexFilter.hpp>

#include <dependencies/vectorGraphics/svgDrawer.hpp>

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>

#include <opencv2/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/mcc.hpp>

#include <string>
#include <fstream>
#include <vector>
#include <unordered_map>

// These constants define the current software version.
// They must be updated when the command line is changed.
#define ALICEVISION_SOFTWARE_VERSION_MAJOR 1
#define ALICEVISION_SOFTWARE_VERSION_MINOR 0

using namespace aliceVision;

namespace fs = boost::filesystem;
namespace po = boost::program_options;

namespace ccheckerSVG {

    const std::vector< cv::Point2f > MACBETH_CCHART_CORNERS_POS = {
        {0.00, 0.00}, {16.75, 0.00}, {16.75, 11.25}, {0.00, 11.25},
    };

    const std::vector< cv::Point2f > MACBETH_CCHART_CELLS_POS_CENTER =  {
        {1.50f, 1.50f}, {4.25f, 1.50f}, {7.00f, 1.50f}, {9.75f, 1.50f}, {12.50f, 1.50f}, {15.25f, 1.50f},
        {1.50f, 4.25f}, {4.25f, 4.25f}, {7.00f, 4.25f}, {9.75f, 4.25f}, {12.50f, 4.25f}, {15.25f, 4.25f},
        {1.50f, 7.00f}, {4.25f, 7.00f}, {7.00f, 7.00f}, {9.75f, 7.00f}, {12.50f, 7.00f}, {15.25f, 7.00f},
        {1.50f, 9.75f}, {4.25f, 9.75f}, {7.00f, 9.75f}, {9.75f, 9.75f}, {12.50f, 9.75f}, {15.25f, 9.75f}
    };

    const float MACBETH_CCHART_CELLS_SIZE = 2.50f * .5f;

    struct Quad
    {
        std::vector<float> xCoords;
        std::vector<float> yCoords;

        Quad() = default;

        Quad(const std::vector<cv::Point2f>& points)
        {
            if (points.size() != 4)
            {
                ALICEVISION_LOG_ERROR("Invalid color checker box: size is not equal to 4");
                exit(EXIT_FAILURE);
            }
            for(const auto& p : points)
            {
                xCoords.push_back(p.x);
                yCoords.push_back(p.y);
            }
            // close polyline
            xCoords.push_back(points[0].x);
            yCoords.push_back(points[0].y);
        }

        void transform(cv::Matx33f transformMatrix)
        {
            for (int i = 0; i < 5; ++i)
            {
                cv::Point3f p(xCoords[i], yCoords[i], 1.);
                p = transformMatrix * p;
                xCoords[i] = p.x / p.z;
                yCoords[i] = p.y / p.z;
            }
        }
    };

    void draw(const cv::Ptr<cv::mcc::CChecker> &checker, std::string outputPath)
    {
        std::vector< Quad > quadsToDraw;

        // Push back the quad representing the color checker
        quadsToDraw.push_back( Quad(checker->getBox()) );

        // Transform matrix from 'theoric' to 'measured'
        cv::Matx33f tMatrix = cv::getPerspectiveTransform(MACBETH_CCHART_CORNERS_POS,checker->getBox());

        // Push back quads representing color checker cells
        for (const auto& center : MACBETH_CCHART_CELLS_POS_CENTER)
        {
            Quad quad({
                cv::Point2f( center.x - MACBETH_CCHART_CELLS_SIZE * .5, center.y - MACBETH_CCHART_CELLS_SIZE * .5 ),
                cv::Point2f( center.x + MACBETH_CCHART_CELLS_SIZE * .5, center.y - MACBETH_CCHART_CELLS_SIZE * .5 ),
                cv::Point2f( center.x + MACBETH_CCHART_CELLS_SIZE * .5, center.y + MACBETH_CCHART_CELLS_SIZE * .5 ),
                cv::Point2f( center.x - MACBETH_CCHART_CELLS_SIZE * .5, center.y + MACBETH_CCHART_CELLS_SIZE * .5 ),
            });
            quad.transform(tMatrix);
            quadsToDraw.push_back(quad);
        }

        svg::svgDrawer svgSurface;
        for (const auto& quad : quadsToDraw)
        {
            svgSurface.drawPolyline(
                quad.xCoords.begin(), quad.xCoords.end(),
                quad.yCoords.begin(), quad.yCoords.end(),
                svg::svgStyle().stroke("red", 2));
        }

        std::ofstream svgFile(outputPath.c_str());
        svgFile << svgSurface.closeSvgFile().str();
        svgFile.close();
    }
} // namespace ccheckerSVG

void serializeColorMatrixToTextFile(const std::string &outputColorData, cv::Mat &colorData)
{
    std::ofstream f;
    f.open(outputColorData);
    for(int row = 0; row < colorData.rows; row++)
    {
        cv::Vec3d* rowPtr = colorData.ptr<cv::Vec3d>(row); // pointer which points to the first place of each row
        for(int col = 0; col < colorData.cols; col++)
        {
            const cv::Vec3d& matPixel = rowPtr[col];
            for(unsigned int i = 0; i < 3; ++i)
            {
                f << std::setprecision(std::numeric_limits<double>::digits10 + 2) << matPixel[i] << std::endl;
            }
        }
    }
    f.close();
}


// TODO refactor with imageProcessing
cv::Mat imageRGBAToCvMatBGRi(const image::Image<image::RGBAfColor>& img)
{
    cv::Mat mat(img.Height(), img.Width(), CV_8UC3);
    for(int row = 0; row < img.Height(); row++)
    {
        for(int col = 0; col < img.Width(); col++)
        {
            mat.at<cv::Vec3b>(row, col)[0] = (uint8_t) (img(row, col).b() * 256);
            mat.at<cv::Vec3b>(row, col)[1] = (uint8_t) (img(row, col).g() * 256);
            mat.at<cv::Vec3b>(row, col)[2] = (uint8_t) (img(row, col).r() * 256);
        }
    }
    return mat;
}



// TODO refactor with imageProcessing
cv::Mat imageRGBAToCvMatBGRf(const image::Image<image::RGBAfColor>& img)
{
    cv::Mat mat(img.Height(), img.Width(), CV_32FC3);
    for(int row = 0; row < img.Height(); row++)
    {
        cv::Vec3f* rowPtr = mat.ptr<cv::Vec3f>(row);
        for(int col = 0; col < img.Width(); col++)
        {
            cv::Vec3f& matPixel = rowPtr[col];
            const image::RGBAfColor& imgPixel = img(row, col);
            matPixel = cv::Vec3f(imgPixel.b(), imgPixel.g(), imgPixel.r());
        }
    }
    return mat;
}


// TODO refactor with imageProcessing
void cvMatBGRToImageRGBA(const cv::Mat& matIn, image::Image<image::RGBAfColor>& imageOut)
{
    for(int row = 0; row < imageOut.Height(); row++)
    {
        const cv::Vec3f* rowPtr = matIn.ptr<cv::Vec3f>(row);
        for(int col = 0; col < imageOut.Width(); col++)
        {
            const cv::Vec3f& matPixel = rowPtr[col];
            imageOut(row, col) = image::RGBAfColor(matPixel[2], matPixel[1], matPixel[0], imageOut(row, col).a());
        }
    }
}


void detectColorChecker(
    const fs::path &imgPath,
    const image::ImageReadOptions &imgReadOptions,
    const std::string &outputColorData,
    const bool debug)
{
    const int nc = 1; // Number of charts in an image
    const std::string outputFolder = fs::path(outputColorData).parent_path().string();
    const std::string imgSrcPath = imgPath.string();
    const std::string imgSrcStem = imgPath.stem().string();
    const std::string imgDestStem = imgSrcStem;
    const std::string imgDestPath = outputFolder + "/" + imgDestStem + ".jpg";

    // Load image
    image::Image<image::RGBAfColor> image;
    image::readImage(imgSrcPath, image, imgReadOptions);
    cv::Mat imageBGR = imageRGBAToCvMatBGRi(image);

    if(imageBGR.cols == 0 || imageBGR.rows == 0)
    {
        ALICEVISION_LOG_ERROR("Image at: '" << imgSrcPath << "'.\n" << "is empty.");
        exit(EXIT_FAILURE);
    }

    cv::Ptr<cv::mcc::CCheckerDetector> detector = cv::mcc::CCheckerDetector::create();

    if(!detector->process(imageBGR, cv::mcc::TYPECHART::MCC24, nc))
    {
        ALICEVISION_LOG_INFO("Checker not detected in image at: '" << imgSrcPath << "'");
        return;
    }

    ALICEVISION_LOG_INFO("Checker successfully detected in '" << imgSrcStem << "'");

    for(const cv::Ptr<cv::mcc::CChecker> checker : detector->getListColorChecker())
    {
        if(debug)
        {
            // Output debug data
            ccheckerSVG::draw(checker, outputFolder + "/" + imgDestStem + ".svg");

            cv::Ptr<cv::mcc::CCheckerDraw> cdraw = cv::mcc::CCheckerDraw::create(checker, CV_RGB(250, 0, 0), 3);
            cdraw->draw(imageBGR);

            cv::imwrite(imgDestPath, imageBGR);
        }

        // Get colors data
        cv::Mat chartsRGB = checker->getChartsRGB();

        // Extract average colors
        cv::Mat colorData = chartsRGB.col(1).clone().reshape(3, chartsRGB.rows / 3);
        colorData /= 255.0; // conversion to float

        serializeColorMatrixToTextFile(outputColorData, colorData);
    }
}


int aliceVision_main(int argc, char** argv)
{
    // command-line parameters
    std::string verboseLevel = system::EVerboseLevel_enumToString(system::Logger::getDefaultVerboseLevel());
    std::string inputExpression;
    std::string outputColorData;

    // user optional parameters
    bool debug = false;

    po::options_description allParams(
        "This program is used to perform color checker detection\n"
        "AliceVision colorCheckerDetection");

    po::options_description inputParams("Required parameters");
    inputParams.add_options()
        ("input,i", po::value<std::string>(&inputExpression)->required(),
         "SfMData file input, image filenames or regex(es) on the image file path (supported regex: '#' matches a single digit, '@' one or more digits, '?' one character and '*' zero or more).")
        ("outputColorData", po::value<std::string>(&outputColorData)->required(),
         "Output path for the color data file.");

    po::options_description optionalParams("Optional parameters");
    optionalParams.add_options()
        ("debug", po::value<bool>(&debug),
         "Output debug data.");

    po::options_description logParams("Log parameters");
    logParams.add_options()
        ("verboseLevel,v", po::value<std::string>(&verboseLevel)->default_value(verboseLevel),
         "verbosity level (fatal, error, warning, info, debug, trace).");

    allParams.add(inputParams).add(optionalParams).add(logParams);

    po::variables_map vm;
    try
    {
        po::store(po::parse_command_line(argc, argv, allParams), vm);

        if(vm.count("help") || (argc == 1))
        {
            ALICEVISION_COUT(allParams);
            return EXIT_SUCCESS;
        }
        po::notify(vm);
    }
    catch(boost::program_options::required_option& e)
    {
        ALICEVISION_CERR("ERROR: " << e.what());
        ALICEVISION_COUT("Usage:\n\n" << allParams);
        return EXIT_FAILURE;
    }
    catch(boost::program_options::error& e)
    {
        ALICEVISION_CERR("ERROR: " << e.what());
        ALICEVISION_COUT("Usage:\n\n" << allParams);
        return EXIT_FAILURE;
    }

    ALICEVISION_COUT("Program called with the following parameters:");
    ALICEVISION_COUT(vm);

    // set verbose level
    system::Logger::get()->setLogLevel(verboseLevel);

    // Check if inputExpression is recognized as sfm data file
    const std::string inputExt = boost::to_lower_copy(fs::path(inputExpression).extension().string());
    static const std::array<std::string, 2> sfmSupportedExtensions = {".sfm", ".abc"};
    if(std::find(sfmSupportedExtensions.begin(), sfmSupportedExtensions.end(), inputExt) != sfmSupportedExtensions.end())
    {
        // load input as sfm data file
        sfmData::SfMData sfmData;
        if (!sfmDataIO::Load(sfmData, inputExpression, sfmDataIO::ESfMData(sfmDataIO::VIEWS)))
        {
            ALICEVISION_LOG_ERROR("The input SfMData file '" << inputExpression << "' cannot be read.");
            return EXIT_FAILURE;
        }

        int counter = 0;

        // Detect color checker for each images
        for(const auto& viewIt : sfmData.getViews())
        {
            const sfmData::View& view = *(viewIt.second);

            ALICEVISION_LOG_INFO(++counter << "/" << sfmData.getViews().size() << " - Process image at: '" << view.getImagePath() << "'.");

            image::ImageReadOptions options;
            options.outputColorSpace = image::EImageColorSpace::NO_CONVERSION;
            options.applyWhiteBalance = view.getApplyWhiteBalance();
            detectColorChecker(view.getImagePath(), options, outputColorData, debug);
        }

    }
    else
    {
        // load input as image file or image folder

    }

    return EXIT_SUCCESS;
}
