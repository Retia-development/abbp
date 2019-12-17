#include <cv_bridge/cv_bridge.h>
#include <mask_rcnn_ros/RectArray.h>
#include <opencv2/opencv.hpp>
#include <ros/ros.h>

#include "avans-vision-lib/utils.h"

#include "mask_node.h"

using namespace std;

int main(int argc, char** argv) {
  init(argc, argv, "mask_node");
  MaskNode masknode;
  namedWindow("Live", cv::WINDOW_AUTOSIZE);
  namedWindow("Result", cv::WINDOW_AUTOSIZE);
  masknode.loop();
}

MaskNode::MaskNode() {
  ROS_INFO("Masking init");
  colorImageListener = nodeHandle.subscribe("/camera/color/image_raw", 10, &MaskNode::onColorImage, this);
  depthImageListener = nodeHandle.subscribe("/camera/aligned_depth_to_color/image_raw", 10, &MaskNode::onDepthImage, this);
  maskDetectionListener = nodeHandle.subscribe("/object_detector/rects", 10, &MaskNode::onMaskDetection, this);

  colorImagePublisher = nodeHandle.advertise<sensor_msgs::Image>("/mask_node/camera/color/image_raw", 1);
}

void MaskNode::loop() {
  ROS_INFO("Masking started");
  Rate fastRate(10);
  Rate slowRate(1);

  while(ros::ok()) {
    //Needed to let ROS update its pubs 'n subs.
    spinOnce();

    //Needed to let OpenCV update its windows.
    int k = cv::waitKey(1);

    if (colorImagePtr == nullptr || depthImagePtr == nullptr) {
      ROS_INFO("Waiting for images...");
      slowRate.sleep();
      continue;
    }

    if (k == 'a') {
      savedColorImagePtr = colorImagePtr;
      ROS_INFO("Published photo!");
      colorImagePublisher.publish(savedColorImagePtr->toImageMsg());
    }

    fastRate.sleep();
  }
  
  ROS_INFO("Masking stopped");
}

void MaskNode::onColorImage(const sensor_msgs::ImageConstPtr& msg) {
  try {
    colorImagePtr = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::BGR8);
    ImageUtils::window(colorImagePtr->image, "Live", true);
  } catch (cv_bridge::Exception& e) {
    ROS_ERROR("Error reading color image: %s", e.what());
    colorImagePtr = nullptr;
    return;
  }
}

void MaskNode::onDepthImage(const sensor_msgs::ImageConstPtr& msg) {
  try {
    depthImagePtr = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::TYPE_32FC1);
  } catch (cv_bridge::Exception& e) {
    ROS_ERROR("Error reading depth image: %s", e.what());
    depthImagePtr = nullptr;
    return;
  }
}

void MaskNode::onMaskDetection(const mask_rcnn_ros::RectArrayConstPtr& msg) {
  ROS_INFO("Mask produced!");
  if (msg->indices.size() < 1) {
    ROS_INFO("  Nothing");
    return;
  }

  cv::Mat masked_image(savedColorImagePtr->image);

  int imageWidth = savedColorImagePtr->image.cols;
  int imageHeight = savedColorImagePtr->image.rows;
  Point textOffset(0, - 8);
  vector<Color> randomColors = ColorUtils::randomColors(msg->labels.size());

  for (int i = 0; i < msg->labels.size(); i++) {
    Point tl = Point(msg->polygon[i].polygon.points[0].x, msg->polygon[i].polygon.points[0].y);
    Point br = Point(msg->polygon[i].polygon.points[1].x, msg->polygon[i].polygon.points[1].y);
    Point objectPosition(tl);
    Rect objectSize(tl, br);

    int si = i * imageWidth * imageHeight;
    int ei = (i + 1) * imageWidth * imageHeight - 1;
    vector<int16_t> sub16S;
    for (int i = si; i < ei; i++) {
      sub16S.push_back(msg->indices[i]);
    }
    cv::Mat objectMask(imageHeight, imageWidth, CV_16S, (void*)(sub16S.data()));

    ImageUtils::forEachPixel<Color>(masked_image, masked_image, [&](cv::Point pc, Color px) {
       return ImageUtils::getPixel<int16_t>(objectMask, pc) == 0 ? px : px * (1 - 0.5) + randomColors[i] * 0.5;
    });

    cv::rectangle(masked_image, objectSize, randomColors[i]);

    ostringstream s;
    s << msg->names[i] << " (" << fixed << setprecision(3) << msg->likelihood[i] << ")";
    cv::putText(masked_image, s.str(), tl + textOffset, cv::FONT_HERSHEY_SIMPLEX , 0.5, Colors::WHITE);
    ROS_INFO("  %s", s.str().c_str());
  }

  ImageUtils::window(masked_image, "Result", true);
}