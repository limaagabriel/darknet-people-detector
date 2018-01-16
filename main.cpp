// Brief Sample of using OpenCV dnn module in real time with device capture, video and image.
// VIDEO DEMO: https://www.youtube.com/watch?v=NHtRlndE2cg

#include <fstream>
#include <iostream>
#include <unistd.h>
#include <pthread.h>
#include "include/firmata.h"
#include "include/firmserial.h"
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/dnn/shape_utils.hpp>

#define PERSON_CLASS 14
#define SLEEP_TIME 5
#define DEBUG 1

using namespace std;
using namespace cv;
using namespace cv::dnn;

static const char* about =
        "This sample uses You only look once (YOLO)-Detector (https://arxiv.org/abs/1612.08242) to detect objects on camera/video/image.\n"
                "Models can be downloaded here: https://pjreddie.com/darknet/yolo/\n"
                "Default network is 416x416.\n"
                "Class names can be downloaded here: https://github.com/pjreddie/darknet/tree/master/data\n";

static const char* params =
        "{ help           | false | print usage         }"
                "{ cfg            |       | model configuration }"
                "{ model          |       | model weights       }"
                "{ camera_device  | 0     | camera device number}"
                "{ source         |       | video or image for detection}"
                "{ style          | box   | box or line style draw }"
                "{ min_confidence | 0.6   | min confidence      }"
                "{ class_names    |       | File with class names, [PATH-TO-DARKNET]/data/coco.names }";

bool hardware_flag = true;
vector<firmata::PortInfo> ports = firmata::FirmSerial::listPorts();
firmata::Firmata<firmata::Base, firmata::I2C>* f = NULL;
firmata::FirmSerial* serialio;

void *hardware_worker(void *data);

