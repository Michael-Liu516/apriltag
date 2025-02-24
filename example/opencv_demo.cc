/* Copyright (C) 2013-2016, The Regents of The University of Michigan.
All rights reserved.
This software was developed in the APRIL Robotics Lab under the
direction of Edwin Olson, ebolson@umich.edu. This software may be
available under alternative licensing terms; contact the address above.
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
The views and conclusions contained in the software and documentation are those
of the authors and should not be interpreted as representing official policies,
either expressed or implied, of the Regents of The University of Michigan.
*/

#include <iostream>

#include "opencv2/opencv.hpp"
#include <chrono>
#include <cmath>
#include <Eigen/Dense>

extern "C"
{
#include "apriltag.h"
#include "tag36h11.h"
#include "tag25h9.h"
#include "tag16h5.h"
#include "tagCircle21h7.h"
#include "tagStandard41h12.h"
#include "common/getopt.h"
#include "apriltag_pose.h"
}

using namespace std;
using namespace cv;
using std::chrono::high_resolution_clock;
using std::chrono::milliseconds;

template <typename _Tp>
void print_matrix(const _Tp *data, const int rows, const int cols)
{
    for (int y = 0; y < rows; ++y)
    {
        for (int x = 0; x < cols; ++x)
        {
            fprintf(stderr, "  %f  ", static_cast<float>(data[y * cols + x]));
        }
        fprintf(stderr, "\n");
    }
    fprintf(stderr, "\n");
}

void test_inverse_matrix(std::vector<float> input_matrix)
{
    //std::vector<float> vec{ 5, -2, 2, 7, 1, 0, 0, 3, -3, 1, 5, 0, 3, -1, -9, 4 };
    const int N{3};
    if (input_matrix.size() != (int)pow(N, 2))
    {
        fprintf(stderr, "input_matrix must be N^2\n");
        return;
    }

    Eigen::Map<Eigen::MatrixXf> map(input_matrix.data(), 3, 3);
    Eigen::MatrixXf inv = map.inverse();
    fprintf(stderr, "source matrix:\n");
    print_matrix<float>(input_matrix.data(), N, N);
    fprintf(stderr, "eigen inverse matrix:\n");
    print_matrix<float>(inv.data(), N, N);
}

