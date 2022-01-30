#include "kalman.hpp"

#include "buff_detector.hpp"
#include "gtest/gtest.h"
#include "opencv2/opencv.hpp"
#include "spdlog/spdlog.h"

namespace {

const cv::Scalar kBLUE(255., 0., 0.);
const cv::Scalar kGREEN(0., 255., 0.);
const cv::Scalar kRED(0., 0., 255.);

const std::vector<cv::Point2d> points = {
    cv::Point2d(50., 50.),  cv::Point2d(100., 50.),  cv::Point2d(150., 50.),
    cv::Point2d(200., 50.), cv::Point2d(250., 50.),  cv::Point2d(300., 50.),
    cv::Point2d(350., 50.), cv::Point2d(400., 50.),  cv::Point2d(450., 50.),
    cv::Point2d(500., 50.), cv::Point2d(550., 50.),  cv::Point2d(600., 50.),
    cv::Point2d(650., 50.), cv::Point2d(700., 50.),  cv::Point2d(750., 50.),
    cv::Point2d(800., 50.), cv::Point2d(850., 50.),  cv::Point2d(900., 50.),
    cv::Point2d(950., 50.), cv::Point2d(1000., 50.), cv::Point2d(1050., 50.)};

const std::string kVIDEO = "../../../../redbuff01.avi";
const std::string kPARAM = "../../../runtime/RMUT2021_Buff.json";

}  // namespace

TEST(TestVision, TestKalman) {
  RMlogger::SetLogger(spdlog::level::debug);
  Kalman filter(4, 2);
  cv::Mat predict_mat;
  cv::Mat img(300, 1200, CV_8UC3, cv::Scalar(0, 0, 0));

  for (auto& pt : points) {
    cv::Mat point_mat(pt);
    predict_mat = filter.Predict(point_mat, img);
    cv::Point2d predict_pt(predict_mat.at<double>(0, 0),
                           predict_mat.at<double>(0, 1));

    cv::circle(img, pt, 3, kBLUE, 2);
    cv::circle(img, predict_pt, 3, kGREEN, 2);

    cv::imshow("img", img);
    // cv::waitKey(0);
  }
  cv::destroyWindow("img");
}

TEST(TestVision, TestKalmanBuffPredictor) {
  RMlogger::SetLogger(spdlog::level::debug);
  Kalman filter(4, 2);
  cv::VideoCapture cap(kVIDEO);
  cv::Mat frame;
  BuffDetector detecter(kPARAM, game::Team::kBLUE);
  if (!cap.isOpened()) SPDLOG_WARN("{}", kVIDEO);

  while (true) {
    cap >> frame;
    auto buffs = detecter.Detect(frame);
    cv::Point2d target_center = buffs.back().GetTarget().ImageCenter();
    cv::Point2d pt = filter.Predict(target_center, frame);
    cv::circle(frame, target_center, 3, kGREEN, -1);
    cv::circle(frame, pt, 3, kBLUE, -1);
    cv::imshow("WINDOW", frame);
    int key = cv::waitKey(10);
    switch (key) {
      case 'q':
        return;
        break;
      case ' ':
        cv::waitKey(0);
      default:
        continue;
        break;
    }
  }
  cv::destroyAllWindows();
  cap.release();
}