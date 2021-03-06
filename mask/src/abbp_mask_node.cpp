#include <opencv2/opencv.hpp>
#include <ros/ros.h>

#include "abbp_mask_node.h"

int main(int argc, char** argv) {
  init(argc, argv, "mask_node");
  MaskNode masknode;
  masknode.loop();
}

MaskNode::MaskNode() {
  ROS_INFO("Masking init");
  colorImageListener = nodeHandle.subscribe("/camera/color/image_raw", 10, &MaskNode::onColorImage, this);
  depthImageListener = nodeHandle.subscribe("/camera/aligned_depth_to_color/image_raw", 10, &MaskNode::onDepthImage, this);
  maskDetectionListener = nodeHandle.subscribe("/object_detector/rects", 10, &MaskNode::onMaskDetection, this);

  nodeHandle.param("/mask_node/disable_circle_depth", hideCircleDepth, true);
  nodeHandle.param("/mask_node/disable_mask_depth", hideMaskDepth, false);

  { // Always live image
    liveResultWindow = new Window("Live");
    liveResultWindow->addDrawable(liveImageView);
    windows.push_back(liveResultWindow);
    objectImagePublisher = nodeHandle.advertise<sensor_msgs::Image>("/abbp_mask_node/object/image_raw", 1);
  }

  if (!hideCircleDepth) {
    circleResultWindow = new Window("Circle Result");
    circleResultWindow->addDrawable(circleImageView);
    windows.push_back(circleResultWindow);
    circlePosePublisher = nodeHandle.advertise<abbp_mask::DepthPose>("/abbp_mask_node/circle/pose", 1);
  }
  
  if (!hideMaskDepth) {
    maskResultWindow = new Window("Mask Result");
    maskResultWindow->addDrawable(maskImageView);
    windows.push_back(maskResultWindow);
    objectPosePublisher = nodeHandle.advertise<abbp_mask::DepthPose>("/abbp_mask_node/object/pose", 1);
    maskServiceListener = nodeHandle.advertiseService("/abbp_mask_node/trigger_mask", &MaskNode::onMaskServiceCall, this);
  }
}

void MaskNode::loop() {
  ROS_INFO("Masking started");

  AsyncSpinner spinner(4);
  spinner.start();
  
  while(ros::ok()) {
    for (Window* window : windows) {
      window->update(0.016);
    }

    if (colorImagePtr == nullptr || depthImagePtr == nullptr) {
      ROS_INFO("Waiting for images...");
      cv::waitKey(1000);
      continue;
    }

    //Needed to let OpenCV update its windows.
    int k = cv::waitKey(10);

    if (!hideMaskDepth && k == 'a') {
      mask();
    } else if (k > 48 && k < 58) {
      int i = k - 48;
      ROS_INFO_STREAM("Choose object #" << i);
      if (i < 1 || i > maskingResults.size()) {
        ROS_INFO_STREAM("  Not existing (max " << maskingResults.size() << ")");
      } else {
        objectPosePublisher.publish(maskingResults[i - 1].pose);
        ROS_INFO("  Published prop");
      }
    }
  }
  
  ROS_INFO("Masking stopped");
}