int main(int argc, char *argv[])
{
    getopt_t *getopt = getopt_create();
    getopt_add_bool(getopt, 'h', "help", 0, "Show this help");
    getopt_add_bool(getopt, 'd', "debug", 0, "Enable debugging output (slow)");
    getopt_add_bool(getopt, 'q', "quiet", 0, "Reduce output");
    getopt_add_string(getopt, 'f', "family", "tag36h11", "Tag family to use");
    getopt_add_int(getopt, 't', "threads", "1", "Use this many CPU threads");
    getopt_add_double(getopt, 'x', "decimate", "1.0", "Decimate input image by this factor");
    getopt_add_double(getopt, 'b', "blur", "0.0", "Apply low-pass blur to input");
    getopt_add_bool(getopt, '0', "refine-edges", 1, "Spend more time trying to align edges of tags");
    if (!getopt_parse(getopt, argc, argv, 1) ||
        getopt_get_bool(getopt, "help"))
    {
        printf("Usage: %s [options]\n", argv[0]);
        getopt_do_usage(getopt);
        exit(0);
    }
    // Initialize camera
    VideoCapture cap(0);
    if (!cap.isOpened())
    {
        cerr << "Couldn't open video capture device" << endl;
        return -1;
    }
    // Initialize tag detector with options
    apriltag_family_t *tf = NULL;
    const char *famname = getopt_get_string(getopt, "family");
    if (!strcmp(famname, "tag36h11"))
    {
        tf = tag36h11_create();
    }
    else if (!strcmp(famname, "tag25h9"))
    {
        tf = tag25h9_create();
    }
    else if (!strcmp(famname, "tag16h5"))
    {
        tf = tag16h5_create();
    }
    else if (!strcmp(famname, "tagCircle21h7"))
    {
        tf = tagCircle21h7_create();
    }
    else if (!strcmp(famname, "tagStandard41h12"))
    {
        tf = tagStandard41h12_create();
    }
    else
    {
        printf("Unrecognized tag family name. Use e.g. \"tag36h11\".\n");
        exit(-1);
    }
    apriltag_detector_t *td = apriltag_detector_create();
    apriltag_detector_add_family(td, tf);
    td->quad_decimate = getopt_get_double(getopt, "decimate");
    td->quad_sigma = getopt_get_double(getopt, "blur");
    td->nthreads = getopt_get_int(getopt, "threads");
    td->debug = getopt_get_bool(getopt, "debug");
    td->refine_edges = getopt_get_bool(getopt, "refine-edges");
    Mat frame, gray;
    apriltag_detection_info_t info;
    info.tagsize = 0.135;         // tag_size in meter
    info.fx = 1952.992318829338;  // fx
    info.fy = 1951.357135681735;  // fy
    info.cx = 539.6076735381756;  // cx
    info.cy = 276.4885069533516;  // cy
    while (true)
    {
        high_resolution_clock::time_point beginTime = high_resolution_clock::now();
        cap >> frame;
        cvtColor(frame, gray, COLOR_BGR2GRAY);
        // Make an image_u8_t header for the Mat data
        image_u8_t im = {.width = gray.cols,
                         .height = gray.rows,
                         .stride = gray.cols,
                         .buf = gray.data};
        zarray_t *detections = apriltag_detector_detect(td, &im);
        cout << zarray_size(detections) << " tags detected" << endl;
        // Draw detection outlines
        for (int i = 0; i < zarray_size(detections); i++)
        {
            apriltag_detection_t *det;
            zarray_get(detections, i, &det);
            info.det = det;
            apriltag_pose_t pose;
            double err = estimate_tag_pose(&info, &pose);
            cout << "T is " << pose.t->data[0] << "  " << pose.t->data[1] << "  " << pose.t->data[2] << endl;
            cout << "R is " << pose.R->data[0] << "  " << pose.R->data[1] << "  " << pose.R->data[2] << endl;
            cout << pose.R->data[3] << " " << pose.R->data[4] << "  " << pose.R->data[5] << endl;
            cout << pose.R->data[6] << " " << pose.R->data[7] << "  " << pose.R->data[8] << endl;
            auto theta_x = atan2(pose.R->data[7], pose.R->data[8]) /3.1415926 * 180;
            auto theta_y = atan2(-pose.R->data[6], std::sqrt(std::pow(pose.R->data[7], 2) + std::pow(pose.R->data[8], 2)))/3.1415926 * 180;
            auto theta_z = atan2(pose.R->data[3], pose.R->data[0])/3.1415926 * 180;
            Eigen::MatrixXd matrix(3, 3);
            matrix(0,0) = pose.R->data[0];
            matrix(0,1) = pose.R->data[3];
            matrix(0,2) = pose.R->data[6];
            matrix(1,0) = pose.R->data[1];
            matrix(1,1) = pose.R->data[4];
            matrix(1,2) = pose.R->data[7];
            matrix(2,0) = pose.R->data[2];
            matrix(2,1) = pose.R->data[5];
            matrix(2,2) = pose.R->data[8];

            Eigen::VectorXd vector(3);
            vector[0] = -pose.t->data[0];
            vector[1] = -pose.t->data[1];
            vector[2] = -pose.t->data[2];
            Eigen::VectorXd result = matrix * vector;
            cout << "result is " << result[0] << "  " << result[1] << "  " << result[2] << endl;
            //test_inverse_matrix(input_matrix);

            std::cout << "theta_x is " << theta_x << "\ttheta_y is " << theta_y << "\ttheta_z is " << theta_z << std::endl;
            line(frame, Point(det->p[0][0], det->p[0][1]),
                 Point(det->p[1][0], det->p[1][1]),
                 Scalar(0, 0xff, 0), 2);
            line(frame, Point(det->p[0][0], det->p[0][1]),
                 Point(det->p[3][0], det->p[3][1]),
                 Scalar(0, 0, 0xff), 2);
            line(frame, Point(det->p[1][0], det->p[1][1]),
                 Point(det->p[2][0], det->p[2][1]),
                 Scalar(0xff, 0, 0), 2);
            line(frame, Point(det->p[2][0], det->p[2][1]),
                 Point(det->p[3][0], det->p[3][1]),
                 Scalar(0xff, 0, 0), 2);
            stringstream ss;
            ss << det->id;
            String text = ss.str();
            int fontface = FONT_HERSHEY_SCRIPT_SIMPLEX;
            double fontscale = 1.0;
            int baseline;
            Size textsize = getTextSize(text, fontface, fontscale, 2,
                                        &baseline);
            putText(frame, text, Point(det->c[0] - textsize.width / 2, det->c[1] + textsize.height / 2),
                    fontface, fontscale, Scalar(0xff, 0x99, 0), 2);
        }
        zarray_destroy(detections);
        imshow("Tag Detections", frame);
        high_resolution_clock::time_point endTime = high_resolution_clock::now();
        milliseconds timeInterval = std::chrono::duration_cast<milliseconds>(endTime - beginTime);
        std::cout << "Time spent: " << timeInterval.count() << "ms" << std::endl;
        if (waitKey(30) == 'q')
        {
            //printf("save!");
            //imwrite("test.jpg",frame);
            break;
        }
    }
    apriltag_detector_destroy(td);
    if (!strcmp(famname, "tag36h11"))
    {
        tag36h11_destroy(tf);
    }
    else if (!strcmp(famname, "tag25h9"))
    {
        tag25h9_destroy(tf);
    }
    else if (!strcmp(famname, "tag16h5"))
    {
        tag16h5_destroy(tf);
    }
    else if (!strcmp(famname, "tagCircle21h7"))
    {
        tagCircle21h7_destroy(tf);
    }
    else if (!strcmp(famname, "tagStandard41h12"))
    {
        tagStandard41h12_destroy(tf);
    }
    getopt_destroy(getopt);
    return 0;
}