int main(int argc, char** argv)
{
    pthread_t t1;
    CommandLineParser parser(argc, argv, params);

    for (auto port : ports) {
        if (f != NULL) {
            delete f;
            f = NULL;
        }

        if(port.port.find("ACM") == std::string::npos) continue;
        std::cout << port.port << std::endl;

        try {
            serialio = new firmata::FirmSerial(port.port.c_str());

            if (serialio->available()) {
                sleep(3); // Seems necessary on linux
                f = new firmata::Firmata<firmata::Base, firmata::I2C>(serialio);
                sleep(1);
            }
        }
        catch(firmata::IOException e) {
            std::cout << e.what() << std::endl;
        }
        catch(firmata::NotOpenException e) {
            std::cout << e.what() << std::endl;
        }
        if (f != NULL && f->ready()) {
            break;
        }
    }

    if (f == NULL || !f->ready()) {
        cout << "Erro. Primeiramente, conecte o dispositivo numa porta USB e tente novamente." << endl;
        return 1;
    }

    if (parser.get<bool>("help"))
    {
        cout << about << endl;
        parser.printMessage();
        return 0;
    }

    String modelConfiguration = parser.get<String>("cfg");
    String modelBinary = parser.get<String>("model");

    //! [Initialize network]
    dnn::Net net = readNetFromDarknet(modelConfiguration, modelBinary);
    //! [Initialize network]

    if (net.empty())
    {
        cerr << "Can't load network by using the following files: " << endl;
        cerr << "cfg-file:     " << modelConfiguration << endl;
        cerr << "weights-file: " << modelBinary << endl;
        cerr << "Models can be downloaded here:" << endl;
        cerr << "https://pjreddie.com/darknet/yolo/" << endl;
        exit(-1);
    }

    VideoCapture cap;
    if (parser.get<String>("source").empty())
    {
        int cameraDevice = parser.get<int>("camera_device");
        cap = VideoCapture(cameraDevice);

        cap.set(CV_CAP_PROP_FRAME_WIDTH , 320);
        cap.set(CV_CAP_PROP_FRAME_HEIGHT , 240);

        if(!cap.isOpened())
        {
            cout << "Couldn't find camera: " << cameraDevice << endl;
            return -1;
        }
    }
    else
    {
        cap.open(parser.get<String>("source"));
        if(!cap.isOpened())
        {
            cout << "Couldn't open image or video: " << parser.get<String>("video") << endl;
            return -1;
        }
    }

    vector<String> classNamesVec;
    ifstream classNamesFile(parser.get<String>("class_names").c_str());
    if (classNamesFile.is_open())
    {
        string className = "";
        while (std::getline(classNamesFile, className))
            classNamesVec.push_back(className);
    }

    String object_roi_style = parser.get<String>("style");

    for(;;) {

        Mat frame;
        cap >> frame; // get a new frame from camera/video or read image

        if (frame.empty()) {
            waitKey();
            break;
        }

        if (frame.channels() == 4)
            cvtColor(frame, frame, COLOR_BGRA2BGR);

        //! [Prepare blob]
        Mat inputBlob = blobFromImage(frame, 1 / 255.F, Size(320, 240), Scalar(), true,
                                      false); //Convert Mat to batch of images
        //! [Prepare blob]

        //! [Set input blob]
        net.setInput(inputBlob, "data");                   //set the network input
        //! [Set input blob]

        //! [Make forward pass]
        Mat detectionMat = net.forward("detection_out");   //compute output
        //! [Make forward pass]

        vector<double> layersTimings;
        double tick_freq = getTickFrequency();
        double time_ms = net.getPerfProfile(layersTimings) / tick_freq * 1000;
        putText(frame, format("FPS: %.2f ; Tempo: %.2f ms", 1000.f / time_ms, time_ms),
                Point(20, 20), 0, 0.5, Scalar(0, 0, 255));

        putText(frame, "Pressione ESC para sair", Point(20, 200), 0, 0.5, Scalar(0, 0, 255));
        if(!hardware_flag)
            putText(frame, "Dispositivo ocupado!", Point(20,220), 0, 0.5, Scalar(0, 0, 255));
        else
            putText(frame, "Pronto para realizar o procedimento!", Point(20,220), 0, 0.5, Scalar(0, 0, 255));


        float confidenceThreshold = parser.get<float>("min_confidence");
        for (int i = 0; i < detectionMat.rows; i++) {
            const int probability_index = 5;
            const int probability_size = detectionMat.cols - probability_index;
            float *prob_array_ptr = &detectionMat.at<float>(i, probability_index);

            size_t objectClass = max_element(prob_array_ptr, prob_array_ptr + probability_size) - prob_array_ptr;
            float confidence = detectionMat.at<float>(i, (int) objectClass + probability_index);

            if (objectClass == PERSON_CLASS && confidence > confidenceThreshold) {
                float x_center = detectionMat.at<float>(i, 0) * frame.cols;
                float y_center = detectionMat.at<float>(i, 1) * frame.rows;
                float width = detectionMat.at<float>(i, 2) * frame.cols;
                float height = detectionMat.at<float>(i, 3) * frame.rows;
                Point p1(cvRound(x_center - width / 2), cvRound(y_center - height / 2));
                Point p2(cvRound(x_center + width / 2), cvRound(y_center + height / 2));
                Rect object(p1, p2);
#if DEBUG
                cout << "Width: " << width << "\t" << "Height: " << height << endl;
#endif

                Scalar object_roi_color(0, 255, 0);

                if (object_roi_style == "box") {
                    rectangle(frame, object, object_roi_color);
                } else {
                    Point p_center(cvRound(x_center), cvRound(y_center));
                    line(frame, object.tl(), p_center, object_roi_color, 1);
                }

                //String className = objectClass < classNamesVec.size() ? classNamesVec[objectClass] : cv::format("unknown(%d)", objectClass);
                String className = "Person";
                String label = format("%s: %.2f", className.c_str(), confidence);


                int baseLine = 0;
                Size labelSize = getTextSize(label, FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);
                rectangle(frame, Rect(p1, Size(labelSize.width, labelSize.height + baseLine)),
                          object_roi_color, CV_FILLED);
                putText(frame, label, p1 + Point(0, labelSize.height),
                        FONT_HERSHEY_SIMPLEX, 0.5, Scalar(0, 0, 0));

                if (hardware_flag) {
                    hardware_flag = false;
                    pthread_create(&t1, NULL, hardware_worker, NULL);
                }
            }
        }

        imshow("L2: People detection", frame);
        if (waitKey(1) == 27) break;
        if (f == NULL || !f->ready()) break;
    }

    if(f != NULL)
    {
        delete f;
        f = NULL;
    }

    cap.release();
    destroyAllWindows();
    if(!hardware_flag) pthread_join(t1, NULL);

    return 0;
} // main

void *hardware_worker(void *data)
{
    try {
        f->setSamplingInterval(100);

        //std::cout << f->name << std::endl;
        //std::cout << f->major_version << std::endl;
        //std::cout << f->minor_version << std::endl;

        f->pinMode(13, MODE_OUTPUT);

        for(int i = 0; i < 6; i++) {
            f->parse();
            int a0 = f->digitalRead(13);
            f->digitalWrite(13, a0? LOW : HIGH);
            sleep(1);
        };
    }
    catch (firmata::IOException e) {
        std::cout << e.what() << std::endl;
        delete f;
        f = NULL;
    }
    catch (firmata::NotOpenException e) {
        std::cout << e.what() << std::endl;
        delete f;
        f = NULL;
    }

    sleep(SLEEP_TIME);
    hardware_flag = true;
}