void MaskNode::onColorImage(const sensor_msgs::ImageConstPtr& msg) {
  try {
    colorImagePtr = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::BGR8);
  } catch (cv_bridge::Exception& e) {
    ROS_ERROR("Error reading color image: %s", e.what());
    colorImagePtr = nullptr;
    return;
  }

  liveImageView->set(colorImagePtr->image);

  if (hideCircleDepth) {
    return;
  }

  Mat grayImage;
  cvtColor(colorImagePtr->image, grayImage, CV_RGB2GRAY);

  // Setup SimpleBlobDetector parameters.
  SimpleBlobDetector::Params params;
  params.minThreshold = 60;
  params.maxThreshold = 150;
  params.filterByArea = true;
  params.minArea = 600;
  params.maxArea = 16000;
  params.filterByCircularity = true;
  params.minCircularity = 0.4;
  params.filterByConvexity = false;
  params.minConvexity = 0.87;
  params.filterByInertia = false;
  params.minInertiaRatio = 0.01;
  Ptr<SimpleBlobDetector> detector = SimpleBlobDetector::create(params);

  // Detect blobs
  vector<KeyPoint> keypoints;
  detector->detect(grayImage, keypoints);

  // Draw detected blobs as red circles.
  // DrawMatchesFlags::DRAW_RICH_KEYPOINTS flag ensures
  // the size of the circle corresponds to the size of blob
  Mat resultImage;
  colorImagePtr->image.copyTo(resultImage);
  if (keypoints.size() > 0) {
    if (depthImagePtr != nullptr) {
      abbp_mask::DepthPose pose;
      pose.x = keypoints[0].pt.x;
      pose.y = keypoints[0].pt.y;
      pose.depth = ImageUtils::getPixel<float>(depthImagePtr->image, keypoints[0].pt);
      circlePosePublisher.publish(pose);
    }

    drawKeypoints(resultImage, keypoints, resultImage, Colors::RED * 0.5, DrawMatchesFlags::DRAW_RICH_KEYPOINTS);
    drawMarker(resultImage, keypoints[0].pt, Colors::RED, MarkerTypes::MARKER_CROSS);
  } else {
    ImageUtils::simpleOverlay(resultImage, "NOT FOUND");
  }

  circleImageView->set(resultImage);
  //WindowUtils::showColor(resultImage, "Circle Result", hideCircleDepth);
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
  maskingResults.clear();

  cv::Mat masked_image(colorImageSnapshotPtr->image);

  if (msg->indices.size() < 1) {
    ROS_INFO("  Nothing");
    progressCircle->overlay->label->text = "NO RESULT";
    progressCircle->progressBar->visible = false;
  } else {
    int imageWidth = colorImageSnapshotPtr->image.cols;
    int imageHeight = colorImageSnapshotPtr->image.rows;
    Point textOffset(0, - 8);
    vector<Color> randomColors = ColorUtils::randomColors(msg->labels.size());

    for (int i = 0; i < msg->labels.size(); i++) {
      Point tl = Point(msg->polygon[i].polygon.points[0].x, msg->polygon[i].polygon.points[0].y);
      Point br = Point(msg->polygon[i].polygon.points[1].x, msg->polygon[i].polygon.points[1].y);
      Rect objectRect(tl, br);
      Point objectPosition(tl);
      Point objectCenter((br - tl) / 2 + tl);
      String objectName = msg->names[i];
      float objectDepth = ImageUtils::getPixel<float>(depthImageSnapshotPtr->image, objectCenter);

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

      cv::rectangle(masked_image, objectRect, randomColors[i]);
      cv::drawMarker(masked_image, objectCenter, Colors::BLACK, cv::MarkerTypes::MARKER_CROSS, 8);

      maskingResults.push_back(MaskResult::of(i + 1, objectName, objectCenter, objectDepth));

      ostringstream s;
      s << "#" << i + 1 << " " << objectName << " (" << fixed << setprecision(3) << msg->likelihood[i] << ") ";
      cv::putText(masked_image, s.str(), objectPosition + textOffset, cv::FONT_HERSHEY_SIMPLEX , 0.5, Colors::WHITE);

      ROS_INFO("  %s", s.str().c_str());
    }

    progressCircle->visible = false;
  }

  maskImageView->set(masked_image);
  maskingDone = true;
}

bool MaskNode::onMaskServiceCall(abbp_mask::DepthPoseService::Request& request, abbp_mask::DepthPoseService::Response& response) {
  mask();
  ros::Rate rate(1);
  while (!maskingDone) {
    rate.sleep();
    ROS_INFO_NAMED("DepthPoseService", "Waiting for mask..");
  }

  if (maskingResults.empty()) {
    response.success = false;
    return true;
  }

  MaskResult* closest = &maskingResults[0];
  for (MaskResult p : maskingResults) {
    if (p.pose.depth < closest->pose.depth) {
      closest = &p;
    }
  }

  ROS_INFO_STREAM_NAMED("DepthPoseService", "Returning closest object: #"
      << closest->id << " (type: '" << closest->className << "', depth: " << closest->pose.depth << ")");
  response.success = true;
  response.pose = closest->pose;
  return true;
}

void MaskNode::mask() {
  maskingDone = false;
  ROS_INFO("Making snapshot of camera..");
  colorImageSnapshotPtr = colorImagePtr;
  depthImageSnapshotPtr = depthImagePtr;
  objectImagePublisher.publish(colorImageSnapshotPtr->toImageMsg());
  ROS_INFO("  Published color image");
  if (progressCircle == nullptr) {
    Size size = colorImagePtr->image.size();
    Point center(size.width / 2, size.height / 2);
    progressCircle = new IndeterminateOverlay("S..");
    maskResultWindow->addDrawable(progressCircle);
  }
  progressCircle->overlay->label->text = "SEARCHING...";
  progressCircle->visible = true;
  progressCircle->progressBar->visible = true;
  maskImageView->set(colorImageSnapshotPtr->image);
  /*Mat o;
  colorImagePtr->image.copyTo(o);
  ImageUtils::simpleOverlay(o, "SEARCHING..");
  maskImageView->set(o);*/
}