/* Copyright (c) 2012-2013 Stanislaw Halik <sthalik@misaki.pl>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

/*
 * this file appeared originally in facetracknoir, was rewritten completely
 * following opentrack fork.
 */

#include "tracker.h"
#include "facetracknoir.h"
#include <opencv2/core/core.hpp>
#include <cmath>

#if defined(_WIN32)
#   include <windows.h>
#endif

Tracker::Tracker( FaceTrackNoIR *parent ) :
    should_quit(false),
    do_center(false),
    enabled(true),
    compensate(true)
{
    mainApp = parent;
}

Tracker::~Tracker()
{
}

static void get_curve(double pos, double& out, THeadPoseDOF& axis) {
    bool altp = (pos < 0) && axis.altp;
    if (altp) {
        out = axis.invert * axis.curveAlt.getValue(pos);
        axis.curve.setTrackingActive( false );
        axis.curveAlt.setTrackingActive( true );
    }
    else {
        out = axis.invert * axis.curve.getValue(pos);
        axis.curve.setTrackingActive( true );
        axis.curveAlt.setTrackingActive( false );
    }
    out += axis.zero;
}

/** QThread run method @override **/
void Tracker::run() {
    T6DOF offset_camera, gameoutput_camera, target_camera;

    bool bTracker1Confid = false;
    bool bTracker2Confid = false;

    double newpose[6] = {0};
    double last_post_filter[6] ;

#if defined(_WIN32)
    (void) timeBeginPeriod(1);
#endif

    for (;;)
    {
        if (should_quit)
            break;

        if (Libraries->pSecondTracker) {
            bTracker2Confid = Libraries->pSecondTracker->GiveHeadPoseData(newpose);
        }

        if (Libraries->pTracker) {
            bTracker1Confid = Libraries->pTracker->GiveHeadPoseData(newpose);
        }

        {
            QMutexLocker foo(&mtx);
            const bool confid = bTracker1Confid || bTracker2Confid;

            if ( confid ) {
                for (int i = 0; i < 6; i++)
                    mainApp->axis(i).headPos = newpose[i];
            }

            if (do_center)  {
                for (int i = 0; i < 6; i++)
                    offset_camera.axes[i] = target_camera.axes[i];

                do_center = false;

                if (Libraries->pFilter)
                    Libraries->pFilter->Initialize();
            }

            T6DOF target_camera2, new_camera;

            if (enabled && confid)
            {
                for (int i = 0; i < 6; i++)
                    target_camera.axes[i] = mainApp->axis(i).headPos;

                target_camera2 = target_camera - offset_camera;
            }

            if (Libraries->pFilter) {
                for (int i = 0; i < 6; i++)
                    last_post_filter[i] = gameoutput_camera.axes[i];
                Libraries->pFilter->FilterHeadPoseData(target_camera2.axes, new_camera.axes, last_post_filter);
            } else {
                new_camera = target_camera2;
            }

            for (int i = 0; i < 6; i++) {
                get_curve(new_camera.axes[i], output_camera.axes[i], mainApp->axis(i));
            }

            if (compensate)
            {
                const auto H = output_camera.axes[Yaw] * M_PI / 180;
                const auto P = output_camera.axes[Pitch] * M_PI / 180;
                const auto B = output_camera.axes[Roll] * M_PI / 180;

                const auto cosH = cos(H);
                const auto sinH = sin(H);
                const auto cosP = cos(P);
                const auto sinP = sin(P);
                const auto cosB = cos(B);
                const auto sinB = sin(B);

                double foo[] = {
                    cosH * cosB - sinH * sinP * sinB,
                    - sinB * cosP,
                    sinH * cosB + cosH * sinP * sinB,
                    cosH * sinB + sinH * sinP * cosB,
                    cosB * cosP,
                    sinB * sinH - cosH * sinP * cosB,
                    - sinH * cosP,
                    - sinP,
                    cosH * cosP,
                };

                cv::Mat rmat(3, 3, CV_64F, foo);
                cv::Mat tvec(3, 1, CV_64F, output_camera.axes);
                cv::Mat ret = rmat * tvec;

                for (int i = 0; i < 3; i++)
                    output_camera.axes[i] = ret.at<double>(i);
            }

            if (Libraries->pProtocol) {
                gameoutput_camera = output_camera;
                Libraries->pProtocol->sendHeadposeToGame( gameoutput_camera.axes );	// degrees & centimeters
            }
        }

        msleep(15);
    }
#if defined(_WIN32)
    (void) timeEndPeriod(1);
#endif

    for (int i = 0; i < 6; i++)
    {
        mainApp->axis(i).curve.setTrackingActive(false);
        mainApp->axis(i).curveAlt.setTrackingActive(false);
    }
}

void Tracker::getHeadPose( double *data ) {
    QMutexLocker foo(&mtx);
    for (int i = 0; i < 6; i++)
    {
        data[i] = mainApp->axis(i).headPos;
    }
}

void Tracker::getOutputHeadPose( double *data ) {
    QMutexLocker foo(&mtx);
    for (int i = 0; i < 6; i++)
        data[i] = output_camera.axes[i];
}

void Tracker::setInvertAxis(Axis axis, bool invert) { mainApp->axis(axis).invert = invert? -1.0 : 1.0; }
