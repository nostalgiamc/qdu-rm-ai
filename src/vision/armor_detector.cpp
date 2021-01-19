#include "armor_detector.hpp"

#include <ostream>

#include "opencv2/opencv.hpp"
#include "spdlog/spdlog.h"

namespace {

const auto kCV_FONT = cv::FONT_HERSHEY_SIMPLEX;
const auto kGREEN = cv::Scalar(0., 255., 0.);

}  // namespace

void ArmorDetector::InitDefaultParams(std::string params_path) {
  fs_.open(params_path, cv::FileStorage::WRITE | cv::FileStorage::FORMAT_JSON);

  fs_ << "binary_th" << 230;
  fs_ << "erosion_size" << 5;

  fs_ << "contour_size_th" << 20;
  fs_ << "contour_area_low_th" << 100;
  fs_ << "contour_area_high_th" << 10000;
  fs_ << "bar_area_low_th" << 100;
  fs_ << "bar_area_high_th" << 10000;
  fs_ << "angle_high_th" << 60;
  fs_ << "aspect_ratio_low_th" << 2;
  fs_ << "aspect_ratio_high_th" << 5;

  fs_ << "angle_diff_th" << 0.5;
  fs_ << "length_diff_th" << 0.5;
  fs_ << "height_diff_th" << 0.5;
  fs_ << "area_diff_th" << 0.5;
  fs_ << "center_dist_low_th" << 1.5;
  fs_ << "center_dist_high_th" << 5;
  fs_.release();

  SPDLOG_DEBUG("Inited params.");
}

void ArmorDetector::PrepareParams() {
  params_.binary_th = fs_["binary_th"];
  params_.erosion_size = fs_["erosion_size"];

  params_.contour_size_th = int(fs_["contour_size_th"]);
  params_.contour_area_low_th = fs_["contour_area_low_th"];
  params_.contour_area_high_th = fs_["contour_area_high_th"];
  params_.bar_area_low_th = fs_["bar_area_low_th"];
  params_.bar_area_high_th = fs_["bar_area_high_th"];
  params_.angle_high_th = fs_["angle_high_th"];
  params_.aspect_ratio_low_th = fs_["aspect_ratio_low_th"];
  params_.aspect_ratio_high_th = fs_["aspect_ratio_high_th"];

  params_.angle_diff_th = fs_["angle_diff_th"];
  params_.length_diff_th = fs_["length_diff_th"];
  params_.height_diff_th = fs_["height_diff_th"];
  params_.area_diff_th = fs_["area_diff_th"];
  params_.center_dist_low_th = fs_["center_dist_low_th"];
  params_.center_dist_high_th = fs_["center_dist_high_th"];
}

void ArmorDetector::FindLightBars(const cv::Mat &frame) {
  const auto start = std::chrono::high_resolution_clock::now();
  lightbars_.clear();
  armors_.clear();

  frame_size_ = cv::Size(frame.cols, frame.rows);

  cv::Mat channels[3];
  cv::split(frame, channels);

  cv::Mat result = channels[0];
  // if (enemy_team_ == game::Team::kBLUE) {
  //   result = channels[0] - channels[2];
  // } else if (enemy_team_ == game::Team::kRED) {
  //   result = channels[2] - channels[0];
  // }

  // TODO: sharpen blur add contrast.

  cv::threshold(result, result, params_.binary_th, 255., cv::THRESH_BINARY);

  const int erosion_size = params_.erosion_size;
  cv::Mat kernel = cv::getStructuringElement(
      cv::MORPH_RECT, cv::Size(2 * erosion_size + 1, 2 * erosion_size + 1));
  cv::morphologyEx(result, result, cv::MORPH_OPEN, kernel);

  std::vector<std::vector<cv::Point> > contours;
  cv::findContours(result, contours, cv::RETR_TREE, cv::CHAIN_APPROX_SIMPLE);
  SPDLOG_DEBUG("Found contours: {}", contours.size());

  for (const auto &contour : contours) {
    if (contour.size() < params_.contour_size_th) continue;

    const double c_area = cv::contourArea(contour);
    if (c_area < params_.contour_area_low_th) continue;
    if (c_area > params_.contour_area_high_th) continue;

    auto potential_bar = LightBar(cv::minAreaRect(contour));

    if (std::abs(potential_bar.Angle()) > double(params_.angle_high_th))
      continue;

    const double b_area = potential_bar.Area();
    if (b_area < params_.bar_area_low_th) continue;
    if (b_area > params_.bar_area_high_th) continue;

    const double ar = potential_bar.AspectRatio();
    if (ar < params_.aspect_ratio_low_th) continue;
    if (ar > params_.aspect_ratio_high_th) continue;

    lightbars_.emplace_back(potential_bar);
  }

  SPDLOG_DEBUG("Found light bars: {}", lightbars_.size());

  std::sort(lightbars_.begin(), lightbars_.end(),
            [](LightBar &bar1, LightBar &bar2) {
              return bar1.Center().x < bar2.Center().x;
            });
  const auto stop = std::chrono::high_resolution_clock::now();
  duration_bars_ =
      std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);

  SPDLOG_DEBUG("duration_bars_: {} ms", duration_bars_.count());
}

void ArmorDetector::MatchLightBars() {
  const auto start = std::chrono::high_resolution_clock::now();
  for (auto iti = lightbars_.begin(); iti != lightbars_.end(); ++iti) {
    for (auto itj = iti + 1; itj != lightbars_.end(); ++itj) {
      const double angle_diff = std::abs(iti->Angle() - itj->Angle());
      if (angle_diff > params_.angle_diff_th) continue;

      const double length_diff =
          std::abs(iti->Length() - itj->Length()) / iti->Length();
      if (length_diff > params_.length_diff_th) continue;

      const double height_diff = std::abs(iti->Center().y - itj->Center().y);
      if (height_diff > (params_.height_diff_th * frame_size_.height)) continue;

      const double area_diff =
          std::abs(iti->Area() - itj->Area()) / iti->Area();
      if (area_diff > params_.area_diff_th) continue;

      const double center_dist = cv::norm(iti->Center() - itj->Center());
      const double l = iti->Length();
      if (center_dist < l * params_.center_dist_low_th) continue;
      if (center_dist > l * params_.center_dist_high_th) continue;

      auto armor = Armor(*iti, *itj);
      armor.SetModel(armor_classifier_.Classify(armor));
      armors_.emplace_back(armor);
      break;
    }
  }
  SPDLOG_DEBUG("Found armors: {}", armors_.size());

  const auto stop = std::chrono::high_resolution_clock::now();
  duration_armors_ =
      std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);
  SPDLOG_DEBUG("duration_armors_: {} ms", duration_armors_.count());
}

void ArmorDetector::VisualizeLightBar(cv::Mat &output, bool add_lable) {
  if (!lightbars_.empty()) {
    for (auto &bar : lightbars_) {
      auto vertices = bar.Vertices();
      for (size_t i = 0; i < vertices.size(); ++i)
        cv::line(output, vertices[i], vertices[(i + 1) % 4], kGREEN);

      cv::drawMarker(output, bar.Center(), kGREEN, cv::MARKER_CROSS);

      if (add_lable) {
        std::ostringstream buf;
        buf << bar.Center().x << ", " << bar.Center().y;
        cv::putText(output, buf.str(), vertices[1], kCV_FONT, 1.0, kGREEN);
      }
    }
  }
}

void ArmorDetector::VisualizeArmor(cv::Mat &output, bool add_lable) {
  if (!armors_.empty()) {
    for (auto &armor : armors_) {
      auto vertices = armor.Vertices();
      for (size_t i = 0; i < vertices.size(); ++i)
        cv::line(output, vertices[i], vertices[(i + 1) % 4], kGREEN);

      cv::drawMarker(output, armor.Center(), kGREEN, cv::MARKER_DIAMOND);

      if (add_lable) {
        std::ostringstream buf;
        buf << armor.Center().x << ", " << armor.Center().y;
        cv::putText(output, buf.str(), vertices[1], kCV_FONT, 1.0, kGREEN);
      }
    }
  }
}

ArmorDetector::ArmorDetector() { SPDLOG_DEBUG("Constructed."); }

ArmorDetector::ArmorDetector(std::string params_path, game::Team enemy_team) {
  Init(params_path, enemy_team);
  SPDLOG_DEBUG("Constructed.");
}

ArmorDetector::~ArmorDetector() {
  fs_.release();
  SPDLOG_DEBUG("Destructed.");
}

void ArmorDetector::Init(std::string params_path, game::Team enemy_team) {
  fs_.open(params_path, cv::FileStorage::READ | cv::FileStorage::FORMAT_JSON);

  if (fs_.isOpened()) {
    PrepareParams();
  } else {
    InitDefaultParams(params_path);
    fs_.open(params_path, cv::FileStorage::READ | cv::FileStorage::FORMAT_JSON);
    PrepareParams();
    SPDLOG_WARN("Can not find parasm file. Created and reloaded.");
  }
  enemy_team_ = enemy_team;
  SPDLOG_DEBUG("Inited.");
}

const std::vector<Armor> &ArmorDetector::Detect(cv::Mat &frame) {
  SPDLOG_DEBUG("Detecting");
  FindLightBars(frame);
  MatchLightBars();
  SPDLOG_DEBUG("Detected.");
  return armors_;
}

void ArmorDetector::VisualizeResult(cv::Mat &output, bool draw_bars,
                                    bool draw_armor, bool add_lable) {
  if (draw_bars) VisualizeLightBar(output, add_lable);
  if (draw_armor) VisualizeArmor(output, add_lable);
  if (add_lable) {
    std::ostringstream buf;
    buf << lightbars_.size() << " bars in " << duration_bars_.count() << " ms.";

    int baseLine;
    int v_pos = 0;
    cv::Size text_size =
        cv::getTextSize(buf.str(), kCV_FONT, 1.0, 2, &baseLine);
    v_pos += static_cast<int>(1.3 * text_size.height);
    cv::putText(output, buf.str(), cv::Point(0, v_pos), kCV_FONT, 1.0, kGREEN);

    buf.str(std::string());
    buf << armors_.size() << " armors in " << duration_armors_.count()
        << " ms.";
    text_size = cv::getTextSize(buf.str(), kCV_FONT, 1.0, 2, &baseLine);
    v_pos += static_cast<int>(1.3 * text_size.height);
    cv::putText(output, buf.str(), cv::Point(0, v_pos), kCV_FONT, 1.0, kGREEN);
  }
}